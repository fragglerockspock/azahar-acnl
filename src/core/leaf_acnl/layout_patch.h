#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "common/common_types.h"

namespace LeafACNL::LayoutPatch {
using Bytes=std::vector<u8>;
struct ArcEntry {std::string name;Bytes bytes;};
inline bool Range(size_t offset,size_t count,size_t size) {return offset<=size && count<=size-offset;}
inline u32 Word(const Bytes& b,size_t p) {
    if(!Range(p,4,b.size()))return 0;
    return u32{b[p]}|(u32{b[p+1]}<<8)|(u32{b[p+2]}<<16)|(u32{b[p+3]}<<24);
}
inline u16 Half(const Bytes& b,size_t p) {return Range(p,2,b.size()) ? b[p]|(u16{b[p+1]}<<8) : 0;}
inline void Word(Bytes& b,size_t p,u32 value) {for(size_t i=0;i<4;++i)b[p+i]=value>>(8*i);}
inline void Half(Bytes& b,size_t p,u16 value) {b[p]=value;b[p+1]=value>>8;}
inline float Float(const Bytes& b,size_t p) {float f=0;std::memcpy(&f,b.data()+p,4);return f;}
inline void Float(Bytes& b,size_t p,float f) {std::memcpy(b.data()+p,&f,4);}
inline std::string Text(const Bytes& b,size_t p,size_t limit) {
    if(!Range(p,limit,b.size()))return {};
    const auto first=b.begin()+p,last=first+limit,end=std::find(first,last,0);
    return end==last ? std::string{} : std::string(first,end);
}
inline bool Name(Bytes& b,size_t p,size_t limit,std::string_view name) {
    if(!Range(p,limit,b.size()) || name.size()>=limit)return false;
    std::fill_n(b.begin()+p,limit,0);std::copy(name.begin(),name.end(),b.begin()+p);return true;
}
inline bool ReadArc(const Bytes& b,std::vector<ArcEntry>& entries) {
    if(b.size()<28 || b.size()>8*1024*1024 || std::memcmp(b.data(),"darc",4))return false;
    const size_t table=Word(b,16),count=Word(b,table+8);
    if(table<28 || !count || count>4096 || !Range(table,count*12,b.size()))return false;
    const size_t names=table+count*12;
    std::vector<std::pair<size_t,std::string>> folders;
    std::vector<ArcEntry> parsed;
    for(size_t i=0;i<count;++i) {
        while(!folders.empty() && i>=folders.back().first)folders.pop_back();
        const u32 flag=Word(b,table+i*12);
        size_t p=names+(flag&0xffffff);std::string name;bool terminated=false;
        for(size_t n=0;n<256 && Range(p,2,b.size());++n,p+=2) {
            const u16 c=Half(b,p);
            if(!c){terminated=true;break;}
            if(c>127 || c=='/' || c=='\\')return false;
            name+=static_cast<char>(c);
        }
        if(!terminated || (i && (name.empty() || name==".." || (name=="." && !(flag>>24)))))return false;
        const auto prefix=folders.empty() ? std::string{} : folders.back().second;
        if(flag>>24) {
            const size_t end=Word(b,table+i*12+8);
            if(end<=i || end>count || (!folders.empty() && end>folders.back().first))return false;
            folders.emplace_back(end,prefix+((name.empty() || name==".") ? "" : name+"/"));
        } else {
            const size_t offset=Word(b,table+i*12+4),size=Word(b,table+i*12+8);
            if(!Range(offset,size,b.size()) || size>4*1024*1024)return false;
            const auto path=prefix+name;
            if(std::any_of(parsed.begin(),parsed.end(),[&](const auto& e){return e.name==path;}))return false;
            parsed.push_back({path,Bytes(b.begin()+offset,b.begin()+offset+size)});
        }
    }
    entries=std::move(parsed);return !entries.empty();
}
inline Bytes WriteArc(const std::vector<ArcEntry>& entries) {
    struct Node {std::map<std::string,Node> children;const Bytes* file=nullptr;};
    Node root;
    for(const auto& entry:entries) {
        Node* node=&root.children["."];size_t begin=0;
        const std::string path=entry.name.find("bclim/")==0 ? "timg/"+entry.name.substr(6) : entry.name;
        while(begin<path.size()) {
            const size_t end=path.find('/',begin);
            const auto part=path.substr(begin,end==std::string::npos ? end : end-begin);
            if(part.empty() || part=="." || part==".." || node->file)return {};
            node=&node->children[part];
            if(end==std::string::npos)break;
            begin=end+1;
        }
        if(node->file || !node->children.empty())return {};
        node->file=&entry.bytes;
    }
    struct Record {std::string name;u32 parent=0,end=0;const Bytes* file=nullptr;};
    std::vector<Record> records;
    const auto flatten=[&](const auto& self,const Node& node,std::string name,u32 parent)->void {
        const auto index=static_cast<u32>(records.size());
        records.push_back({std::move(name),parent,0,node.file});
        for(const auto& [child_name,child]:node.children)self(self,child,child_name,index);
        records[index].end=static_cast<u32>(records.size());
    };
    flatten(flatten,root,"",0);
    if(records.size()>4096)return {};
    Bytes names;std::vector<u32> offsets;
    for(const auto& record:records) {
        offsets.push_back(static_cast<u32>(names.size()));
        for(const unsigned char c:record.name){names.push_back(c);names.push_back(0);}
        names.insert(names.end(),{0,0});
    }
    const size_t table=28,table_size=records.size()*12+names.size(),data=(table+table_size+31)&~size_t{31};
    Bytes b(data);std::memcpy(b.data(),"darc",4);Half(b,4,0xfeff);Half(b,6,28);Word(b,8,0x1000000);
    Word(b,16,table);Word(b,20,table_size);Word(b,24,data);
    std::copy(names.begin(),names.end(),b.begin()+table+records.size()*12);
    for(size_t i=0;i<records.size();++i) {
        const auto& record=records[i];const size_t p=table+i*12;
        Word(b,p,offsets[i]|(record.file ? 0 : 0x1000000));
        if(!record.file){Word(b,p+4,record.parent);Word(b,p+8,record.end);continue;}
        const size_t alignment=record.name.size()>=6 && record.name.substr(record.name.size()-6)==".bclim" ? 128 : 32;
        const size_t offset=(b.size()+alignment-1)&~(alignment-1);
        if(offset+record.file->size()>8*1024*1024)return {};
        b.resize(offset);Word(b,p+4,offset);Word(b,p+8,record.file->size());
        b.insert(b.end(),record.file->begin(),record.file->end());
    }
    Word(b,12,b.size());return b;
}
inline ArcEntry* Find(std::vector<ArcEntry>& entries,std::string_view name) {
    const auto it=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.name==name;});
    return it==entries.end() ? nullptr : &*it;
}
struct Layout {Bytes header;std::vector<Bytes> sections;};
inline bool ReadLayout(const Bytes& b,std::string_view magic,Layout& out) {
    if(b.size()<20 || magic.size()!=4 || std::memcmp(b.data(),magic.data(),4) || Half(b,4)!=0xfeff || Word(b,12)!=b.size())return false;
    const size_t header=Half(b,6),count=Half(b,16);
    if(header<20 || header>b.size() || count>4096)return false;
    Layout parsed{Bytes(b.begin(),b.begin()+header),{}};size_t p=header;
    for(size_t i=0;i<count;++i) {
        const size_t size=Word(b,p+4);
        if(size<8 || !Range(p,size,b.size()))return false;
        parsed.sections.emplace_back(b.begin()+p,b.begin()+p+size);p+=size;
    }
    if(p!=b.size())return false;
    out=std::move(parsed);return true;
}
inline Bytes WriteLayout(const Layout& layout) {
    Bytes b=layout.header;
    for(const auto& section:layout.sections)b.insert(b.end(),section.begin(),section.end());
    Word(b,12,b.size());Half(b,16,layout.sections.size());return b;
}
inline bool Type(const Bytes& section,std::string_view type) {return section.size()>=8 && type.size()==4 && !std::memcmp(section.data(),type.data(),4);}
inline int Section(const Layout& l,std::string_view type) {
    for(size_t i=0;i<l.sections.size();++i)if(Type(l.sections[i],type))return static_cast<int>(i);
    return -1;
}
inline int Pane(const Layout& l,std::string_view name) {
    for(size_t i=0;i<l.sections.size();++i) {
        const auto& s=l.sections[i];
        if((Type(s,"pan1") || Type(s,"wnd1") || Type(s,"txt1") || Type(s,"pic1")) && Text(s,12,24)==name)return static_cast<int>(i);
    }
    return -1;
}
inline bool TextureNames(const Bytes& b,std::vector<std::string>& names) {
    const size_t count=Half(b,8);
    if(!Range(12,count*4,b.size()) || count>1024)return false;
    for(size_t i=0;i<count;++i) {
        const size_t p=12+size_t{Word(b,12+i*4)};
        if(p>=b.size())return false;
        const auto name=Text(b,p,std::min(size_t{256},b.size()-p));if(name.empty())return false;
        names.push_back(name);
    }
    return true;
}
inline Bytes TextureSection(const std::vector<std::string>& names) {
    Bytes b(12+names.size()*4);std::memcpy(b.data(),"txl1",4);Half(b,8,names.size());
    for(size_t i=0;i<names.size();++i) {
        Word(b,12+i*4,b.size()-12);b.insert(b.end(),names[i].begin(),names[i].end());b.push_back(0);
    }
    b.resize((b.size()+3)&~size_t{3});Word(b,4,b.size());return b;
}
inline bool Materials(const Bytes& b,std::vector<Bytes>& materials) {
    const size_t count=Half(b,8);
    if(count>1024 || !Range(12,count*4,b.size()))return false;
    for(size_t i=0;i<count;++i) {
        const size_t first=Word(b,12+i*4),end=i+1<count ? Word(b,16+i*4) : b.size();
        if(first<12+count*4 || end<first || end>b.size() || end-first<52)return false;
        materials.emplace_back(b.begin()+first,b.begin()+end);
    }
    return true;
}
inline Bytes MaterialSection(const std::vector<Bytes>& materials) {
    Bytes b(12+materials.size()*4);std::memcpy(b.data(),"mat1",4);Half(b,8,materials.size());
    for(size_t i=0;i<materials.size();++i) {
        Word(b,12+i*4,b.size());b.insert(b.end(),materials[i].begin(),materials[i].end());
    }
    Word(b,4,b.size());return b;
}
inline bool GraftTitleWindow(std::vector<ArcEntry>& target,std::vector<ArcEntry>& donor) {
    auto* dst=Find(target,"blyt/opng_titl_00.bclyt");const auto* src=Find(donor,"blyt/cmn_slct_win.bclyt");
    Layout d,s;
    if(!dst || !src || !ReadLayout(dst->bytes,"CLYT",d) || !ReadLayout(src->bytes,"CLYT",s) || Pane(d,"W_leaf_press")>=0)return false;
    const int attach=Pane(d,"T_tex_00"),window=Pane(s,"W_slct_00"),dt=Section(d,"txl1"),st=Section(s,"txl1"),dm=Section(d,"mat1"),sm=Section(s,"mat1");
    if(std::min({attach,window,dt,st,dm,sm})<0)return false;
    std::vector<std::string> textures,source_textures;std::vector<Bytes> materials,source_materials;
    if(!TextureNames(d.sections[dt],textures) || !TextureNames(s.sections[st],source_textures) ||
       !Materials(d.sections[dm],materials) || !Materials(s.sections[sm],source_materials))return false;
    Bytes pane=s.sections[window];
    if(!Type(pane,"wnd1") || pane.size()<104)return false;
    const size_t content=Word(pane,96),frames=Word(pane,100),count=pane[92];
    if(count>16 || !Range(content,20,pane.size()) || !Range(frames,count*4,pane.size()))return false;
    std::vector<size_t> material_positions{content+16};
    for(size_t i=0;i<count;++i) {
        const size_t p=Word(pane,frames+i*4);if(!Range(p,4,pane.size()))return false;
        material_positions.push_back(p);
    }
    std::map<u16,u16> imported;std::vector<ArcEntry> additions;
    for(const size_t position:material_positions) {
        const u16 old=Half(pane,position);
        if(old>=source_materials.size())return false;
        if(!imported.count(old)) {
            auto material=source_materials[old];const auto name=Text(material,0,20);
            if(name.find("W_slct_00")!=0 || !Name(material,0,20,"W_leaf_press"+name.substr(9)))return false;
            const size_t maps=Word(material,48)&3;
            if(!Range(52,maps*4,material.size()))return false;
            for(size_t i=0;i<maps;++i) {
                const size_t p=52+i*4,index=Half(material,p);
                if(index>=source_textures.size())return false;
                const auto& texture=source_textures[index];
                auto found=std::find(textures.begin(),textures.end(),texture);
                if(found==textures.end()) {
                    const auto entry=std::find_if(donor.begin(),donor.end(),[&](const auto& e){return e.name=="bclim/"+texture || e.name=="timg/"+texture;});
                    if(entry==donor.end())return false;
                    if(!Find(target,"bclim/"+texture))additions.push_back({"bclim/"+texture,entry->bytes});
                    textures.push_back(texture);found=textures.end()-1;
                }
                Half(material,p,std::distance(textures.begin(),found));
            }
            imported[old]=static_cast<u16>(materials.size());materials.push_back(std::move(material));
        }
        Half(pane,position,imported[old]);
    }
    Name(pane,12,24,"W_leaf_press");Float(pane,36,0);Float(pane,40,23);Float(pane,60,.8f);Float(pane,64,.72f);Float(pane,68,180);Float(pane,72,64);
    d.sections[dt]=TextureSection(textures);d.sections[dm]=MaterialSection(materials);
    d.sections.insert(d.sections.begin()+attach,std::move(pane));dst->bytes=WriteLayout(d);
    target.insert(target.end(),additions.begin(),additions.end());return true;
}
inline bool RetargetAnimation(Bytes& bytes,std::string_view from,std::string_view to) {
    Layout l;if(!ReadLayout(bytes,"CLAN",l) || to.size()>=20)return false;
    bool changed=false;
    for(auto& s:l.sections) {
        if(!Type(s,"pai1"))continue;
        if(s.size()<20)return false;
        const size_t count=Half(s,14),table=Word(s,16);
        if(count>4096 || !Range(table,count*4,s.size()))return false;
        for(size_t i=0;i<count;++i) {
            const size_t p=Word(s,table+4*i);
            if(!Range(p,24,s.size()))return false;
            if(Text(s,p,20)==from && s[p+21]==0){Name(s,p,20,to);changed=true;}
        }
    }
    if(changed)bytes=WriteLayout(l);
    return changed;
}
inline bool WrapTitleAnimation(std::vector<ArcEntry>& entries) {
    auto* file=Find(entries,"blyt/opng_titl_00.bclyt");Layout l;
    if(!file || !ReadLayout(file->bytes,"CLYT",l) || Pane(l,"N_leaf_press")>=0)return false;
    const int first=Pane(l,"W_leaf_press"),last=Pane(l,"T_tex_00"),root=Pane(l,"RootPane");
    if(first<0 || last<first || root<0 || l.sections[root].size()!=76 || l.sections[last].size()<76)return false;
    int depth=0;std::vector<int> levels;
    for(const auto& s:l.sections){if(Type(s,"pae1"))--depth;levels.push_back(depth);if(Type(s,"pas1"))++depth;if(depth<0)return false;}
    if(depth || levels[first]!=levels[last])return false;
    size_t end=last+1;
    if(end<l.sections.size() && Type(l.sections[end],"pas1")) {
        int level=1;++end;
        while(end<l.sections.size() && level) {
            if(Type(l.sections[end],"pas1"))++level;
            if(Type(l.sections[end],"pae1"))--level;
            ++end;
        }
        if(level)return false;
    }
    auto parent=l.sections[root];Name(parent,12,24,"N_leaf_press");parent[8]|=1;parent[10]=255;
    const auto original=l.sections[last];std::copy_n(original.begin()+36,32,parent.begin()+36);
    const float x=Float(original,36),y=Float(original,40);
    for(int i=first;i<=last;++i)if(levels[i]==levels[first] && l.sections[i].size()>=76) {
        Float(l.sections[i],36,Float(l.sections[i],36)-x);Float(l.sections[i],40,Float(l.sections[i],40)-y);
    }
    Float(l.sections[last],60,1);Float(l.sections[last],64,1);
    Bytes begin(8),finish(8);std::memcpy(begin.data(),"pas1",4);Word(begin,4,8);std::memcpy(finish.data(),"pae1",4);Word(finish,4,8);
    l.sections.insert(l.sections.begin()+end,finish);
    l.sections.insert(l.sections.begin()+first,{parent,begin});
    for(auto& section:l.sections)if(Type(section,"grp1")) {
        const size_t count=Half(section,24);
        if(!Range(28,count*16,section.size()))return false;
        for(size_t i=0;i<count;++i)if(Text(section,28+i*16,16)=="T_tex_00")Name(section,28+i*16,16,"N_leaf_press");
    }
    for(const auto name:{"anim/opng_titl_00_in.bclan","anim/opng_titl_00_loop.bclan"}) {
        auto* animation=Find(entries,name);
        if(!animation || !RetargetAnimation(animation->bytes,"T_tex_00","N_leaf_press"))return false;
    }
    file->bytes=WriteLayout(l);return true;
}
inline Bytes TitleProbeTexture() {
    constexpr size_t payload=8*8*4;
    Bytes b(payload+40);
    for(size_t i=0;i<payload;i+=4){b[i+1]=0x4c;b[i+2]=0x54;b[i+3]=0x50;}
    std::memcpy(b.data()+payload,"CLIM",4);Half(b,payload+4,0xfeff);Half(b,payload+6,20);
    Word(b,payload+8,0x02020000);Word(b,payload+12,b.size());Word(b,payload+16,1);
    std::memcpy(b.data()+payload+20,"imag",4);Word(b,payload+24,16);
    Half(b,payload+28,8);Half(b,payload+30,8);Word(b,payload+32,9);Word(b,payload+36,payload);
    return b;
}
inline bool AddTitleProbe(std::vector<ArcEntry>& entries) {
    auto* file=Find(entries,"blyt/opng_titl_00.bclyt");Layout l;
    if(!file || !ReadLayout(file->bytes,"CLYT",l) || Pane(l,"P_leaf_title_tag")>=0)return false;
    const int root=Pane(l,"RootPane"),parent=Pane(l,"N_leaf_press"),window=Pane(l,"W_leaf_press");
    const int txl=Section(l,"txl1"),mat=Section(l,"mat1");
    if(std::min({root,parent,window,txl,mat})<0 || l.sections[root].size()!=76)return false;
    std::vector<std::string> names;std::vector<Bytes> materials;
    if(!TextureNames(l.sections[txl],names) || !Materials(l.sections[mat],materials))return false;
    const auto& frame=l.sections[window];const size_t content=Word(frame,96);
    if(!Range(content,20,frame.size()))return false;
    const size_t index=Half(frame,content+16);if(index>=materials.size())return false;
    auto material=materials[index];
    if(material.size()<56 || (Word(material,48)&3)!=1 || !Name(material,0,20,"M_leaf_title_tag"))return false;
    Half(material,52,names.size());names.push_back("leaf_title_tag.bclim");
    auto picture=l.sections[root];picture.resize(128);
    std::memcpy(picture.data(),"pic1",4);Word(picture,4,picture.size());
    Name(picture,12,24,"P_leaf_title_tag");picture[8]|=1;picture[10]=255;
    for(size_t p=36;p<60;p+=4)Float(picture,p,0);
    Float(picture,36,150);Float(picture,40,100);Float(picture,60,1);Float(picture,64,1);
    Float(picture,68,1);Float(picture,72,1);std::fill(picture.begin()+76,picture.begin()+92,255);
    Half(picture,92,materials.size());picture[94]=1;
    const std::array<float,8> uv{0,0,1,0,0,1,1,1};
    std::memcpy(picture.data()+96,uv.data(),sizeof(uv));
    materials.push_back(std::move(material));
    l.sections[txl]=TextureSection(names);l.sections[mat]=MaterialSection(materials);
    l.sections.insert(l.sections.begin()+parent,std::move(picture));
    file->bytes=WriteLayout(l);
    entries.push_back({"bclim/leaf_title_tag.bclim",TitleProbeTexture()});
    return true;
}
inline size_t PatchTitlePressA(Bytes& bytes,const Bytes& donor_bytes,bool identify=true) {
    std::vector<ArcEntry> target,donor;
    if(!ReadArc(bytes,target) || !ReadArc(donor_bytes,donor) ||
       !GraftTitleWindow(target,donor) || !WrapTitleAnimation(target) || (identify && !AddTitleProbe(target)))return 0;
    auto result=WriteArc(target);if(result.empty())return 0;
    bytes=std::move(result);return 1;
}
}
