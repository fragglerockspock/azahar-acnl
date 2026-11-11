from pathlib import Path
from zipfile import ZipFile
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leaf-top-layout-") as temp:
    folder = Path(temp)
    (folder / "test.cpp").write_text(r'''
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include "core/leaf_acnl/top_layout_scale.h"
std::vector<u8> Arc(float width,const char* name="RootPane") {
    std::vector<u8> b(372);
    const auto word=[&](size_t p,u32 v){std::memcpy(b.data()+p,&v,4);};
    std::memcpy(b.data(),"darc",4);word(16,28);word(28,0x1000000);word(36,2);
    word(44,256);word(48,116);
    std::memcpy(b.data()+256,"CLYT",4);
    const u16 header=20;std::memcpy(b.data()+262,&header,2);
    std::memcpy(b.data()+276,"lyt1",4);word(280,20);
    const float height=240,scale=1;
    std::memcpy(b.data()+288,&width,4);std::memcpy(b.data()+292,&height,4);
    std::memcpy(b.data()+296,"pan1",4);word(300,76);
    b[304]=1;b[305]=123;std::memcpy(b.data()+308,name,std::strlen(name));
    std::memcpy(b.data()+356,&scale,4);std::memcpy(b.data()+360,&scale,4);
    return b;
}
int main(int argc,char** argv) {
    using LeafACNL::PatchTopLayoutScale;
    const float correction=.7692308f;
    auto top=Arc(400),expected=top;
    std::memcpy(expected.data()+356,&correction,4);
    assert(PatchTopLayoutScale(top,correction)==1 && top==expected);
    assert(PatchTopLayoutScale(top,correction)==0 && top==expected);
    for(float width:{320.0f,256.0f,512.0f}) {
        auto bottom=Arc(width);const auto original=bottom;
        assert(PatchTopLayoutScale(bottom,correction)==0 && bottom==original);
    }
    auto child=Arc(400,"ChildPane");const auto original=child;
    assert(PatchTopLayoutScale(child,correction)==0 && child==original);
    for(size_t size=0;size<372;++size) {
        auto truncated=Arc(400);truncated.resize(size);
        assert(PatchTopLayoutScale(truncated,correction)==0);
    }
    if(argc==2) {
        size_t scanned=0,changed=0;
        for(const auto& file:std::filesystem::directory_iterator(argv[1])) {
            if(file.path().extension()!=".arc")continue;
            std::ifstream input(file.path(),std::ios::binary);
            std::vector<u8> bytes{std::istreambuf_iterator<char>(input),{}};
            const auto before=bytes;
            const auto count=PatchTopLayoutScale(bytes,correction);
            assert(bytes.size()==before.size());
            size_t differences=0;
            for(size_t i=0;i<bytes.size();++i)differences+=bytes[i]!=before[i];
            assert(differences<=4*count);
            assert(PatchTopLayoutScale(bytes,correction)==0);
            std::ofstream output(file.path().string()+".out",std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            ++scanned;changed+=count;
        }
        assert(scanned>0 && changed>0);
        std::cout<<scanned<<" supplied archives checked; only "<<changed<<" top root scales changed\n";
    }
    std::cout<<"Independent top/bottom layout scales and unchanged pane bytes passed\n";
}
''')
    sanitizer = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.environ.get("LEAF_SANITIZE") else []
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror"] + sanitizer +
                   ["-I" + str(root / "src"), str(folder / "test.cpp"), "-o", str(folder / "test")], check=True)
    arguments = [str(folder / "test")]
    archives = {}
    if os.environ.get("LEAF_LAYOUT_ZIP"):
        inputs = folder / "inputs"
        inputs.mkdir()
        with ZipFile(os.environ["LEAF_LAYOUT_ZIP"]) as archive:
            for index, name in enumerate(archive.namelist()):
                if not name.endswith(".arc"):
                    continue
                path = inputs / f"{index}.arc"
                data = archive.read(name)
                path.write_bytes(data)
                archives[name] = (path, data)
        arguments.append(str(inputs))
    subprocess.run(arguments, check=True)
    for name in ("Menu/menu_tab.arc", "selWin/slct_win.arc"):
        if name in archives:
            path, before = archives[name]
            assert Path(str(path) + ".out").read_bytes() == before, name
    for name in ("Menu/mvmnt_undscrn.arc", "msgWin/msg_win.arc"):
        if name in archives:
            path, before = archives[name]
            assert Path(str(path) + ".out").read_bytes() != before, name
