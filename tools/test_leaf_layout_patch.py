from pathlib import Path
from zipfile import ZipFile
import hashlib
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leaf-layout-patch-") as directory:
    folder=Path(directory)
    source=folder/"test.cpp"
    source.write_text(r'''
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include "core/leaf_acnl/layout_patch.h"
using namespace LeafACNL::LayoutPatch;
Bytes Header(const char* magic) {
    Bytes b(20);std::memcpy(b.data(),magic,4);Half(b,4,0xfeff);Half(b,6,20);Word(b,8,0x2020000);Word(b,12,20);return b;
}
Bytes PaneBytes(const char* type,const char* name,size_t size=76) {
    Bytes b(size);std::memcpy(b.data(),type,4);Word(b,4,size);b[8]=1;b[9]=4;b[10]=255;
    Name(b,12,24,name);Float(b,60,1);Float(b,64,1);Float(b,68,320);Float(b,72,240);return b;
}
Bytes Marker(const char* type) {Bytes b(8);std::memcpy(b.data(),type,4);Word(b,4,8);return b;}
Bytes Animation() {
    Bytes b(48);std::memcpy(b.data(),"pai1",4);Word(b,4,b.size());Half(b,14,1);Word(b,16,20);Word(b,20,24);Name(b,24,20,"T_tex_00");
    return WriteLayout({Header("CLAN"),{b}});
}
Bytes Load(const char* path) {std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Save(const std::string& path,const Bytes& bytes) {std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
int main(int argc,char** argv) {
    auto root_pane=PaneBytes("pan1","RootPane"),text=PaneBytes("txt1","T_tex_00",116);
    Float(text,36,8);Float(text,40,-10);Float(text,60,0);Float(text,64,0);
    Bytes material(80);Name(material,0,20,"W_slct_00C ");Word(material,48,0x15);Half(material,52,0);
    Bytes window=PaneBytes("wnd1","W_slct_00",128);window[92]=0;Word(window,96,104);Word(window,100,124);Half(window,120,0);
    Layout target{Header("CLYT"),{TextureSection({}),MaterialSection({}),root_pane,Marker("pas1"),text,Marker("pae1")}};
    Layout donor{Header("CLYT"),{TextureSection({"window.bclim"}),MaterialSection({material}),root_pane,Marker("pas1"),window,Marker("pae1")}};
    std::vector<ArcEntry> target_entries{{"blyt/opng_titl_00.bclyt",WriteLayout(target)},
        {"anim/opng_titl_00_in.bclan",Animation()},{"anim/opng_titl_00_loop.bclan",Animation()},
        {"anim/opng_titl_00_out.bclan",Animation()}};
    std::vector<ArcEntry> donor_entries{{"blyt/cmn_slct_win.bclyt",WriteLayout(donor)},{"bclim/window.bclim",Bytes{1,2,3}}};
    auto archive=WriteArc(target_entries),original=archive,source=WriteArc(donor_entries);
    assert(PatchTitlePressA(archive,source)==1 && archive!=original);
    const auto patched=archive;assert(PatchTitlePressA(archive,source)==0 && archive==patched);
    std::vector<ArcEntry> result;assert(ReadArc(archive,result));
    assert(Find(result,"timg/window.bclim")->bytes==Bytes({1,2,3}));
    Layout layout;assert(ReadLayout(Find(result,"blyt/opng_titl_00.bclyt")->bytes,"CLYT",layout));
    const int parent=Pane(layout,"N_leaf_press"),graft=Pane(layout,"W_leaf_press"),caption=Pane(layout,"T_tex_00");
    assert(parent>=0 && graft==parent+2 && caption==graft+1);
    assert(Type(layout.sections[parent+1],"pas1") && Type(layout.sections[caption+1],"pae1"));
    assert(Float(layout.sections[parent],36)==8 && Float(layout.sections[parent],40)==-10);
    assert(Float(layout.sections[parent],60)==0 && Float(layout.sections[parent],64)==0);
    assert(Float(layout.sections[caption],36)==0 && Float(layout.sections[caption],40)==0);
    assert(Float(layout.sections[caption],60)==1 && Float(layout.sections[caption],64)==1);
    assert(Float(layout.sections[graft],36)==-8 && Float(layout.sections[graft],40)==33);
    assert(Float(layout.sections[graft],68)==180 && Float(layout.sections[graft],72)==64);
    assert(Find(result,"anim/opng_titl_00_out.bclan")->bytes==Animation());
    for(const auto name:{"anim/opng_titl_00_in.bclan","anim/opng_titl_00_loop.bclan"}) {
        const auto& b=Find(result,name)->bytes;const std::string needle="N_leaf_press";assert(std::search(b.begin(),b.end(),needle.begin(),needle.end())!=b.end());
        assert(!RetargetAnimation(Find(result,name)->bytes,"T_tex_00","N_leaf_press"));
    }
    donor_entries.pop_back();auto missing=WriteArc(donor_entries);auto unchanged=original;
    assert(PatchTitlePressA(unchanged,missing)==0 && unchanged==original);
    for(size_t length=0;length<original.size();++length) {
        auto truncated=original;truncated.resize(length);const auto before=truncated;
        assert(PatchTitlePressA(truncated,source)==0 && truncated==before);
    }
    auto corrupt=original;Word(corrupt,36,UINT32_MAX);const auto invalid=corrupt;
    assert(PatchTitlePressA(corrupt,source)==0 && corrupt==invalid);
    if(argc==4) {
        auto game=Load(argv[1]),other=Load(argv[2]);assert(PatchTitlePressA(game,other)==1);
        Save(argv[3],game);std::vector<ArcEntry> files;assert(ReadArc(game,files));
        for(const auto& file:files) {
            const auto path=std::string(argv[3])+"."+file.name.substr(file.name.find_last_of('/')+1);
            Save(path,file.bytes);
        }
    }
    std::cout<<"Leaf native title graft, material/texture imports, animation wrapping, archive growth and malformed-input checks passed\n";
}
''')
    sanitizer=["-fsanitize=address,undefined","-fno-omit-frame-pointer"] if os.environ.get("LEAF_SANITIZE") else []
    subprocess.run([os.environ.get("CXX","g++"),"-std=c++17","-Wall","-Wextra","-Werror"]+sanitizer+["-I"+str(root/"src"),str(source),"-o",str(folder/"test")],check=True)
    arguments=[str(folder/"test")]
    if os.environ.get("LEAF_LAYOUT_ZIP"):
        with ZipFile(os.environ["LEAF_LAYOUT_ZIP"]) as archive:
            title=archive.read("Title/opng_titl.arc");donor=archive.read("selWin/cmn_slct_win.arc")
        (folder/"title.arc").write_bytes(title);(folder/"donor.arc").write_bytes(donor)
        arguments += [str(folder/"title.arc"),str(folder/"donor.arc"),str(folder/"patched.arc")]
    subprocess.run(arguments,check=True)
    if os.environ.get("LEAF_LAYOUT_ZIP") and hashlib.sha256(title).hexdigest()=="c2f4d251b1cdd6dfed433ed1188be01bdcb906b6423c1aefb5c38f6e9102b739" and hashlib.sha256(donor).hexdigest()=="40707130a67a16fcc6a19f84694e6e4bf786fd6aa6253f7b8b9e1ef78536f28b":
        golden={"opng_titl_00.bclyt":"481a46a4d650d766355d61be4579461c5b8a385a75cbf478083109fa5075ad25",
                "opng_titl_00_in.bclan":"8c283b3028f7674c290e8a2d45ebaec99449f7a58fa12bb0bd9b6eea1557ad84",
                "opng_titl_00_loop.bclan":"08ce8dfbbbf88dead86423d587bdc609eff4e1295fe60754895b1be333ec7e8c"}
        for name,digest in golden.items():
            assert hashlib.sha256((folder/("patched.arc."+name)).read_bytes()).hexdigest()==digest,name
        print("Native APK golden layout and animation hashes passed")
    if os.environ.get("LEAF_NATIVE_LAYOUT_ORACLE"):
        oracle=Path(os.environ["LEAF_NATIVE_LAYOUT_ORACLE"])
        count=0
        for path in oracle.rglob("*"):
            if path.is_file():
                actual=folder/("patched.arc."+path.name)
                assert actual.read_bytes()==path.read_bytes(),path.name
                count+=1
        print(f"All {count} title archive files match Leaf native GraftWindow/WrapForAnimation byte for byte")
