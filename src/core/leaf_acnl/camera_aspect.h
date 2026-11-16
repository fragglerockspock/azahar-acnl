#pragma once

#include <array>
#include <vector>
#include "common/common_types.h"

namespace LeafACNL {
inline std::vector<u32> CameraAspectTargets(const std::vector<u8>& constants,u32 constants_address,
                                           const std::vector<u8>& code,u32 data_address,u32 data_size) {
    const auto word=[](const std::vector<u8>& bytes,size_t offset) {
        return u32{bytes[offset]} | (u32{bytes[offset+1]}<<8) |
               (u32{bytes[offset+2]}<<16) | (u32{bytes[offset+3]}<<24);
    };
    constexpr std::array<u32,4> signature{0x42480000,0x44dac000,0x41b00000,0x3fd55555};
    std::vector<u32> targets;
    const u64 data_end=u64{data_address}+data_size+0x800000;
    for(size_t offset=0;offset+16<=constants.size();offset+=4) {
        bool matches=true;
        for(size_t i=0;i<signature.size();++i)
            if(word(constants,offset+4*i)!=signature[i]) {matches=false;break;}
        const u64 camera=u64{constants_address}+offset;
        if(!matches || camera+15>UINT32_MAX)continue;
        targets.push_back(static_cast<u32>(camera+12));
        if(!data_address || data_end>UINT32_MAX)continue;
        for(size_t p=0;p+8<=code.size();p+=4) {
            if(word(code,p)!=camera)continue;
            const u32 clone=word(code,p+4);
            if(clone<data_address || u64{clone}+15>=data_end || (clone&3))continue;
            targets.push_back(clone+12);
            break;
        }
    }
    return targets;
}
}
