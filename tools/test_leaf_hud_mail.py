from pathlib import Path
from zipfile import ZipFile
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leaf-hud-mail-") as temp:
    folder = Path(temp)
    (folder / "test.cpp").write_text(r'''
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include "core/leaf_acnl/layout_patch.h"
int main(int argc,char** argv) {
    using namespace LeafACNL::LayoutPatch;
    assert(argc==3);
    auto read=[](const char* path){std::ifstream f(path,std::ios::binary);return Bytes{std::istreambuf_iterator<char>(f),{}};};
    auto target=read(argv[1]);const auto donor=read(argv[2]);
    std::vector<ArcEntry> before,after;
    assert(ReadArc(target,before));
    assert(PatchHudMailProbe(target,donor));
    assert(ReadArc(target,after));
    for(const auto& entry:before) {
        const auto* result=Find(after,entry.name);assert(result);
        if(entry.name!="blyt/menu_tab.bclyt")assert(result->bytes==entry.bytes);
    }
    Layout original,patched;
    assert(ReadLayout(Find(before,"blyt/menu_tab.bclyt")->bytes,"CLYT",original));
    assert(ReadLayout(Find(after,"blyt/menu_tab.bclyt")->bytes,"CLYT",patched));
    assert(patched.sections.size()==original.sections.size()+5);
    const int probe=Pane(patched,"N_leaf_mail_hud");assert(probe>=0);
    assert(Float(patched.sections[probe],36)==22 && Float(patched.sections[probe],40)==108);
    for(const auto name:{"P_leaf_mail_P_btn","P_leaf_mail_P_ltr"})assert(Pane(patched,name)>=0);
    patched.sections.erase(patched.sections.begin()+probe,patched.sections.begin()+probe+5);
    for(size_t i=0;i<original.sections.size();++i) {
        if(Type(original.sections[i],"txl1") || Type(original.sections[i],"mat1"))continue;
        assert(patched.sections[i]==original.sections[i]);
    }
    const auto saved=target;assert(!PatchHudMailProbe(target,donor) && target==saved);
    std::cout<<"Guest HUD mail panes and texture/material graft verified; original HUD panes and animations preserved\n";
}
''')
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + str(root / "src"), str(folder / "test.cpp"), "-o", str(folder / "test")], check=True)
    with ZipFile(os.environ["LEAF_LAYOUT_ZIP"]) as archive:
        for name, output in [("Menu/menu_tab.arc", "hud.arc"), ("ItemWin/item_scrn.arc", "inventory.arc")]:
            (folder / output).write_bytes(archive.read(name))
    subprocess.run([str(folder / "test"), str(folder / "hud.arc"), str(folder / "inventory.arc")], check=True)
