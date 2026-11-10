from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
temp = Path(tempfile.mkdtemp(prefix="leaf-ui-"))
def put(name, content):
    path = temp / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)

put("common/logging/log.h", "#pragma once\n#define LOG_INFO(...) (void)0\n")
put("test.cpp", r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include "core/leaf_acnl/ui.cpp"
std::vector<u8> Archive(const std::string& name, u8 seed) {
    const size_t offset=256, payload=64, size=payload+40;
    std::vector<u8> bytes(offset+size);
    const auto word=[&](size_t p,u32 v){std::memcpy(bytes.data()+p,&v,4);};
    std::memcpy(bytes.data(),"darc",4);
    word(16,28);word(28,0x1000000);word(36,2);
    word(40,0);word(44,offset);word(48,size);
    for(size_t i=0;i<name.size();++i)bytes[52+i*2]=name[i];
    for(size_t i=0;i<payload;++i)bytes[offset+i]=seed+i;
    std::memcpy(bytes.data()+offset+payload,"CLIM",4);
    std::memcpy(bytes.data()+offset+payload+20,"imag",4);
    word(offset+payload+28,8|(8<<16));
    word(offset+payload+36,payload);
    return bytes;
}
u32 Register(const std::string& path,const std::string& name,u8 seed) {
    auto bytes=Archive(name,seed);
    LeafACNL::RegisterUiArchive(path,bytes);
    return LeafACNL::UiTexture(bytes.data()+256,64,8,8);
}
int main(int argc,char** argv) {
    using namespace LeafACNL;
    const auto check_rect=[](UiRect got,UiRect expected){
        assert(got.left==expected.left && got.top==expected.top && got.right==expected.right && got.bottom==expected.bottom);
    };
    check_rect(UiLandscapeBounds({100,20,150,80}),{240,90,300,140});
    check_rect(Region({40,1,280,216},522,240,.965f,.54f,1,1,.8f).destination,{312,0,504,130});
    check_rect(Region({63,17,258,201},522,240,.97f,.03f,.5f,0,.5f,-64,0).destination,{394,7,491,99});
    check_rect(Region({196,204,320,240},522,240,.97f,.03f,1,0,.5f,0,96).destination,{444,103,506,121});
    check_rect(Region({0,204,124,240},522,240,.97f,.03f,1,0,.5f,-66,96).destination,{378,103,440,121});
    check_rect(Region({0,0,320,224},522,240,.5f,.485f,.5f,1,.5f).destination,{181,4,341,116});
    check_rect(Region({0,1,320,240},522,240,.5f,.5f,.5f,.5f,1,0,0,true).destination,{100,0,422,240});
    check_rect(Region({0,0,210,25},522,240,0,0,0,0,1,4,0).destination,{4,0,214,25});
    check_rect(Region({64,26,280,214},522,240,1,1,1,1,.42f,-4,-4).destination,{427,157,518,236});
    std::array<u8,64> native_key_fixture{};
    for(size_t i=0;i<native_key_fixture.size();++i)native_key_fixture[i]=i;
    assert(Key(native_key_fixture.data(),64,8,8)==0x618feb9889b0ed55ULL);
    ResetUi();
    assert(!UiEnabled());
    assert(!IsUiArchivePath("Layout/ItemWin/item_icon.arc"));
    assert(IsUiArchivePath("/romfs/Layout/Menu/map_village.arc"));
    assert(IsUiArchivePath("Layout/selWin/slct_win.arc"));
    std::vector<u8> malformed(64);
    RegisterUiArchive("Layout/Menu/menu_tab.arc",malformed);
    assert(!UiEnabled());
    const auto tab=Register("Layout/Menu/menu_tab.arc","my_men_other.bclim",1);
    const auto hud=Register("Layout/Menu/menu_tab.arc","my_men_btn_00.bclim",2);
    const auto choice=Register("Layout/selWin/slct_win.arc","my_slct_body.bclim",3);
    const auto map=Register("Layout/Menu/map_village.arc","tc_map_list_win_00.bclim",4);
    assert(tab==1 && hud==3 && choice==8 && map==(4|32));
    UiDraw(100,tab);
    UiTransfer(100,200);
    FinishUiFrame(200);
    auto regions=UiRegions(522,240);
    assert(regions.size()==1 && regions[0].shade==.45f);
    assert(regions[0].destination.left==100 && regions[0].destination.right==422);
    float tx=0,ty=0;
    assert(UiTouch(261,120,522,240,tx,ty));
    assert(std::abs(tx-.5f)<.0001f && std::abs(ty-120.5f/240)<.0001f);
    assert(!UiTouch(0,120,522,240,tx,ty));
    const size_t pitch=522*4+16;
    std::vector<u8> output(pitch*240,200), bottom(320*240*4);
    for(size_t i=0;i<bottom.size();i+=4){bottom[i]=10;bottom[i+1]=20;bottom[i+2]=30;bottom[i+3]=255;}
    CompositeUi(output.data(),pitch,522,240,bottom.data(),bottom.size(),320);
    assert(output[0]==110 && output[3]==200);
    assert(output[101*4]==30 && output[101*4+1]==20 && output[101*4+2]==10 && output[101*4+3]==0);
    assert(output[522*4]==200);
    for(int frame=0;frame<180;++frame) FinishUiFrame(200);
    assert(UiRegions(522,240).size()==1);
    UiDraw(100,hud);
    UiTransfer(100,201);
    FinishUiFrame(201);
    assert(UiRegions(522,240).size()==1);
    UiDraw(100,hud|map,{20,26,300,230});
    UiTransfer(100,200);
    FinishUiFrame(200);
    regions=UiRegions(522,240);
    assert(regions.size()==2 && regions[1].source.top==26);
    assert(regions[1].destination.right<522 && regions[1].destination.bottom<240);
    UiDraw(100,choice,{100,110,300,210});
    UiDraw(100,choice,{105,105,280,230});
    UiTransfer(100,201);
    FinishUiFrame(201);
    regions=UiRegions(522,240);
    assert(regions.size()==1 && regions[0].source.left==97 && regions[0].source.top==102 && regions[0].source.bottom==233);
    assert(regions[0].destination.right==504 && regions[0].destination.bottom==130);
    UiDraw(100,0);
    UiTransfer(100,200);
    FinishUiFrame(200);
    assert(UiRegions(522,240).empty());
    UiDraw(100,map);
    UiTransfer(100,201);
    FinishUiFrame(201);
    regions=UiRegions(522,240);
    assert(regions.size()==1 && regions[0].destination.bottom==116 && regions[0].destination.top==4);
    UiDraw(100,choice|map);
    UiTransfer(100,200);
    FinishUiFrame(200);
    regions=UiRegions(522,240);
    assert(regions.size()==1 && regions[0].source.right==320);
    UiDraw(100,map|16);
    UiTransfer(100,200);
    FinishUiFrame(200);
    regions=UiRegions(522,240);
    assert(regions.size()==3);
    const auto& button=regions[1];
    assert(UiTouch((button.destination.left+button.destination.right)/2,
                   (button.destination.top+button.destination.bottom)/2,522,240,tx,ty));
    assert(std::abs(tx-258.0f/320)<.0001f && std::abs(ty-222.0f/240)<.0001f);
    UiTransfer(999,200);
    FinishUiFrame(200);
    assert(UiRegions(522,240).empty());
    ResetUi();
    assert(!UiEnabled() && UiRegions(522,240).empty());
    if(argc>1) {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<u8> archive{std::istreambuf_iterator<char>(file),{}};
        RegisterUiArchive("Layout/Menu/menu_tab.arc",archive);
        assert(UiEnabled() && textures.size()>5);
        bool found_hud=false;
        for(const auto& entry:textures) found_hud |= (entry.second&HudButton)!=0;
        assert(found_hud);
    }
    std::cout << "Leaf UI registry, scene routing, transfers, crops and software composition passed\n";
}
''')
try:
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DXXH_INLINE_ALL",
                    "-I"+str(temp), "-I"+str(root/"src"), "-I"+str(root/"externals/xxHash"),
                    str(temp/"test.cpp"), "-o", str(temp/"test")], check=True)
    arguments = [str(temp/"test")]
    if os.environ.get("LEAF_MENU_TAB_ARCHIVE"):
        arguments.append(os.environ["LEAF_MENU_TAB_ARCHIVE"])
    subprocess.run(arguments, check=True)
finally:
    shutil.rmtree(temp)
