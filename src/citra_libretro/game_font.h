#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "common/common_types.h"

namespace LibRetro {
struct OverlayGlyph {
    int left=0,advance=0,width=0,height=0;
    std::vector<u8> alpha;
};
struct OverlayFont {
    int height=0;
    std::array<OverlayGlyph,95> glyphs;
    bool Valid() const { return height>0 && std::any_of(glyphs['A'-32].alpha.begin(),glyphs['A'-32].alpha.end(),[](u8 a){return a!=0;}); }
};

template<class Sample>
OverlayFont ReadGameFont(const std::vector<u8>& bytes,Sample sample) {
    OverlayFont font;
    if(bytes.size()<20 || bytes.size()>16*1024*1024 || std::memcmp(bytes.data(),"CFNT",4))return font;
    const auto half=[&](size_t p)->u32 {return p+2<=bytes.size() ? u32(bytes[p])|(u32(bytes[p+1])<<8) : 0;};
    const auto word=[&](size_t p)->u32 {return p+4<=bytes.size() ? half(p)|(half(p+2)<<16) : 0;};
    if(half(4)!=0xfeff || word(12)<20 || word(12)>bytes.size())return font;
    size_t tglp=0,finf=0;
    std::vector<size_t> maps,widths;
    size_t p=half(6);
    const auto blocks=half(16);
    if(!blocks || blocks>128)return {};
    for(unsigned i=0;i<blocks;++i) {
        if(p+8>bytes.size())return {};
        const size_t size=word(p+4);
        if(size<8 || size>bytes.size()-p)return {};
        if(!std::memcmp(bytes.data()+p,"TGLP",4))tglp=p;
        if(!std::memcmp(bytes.data()+p,"FINF",4))finf=p;
        if(!std::memcmp(bytes.data()+p,"CMAP",4))maps.push_back(p);
        if(!std::memcmp(bytes.data()+p,"CWDH",4))widths.push_back(p);
        p+=size;
    }
    if(!tglp || !finf || word(tglp+4)<32 || word(finf+4)<32)return {};
    const u32 cw=bytes[tglp+8],ch=bytes[tglp+9],sheet_size=word(tglp+12);
    const u32 sheets=half(tglp+16),format=half(tglp+18),columns=half(tglp+20),rows=half(tglp+22);
    const u32 sw=half(tglp+24),sh=half(tglp+26),data=word(tglp+28);
    constexpr std::array<u32,14> bits{32,24,16,16,16,16,16,8,8,8,4,4,4,8};
    if(!cw || !ch || cw>128 || ch>128 || !columns || !rows || !sheets || sheets>64 ||
       sw<8 || sh<8 || sw>2048 || sh>2048 || sw%8 || sh%8 || format>=bits.size() ||
       u64(sw)*sh*bits[format]/8>sheet_size || data>bytes.size() ||
       u64(sheet_size)*sheets>bytes.size()-data || u64(columns)*(cw+1)>sw || u64(rows)*(ch+1)>sh)return {};
    font.height=ch;
    for(u32 code=32;code<=126;++code) {
        u32 index=0xffff;
        for(auto map:maps) {
            const size_t end=map+word(map+4);
            if(end<map+20 || code<half(map+8) || code>half(map+10))continue;
            const auto method=half(map+12);
            if(method==0 && map+22<=end)index=half(map+20)+code-half(map+8);
            if(method==1 && u64(map)+22+2*(code-half(map+8))<=end)index=half(map+20+2*(code-half(map+8)));
            if(method==2 && map+22<=end) {
                const auto count=half(map+20);
                if(u64(map)+22+u64(count)*4>end)return {};
                for(u32 i=0;i<count;++i)if(half(map+22+4*i)==code){index=half(map+24+4*i);break;}
            }
            if(index!=0xffff)break;
        }
        if(index==0xffff || index>=u64(columns)*rows*sheets)continue;
        auto& glyph=font.glyphs[code-32];
        glyph.left=static_cast<int8_t>(bytes[finf+12]);glyph.width=bytes[finf+13];glyph.advance=bytes[finf+14];
        for(auto width:widths) {
            const size_t end=width+word(width+4);
            if(end<width+16 || index<half(width+8) || index>half(width+10))continue;
            const size_t at=width+16+(index-half(width+8))*3;
            if(at+3>end)return {};
            glyph.left=static_cast<int8_t>(bytes[at]);glyph.width=bytes[at+1];glyph.advance=bytes[at+2];break;
        }
        if(glyph.width>static_cast<int>(cw) || glyph.advance>256)return {};
        glyph.height=ch;glyph.alpha.resize(glyph.width*ch);
        const auto sheet=index/(columns*rows),cell=index%(columns*rows);
        const auto x=cell%columns*(cw+1)+1,y=cell/columns*(ch+1)+1;
        for(int row=0;row<glyph.height;++row)for(int col=0;col<glyph.width;++col)
            glyph.alpha[row*glyph.width+col]=sample(bytes.data()+data+sheet*sheet_size,format,sw,sh,x+col,y+row);
    }
    return font.Valid() ? font : OverlayFont{};
}
}
