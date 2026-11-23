from pathlib import Path
from zipfile import ZipFile
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leaf-compact-pocket-") as temp:
    folder = Path(temp)
    (folder / "test.cpp").write_text(r'''
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include "core/leaf_acnl/layout_patch.h"
int main(int argc,char** argv) {
    using namespace LeafACNL::LayoutPatch;
    assert(argc==2);
    std::ifstream input(argv[1],std::ios::binary);
    Bytes bytes{std::istreambuf_iterator<char>(input),{}};
    const auto original=bytes;
    assert(!PatchCompactPocket("Layout/Menu/menu_tab.arc",bytes) && bytes==original);
    std::vector<ArcEntry> before,after;
    assert(ReadArc(bytes,before));
    assert(PatchCompactPocket("Layout/ItemWin/item_scrn.arc",bytes)==1);
    assert(ReadArc(bytes,after));
    for(const auto& entry:before) {
        auto* result=Find(after,entry.name);assert(result);
        if(entry.name!="blyt/item_scrn.bclyt" && entry.name!="blyt/item_scrn_chara.bclyt")assert(result->bytes==entry.bytes);
    }
    Layout layout,character;
    assert(ReadLayout(Find(after,"blyt/item_scrn.bclyt")->bytes,"CLYT",layout));
    assert(Pane(layout,"P_leaf_pocket")>=0);
    for(size_t n=0;n<16;++n) {
        const auto name=std::string("P_itm_")+(n<10 ? "0" : "")+std::to_string(n);
        const int i=Pane(layout,name);assert(i>=0);
        assert(Float(layout.sections[i],36)==-126+36*static_cast<float>(n%8));
        assert(Float(layout.sections[i],40)==(n<8 ? 50 : 10));
        assert(Float(layout.sections[i],60)==.48f);
    }
    assert(ReadLayout(Find(after,"blyt/item_scrn_chara.bclyt")->bytes,"CLYT",character));
    for(const auto& section:character.sections)if(Type(section,"pic1"))assert(Float(section,68)==0 && Float(section,72)==0);
    const auto* texture=Find(after,"timg/leaf_pocket_panel.bclim");assert(texture);
    assert(texture->bytes.size()==256*128*4+40);
    assert(texture->bytes[0]==0);
    const auto saved=bytes;
    assert(!PatchCompactPocket("Layout/ItemWin/item_scrn.arc",bytes) && bytes==saved);
    std::cout<<"Compact pocket 16-slot layout, panel texture, portrait removal and unchanged animations verified\n";
}
''')
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + str(root / "src"), str(folder / "test.cpp"), "-o", str(folder / "test")], check=True)
    with ZipFile(os.environ["LEAF_LAYOUT_ZIP"]) as archive:
        source = folder / "inventory.arc"
        source.write_bytes(archive.read("ItemWin/item_scrn.arc"))
    subprocess.run([str(folder / "test"), str(source)], check=True)
