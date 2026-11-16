#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <string>
#include "core/leaf_acnl/tool_ring.h"

namespace LibRetro {
struct ToolRingVisual {
    float opacity = 0;
    float scale = .94f;
    std::array<float,8> emphasis{};
    int selected = 0;
    std::vector<LeafACNL::Tool> tools;
};

inline std::string ToolRingName(const LeafACNL::Tool& tool) {
    constexpr const char* names[6][4]={
        {"RENTAL AXE","AXE","SILVER AXE","GOLDEN AXE"},
        {"RENTAL NET","NET","SILVER NET","GOLDEN NET"},
        {"RENTAL ROD","FISHING ROD","SILVER ROD","GOLDEN ROD"},
        {"RENTAL SHOVEL","SHOVEL","SILVER SHOVEL","GOLDEN SHOVEL"},
        {"RENTAL SLINGSHOT","SLINGSHOT","SILVER SLINGSHOT","GOLDEN SLINGSHOT"},
        {"RENTAL CAN","WATERING CAN","SILVER WATERING CAN","GOLDEN CAN"}};
    if(tool.kind>=14 && tool.kind<=19) return names[tool.kind-14][std::min<int>(tool.variant,3)];
    return tool.kind==21 ? "MEGAPHONE" : "TOOL";
}

struct DialogueCueVisual {
    float opacity=0;
    float scale=.82f;
};
class DialogueCueAnimation {
public:
    void Update(bool visible,bool pressed,float seconds) {
        if(!std::isfinite(seconds))return;
        seconds=std::clamp(seconds,0.0f,1.0f);
        progress=std::clamp(progress+(visible ? seconds/.20f : -seconds/.18f),0.0f,1.0f);
        phase=std::fmod(phase+seconds,3.2f);
        if(pressed && progress>0)press_age=0;
        else press_age=std::min(.26f,press_age+seconds);
        if(progress==0){phase=0;press_age=.26f;}
    }
    DialogueCueVisual Visual() const {
        const auto ease=[](float t){return t*t*(3-2*t);};
        const float opacity=ease(progress);
        const float pulse=.985f+.015f*std::cos(phase*6.28318530718f/3.2f);
        const float press=press_age<.07f ? ease(press_age/.07f) : 1-ease((press_age-.07f)/.19f);
        return {opacity,(.82f+.18f*opacity)*pulse*(1-.14f*press)};
    }
private:
    float progress=0,phase=0,press_age=.26f;
};

class ToolRingAnimation {
public:
    void Update(bool open, int selected, float seconds) {
        if (!std::isfinite(seconds)) return;
        seconds=std::clamp(seconds,0.0f,1.0f);
        progress=std::clamp(progress+(open ? seconds/.14f : -seconds/.10f),0.0f,1.0f);
        for(size_t i=0;i<emphasis.size();++i) {
            const float target=static_cast<int>(i)==selected ? 1.0f : 0.0f;
            const float step=seconds/.09f;
            emphasis[i]+=std::clamp(target-emphasis[i],-step,step);
        }
    }
    ToolRingVisual Visual() const {
        ToolRingVisual visual;
        visual.opacity=progress*progress*(3-2*progress);
        visual.scale=.94f+.06f*visual.opacity;
        visual.emphasis=emphasis;
        return visual;
    }
private:
    float progress=0;
    std::array<float,8> emphasis{};
};
}
