#pragma once

#include <cmath>
#include <cstring>
#include <vector>
#include <string_view>
#include "common/common_types.h"

namespace LeafACNL {
inline size_t PatchTopLayoutScale(std::vector<u8>& bytes, float correction) {
    if (bytes.size()<28 || std::memcmp(bytes.data(),"darc",4) ||
        !std::isfinite(correction) || correction<=0 || correction>=2) return 0;
    const auto read=[&](size_t p) {
        u32 value=0;
        if (p<=bytes.size()-4) std::memcpy(&value,bytes.data()+p,4);
        return value;
    };
    const size_t table=read(16);
    if (table>bytes.size()-12) return 0;
    const size_t count=read(table+8);
    if (!count || count>(bytes.size()-table)/12) return 0;
    size_t changes=0;
    for (size_t i=0;i<count;++i) {
        if (read(table+i*12)>>24) continue;
        const size_t offset=read(table+i*12+4), size=read(table+i*12+8);
        if (offset>bytes.size() || size>bytes.size()-offset || size<20 ||
            std::memcmp(bytes.data()+offset,"CLYT",4)) continue;
        u16 header=0;
        std::memcpy(&header,bytes.data()+offset+6,2);
        if (header<20 || header>size) continue;
        const size_t end=offset+size;
        bool top=false;
        for (size_t p=offset+header;p+8<=end;) {
            const size_t section=read(p+4);
            if (section<8 || section>end-p) break;
            if (!std::memcmp(bytes.data()+p,"lyt1",4) && section>=20) {
                float width=0,height=0;
                std::memcpy(&width,bytes.data()+p+12,4);
                std::memcpy(&height,bytes.data()+p+16,4);
                top=width==400 && height==240;
            }
            if (top && section>=76 && !std::memcmp(bytes.data()+p,"pan1",4) &&
                !std::memcmp(bytes.data()+p+12,"RootPane\0",9)) {
                float original=0;
                std::memcpy(&original,bytes.data()+p+60,4);
                if (original==1 && correction!=1) {
                    std::memcpy(bytes.data()+p+60,&correction,4);
                    ++changes;
                }
            }
            p+=section;
        }
    }
    return changes;
}
inline size_t PatchLeafPaneMasks(std::string_view path,std::vector<u8>& bytes) {
    const auto layout=path.find("Layout/");
    if (layout!=std::string_view::npos) path.remove_prefix(layout+7);
    const bool tab=path=="Menu/menu_tab.arc",choice=path=="selWin/slct_win.arc";
    const bool wallpaper=path=="Menu/mvmnt_undscrn.arc";
    const bool inventory=path=="ItemWin/item_scrn.arc";
    const bool title=path=="Title/opng_titl.arc";
    if ((!tab && !choice && !wallpaper && !inventory && !title) || bytes.size()<28 || std::memcmp(bytes.data(),"darc",4)) return 0;
    const auto read=[&](size_t p) { u32 value=0;if(p<=bytes.size()-4)std::memcpy(&value,bytes.data()+p,4);return value; };
    const size_t table=read(16);
    if (table>bytes.size()-12) return 0;
    const size_t count=read(table+8);
    if (!count || count>(bytes.size()-table)/12) return 0;
    size_t changes=0;
    for(size_t i=0;i<count;++i) {
        if(read(table+i*12)>>24)continue;
        const size_t offset=read(table+i*12+4),size=read(table+i*12+8);
        if(offset>bytes.size() || size>bytes.size()-offset || size<20 || std::memcmp(bytes.data()+offset,"CLYT",4))continue;
        u16 header=0;std::memcpy(&header,bytes.data()+offset+6,2);
        if(header<20 || header>size)continue;
        bool canvas=false;
        for(size_t p=offset+header;p+8<=offset+size;) {
            const size_t section=read(p+4);
            if(section<8 || section>offset+size-p)break;
            if(!std::memcmp(bytes.data()+p,"lyt1",4) && section>=20) {
                float width=0,height=0;std::memcpy(&width,bytes.data()+p+12,4);std::memcpy(&height,bytes.data()+p+16,4);
                canvas=height==240 && width==(wallpaper ? 400 : 320);
            }
            if(canvas && section>=76) {
                const char* text=reinterpret_cast<const char*>(bytes.data()+p+12);
                const std::string_view pane(text,strnlen(text,24));
                const bool hide=(tab && (pane=="im_tab_05" || pane=="B_back_00" || pane=="P_post_00" || pane=="B_post_00")) ||
                    (choice && (pane=="P_kuchi_00" || pane=="P_kuchi_sh_00")) ||
                    (wallpaper && (pane=="P_base_00" || pane=="P_base_01")) ||
                    (inventory && (pane=="P_bg_C" || pane=="P_bg_T" || pane=="P_bg_B")) ||
                    (title && (pane=="P_bg_00" || pane=="P_bg_01" || pane=="P_bg_02" ||
                               pane=="P_bg_03" || pane=="P_bg_04" || pane=="P_filter"));
                if(hide && (bytes[p+8]&1)) {bytes[p+8]&=254;++changes;}
            }
            p+=section;
        }
    }
    return changes;
}

inline size_t PatchLeafFade(std::string_view path,std::vector<u8>& bytes,float correction) {
    const bool fade=path.find("Layout/Wipe/fade.arc")!=std::string_view::npos;
    const bool circle=path.find("Layout/Wipe/wipe_circle.arc")!=std::string_view::npos;
    if ((!fade && !circle) || !std::isfinite(correction) ||
        correction<=0 || correction>=2 || bytes.size()<28 || std::memcmp(bytes.data(),"darc",4)) return 0;
    const auto read=[&](size_t p) { u32 value=0;if(p<=bytes.size()-4)std::memcpy(&value,bytes.data()+p,4);return value; };
    const size_t table=read(16);
    if(table>bytes.size()-12)return 0;
    const size_t count=read(table+8);
    if(!count || count>(bytes.size()-table)/12)return 0;
    size_t changes=0;
    for(size_t i=0;i<count;++i) {
        if(read(table+i*12)>>24)continue;
        const size_t offset=read(table+i*12+4),size=read(table+i*12+8);
        if(offset>bytes.size() || size>bytes.size()-offset || size<20 || std::memcmp(bytes.data()+offset,"CLYT",4))continue;
        u16 header=0;std::memcpy(&header,bytes.data()+offset+6,2);
        if(header<20 || header>size)continue;
        for(size_t p=offset+header;p+76<=offset+size;) {
            const size_t section=read(p+4);
            if(section<8 || section>offset+size-p)break;
            const char* name=reinterpret_cast<const char*>(bytes.data()+p+12);
            const std::string_view pane(name,strnlen(name,24));
            if(circle && section>=76 && !std::memcmp(bytes.data()+p,"pan1",4) && pane=="N_wipe_00") {
                float x=0,y=0;std::memcpy(&x,bytes.data()+p+60,4);std::memcpy(&y,bytes.data()+p+64,4);
                if(x==1 && y==1 && correction!=1) {
                    const float scale=1/correction;
                    std::memcpy(bytes.data()+p+60,&scale,4);std::memcpy(bytes.data()+p+64,&scale,4);++changes;
                }
            }
            const bool fill=fade && pane=="P_fade_00";
            if(section>=76 && !std::memcmp(bytes.data()+p,"pic1",4) && fill) {
                float width=0;std::memcpy(&width,bytes.data()+p+68,4);
                if(width==400 && correction!=1) {
                    width/=correction;std::memcpy(bytes.data()+p+68,&width,4);++changes;
                }
            }
            p+=section;
        }
    }
    return changes;
}

}
