from pathlib import Path
from zipfile import ZipFile
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
    const std::array<size_t,14> expected_sizes{256,192,128,128,128,128,128,64,64,64,32,32,32,64};
    for(u32 format=0;format<14;++format) assert(UiTextureSize(format,8,8)==expected_sizes[format]);
    assert(UiTextureSize(14,8,8)==0 && UiTextureSize(12,0,8)==0);
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
    assert(Tags("selWin/slct_win.arc:im_cmn_arrow32_00.bclim")==0);
    assert(Tags("selWin/slct_win.arc:im_cmn_crl_W10_00.bclim")==0);
    const auto shared_tab=Register("Layout/Menu/menu_tab.arc","im_cmn_arrow32_00.bclim",80);
    const auto shared_choice=Register("Layout/selWin/slct_win.arc","im_cmn_arrow32_00.bclim",80);
    assert(shared_tab==0 && shared_choice==0);
    assert(UiTextureSize(12,32,32)==512 && UiTextureSize(13,32,32)==1024);
    for(u32 format:{12u,13u}) {
        auto archive=Archive("my_men_tab_00.bclim",static_cast<u8>(20+format));
        const size_t payload=UiTextureSize(format,8,8);
        archive.resize(256+payload+40);
        for(size_t i=0;i<payload;++i)archive[256+i]=static_cast<u8>(i+format);
        std::memcpy(archive.data()+256+payload,"CLIM",4);
        std::memcpy(archive.data()+256+payload+20,"imag",4);
        const u32 dims=8|(8<<16), length=static_cast<u32>(payload), size=length+40;
        std::memcpy(archive.data()+48,&size,4);
        std::memcpy(archive.data()+256+payload+28,&dims,4);
        std::memcpy(archive.data()+256+payload+36,&length,4);
        RegisterUiArchive("Layout/Menu/menu_tab.arc",archive);
        assert(UiTexture(archive.data()+256,UiTextureSize(format,8,8),8,8)==1);
    }
    UiDraw(100,tab);
    UiTransfer(100,200);
    FinishUiFrame(200);
    auto regions=UiRegions(522,240);
    assert(regions.size()==4 && regions[0].shade==.45f);
    assert(regions[0].destination.left==101 && regions[0].destination.right==421);
    float tx=0,ty=0;
    assert(UiTouch(261,120,522,240,tx,ty));
    assert(std::abs(tx-.5f)<.0001f && std::abs(ty-120.0f/240)<.0001f);
    assert(!UiTouch(0,120,522,240,tx,ty));
    const size_t pitch=522*4+16;
    std::vector<u8> output(pitch*240,200), bottom(320*240*4);
    for(size_t i=0;i<bottom.size();i+=4){bottom[i]=10;bottom[i+1]=20;bottom[i+2]=30;bottom[i+3]=255;}
    CompositeUi(output.data(),pitch,522,240,bottom.data(),bottom.size(),320);
    assert(output[0]==110 && output[3]==200);
    assert(output[101*4]==30 && output[101*4+1]==20 && output[101*4+2]==10 && output[101*4+3]==200);
    assert(output[522*4]==200);
    for(int frame=0;frame<180;++frame) FinishUiFrame(200);
    assert(UiRegions(522,240).size()==4);
    UiDraw(100,hud);
    UiTransfer(100,201);
    FinishUiFrame(201);
    assert(UiRegions(522,240).size()==3);
    UiDraw(100,hud|map,{20,26,300,230});
    UiTransfer(100,200);
    FinishUiFrame(200);
    regions=UiRegions(522,240);
    assert(regions.size()==4 && regions[3].source.top==26);
    assert(regions[3].destination.right<522 && regions[3].destination.bottom<240);
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
    assert(UiRegions(522,240).size()==3);
    UiDraw(300,tab);
    FinishUiFrame(900);
    assert(UiRegions(522,240).size()==3);
    UiCommandComplete();
    FinishUiFrame(900);
    assert(UiRegions(522,240).size()==4 && UiRegions(522,240)[0].shade==.45f);
    UiTransfer(300,901);
    FinishUiFrame(900);
    assert(UiRegions(522,240).size()==4 && UiRegions(522,240)[0].shade==.45f);
    UiDraw(300,hud);
    for(int frame=0;frame<180;++frame) FinishUiFrame(900);
    assert(UiRegions(522,240).size()==4 && UiRegions(522,240)[0].shade==.45f);
    UiDraw(300,map,{64,26,280,214});
    UiTransfer(300,902);
    FinishUiFrame(902);
    assert(UiRegions(522,240).size()==4);
    UiDraw(400,0);
    UiTransfer(400,401);
    for(int frame=0;frame<180;++frame) FinishUiFrame(902);
    assert(UiRegions(522,240).size()==4);
    UiDraw(300,0);
    UiTransfer(300,902);
    FinishUiFrame(902);
    assert(UiRegions(522,240).empty());
    UiDraw(500,choice,{100,105,280,230});
    FinishUiFrame(902);
    assert(UiRegions(522,240).empty());
    UiPresent(9999);
    FinishUiFrame(9999);
    assert(UiRegions(522,240).size()==1);
    UiDraw(500,0);
    for(int frame=0;frame<180;++frame)FinishUiFrame(9999);
    assert(UiRegions(522,240).size()==1);
    UiPresent(9999);
    FinishUiFrame(9999);
    assert(UiRegions(522,240).empty());
    SetUiAlphaReady(true);
    const std::array<u32,5> key_values{0xff00ffff,0xff00ff,0xf83f,0xf81f,0xf0ff};
    const std::array<u32,5> pixel_bytes{4,3,2,2,2},pica_to_gpu{0,1,3,2,4};
    u32 value=123,control=0,copy_end=0;
    for(u32 format=0;format<5;++format) {
        const u32 address=0x100000+format*0x100000,end=address+256*416*pixel_bytes[format];
        UiTarget(address,256,416,format,240,400);UiDraw(address,0);
        value=123;control=0;
        assert(!UiPrepareFill(address,end,value,control) && value==123 && control==0);
        assert(!UiAfterDisplayTransfer(address,address+8,256,416,240,400,pica_to_gpu[format],copy_end,value,control));
        assert(value==123 && control==0);
        UiTransfer(address,0x3000000);
        assert(UiAfterDisplayTransfer(address,0x3000000,256,416,240,400,pica_to_gpu[format],copy_end,value,control));
        assert(copy_end==end && value==key_values[format]);
        UiTarget(address,256,416,format,240,320);UiDraw(address,tab);
        value=0;control=0x80000000;
        assert(!UiPrepareFill(address+8,end,value,control));
        assert(!UiPrepareFill(address,end-8,value,control));
        assert(!UiPrepareFill(address,end+8,value,control));
        assert(UiPrepareFill(address,end,value,control) && value==key_values[format]);
        UiDraw(address,hud);UiCommandComplete();UiTransfer(address,0x3100000);
        assert(!UiAfterDisplayTransfer(address,0x3100000,256,320,240,320,pica_to_gpu[format],copy_end,value,control));
        value=123;control=0;
        assert(!UiPrepareFill(address,end,value,control) && value==123 && control==0);
        UiTarget(address,256,416,format,240,400);UiDraw(address,0);
        assert(UiAfterDisplayTransfer(address,0x3000000,256,416,240,400,pica_to_gpu[format],copy_end,value,control));
        const u32 bottom_end=address+256*320*pixel_bytes[format];
        assert(UiPrepareFill(address,bottom_end,value,control) && value==key_values[format]);
        assert(!UiAfterDisplayTransfer(address,0x3100000,256,320,240,320,pica_to_gpu[format],copy_end,value,control));
    }
    value=123;control=0;
    assert(!UiPrepareFill(0x900000,0xa00000,value,control) && value==123 && control==0);
    const u32 scratch=0x4000000,top_lcd=0x4200000,bottom_lcd=0x4300000;
    std::vector<u32> shared_pixels(256*416,0x305070ff);
    shared_pixels[100]=0xff0000ff;
    const auto saved_top=shared_pixels;
    UiTarget(scratch,256,416,0,240,400);UiDraw(scratch,0);UiTransfer(scratch,top_lcd);
    assert(UiAfterDisplayTransfer(scratch,top_lcd,256,416,240,400,0,copy_end,value,control));
    std::fill(shared_pixels.begin(),shared_pixels.end(),value);
    assert(saved_top[100]==0xff0000ff && shared_pixels[100]==0xff00ffff);
    const u32 blank_pixel=shared_pixels[100];
    assert(UiKeyPixel((blank_pixel>>24)/255.0f,((blank_pixel>>16)&255)/255.0f,((blank_pixel>>8)&255)/255.0f)[3]==0);
    shared_pixels[200]=0x00ff00ff;
    UiTarget(scratch,256,416,0,240,320);UiDraw(scratch,choice,{40,1,280,216});UiCommandComplete();
    const auto saved_bottom=shared_pixels;
    UiTransfer(scratch,bottom_lcd);
    assert(!UiAfterDisplayTransfer(scratch,bottom_lcd,256,320,240,320,0,copy_end,value,control));
    FinishUiFrame(bottom_lcd);assert(displayed.tags==choice);
    UiTarget(scratch,256,416,0,240,320);UiDraw(scratch,tab);UiCommandComplete();
    FinishUiFrame(bottom_lcd);assert(displayed.tags==choice);
    FinishUiFrame(0x4500000);assert(displayed.tags==choice);
    UiTarget(0x4600000,320,240,0);UiDraw(0x4600000,hud);UiTransfer(0x4600000,0x4700000);
    FinishUiFrame(0x4500000);assert(displayed.tags==choice);
    assert(saved_bottom[100]==0xff00ffff && saved_bottom[200]==0x00ff00ff);
    value=0;control=0;
    assert(!UiPrepareFill(scratch,scratch+256*416*4,value,control) && value==0);
    std::fill(shared_pixels.begin(),shared_pixels.end(),0x305070ff);
    UiTarget(scratch,256,416,0,240,400);UiDraw(scratch,0);UiTransfer(scratch,top_lcd);
    assert(UiAfterDisplayTransfer(scratch,top_lcd,256,416,240,400,0,copy_end,value,control));
    std::fill(shared_pixels.begin(),shared_pixels.end(),value);
    UiTransfer(scratch,bottom_lcd);
    assert(!UiAfterDisplayTransfer(scratch,bottom_lcd,256,320,240,320,0,copy_end,value,control));
    for(int frame=0;frame<180;++frame)FinishUiFrame(0x4500000);
    assert(UiRegions(522,240).empty());
    const auto transparent=UiKeyPixel(1,0,1),white=UiKeyPixel(1,1,1),black=UiKeyPixel(0,0,0);
    assert(transparent[3]==0 && white[3]==1 && black[3]==1);
    const auto edge=UiKeyPixel(1,.5f,1);
    assert(edge[0]==1 && edge[1]==1 && edge[2]==1 && edge[3]==.5f);
    have_display_copy=false;displayed_copies.clear();
    UiTarget(0xb00000,320,240,0);
    UiDraw(0xb00000,hud);
    UiTransfer(0xb00000,0xc00000);
    FinishUiFrame(0xc00000);
    std::fill(output.begin(),output.end(),100);
    for(size_t i=0;i<bottom.size();i+=4){bottom[i]=255;bottom[i+1]=0;bottom[i+2]=255;}
    const auto original_output=output;
    CompositeUi(output.data(),pitch,522,240,bottom.data(),bottom.size(),320);
    assert(output==original_output);
    const size_t green_pixel=(1*320+1)*4;
    bottom[green_pixel]=0;bottom[green_pixel+1]=255;bottom[green_pixel+2]=0;
    CompositeUi(output.data(),pitch,522,240,bottom.data(),bottom.size(),320);
    assert(output[pitch+5*4]==0 && output[pitch+5*4+1]==255 && output[pitch+5*4+2]==0);
    assert(output[pitch+5*4+3]==100);
    std::fill(output.begin(),output.end(),100);
    bottom[green_pixel]=255;bottom[green_pixel+1]=128;bottom[green_pixel+2]=255;
    CompositeUi(output.data(),pitch,522,240,bottom.data(),bottom.size(),320);
    assert(output[pitch+5*4]>170 && output[pitch+5*4]<185);
    assert(output[pitch+5*4]==output[pitch+5*4+1] && output[pitch+5*4]==output[pitch+5*4+2]);
    SetUiAlphaReady(false);
    UiTarget(0xd00000,320,240,0);UiDraw(0xd00000,tab);
    value=123;control=0;
    assert(!UiPrepareFill(0xd00000,0xd00000+320*240*4,value,control) && value==123 && control==0);
    SetUiAlphaReady(true);

    const u32 padded=0x1100000,padded_end=padded+256*512*4,lcd=0x1200000;
    UiTarget(padded,256,512,0,240,400);UiDraw(padded,0);
    assert(UiAfterDisplayTransfer(padded,0x3000000,256,512,240,400,0,copy_end,value,control));
    UiTarget(padded,256,512,0,240,320);
    UiDraw(padded,hud|map,{64,26,280,214});
    UiCommandComplete();
    UiTransfer(0x1300000,lcd);
    FinishUiFrame(lcd);
    assert(UiRegions(522,240).size()==4);
    assert(UiStatus().find("V 240X320 B 256X512")!=std::string::npos);
    value=0;control=0;
    assert(UiPrepareFill(padded,padded_end,value,control) && value==0xff00ffff);
    for(int frame=0;frame<180;++frame)FinishUiFrame(lcd);
    assert(UiRegions(522,240).size()==4);
    UiDraw(padded,choice,{40,1,280,216});
    FinishUiFrame(lcd);
    assert(UiRegions(522,240).size()==4);
    UiCommandComplete();
    FinishUiFrame(lcd);
    assert(UiRegions(522,240).size()==1 && displayed.tags==choice);
    assert(UiPrepareFill(padded,padded_end,value,control));
    for(int frame=0;frame<180;++frame) {
        if(frame%30==0)UiTransfer(0x1300000,lcd);
        FinishUiFrame(lcd);
    }
    assert(displayed.tags==choice);
    UiTransfer(padded,lcd);
    assert(!UiAfterDisplayTransfer(padded,lcd,256,320,240,320,0,copy_end,value,control));
    FinishUiFrame(lcd);
    assert(UiRegions(522,240).empty());
    const u32 shared=0x1400000,shared_end=shared+256*512*4;
    UiTarget(shared,256,512,0,240,400);
    UiDraw(shared,tab);
    UiCommandComplete();
    FinishUiFrame(lcd);
    assert(UiRegions(522,240).empty());
    value=123;control=0;
    assert(!UiPrepareFill(shared,shared_end,value,control) && value==123 && control==0);
    UiTarget(shared,256,512,0,240,320);
    UiDraw(shared,tab);
    assert(!UiPrepareFill(shared,shared_end,value,control) && value==123 && control==0);
    UiTarget(scratch,256,416,0,240,400);UiDraw(scratch,0);
    assert(UiAfterDisplayTransfer(scratch,top_lcd,256,416,240,400,0,copy_end,value,control));
    assert(!UiAfterDisplayTransfer(0x4900000,bottom_lcd,256,320,240,320,0,copy_end,value,control));
    value=0;control=0;
    assert(!UiPrepareFill(scratch,scratch+256*416*4,value,control) && value==0 && control==0);
    have_display_copy=false;displayed_copies.clear();
    const auto title=Register("Layout/selWin/cmn_slct_win.arc","my_csw_LT.bclim",121);
    assert(title==64 && Tags("selWin/cmn_slct_win.arc:im_cmn_arrow32_00.bclim")==256);
    UiDraw(0x1500000,title);UiTransfer(0x1500000,0x1700000);FinishUiFrame(0x1700000);
    assert(UiRegions(522,240).size()==1);
    check_rect(UiRegions(522,240)[0].destination,{142,47,380,217});
    UiTarget(scratch,256,416,0,240,400);UiDraw(scratch,128);UiTransfer(scratch,top_lcd);
    assert(UiAfterDisplayTransfer(scratch,top_lcd,256,416,240,400,0,copy_end,value,control));
    UiTarget(scratch,256,416,0,240,320);UiDraw(scratch,256);UiCommandComplete();
    UiTransfer(scratch,bottom_lcd);
    assert(!UiAfterDisplayTransfer(scratch,bottom_lcd,256,320,240,320,0,copy_end,value,control));
    FinishUiFrame(bottom_lcd);
    check_rect(UiRegions(522,240)[0].destination,{142,47,380,217});
    have_display_copy=false;displayed_copies.clear();
    UiDraw(0x1500000,tab);UiTransfer(0x1500000,0x1700000);FinishUiFrame(0x1700000);
    const auto inventory_tag=Register("Layout/ItemWin/item_scrn.arc","my_itm_leaf.bclim",122);
    assert(inventory_tag==512);
    UiDraw(0x1500000,inventory_tag);UiTransfer(0x1500000,0x1700000);FinishUiFrame(0x1700000);
    const auto inventory=UiRegions(522,240);
    assert(inventory.size()==4 && inventory[0].source.top==25);
    check_rect(inventory[1].destination,{4,0,56,25});
    check_rect(inventory[2].destination,{56,0,108,25});
    assert(inventory[2].preserve_color && !inventory[1].preserve_color && !inventory[3].preserve_color);
    assert(!UiTouch(500,10,522,240,tx,ty));
    assert(UiTouch(10,10,522,240,tx,ty) && tx<.1f && ty<.1f);
    assert(UiStatus().find("UI FIX 2X REG ")==0);
    const auto popup=Register("Layout/ItemWin/itm_slct_win.arc","my_itm_window.bclim",123);
    assert(popup==1024);
    UiDraw(0x1500000,inventory_tag|tab|popup,{47,10,143,84});
    UiTransfer(0x1500000,0x1700000);FinishUiFrame(0x1700000);
    const auto popup_regions=UiRegions(522,240);
    const auto& window=popup_regions.back();
    check_rect(window.source,{44,7,146,87});
    check_rect(window.destination,{145,7,247,87});
    for(size_t i=0;i+1<popup_regions.size();++i) {
        const auto a=popup_regions[i].source,b=window.source;
        assert(std::min(a.right,b.right)<=std::max(a.left,b.left) ||
               std::min(a.bottom,b.bottom)<=std::max(a.top,b.top));
    }
    assert(UiTouch(196,30,522,240,tx,ty));
    assert(std::abs(tx-95.0f/320)<.0001f && std::abs(ty-30.0f/240)<.0001f);
    const auto dialog=Register("Layout/common/cmn_dialog_A.arc","my_cmn_ballon_XXL.bclim",124);
    assert(dialog==2048);
    UiDraw(0x1500000,inventory_tag|tab|dialog,{30,18,290,221});
    UiTransfer(0x1500000,0x1700000);FinishUiFrame(0x1700000);
    check_rect(UiRegions(522,240).back().source,{27,15,293,224});
    check_rect(UiRegions(522,240).back().destination,{128,15,394,224});
    const auto pink=UiKeyPixel(.95f,.3f,.65f,true);
    assert(pink[0]==.95f && pink[1]==.3f && pink[2]==.65f && pink[3]==1);
    assert(UiKeyPixel(.95f,.3f,.65f)[3]<1);
    assert(UiKeyPixel(1,0,1,true)[3]==0 && UiKeyPixel(1,.5f,1,true)[3]==.5f);
    ResetUi();
    const auto image_tab=Register("Layout/Menu/menu_tab.arc","my_men_tab_00.bclim",125);
    const auto image_choice=Register("Layout/selWin/slct_win.arc","my_slct_body.bclim",126);
    UiDraw(0x5100000,image_tab);
    UiTransfer(0x5100000,0x5200000,false);
    UiDraw(0x5100000,8,{40,30,200,180});
    assert(targets[0x5100000].tags==(image_tab|8) && !targets[0x5100000].sealed);
    UiTransfer(0x5100000,0x5200000,false);
    assert(targets[0x5200000].tags==(image_tab|8) && !targets[0x5200000].sealed);
    assert(!have_display_copy);
    UiTransfer(0x5100000,0x5100000,false);
    assert(targets[0x5100000].tags==(image_tab|8));
    for(u32 format=0;format<5;++format)for(u32 scaling=0;scaling<3;++scaling)
    for(bool linear:{false,true})for(bool swizzle:{false,true}) {
        const std::array<u32,5> bpp{4,3,2,2,2};
        const auto image=UiTransferImage(0x5300000,256,240,320,format,scaling,linear,swizzle,true,true);
        assert(image.Valid() && image.width==(scaling ? 120 : 240) && image.height==(scaling==2 ? 160 : 320));
        assert(image.tiled==(linear!=swizzle) && image.stride==image.width*bpp[format]);
        assert(image.offset==16*319*bpp[format]);
    }
    assert(!UiTransferImage(0x5300000,200,240,320,1,0,false,false,true,true).Valid());
    assert(!UiTransferImage(UINT32_MAX-10,256,240,320,1,0,false,false,false,false).Valid());
    const auto copied=UiTransferImage(0x5600000,256,240,320,1,0,true,false,true,true);
    UiTarget(0x5700000,256,416,0,240,320);UiDraw(0x5700000,image_tab);
    UiTransfer(0x5700000,copied.address);
    assert(!UiAfterDisplayTransfer(0x5700000,copied.address,240,320,240,320,0,copy_end,value,control,copied));
    FinishUiFrame(0x5800000);
    assert(UiDisplayedImage().Valid() && UiDisplayedImage().tiled && UiDisplayedImage().offset==15312);

    SetUiAlphaReady(true);
    const UiImage image_a{0x5300000,240,320,720,1},image_b{0x5400000,240,320,960,0};
    assert(image_a.Valid() && image_b.Valid());
    assert((!UiImage{UINT32_MAX-32,240,320,720,1}.Valid()));
    const auto publish=[&](UiImage image,u32 scene) {
        UiTarget(scratch,256,416,0,240,400);UiDraw(scratch,0);UiTransfer(scratch,top_lcd);
        assert(UiAfterDisplayTransfer(scratch,top_lcd,256,416,240,400,0,copy_end,value,control));
        UiTarget(scratch,256,416,0,240,320);UiDraw(scratch,scene);UiCommandComplete();
        UiTransfer(scratch,image.address);
        assert(!UiAfterDisplayTransfer(scratch,image.address,256,320,240,320,0,copy_end,value,control,image));
    };
    publish(image_a,image_choice);publish(image_b,image_tab);
    for(int i=0;i<120;++i) {
        FinishUiFrame(image_a.address);
        assert(displayed.tags==image_choice && UiDisplayedImage().address==image_a.address && UiDisplayedImage().stride==720);
        FinishUiFrame(image_b.address);
        assert(displayed.tags==image_tab && UiDisplayedImage().address==image_b.address && UiDisplayedImage().stride==960);
        FinishUiFrame(0x5500000);
        assert(displayed.tags==image_tab && UiDisplayedImage().address==image_b.address);
    }
    FinishUiFrame(image_b.address,true);assert(UiRegions(522,240).empty() && !UiDisplayedImage().Valid());
    FinishUiFrame(image_b.address);assert(UiDisplayedImage().address==image_b.address);
    publish(image_a,0);FinishUiFrame(image_a.address);
    assert(UiRegions(522,240).empty() && UiDisplayedImage().address==image_a.address);
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
    if(argc>2) {
        std::ifstream file(argv[2],std::ios::binary);
        std::vector<u8> bytes{std::istreambuf_iterator<char>(file),{}};
        RegisterUiArchive("Layout/selWin/cmn_slct_win.arc",bytes);
        size_t title_keys=0;
        for(const auto& entry:textures)title_keys+=(entry.second&TitleMenu)!=0;
        assert(title_keys>=1);
        std::cout<<"Supplied title-menu texture keys passed\n";
    }
    if(argc>3) {
        std::ifstream file(argv[3],std::ios::binary);
        std::vector<u8> bytes{std::istreambuf_iterator<char>(file),{}};
        RegisterUiArchive("Layout/ItemWin/itm_slct_win.arc",bytes);
        size_t popup_keys=0;for(const auto& entry:textures)popup_keys+=(entry.second&ItemPopup)!=0;
        assert(popup_keys>0);
        std::cout<<"Supplied inventory-popup texture keys passed\n";
    }
    if(argc>4) {
        std::ifstream file(argv[4],std::ios::binary);
        std::vector<u8> bytes{std::istreambuf_iterator<char>(file),{}};
        RegisterUiArchive("Layout/common/cmn_dialog_A.arc",bytes);
        size_t dialog_keys=0;for(const auto& entry:textures)dialog_keys+=(entry.second&Dialog)!=0;
        assert(dialog_keys>0);
        std::cout<<"Supplied common-dialog texture keys passed\n";
    }
    std::cout << "Leaf UI registry, scene routing, transfers, crops and software composition passed\n";
}
''')
try:
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DXXH_INLINE_ALL",
                    "-I"+str(temp), "-I"+str(root/"src"), "-I"+str(root/"externals/xxHash"),
                    str(temp/"test.cpp"), "-o", str(temp/"test")], check=True)
    arguments = [str(temp/"test")]
    if os.environ.get("LEAF_LAYOUT_ZIP"):
        with ZipFile(os.environ["LEAF_LAYOUT_ZIP"]) as archive:
            for name in ("Menu/menu_tab.arc", "selWin/cmn_slct_win.arc", "ItemWin/itm_slct_win.arc", "common/cmn_dialog_A.arc"):
                target = temp / Path(name).name
                target.write_bytes(archive.read(name))
                arguments.append(str(target))
    elif os.environ.get("LEAF_MENU_TAB_ARCHIVE"):
        arguments.append(os.environ["LEAF_MENU_TAB_ARCHIVE"])
    subprocess.run(arguments, check=True)
finally:
    shutil.rmtree(temp)
