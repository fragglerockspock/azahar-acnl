from pathlib import Path
from zipfile import ZipFile
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leaf-inventory-anchor-") as temp:
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
    std::ifstream file(argv[1],std::ios::binary);
    Bytes bytes{std::istreambuf_iterator<char>(file),{}};
    const auto original=bytes;
    assert(PatchInventoryMailAnchor("Layout/Menu/menu_tab.arc",bytes)==0 && bytes==original);
    std::vector<ArcEntry> before,after;
    assert(ReadArc(bytes,before));
    assert(PatchInventoryMailAnchor("Layout/ItemWin/item_scrn.arc",bytes)==1);
    assert(ReadArc(bytes,after) && before.size()==after.size());
    size_t modified=0;
    for(size_t i=0;i<before.size();++i) {
        assert(before[i].name==after[i].name);
        if(before[i].bytes==after[i].bytes)continue;
        ++modified;
        assert(before[i].name=="blyt/item_scrn.bclyt");
        Layout old_layout,new_layout;
        assert(ReadLayout(before[i].bytes,"CLYT",old_layout));
        assert(ReadLayout(after[i].bytes,"CLYT",new_layout));
        const int anchor=Pane(old_layout,"N_pos_ltr"),wrapper=Pane(new_layout,"N_leaf_mail");
        assert(anchor>=0 && wrapper==anchor);
        assert(Float(new_layout.sections[wrapper],36)==12);
        assert(Float(new_layout.sections[wrapper],40)==0);
        assert(Float(new_layout.sections[wrapper],60)==1 && Float(new_layout.sections[wrapper],64)==1);
        assert(Type(new_layout.sections[wrapper+1],"pas1"));
        assert(Type(new_layout.sections[wrapper+3],"pae1"));
        assert(new_layout.sections[wrapper+2]==old_layout.sections[anchor]);
        new_layout.sections.erase(new_layout.sections.begin()+wrapper+3);
        new_layout.sections.erase(new_layout.sections.begin()+wrapper,new_layout.sections.begin()+wrapper+2);
        assert(new_layout.sections==old_layout.sections);
    }
    assert(modified==1);
    const auto patched=bytes;
    assert(PatchInventoryMailAnchor("ItemWin/item_scrn.arc",bytes)==0 && bytes==patched);
    for(size_t size=0;size<28;++size) {
        Bytes malformed(original.begin(),original.begin()+size);
        const auto saved=malformed;
        assert(PatchInventoryMailAnchor("ItemWin/item_scrn.arc",malformed)==0 && malformed==saved);
    }
    std::cout<<"Inventory anchor wrapper verified against supplied archive; all original panes, textures and animations unchanged\n";
}
''')
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + str(root / "src"), str(folder / "test.cpp"), "-o", str(folder / "test")], check=True)
    archive_path = os.environ.get("LEAF_LAYOUT_ZIP")
    if not archive_path:
        raise SystemExit("Set LEAF_LAYOUT_ZIP to the supplied Layout.zip for the archive verification")
    with ZipFile(archive_path) as archive:
        names = [name for name in archive.namelist() if name.endswith("ItemWin/item_scrn.arc")]
        assert len(names) == 1
        source = folder / "item_scrn.arc"
        source.write_bytes(archive.read(names[0]))
    subprocess.run([str(folder / "test"), str(source)], check=True)
