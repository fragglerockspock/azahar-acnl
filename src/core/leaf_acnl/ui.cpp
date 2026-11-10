#include <algorithm>
#include <atomic>
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
    u64 epoch = 0;
    u32 tags = 0;
    UiRect choice;
    UiRect map;
};
std::unordered_map<u32, Frame> targets;
u64 epoch = 1;
Frame displayed;
void Union(UiRect& a, UiRect b) {
    if (!b.Valid()) return;
    if (!a.Valid()) { a = b; return; }
    a = {std::min(a.left,b.left), std::min(a.top,b.top),
         std::max(a.right,b.right), std::max(a.bottom,b.bottom)};
}
u32 Tags(std::string_view name) {
    u32 tags = 0;
    if (name.find("Menu/menu_tab.arc:") == 0) tags |= Tab;
    if (name == "Menu/menu_tab.arc:my_men_btn_00.bclim") tags |= HudButton;
    for (const auto prefix : {"Menu/map_village.arc:", "Menu/map_island.arc:",
                              "Menu/map_lobby.arc:", "Menu/map_exb.arc:"}) {
        if (name.find(prefix) == 0) tags |= Map;
    }
    if (name.find("selWin/slct_win.arc:") == 0) tags |= Choice;
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
void ResetUi() {
    std::lock_guard lock(ui_mutex);
    enabled = false;
    textures.clear();
    dimensions.clear();
    targets.clear();
    displayed = {};
    epoch = 1;
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
    if (frame.epoch != epoch) frame = {epoch,0,{}, {}};
    frame.tags |= tags;
    if (tags&Choice) Union(frame.choice,bounds);
    if (tags&HudMap) Union(frame.map,bounds);
}
void UiTransfer(u32 source, u32 destination) {
    if (!UiEnabled()) return;
    std::lock_guard lock(ui_mutex);
    const auto it = targets.find(source);
    if (it != targets.end()) targets[destination] = it->second;
    else targets.erase(destination);
}
void FinishUiFrame(u32 bottom) {
    if (!UiEnabled()) return;
    std::lock_guard lock(ui_mutex);
    const auto it = targets.find(bottom);
    const u32 previous = displayed.tags;
    displayed = it == targets.end() ? Frame{} : it->second;
    if (previous != displayed.tags) LOG_INFO(Core,"Leaf UI displayed scene tags: {}",displayed.tags);
    ++epoch;
    for (auto p=targets.begin();p!=targets.end();) {
        if (p->first!=bottom && p->second.epoch+120<epoch) p=targets.erase(p);
        else ++p;
    }
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
                pixel[0]=src[2]; pixel[1]=src[1]; pixel[2]=src[0]; pixel[3]=0;
            }
        }
    }
}
}
