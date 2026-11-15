#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <cstdlib>
#include "citra_libretro/rounded_overlay_font.h"
#include <unordered_map>
#include <vector>
#include <utility>
#include "citra_libretro/environment.h"
#include "common/settings.h"
#include "core/leaf_acnl/tool_ring.h"
#include "core/leaf_acnl/ui.h"
#ifdef ENABLE_OPENGL
#include <glad/glad.h>
#endif
#include "video_core/texture/texture_decode.h"

namespace LibRetro {
const OverlayFont& OverlayUiFont() {
    static auto fallback=RoundedOverlayFont();
    static std::shared_ptr<const std::vector<u8>> loaded;
    static OverlayFont game;
    const auto bytes=LeafACNL::OverlayFontBytes();
    if(bytes!=loaded) {
        loaded=bytes;
        game=bytes ? ReadGameFont(*bytes,[](const u8* data,u32 format,u32 width,u32 height,u32 x,u32 y) {
            Pica::Texture::TextureInfo info{};
            info.width=width;info.height=height;
            info.format=static_cast<Pica::TexturingRegs::TextureFormat>(format);info.SetDefaultStride();
            const auto color=Pica::Texture::LookupTexture(data,x,y,info);
            return format==7 || format==10 ? color.r() : color.a();
        }) : OverlayFont{};
    }
    static bool announced=false;
    if(!announced){LOG_INFO(Frontend,"Leaf overlay typography Garden/Nunito active");announced=true;}
    return game.Valid() ? game : fallback;
}
ToolRingImage BuildToolRingImage(int width,int height) {
    const auto visual=ToolRingPresentation();
    const bool prompt=visual.opacity<=0 && LeafACNL::UiDialoguePrompt();
    if ((visual.opacity<=0 && !prompt) || width <= 0 || height <= 0)
        return {};
    const auto& tools = visual.tools;
    float layer_alpha=visual.opacity;
    const int h = std::clamp(height,360,1440);
    const int w = static_cast<int>(std::clamp(static_cast<long long>(width)*h/height,1LL,7680LL));
    const int unit=std::max(1,static_cast<int>(std::lround(h/360.0)));
    std::vector<u8> image(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < image.size(); i += 4) {
        image[i] = 12;
        image[i + 1] = 22;
        image[i + 2] = 18;
        image[i + 3] = static_cast<u8>(std::lround(110*visual.opacity));
    }
    auto pixel = [&](int x, int y, std::array<u8, 4> color) {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return;
        const size_t offset = (static_cast<size_t>(y) * w + x) * 4;
        color[3]=static_cast<u8>(std::lround(color[3]*layer_alpha));
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
        const double inner=std::max(0.0,radius-.5),outer=radius+.5;
        for (int y=static_cast<int>(std::floor(cy-radius-1));y<=cy+radius+1;++y)
            for (int x=static_cast<int>(std::floor(cx-radius-1));x<=cx+radius+1;++x) {
                const double dx=x+.5-cx,dy=y+.5-cy,distance=dx*dx+dy*dy;
                if (distance>=outer*outer) continue;
                const double coverage=distance<=inner*inner ? 1 : std::clamp(outer-std::sqrt(distance),0.0,1.0);
                auto edge=color;
                edge[3]=static_cast<u8>(std::lround(color[3]*coverage));
                pixel(x,y,edge);
            }
    };
    for (int i = 0; visual.opacity>0 && !tools.empty() && i < 8; ++i) {
        const double angle = i * 3.141592653589793 / 4 - 3.141592653589793 / 2;
        const double cx = w / 2.0 + std::cos(angle) * h * 0.22 * visual.scale,
                     cy = h * 0.56 + std::sin(angle) * h * 0.22 * visual.scale;
        const double radius = h * 0.062 * visual.scale * (1.0 + .2 * visual.emphasis[i]);
        circle(cx + radius * 0.05, cy + radius * 0.09, radius, {0, 0, 0, 100});
        circle(cx, cy, radius,
               i == visual.selected ? std::array<u8, 4>{115, 205, 179, 255}
                                       : std::array<u8, 4>{245, 240, 213, 245});
        if (i == visual.selected)
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
            for (int t=0;t<7*unit;++t)
                circle(cx-radius*.68-3*unit+t,cy-radius*.68+3*unit-t,
                       std::max(.5,unit*.55),{255,255,255,255});
        }
    }
    {
        const auto& font=OverlayUiFont();
        const std::array<u8,4> ink{103,76,49,255},cream{255,249,224,255};
        const auto measure=[&](const std::string& value,float height) {
            float result=0;
            for(unsigned char c:value) if(c>=32 && c<=126) result+=font.glyphs[c-32].advance*height/font.height;
            return result;
        };
        const auto text=[&](const std::string& value,float center,float top,float height,std::array<u8,4> color) {
            const float scale=height/font.height;
            float pen=center-measure(value,height)/2;
            for(unsigned char c:value) {
                if(c<32 || c>126)continue;
                const auto& g=font.glyphs[c-32];
                const float left=pen+g.left*scale;
                const auto sample=[&](int x,int y)->float {
                    return x>=0 && y>=0 && x<g.width && y<g.height ? g.alpha[y*g.width+x]/255.0f : 0;
                };
                for(int y=static_cast<int>(std::floor(top));y<top+g.height*scale;++y)
                    for(int x=static_cast<int>(std::floor(left));x<left+g.width*scale;++x) {
                        const float sx=(x+.5f-left)/scale-.5f,sy=(y+.5f-top)/scale-.5f;
                        const int ix=static_cast<int>(std::floor(sx)),iy=static_cast<int>(std::floor(sy));
                        const float fx=sx-ix,fy=sy-iy;
                        const float alpha=sample(ix,iy)*(1-fx)*(1-fy)+sample(ix+1,iy)*fx*(1-fy)+
                            sample(ix,iy+1)*(1-fx)*fy+sample(ix+1,iy+1)*fx*fy;
                        auto shaded=color;shaded[3]=static_cast<u8>(std::lround(color[3]*alpha));pixel(x,y,shaded);
                    }
                pen+=g.advance*scale;
            }
        };
        const auto rounded=[&](float left,float top,float right,float bottom,float radius,std::array<u8,4> color) {
            for(int y=static_cast<int>(std::floor(top));y<bottom;++y)for(int x=static_cast<int>(std::floor(left));x<right;++x) {
                const float dx=std::max({left+radius-x-.5f,x+.5f-(right-radius),0.0f});
                const float dy=std::max({top+radius-y-.5f,y+.5f-(bottom-radius),0.0f});
                const float coverage=std::clamp(radius+.5f-std::hypot(dx,dy),0.0f,1.0f);
                auto edge=color;edge[3]=static_cast<u8>(std::lround(color[3]*coverage));pixel(x,y,edge);
            }
        };
        const auto panel=[&](float left,float top,float right,float bottom,float radius) {
            rounded(left+unit,top+2*unit,right+unit,bottom+2*unit,radius,{56,38,20,90});
            rounded(left,top,right,bottom,radius,{184,151,99,255});
            rounded(left+unit,top+unit,right-unit,bottom-unit,std::max(1.0f,radius-unit),cream);
        };
        const auto badge=[&](char letter,float cx,float cy,float diameter) {
            circle(cx,cy,diameter/2,{90,155,117,255});
            circle(cx,cy,diameter/2-unit,{234,248,212,255});
            text(std::string(1,letter),cx,cy-diameter*.45f,diameter*.9f,ink);
        };
        const auto hint=[&](char button,const std::string& label,float center,float top,float height,bool boxed) {
            const float pad=std::max(3.0f,h*.009f),diameter=height*1.1f;
            const float width=diameter+pad+measure(label,height);
            const float left=center-width/2;
            if(boxed)panel(left-pad,top-pad,left+width+pad,top+diameter+pad,(diameter+2*pad)/2);
            badge(button,left+diameter/2,top+diameter/2,diameter);
            text(label,left+diameter+pad+measure(label,height)/2,top,height,ink);
        };
        if(visual.opacity>0 && tools.empty()) {
            const auto message=LeafACNL::ToolRingStatus();
            const float size=std::min(h*.035f,(w-24.0f)*font.height/std::max(1.0f,measure(message,static_cast<float>(font.height))));
            text(message,w/2.0f,h/2.0f,size,cream);
        }
        if(visual.opacity>0 && !tools.empty() && visual.selected>=0 && static_cast<size_t>(visual.selected)<tools.size()) {
            const auto& tool=tools[visual.selected];
            auto name=ToolRingName(tool);
            for(size_t i=1;i<name.size();++i)if(name[i]>='A' && name[i]<='Z')name[i]+=32;
            const std::string action=tool.slot<0 ? "Put away" : "Equip";
            const float size=h*.052f,hint_size=h*.035f,pad=h*.012f;
            const float width=std::max(measure(name,size),hint_size*1.1f+pad+measure(action,hint_size))+2*pad;
            const float left=w/2.0f-width/2,top=h*.09f,bottom=top+size+hint_size*1.1f+3*pad;
            panel(left,top,left+width,bottom,5*unit);
            for(int y=0;y<5*unit;++y)for(int x=w/2-(5*unit-y);x<w/2+(5*unit-y);++x)pixel(x,static_cast<int>(bottom)+y,cream);
            text(name,w/2.0f,top+pad,size,ink);
            hint('A',action,w/2.0f,top+size+2*pad,hint_size,false);
            hint('B',"Cancel",w/2.0f,h*.88f,hint_size,true);
        }
        if(prompt) {
            layer_alpha=1;
            const float size=h*.035f,pad=std::max(3.0f,h*.009f);
            const float width=size*1.1f+pad+measure("Continue",size);
            const float center=w-3*pad-width/2,top=h-4*pad-size*1.1f;
            hint('A',"Continue",center,top,size,true);
        }
        if(visual.opacity>0 && (tools.empty() || std::getenv("LEAF_UI_DEBUG"))) {
            const auto status=LeafACNL::UiStatus();
            const float size=std::min(h*.022f,(w-24.0f)*font.height/std::max(1.0f,measure(status,static_cast<float>(font.height))));
            text(status,w/2.0f,2*unit,size,{255,255,255,255});
        }
    }
    return {w,h,std::move(image)};
}
std::vector<ToolRingVertex> BuildToolRingVertices(int width,int height) {
    const auto overlay=BuildToolRingImage(width,height);
    std::vector<ToolRingVertex> vertices;
    if(overlay.pixels.empty())return vertices;
    const auto color=[&](int x,int y){const size_t p=(static_cast<size_t>(y)*overlay.width+x)*4;return static_cast<u32>(overlay.pixels[p])|(static_cast<u32>(overlay.pixels[p+1])<<8)|(static_cast<u32>(overlay.pixels[p+2])<<16)|(static_cast<u32>(overlay.pixels[p+3])<<24);};
    for(int y=0;y<overlay.height;++y)for(int x=0;x<overlay.width;) {
        const u32 rgba=color(x,y);
        int end=x+1;while(end<overlay.width && color(end,y)==rgba)++end;
        if(rgba>>24) {
            const float left=2.0f*x/overlay.width-1.0f,right=2.0f*end/overlay.width-1.0f;
            const float top=2.0f*y/overlay.height-1.0f,bottom=2.0f*(y+1)/overlay.height-1.0f;
            vertices.insert(vertices.end(),{{left,top,rgba},{right,top,rgba},{right,bottom,rgba},{left,top,rgba},{right,bottom,rgba},{left,bottom,rgba}});
        }
        x=end;
    }
    return vertices;
}
void RenderToolRing(int width,int height,void* framebuffer,size_t pitch) {
    const auto overlay=BuildToolRingImage(width,height);
    if(overlay.pixels.empty())return;
    const auto& image=overlay.pixels;
    const int w=overlay.width,h=overlay.height;
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
