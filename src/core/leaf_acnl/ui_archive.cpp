#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include "core/leaf_acnl/ui_archive.h"
namespace LeafACNL {
UiArchiveChanges PatchUiArchive(std::string_view path, std::vector<u8>& bytes, float top_scale) {
    UiArchiveChanges changes;
    if (bytes.size()<28 || std::memcmp(bytes.data(),"darc",4)) return changes;
    const auto read = [&](size_t p) { u32 v=0; if(p<=bytes.size()-4) std::memcpy(&v,bytes.data()+p,4); return v; };
    const size_t table=read(16);
    if (table>bytes.size()-12) return changes;
    const size_t count=read(table+8);
    if (!count || count>(bytes.size()-table)/12) return changes;
    const size_t names=table+count*12;
    const auto layout=path.find("Layout/");
    if (layout!=std::string_view::npos) path.remove_prefix(layout+7);
    for (size_t i=0;i<count;++i) {
        const u32 entry=read(table+i*12);
        if (entry>>24) continue;
        const size_t offset=read(table+i*12+4), size=read(table+i*12+8);
        if (offset>bytes.size() || size>bytes.size()-offset) continue;
        std::string name;
        for(size_t p=names+(entry&0xffffff);p+1<bytes.size() && bytes[p] && !bytes[p+1] && name.size()<128;p+=2) name+=static_cast<char>(bytes[p]);
        if (path=="Menu/mvmnt_undscrn.arc" && size>=40) {
            const size_t footer=offset+size-40;
            if (!std::memcmp(bytes.data()+footer,"CLIM",4) && !std::memcmp(bytes.data()+footer+20,"imag",4)) {
                const u32 format=read(footer+32), payload=read(footer+36);
                if (payload<=size-40 && (format==10 || format==11)) {
                    constexpr std::array<u8,16> key{255,255,255,255,255,255,255,255,0,0,0,0,0,255,0,255};
                    const size_t block=format==10 ? 8 : 16, start=format==10 ? 8 : 0;
                    for(size_t p=0;p+block<=payload;p+=block) std::memcpy(bytes.data()+offset+p,key.data()+start,block);
                    ++changes.keyed_textures;
                }
            }
        }
        if (size<40 || std::memcmp(bytes.data()+offset,"CLYT",4)) continue;
        const size_t end=offset+size;
        u16 header_size=0;
        std::memcpy(&header_size,bytes.data()+offset+6,2);
        bool top=false;
        for(size_t p=offset+header_size;header_size>=20 && p+8<=end;) {
            const size_t section=read(p+4);
            if (section<8 || section>end-p) break;
            if (!std::memcmp(bytes.data()+p,"lyt1",4) && section>=20) {
                float width=0,height=0;
                std::memcpy(&width,bytes.data()+p+12,4); std::memcpy(&height,bytes.data()+p+16,4);
                top=width==400 && height==240;
            }
            if (section>=76) {
                const auto pane=std::string_view(reinterpret_cast<const char*>(bytes.data()+p+12),strnlen(reinterpret_cast<const char*>(bytes.data()+p+12),24));
                if (top && !std::memcmp(bytes.data()+p,"pan1",4) && pane=="RootPane" && std::isfinite(top_scale) && top_scale>0 && top_scale<2) {
                    float original=0; std::memcpy(&original,bytes.data()+p+60,4);
                    if (original==1 && top_scale!=1) { std::memcpy(bytes.data()+p+60,&top_scale,4); ++changes.top_roots; }
                }
                const bool hide=(path=="Menu/mvmnt_undscrn.arc" && (pane=="P_base_00" || pane=="P_base_01")) ||
                    (path=="Menu/menu_tab.arc" && (pane=="im_tab_05" || pane=="B_back_00" || pane=="P_post_00" || pane=="B_post_00")) ||
                    (path=="selWin/slct_win.arc" && (pane=="P_kuchi_00" || pane=="P_kuchi_sh_00"));
                if (hide) { bytes[p+8]&=254; ++changes.hidden_panes; }
                if (path=="selWin/slct_win.arc" && pane=="P_pattern_00") { bytes[p+9]=255; ++changes.hidden_panes; }
            }
            p+=section;
        }
    }
    return changes;
}
}
