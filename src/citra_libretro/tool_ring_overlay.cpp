#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "citra_libretro/environment.h"
#include "common/settings.h"
#include "core/leaf_acnl/tool_ring.h"
#ifdef ENABLE_OPENGL
#include <glad/glad.h>
#endif
#include "video_core/texture/texture_decode.h"

namespace LibRetro {
void RenderToolRing(int width, int height, void* framebuffer, size_t pitch) {
    if (!ToolRingVisible() || width <= 0 || height <= 0)
        return;
    const auto tools = LeafACNL::Tools();
    const int h = 360, w = std::max(1, width * h / height);
    std::vector<u8> image(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < image.size(); i += 4) {
        image[i] = 12;
        image[i + 1] = 22;
        image[i + 2] = 18;
        image[i + 3] = 110;
    }
    auto pixel = [&](int x, int y, std::array<u8, 4> color) {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;
        const size_t offset = (static_cast<size_t>(y) * w + x) * 4;
        const unsigned alpha = color[3], old_alpha = image[offset + 3];
        const unsigned combined = alpha + old_alpha * (255 - alpha) / 255;
        if (!combined)
            return;
        for (int c = 0; c < 3; ++c)
            image[offset + c] = static_cast<u8>(
                (color[c] * alpha + image[offset + c] * old_alpha * (255 - alpha) / 255) /
                combined);
        image[offset + 3] = static_cast<u8>(combined);
    };
    auto circle = [&](double cx, double cy, double radius, std::array<u8, 4> color) {
        for (int y = static_cast<int>(cy - radius); y <= cy + radius; ++y)
            for (int x = static_cast<int>(cx - radius); x <= cx + radius; ++x)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius)
                    pixel(x, y, color);
    };
    for (int i = 0; !tools.empty() && i < 8; ++i) {
        const double angle = i * 3.141592653589793 / 4 - 3.141592653589793 / 2;
        const double cx = w / 2.0 + std::cos(angle) * h * 0.22,
                     cy = h * 0.56 + std::sin(angle) * h * 0.22;
        const double radius = h * 0.062 * (i == ToolRingSelected() ? 1.2 : 1.0);
        circle(cx + radius * 0.05, cy + radius * 0.09, radius, {0, 0, 0, 100});
        circle(cx, cy, radius,
               i == ToolRingSelected() ? std::array<u8, 4>{115, 205, 179, 255}
                                       : std::array<u8, 4>{245, 240, 213, 245});
        if (i == ToolRingSelected())
            circle(cx, cy, radius * 0.91, {245, 240, 213, 255});
        if (static_cast<size_t>(i) >= tools.size())
            continue;
        const auto bytes = LeafACNL::ToolIcon(tools[i].icon);
        if (bytes.size() < 40)
            continue;
        const size_t footer = bytes.size() - 40;
        if (std::memcmp(bytes.data() + footer, "CLIM", 4) != 0 ||
            std::memcmp(bytes.data() + footer + 20, "imag", 4) != 0)
            continue;
        u16 iw = 0, ih = 0;
        u32 format = 0;
        std::memcpy(&iw, bytes.data() + footer + 28, 2);
        std::memcpy(&ih, bytes.data() + footer + 30, 2);
        std::memcpy(&format, bytes.data() + footer + 32, 4);
        if (iw != 32 || ih != 32 || format != 11 || footer < 1024)
            continue;
        Pica::Texture::TextureInfo info{};
        info.width = iw;
        info.height = ih;
        info.format = Pica::TexturingRegs::TextureFormat::ETC1A4;
        info.SetDefaultStride();
        const double side = radius * 1.5;
        for (int y = static_cast<int>(cy - side / 2); y < cy + side / 2; ++y)
            for (int x = static_cast<int>(cx - side / 2); x < cx + side / 2; ++x) {
                const unsigned sx =
                    std::clamp(static_cast<int>((x - cx + side / 2) * iw / side), 0, 31);
                const unsigned sy =
                    std::clamp(static_cast<int>((y - cy + side / 2) * ih / side), 0, 31);
                const auto color = Pica::Texture::LookupTexture(bytes.data(), sx, 31 - sy, info);
                pixel(x, y, {color.r(), color.g(), color.b(), color.a()});
            }
        if (tools[i].slot < 0) {
            circle(cx - radius * 0.68, cy - radius * 0.68, radius * 0.3, {45, 165, 100, 255});
            for (int t = 0; t < 7; ++t) {
                pixel(static_cast<int>(cx - radius * 0.68) - 3 + t,
                      static_cast<int>(cy - radius * 0.68) + 3 - t, {255, 255, 255, 255});
            }
        }
    }
    if (tools.empty()) {
        constexpr std::array<std::array<u8,5>,26> glyphs{{
            {{0x7e,0x11,0x11,0x11,0x7e}},{{0x7f,0x49,0x49,0x49,0x36}},{{0x3e,0x41,0x41,0x41,0x22}},
            {{0x7f,0x41,0x41,0x22,0x1c}},{{0x7f,0x49,0x49,0x49,0x41}},{{0x7f,0x09,0x09,0x09,0x01}},
            {{0x3e,0x41,0x49,0x49,0x7a}},{{0x7f,0x08,0x08,0x08,0x7f}},{{0x41,0x41,0x7f,0x41,0x41}},
            {{0x20,0x40,0x41,0x3f,0x01}},{{0x7f,0x08,0x14,0x22,0x41}},{{0x7f,0x40,0x40,0x40,0x40}},
            {{0x7f,0x02,0x0c,0x02,0x7f}},{{0x7f,0x04,0x08,0x10,0x7f}},{{0x3e,0x41,0x41,0x41,0x3e}},
            {{0x7f,0x09,0x09,0x09,0x06}},{{0x3e,0x41,0x51,0x21,0x5e}},{{0x7f,0x09,0x19,0x29,0x46}},
            {{0x46,0x49,0x49,0x49,0x31}},{{0x01,0x01,0x7f,0x01,0x01}},{{0x3f,0x40,0x40,0x40,0x3f}},
            {{0x1f,0x20,0x40,0x20,0x1f}},{{0x3f,0x40,0x38,0x40,0x3f}},{{0x63,0x14,0x08,0x14,0x63}},
            {{0x07,0x08,0x70,0x08,0x07}},{{0x61,0x51,0x49,0x45,0x43}}
        }};
        const auto message=LeafACNL::ToolRingStatus();
        const int scale=std::max(1,std::min(2,(w-24)/std::max(1,static_cast<int>(message.size())*6)));
        const int left=(w-static_cast<int>(message.size())*6*scale)/2;
        for(size_t i=0;i<message.size();++i) {
            if(message[i]<'A'||message[i]>'Z')continue;
            const auto& glyph=glyphs[message[i]-'A'];
            for(int x=0;x<5;++x)for(int y=0;y<7;++y)if(glyph[x]&(1<<y))
                for(int sx=0;sx<scale;++sx)for(int sy=0;sy<scale;++sy)
                    pixel(left+static_cast<int>(i)*6*scale+x*scale+sx,h/2+y*scale+sy,{255,255,255,255});
        }
    }
    if (framebuffer) {
        if (pitch < static_cast<size_t>(width)*4) return;
        static bool announced=false;
        if(!announced){LOG_INFO(Frontend,"Leaf ToolRing software overlay active");announced=true;}
        auto* dest=static_cast<u8*>(framebuffer);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const size_t source=(static_cast<size_t>(y)*h/height*w+static_cast<size_t>(x)*w/width)*4;
            const size_t target=static_cast<size_t>(y)*pitch+static_cast<size_t>(x)*4;
            const unsigned alpha=image[source+3];
            dest[target]=(image[source+2]*alpha+dest[target]*(255-alpha)+127)/255;
            dest[target+1]=(image[source+1]*alpha+dest[target+1]*(255-alpha)+127)/255;
            dest[target+2]=(image[source]*alpha+dest[target+2]*(255-alpha)+127)/255;
        }
        return;
    }
