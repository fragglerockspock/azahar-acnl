from pathlib import Path
from zipfile import ZipFile
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leaf-ui-archive-") as temp:
    folder=Path(temp)
    (folder/"test.cpp").write_text(r'''
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include "core/leaf_acnl/ui_archive.cpp"
std::vector<u8> Arc(const std::vector<u8>& data) {
    std::vector<u8> bytes(256+data.size());
    const auto word=[&](size_t p,u32 v){std::memcpy(bytes.data()+p,&v,4);};
    std::memcpy(bytes.data(),"darc",4);word(16,28);word(28,0x1000000);word(36,2);
    word(40,0);word(44,256);word(48,static_cast<u32>(data.size()));
    std::memcpy(bytes.data()+256,data.data(),data.size());return bytes;
}
std::vector<u8> Layout(float width,const char* pane="RootPane") {
    std::vector<u8> b(116);std::memcpy(b.data(),"CLYT",4);
    const u16 header=20;std::memcpy(b.data()+6,&header,2);
    std::memcpy(b.data()+20,"lyt1",4);const u32 section=20;std::memcpy(b.data()+24,&section,4);
    const float height=240;std::memcpy(b.data()+32,&width,4);std::memcpy(b.data()+36,&height,4);
    std::memcpy(b.data()+40,"pan1",4);const u32 pane_size=76;std::memcpy(b.data()+44,&pane_size,4);
    b[48]=1;b[49]=123;std::memcpy(b.data()+52,pane,std::strlen(pane));
    const float scale=1;std::memcpy(b.data()+100,&scale,4);std::memcpy(b.data()+104,&scale,4);
    return b;
}
int main(int argc,char** argv) {
    using namespace LeafACNL;
    for(float width:{320.0f,400.0f}) {
        auto b=Arc(Layout(width));const auto changed=PatchUiArchive("Layout/Menu/menu_tab.arc",b,.7692308f);
        float x=0,y=0;std::memcpy(&x,b.data()+356,4);std::memcpy(&y,b.data()+360,4);
        assert(y==1);
        if(width==400) assert(changed.top_roots==1 && std::abs(x-.7692308f)<.00001f);
        else assert(changed.top_roots==0 && x==1);
    }
    auto hidden=Arc(Layout(320,"P_base_00"));
    assert(PatchUiArchive("Layout/Menu/mvmnt_undscrn.arc",hidden,.7f).hidden_panes==1);
    assert(!(hidden[304]&1));
    for(u32 format:{10u,11u}) {
        const size_t payload=format==10 ? 8 : 16;
        std::vector<u8> data(payload+40,73);
        std::memcpy(data.data()+payload,"CLIM",4);std::memcpy(data.data()+payload+20,"imag",4);
        std::memcpy(data.data()+payload+32,&format,4);const u32 size=static_cast<u32>(payload);std::memcpy(data.data()+payload+36,&size,4);
        const auto footer=std::vector<u8>(data.begin()+payload,data.end());
        auto b=Arc(data);
        assert(PatchUiArchive("Layout/Menu/mvmnt_undscrn.arc",b,.7f).keyed_textures==1);
        constexpr std::array<u8,16> native{255,255,255,255,255,255,255,255,0,0,0,0,0,255,0,255};
        assert(!std::memcmp(b.data()+256,native.data()+(format==10 ? 8 : 0),payload));
        assert(!std::memcmp(b.data()+256+payload,footer.data(),40));
    }
    if(argc==2) {
        for(const auto name:{"menu_tab.arc","mvmnt_undscrn.arc","msg_win.arc","slct_win.arc"}) {
            std::ifstream f(std::string(argv[1])+"/"+name,std::ios::binary);
            std::vector<u8> b{std::istreambuf_iterator<char>(f),{}};assert(!b.empty());
            const auto changed=PatchUiArchive(std::string("Layout/")+(std::string(name)=="msg_win.arc" ? "msgWin/" : (std::string(name)=="slct_win.arc" ? "selWin/" : "Menu/"))+name,b,.7692308f);
            std::cerr<<name<<" roots "<<changed.top_roots<<" panes "<<changed.hidden_panes<<" keys "<<changed.keyed_textures<<"\n";
            if(std::string(name)=="menu_tab.arc")assert(changed.top_roots==0 && changed.hidden_panes==4);
            if(std::string(name)=="mvmnt_undscrn.arc")assert(changed.top_roots==1 && changed.hidden_panes==2 && changed.keyed_textures==11);
            if(std::string(name)=="msg_win.arc")assert(changed.top_roots==2);
            if(std::string(name)=="slct_win.arc")assert(changed.top_roots==0 && changed.hidden_panes==3);
        }
    }
    std::cout<<"Leaf native masks, key blocks and independent top/bottom proportions passed\n";
}
''')
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I"+str(root/"src"),str(folder/"test.cpp"),"-o",str(folder/"test")],check=True)
    args=[str(folder/"test")]
    if os.environ.get("LEAF_LAYOUT_ZIP"):
        with ZipFile(os.environ["LEAF_LAYOUT_ZIP"]) as archive:
            for name in ["Menu/menu_tab.arc","Menu/mvmnt_undscrn.arc","msgWin/msg_win.arc","selWin/slct_win.arc"]:
                (folder/Path(name).name).write_bytes(archive.read(name))
        args.append(str(folder))
    subprocess.run(args,check=True)
