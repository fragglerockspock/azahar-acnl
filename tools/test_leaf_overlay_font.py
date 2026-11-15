from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <cassert>
#include "citra_libretro/rounded_overlay_font.h"
int main() {
    using namespace LibRetro;
    const auto fallback=RoundedOverlayFont();assert(fallback.Valid());
    assert(fallback.glyphs['i'-32].advance<fallback.glyphs['W'-32].advance);
    assert(!fallback.glyphs['a'-32].alpha.empty());
    size_t edges=0;for(const auto& glyph:fallback.glyphs)for(auto a:glyph.alpha)edges+=a>0 && a<255;
    assert(edges>1000);
    for(unsigned method=0;method<3;++method) {
        const u32 finf=20,tglp=52,cwdh=84,cmap=484;
        const u32 map_size=method==0 ? 22 : method==1 ? 210 : 402;
        const u32 data=(cmap+map_size+15)&~15u;
        std::vector<u8> bytes(data+4096);
        const auto half=[&](u32 p,u32 v){bytes[p]=v;bytes[p+1]=v>>8;};
        const auto word=[&](u32 p,u32 v){half(p,v);half(p+2,v>>16);};
        std::memcpy(bytes.data(),"CFNT",4);half(4,0xfeff);half(6,20);word(12,bytes.size());half(16,4);
        std::memcpy(bytes.data()+finf,"FINF",4);word(finf+4,32);bytes[finf+13]=3;bytes[finf+14]=4;
        std::memcpy(bytes.data()+tglp,"TGLP",4);word(tglp+4,32);bytes[tglp+8]=3;bytes[tglp+9]=5;
        word(tglp+12,4096);half(tglp+16,1);half(tglp+18,8);half(tglp+20,16);half(tglp+22,8);
        half(tglp+24,64);half(tglp+26,64);word(tglp+28,data);
        std::memcpy(bytes.data()+cwdh,"CWDH",4);word(cwdh+4,400);half(cwdh+8,0);half(cwdh+10,127);
        for(u32 i=0;i<128;++i){bytes[cwdh+16+i*3]=255;bytes[cwdh+17+i*3]=3;bytes[cwdh+18+i*3]=4;}
        std::memcpy(bytes.data()+cmap,"CMAP",4);word(cmap+4,map_size);half(cmap+8,32);half(cmap+10,126);half(cmap+12,method);
        if(method==1)for(u32 i=0;i<95;++i)half(cmap+20+2*i,i);
        if(method==2){half(cmap+20,95);for(u32 i=0;i<95;++i){half(cmap+22+4*i,32+i);half(cmap+24+4*i,i);}}
        for(u32 i=0;i<4096;++i)bytes[data+i]=i%251;
        const auto sample=[](const u8* data,u32 format,u32 w,u32 h,u32 x,u32 y){assert(format==8 && w==64 && h==64 && x<w && y<h);return data[y*w+x];};
        const auto font=ReadGameFont(bytes,sample);assert(font.Valid() && font.height==5);
        const auto& a=font.glyphs['A'-32];assert(a.left==-1 && a.advance==4 && a.width==3 && a.alpha.size()==15);
        assert(a.alpha[0]==bytes[data+13*64+5]);
        auto bad=bytes;bad.resize(100);assert(!ReadGameFont(bad,sample).Valid());
        bad=bytes;bad[tglp+18]=14;assert(!ReadGameFont(bad,sample).Valid());
        bad=bytes;bad[cmap+4]=255;bad[cmap+7]=255;assert(!ReadGameFont(bad,sample).Valid());
    }
}
'''
with tempfile.TemporaryDirectory(prefix='leaf-font-') as directory:
    p=Path(directory);(p/'test.cpp').write_text(source)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Garden font parser, atlas bounds and rounded fallback checks passed')