#ifdef ENABLE_OPENGL
    static GLuint program = 0, texture = 0, vao = 0;
    if (!glIsProgram(program)) {
        const char* version =
            Settings::values.use_gles ? "#version 300 es\n" : "#version 330 core\n";
        const std::string vs =
            std::string(version) + "precision highp float;out vec2 uv;void main(){vec2 "
                                   "p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=vec2("
                                   "p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0,1);}";
        const std::string fs =
            std::string(version) + "precision highp float;in vec2 uv;uniform sampler2D ring;out "
                                   "vec4 color;void main(){color=texture(ring,uv);}";
        auto shader = [](GLenum kind, const std::string& source) {
            GLuint s = glCreateShader(kind);
            const char* p = source.c_str();
            glShaderSource(s, 1, &p, nullptr);
            glCompileShader(s);
            return s;
        };
        const GLuint vertex = shader(GL_VERTEX_SHADER, vs),
                     fragment = shader(GL_FRAGMENT_SHADER, fs);
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            glDeleteProgram(program);
            program = 0;
            return;
        }
        glGenTextures(1, &texture);
        glGenVertexArrays(1, &vao);
    }
    GLint old_viewport[4], old_framebuffer = 0;
    glGetIntegerv(GL_VIEWPORT, old_viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(GetFramebuffer()));
    glUseProgram(program);
    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
    glUniform1i(glGetUniformLocation(program, "ring"), 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(old_framebuffer));
#endif
}
}
