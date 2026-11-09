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
#include "video_core/texture/texture_decode.h"
#endif

namespace LibRetro {
void RenderToolRing(int width, int height) {
#ifdef ENABLE_OPENGL
    if (!ToolRingVisible() || width <= 0 || height <= 0)
        return;
    const auto tools = LeafACNL::Tools();
    if (tools.empty())
        return;
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
    for (int i = 0; i < 8; ++i) {
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
