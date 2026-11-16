from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="leaf-camera-") as directory:
    path = Path(directory)
    source = path / "test.cpp"
    source.write_text(r'''
#include <cassert>
#include <cstring>
#include "core/leaf_acnl/camera_aspect.h"
int main() {
    const auto word=[](std::vector<u8>& b,size_t p,u32 v){std::memcpy(b.data()+p,&v,4);};
    std::vector<u8> constants(64),code(64);
    for(size_t i=0;i<4;++i)word(constants,16+4*i,std::array<u32,4>{0x42480000,0x44dac000,0x41b00000,0x3fd55555}[i]);
    word(code,8,0x200010);word(code,12,0x400100);
    auto targets=LeafACNL::CameraAspectTargets(constants,0x200000,code,0x400000,0x1000);
    assert((targets==std::vector<u32>{0x20001c,0x40010c}));
    for(u32 invalid:{0x300000u,0xc01000u,0xffffffffu,0x400101u}) {
        word(code,12,invalid);
        assert((LeafACNL::CameraAspectTargets(constants,0x200000,code,0x400000,0x1000)==std::vector<u32>{0x20001c}));
    }
    word(code,12,0x400100);
    assert((LeafACNL::CameraAspectTargets(constants,0x200000,code,0xffffff00,0x1000)==std::vector<u32>{0x20001c}));
    for(size_t length=0;length<32;++length) {
        auto truncated=constants;truncated.resize(length);
        assert(LeafACNL::CameraAspectTargets(truncated,0x200000,code,0x400000,0x1000).empty());
    }
    constants[16]^=1;
    assert(LeafACNL::CameraAspectTargets(constants,0x200000,code,0x400000,0x1000).empty());
    assert(LeafACNL::CameraAspectTargets({},0,{},0,0).empty());
}
''')
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I"+str(root / "src"), str(source), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Leaf camera constant/clone discovery and bounds checks passed")
