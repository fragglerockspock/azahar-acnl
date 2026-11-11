#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "common/hash.h"
#include "common/logging/log.h"
#include "core/leaf_acnl/ui.h"

namespace LeafACNL {
namespace {
constexpr u32 Tab = 1, HudButton = 2, Map = 4, Choice = 8, MapControl = 16, HudMap = 32;
std::atomic_bool enabled{false};
std::mutex ui_mutex;
std::unordered_map<u64, u32> textures;
std::unordered_set<u64> dimensions;
struct Frame {
    bool sealed = false;
    u32 tags = 0;
    UiRect choice;
    UiRect map;
    u64 sequence = 0;
};
std::unordered_map<u32, Frame> targets;
struct TargetInfo { u32 width=0,height=0,format=0; size_t size=0; bool bottom=false; };
std::unordered_map<u32,TargetInfo> target_info;
std::unordered_map<u32,Frame> completed;
Frame last_completed;
bool have_completed=false;
std::atomic_bool alpha_ready{false};
u64 key_count=0,event_sequence=0;
u32 displayed_address=0;
Frame displayed;
u64 draw_count = 0, match_count = 0, transfer_count = 0;
void Union(UiRect& a, UiRect b) {
    if (!b.Valid()) return;
    if (!a.Valid()) { a = b; return; }
    a = {std::min(a.left,b.left), std::min(a.top,b.top),
         std::max(a.right,b.right), std::max(a.bottom,b.bottom)};
}
u32 Tags(std::string_view name) {
    u32 tags = 0;
    if (name.find("Menu/menu_tab.arc:") == 0 && name.find(":im_cmn_")==std::string_view::npos &&
        name.find(":my_cmn_")==std::string_view::npos) tags |= Tab;
    if (name == "Menu/menu_tab.arc:my_men_btn_00.bclim") tags |= HudButton;
    for (const auto prefix : {"Menu/map_village.arc:", "Menu/map_island.arc:",
                              "Menu/map_lobby.arc:", "Menu/map_exb.arc:"}) {
        if (name.find(prefix) == 0 && name.find(":im_cmn_")==std::string_view::npos) tags |= Map;
    }
    if (name.find("selWin/slct_win.arc:") == 0 && name.find(":im_cmn_")==std::string_view::npos) tags |= Choice;
    if (name == "common/cmn_btn.arc:my_cbn_btn.bclim") tags |= MapControl;
    if ((tags&Map) && (name.find(":tc_map_list_win") != std::string_view::npos ||
                       name.find(":tc_map_listwin_arrow.bclim") != std::string_view::npos))
        tags |= HudMap;
    return tags;
}
u64 Key(const u8* bytes, size_t size, u32 width, u32 height) {
    return Common::ComputeHash64(bytes,size) ^ (u64{width} << 48) ^ (u64{height} << 32);
}
UiRect Crop(UiRect rect, float padding) {
    if (!rect.Valid()) return {0,0,320,240};
    return {std::clamp(rect.left-padding,0.0f,320.0f),
            std::clamp(rect.top-padding,0.0f,240.0f),
            std::clamp(rect.right+padding,0.0f,320.0f),
            std::clamp(rect.bottom+padding,0.0f,240.0f)};
}
UiRegion Region(UiRect source, float width, float height, float tx, float ty,
                float ax, float ay, float scale, float ox = 0, float oy = 0, bool fit = false) {
    const float sw = source.right-source.left, sh = source.bottom-source.top;
    const float factor = fit ? std::min(width/sw,height/sh) : height/240*scale;
    const float w = sw*factor, h = sh*factor;
    const float x = width*tx-w*ax+ox*height/240;
    const float y = height*ty-h*ay+oy*height/240;
    return {source,{std::max(0.0f,std::round(x)),std::max(0.0f,std::round(y)),
                    std::max(0.0f,std::round(x+w)),std::max(0.0f,std::round(y+h))},0};
}
}
bool UiRect::Valid() const {
    return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
           std::isfinite(bottom) && right > left && bottom > top;
}
UiRect UiLandscapeBounds(UiRect raw) {
    if (!raw.Valid()) return {};
    return {320-raw.bottom,240-raw.right,320-raw.top,240-raw.left};
}
size_t UiTextureSize(u32 format, u32 width, u32 height) {
    constexpr std::array<u32,14> bits{32,24,16,16,16,16,16,8,8,8,4,4,4,8};
    if (format>=bits.size() || !width || !height || width>2048 || height>2048) return 0;
    return size_t{width}*height*bits[format]/8;
}
std::string UiStatus() {
    std::lock_guard lock(ui_mutex);
    return "UI FIX 2S REG "+std::to_string(textures.size())+" D "+std::to_string(draw_count)+
           " H "+std::to_string(match_count)+" T "+std::to_string(transfer_count)+
           " TAG "+std::to_string(displayed.tags)+" K "+std::to_string(key_count)+
           " A "+(alpha_ready.load() ? "ON" : "OFF");
}
void ResetUi() {
    std::lock_guard lock(ui_mutex);
    enabled = false;
    textures.clear();
    dimensions.clear();
    targets.clear();
    displayed = {};
    target_info.clear();
    completed.clear();
    last_completed={};
    have_completed=false;
    key_count=event_sequence=0;
    displayed_address=0;
    draw_count = match_count = transfer_count = 0;
}
bool IsUiArchivePath(std::string_view path) {
    const auto pos = path.find("Layout/");
    if (pos == std::string_view::npos) return false;
    path.remove_prefix(pos+7);
    return path == "Menu/menu_tab.arc" || path == "selWin/slct_win.arc" ||
           path == "common/cmn_btn.arc" ||
           (path.find("Menu/map_") == 0 && path.size() > 4 && path.substr(path.size()-4) == ".arc");
}
void RegisterUiArchive(std::string_view path, const std::vector<u8>& bytes) {
    if (!IsUiArchivePath(path) || bytes.size() < 28 || std::memcmp(bytes.data(),"darc",4)) return;
    path.remove_prefix(path.find("Layout/")+7);
    const auto read = [&](size_t offset) -> u32 {
        u32 value = 0;
        if (offset <= bytes.size()-4) std::memcpy(&value,bytes.data()+offset,4);
        return value;
    };
    const size_t table = read(16);
    if (table > bytes.size()-12) return;
    const size_t count = read(table+8);
    if (!count || count > (bytes.size()-table)/12) return;
    const size_t names = table+count*12;
    std::lock_guard lock(ui_mutex);
    for (size_t i=0;i<count;++i) {
        const u32 entry = read(table+i*12);
        if (entry>>24) continue;
        size_t pos = names+(entry&0xffffff);
        std::string name;
        while (pos+1<bytes.size() && bytes[pos] && !bytes[pos+1] && name.size()<128) {
            name += static_cast<char>(bytes[pos]);
            pos += 2;
        }
        if (name.size()<6 || name.substr(name.size()-6)!=".bclim") continue;
        const u32 tags = Tags(std::string(path)+":"+name);
        if (!tags) continue;
        const size_t offset = read(table+i*12+4), size = read(table+i*12+8);
        if (size<40 || offset>bytes.size() || size>bytes.size()-offset) continue;
        const size_t footer = offset+size-40;
        if (std::memcmp(bytes.data()+footer,"CLIM",4) ||
            std::memcmp(bytes.data()+footer+20,"imag",4)) continue;
        const u32 packed_dimensions = read(footer+28);
        const u32 width = packed_dimensions&0xffff, height = packed_dimensions>>16;
        const size_t payload = read(footer+36);
        if (!width || !height || !payload || payload>size-40) continue;
        textures[Key(bytes.data()+offset,payload,width,height)] |= tags;
        dimensions.insert((u64{width}<<32)|height);
    }
    enabled = !textures.empty();
    LOG_INFO(Core,"Leaf UI texture registry: {} keys after {}",textures.size(),path);
}
bool UiEnabled() { return enabled.load(std::memory_order_relaxed); }
u32 UiTexture(const u8* data, size_t size, u32 width, u32 height) {
    if (!UiEnabled() || !data || !size) return 0;
    std::lock_guard lock(ui_mutex);
    if (!dimensions.count((u64{width}<<32)|height)) return 0;
    const u64 key = Key(data,size,width,height);
    const auto it = textures.find(key);
    return it == textures.end() ? 0 : it->second;
}
void UiDraw(u32 target, u32 tags, UiRect bounds) {
    if (!UiEnabled()) return;
    std::lock_guard lock(ui_mutex);
    auto& frame = targets[target];
    if (frame.sealed) frame={};
    auto& info=target_info[target];
    const bool bottom_shape=!info.width ||
        ((info.width==240 || info.width==256) && info.height==320) ||
        (info.width==320 && info.height>=240 && info.height<=256);
    if (bottom_shape && (tags&(Tab|Map|Choice))) info.bottom=true;
    frame.tags |= tags;
    ++draw_count;
    frame.sequence = ++event_sequence;
    if (tags) ++match_count;
    if (tags&Choice) Union(frame.choice,bounds);
    if (tags&HudMap) Union(frame.map,bounds);
}
void UiTarget(u32 address, u32 width, u32 height, u32 format) {
    if (!UiEnabled() || !address) return;
    std::lock_guard lock(ui_mutex);
    auto& info=target_info[address];
    if (info.width!=width || info.height!=height || info.format!=format) {
        info={width,height,format,0,false};
        targets[address]={};
    }
    constexpr std::array<u32,5> bytes{4,3,2,2,2};
    if (format<bytes.size() && width && height && width<=1024 && height<=1024)
        info.size=size_t{width}*height*bytes[format];
}
void SetUiAlphaReady(bool ready) { alpha_ready=ready; }
bool UiPrepareFill(u32 start, u32 end, u32& value, u32& control) {
    if (!UiEnabled() || end<=start) return false;
    std::lock_guard lock(ui_mutex);
    const auto it=target_info.find(start);
    if (it==target_info.end() || !it->second.size ||
        u64{end}!=u64{start}+it->second.size) return false;
    targets[start]={};
    targets[start].sequence=++event_sequence;
    if (!alpha_ready || !it->second.bottom) return false;
    control &= ~0x300u;
    switch(it->second.format) {
    case 0: value=0xff00ffff;control|=0x200;break;
    case 1: value=(value&0xff000000)|0xff00ff;control|=0x100;break;
    case 2: value=(value&0xffff0000)|0xf83f;break;
    case 3: value=(value&0xffff0000)|0xf81f;break;
    case 4: value=(value&0xffff0000)|0xf0ff;break;
    default:return false;
    }
    ++key_count;
    return true;
}
void UiTransfer(u32 source, u32 destination) {
    if (!UiEnabled()) return;
    std::lock_guard lock(ui_mutex);
    ++transfer_count;
    const auto it=targets.find(source);
    if (it==targets.end()) {
        targets.erase(destination);
        completed[destination]={};
        if(destination==displayed_address) {last_completed={};have_completed=true;}
        return;
    }
    const Frame snapshot=it->second;
    it->second.sealed=true;
    targets[destination]=snapshot;
    targets[destination].sealed=true;
    completed[destination]=snapshot;
    if (snapshot.tags || target_info[source].bottom) { last_completed=snapshot;have_completed=true; }
}
void UiPresent(u32 bottom) {
    if (!UiEnabled()) return;
    std::lock_guard lock(ui_mutex);
    const auto exact=targets.find(bottom);
    if (exact!=targets.end()) {
        completed[bottom]=exact->second;
        exact->second.sealed=true;
        return;
    }
    u32 selected=0;
    for (const auto& [address,frame]:targets) {
        const auto info=target_info.find(address);
        if (info!=target_info.end() && info->second.bottom &&
            (!selected || frame.sequence>targets.at(selected).sequence)) selected=address;
    }
    if (selected) {
        completed[bottom]=targets.at(selected);
        targets.at(selected).sealed=true;
    }
}
void FinishUiFrame(u32 bottom) {
    if (!UiEnabled()) return;
    std::lock_guard lock(ui_mutex);
    displayed_address=bottom;
    const u32 previous=displayed.tags;
    const auto done=completed.find(bottom);
    if (done!=completed.end()) displayed=done->second;
    else if (have_completed) displayed=last_completed;
    if (previous!=displayed.tags) LOG_INFO(Core,"Leaf UI displayed scene tags: {}",displayed.tags);
}
std::array<float,4> UiKeyPixel(float r,float g,float b) {
    const float a=1-std::clamp(std::min(r,b)-g,0.0f,1.0f);
    const float divisor=std::max(a,.001f);
    const float t=std::clamp((a-.06f)/.08f,0.0f,1.0f);
    return {std::clamp((r-(1-a))/divisor,0.0f,1.0f),
            std::clamp(g/divisor,0.0f,1.0f),
            std::clamp((b-(1-a))/divisor,0.0f,1.0f),a*t*t*(3-2*t)};
}

std::vector<UiRegion> UiRegions(float width, float height) {
    std::lock_guard lock(ui_mutex);
    if (width<=0 || height<=0) return {};
    const u32 tags = displayed.tags;
    if (tags&Choice) {
        if ((tags&Map) && !(tags&HudButton))
            return {Region({0,0,320,224},width,height,.5f,.485f,.5f,1,.52f)};
        const auto source = displayed.choice.Valid() ? Crop(displayed.choice,3) : UiRect{40,1,280,216};
        return {Region(source,width,height,.965f,.54f,1,1,.8f)};
    }
    if ((tags&Map) && (tags&MapControl))
        return {Region({63,17,258,201},width,height,.97f,.03f,.5f,0,.5f,-64,0),
                Region({196,204,320,240},width,height,.97f,.03f,1,0,.5f,0,96),
                Region({0,204,124,240},width,height,.97f,.03f,1,0,.5f,-66,96)};
    if ((tags&Map) && !(tags&Tab))
        return {Region({0,0,320,224},width,height,.5f,.485f,.5f,1,.5f)};
    if ((tags&Tab) && !(tags&HudButton)) {
        auto region = Region({0,1,320,240},width,height,.5f,.5f,.5f,.5f,1,0,0,true);
        region.shade = .45f;
        return {region};
    }
    if ((tags&Tab) && (tags&HudButton)) {
        std::vector<UiRegion> regions{Region({0,0,210,25},width,height,0,0,0,0,1,4,0)};
        if (tags&HudMap) {
            auto source = displayed.map.Valid() ? Crop(displayed.map,1) : UiRect{64,26,280,214};
            source.top = std::max(26.0f,source.top);
            if (source.Valid()) regions.push_back(Region(source,width,height,1,1,1,1,.42f,-4,-4));
        }
        return regions;
    }
    return {};
}
bool UiTouch(float x, float y, float width, float height, float& touch_x, float& touch_y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    const auto regions = UiRegions(width,height);
    for (auto p=regions.rbegin();p!=regions.rend();++p) {
        const auto& d = p->destination;
        if (x<d.left || x>=d.right || y<d.top || y>=d.bottom) continue;
        touch_x = (p->source.left+(x-d.left)/(d.right-d.left)*(p->source.right-p->source.left))/320;
        touch_y = (p->source.top+(y-d.top)/(d.bottom-d.top)*(p->source.bottom-p->source.top))/240;
        return true;
    }
    return false;
}

void CompositeUi(u8* output, size_t pitch, u32 width, u32 height,
                 const u8* bottom, size_t bottom_size, u32 bottom_stride) {
    if (!output || !bottom || bottom_stride<320 || pitch<size_t{width}*4 ||
        bottom_size<size_t{bottom_stride}*240*4) return;
    const auto regions = UiRegions(static_cast<float>(width),static_cast<float>(height));
    static bool announced = false;
    if (!regions.empty() && !announced) { LOG_INFO(Core,"Leaf UI software composition active"); announced = true; }
    for (const auto& region : regions) {
        if (region.shade>0) {
            for (u32 y=0;y<height;++y) for (u32 x=0;x<width;++x)
                for (u32 c=0;c<3;++c) output[y*pitch+x*4+c] =
                    static_cast<u8>(output[y*pitch+x*4+c]*(1-region.shade));
        }
        const auto& dst = region.destination;
        for (int y=std::max(0,static_cast<int>(dst.top)); y<std::min(static_cast<int>(height),static_cast<int>(std::ceil(dst.bottom))); ++y) {
            for (int x=std::max(0,static_cast<int>(dst.left)); x<std::min(static_cast<int>(width),static_cast<int>(std::ceil(dst.right))); ++x) {
                const auto sx = static_cast<u32>(std::clamp(region.source.left+(x+.5f-dst.left)/(dst.right-dst.left)*(region.source.right-region.source.left),0.0f,319.0f));
                const auto sy = static_cast<u32>(std::clamp(region.source.top+(y+.5f-dst.top)/(dst.bottom-dst.top)*(region.source.bottom-region.source.top),0.0f,239.0f));
                const u8* src = bottom+(size_t{sy}*bottom_stride+sx)*4;
                u8* pixel = output+size_t{static_cast<u32>(y)}*pitch+static_cast<u32>(x)*4;
                const auto keyed=UiKeyPixel(src[0]/255.0f,src[1]/255.0f,src[2]/255.0f);
                for (size_t c=0;c<3;++c)
                    pixel[c]=static_cast<u8>(std::lround(keyed[2-c]*255*keyed[3]+pixel[c]*(1-keyed[3])));
            }
        }
    }
}
}
