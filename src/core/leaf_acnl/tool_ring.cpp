#include "core/leaf_acnl/tool_ring.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <utility>
#include "common/logging/log.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"

namespace LeafACNL {
namespace {
using Clock = std::chrono::steady_clock;
using Pattern = std::vector<std::pair<u32, u32>>;
std::mutex mutex;
struct State {
    bool located = false;
    u32 entry = 0, original = 0, equip = 0, global = 0, pointer_offset = 0;
    u32 inventory_offset = 0, cave = 0;
    std::array<std::pair<u16, u16>, 32> ranges{};
    std::array<u32, 16> items{};
    std::array<u8, 16> flags{};
    u32 held = 0x7ffe, player = 0;
    Clock::time_point changed = Clock::now(), sampled{};
    bool ready = false;
    std::optional<int> pending;
    std::optional<int> returning;
} state;
std::optional<Tool> Classify(u32 item, int slot) {
    constexpr std::array<unsigned, 7> kinds{14, 15, 16, 17, 18, 19, 21};
    for (auto kind : kinds) {
        auto [first, last] = state.ranges[kind];
        const u32 id = item & 0x7fff;
        if (id >= first && id <= last) return Tool{slot, static_cast<u8>(kind), static_cast<u8>(item - first)};
    }
    return std::nullopt;
}
bool Valid(Memory::MemorySystem& memory, const Kernel::Process& process, u32 addr, u32 size) {
    if (addr == 0 || size == 0 || addr > UINT32_MAX - size) return false;
    for (u32 p = addr; p < addr + size;) {
        if (!memory.IsValidVirtualAddress(process, p)) return false;
        const u32 step = std::min<u32>(0x1000 - (p & 0xfff), addr + size - p);
        p += step;
    }
    return true;
}
u32 Branch(u32 from, u32 to, bool link = false) {
    return (link ? 0xeb000000 : 0xea000000) | (((to - from - 8) >> 2) & 0xffffff);
}
}
void ResetToolRing() {
    std::lock_guard lock(mutex);
    state = State{};
}
bool Locate(Core::System& system, Kernel::Process& process) {
    std::lock_guard lock(mutex);
    const auto id = process.codeset->program_id & ~0xf00ULL;
    if (id != 0x0004000000198000ULL && id != 0x0004000000086000ULL) return false;
    state = State{};
    auto& memory = system.Memory();
    const auto& segment = process.codeset->CodeSegment();
    if (segment.size < 0xc0 || segment.size > 0x2000000) return false;
    std::vector<u32> code(segment.size / 4);
    memory.ReadBlock(process, segment.addr, code.data(), code.size() * 4);
    auto word = [&](u32 a) -> u32 {
        if (a < segment.addr || a - segment.addr >= code.size() * 4 || (a & 3)) return 0;
        return code[(a - segment.addr) / 4];
    };
    auto matches = [&](const Pattern& pattern) {
        std::vector<u32> result;
        for (size_t i = 0; i + pattern.size() <= code.size(); ++i) {
            bool ok = true;
            for (size_t j = 0; j < pattern.size(); ++j) {
                if (((code[i+j] ^ pattern[j].first) & pattern[j].second) != 0) { ok = false; break; }
            }
            if (ok) result.push_back(segment.addr + static_cast<u32>(i * 4));
        }
        return result;
    };
    auto unique = [&](const Pattern& p) -> std::optional<u32> {
        const auto found = matches(p);
        if (found.size() != 1) return std::nullopt;
        return found.front();
    };
    const auto inventory = unique({{0xe92d4070,~0u},{0xe1a05001,~0u},{0xe1a04002,~0u},{0xe1a06000,~0u},{0xeb000000,0xff000000},{0xe5951000,~0u},{0xe1a02000,~0u},{0xe3a03000,~0u},{0xe5a01000,0xffffff00},{0xe3a01000,0xffffff00},{0xe5c04004,~0u},{0xe1a00006,~0u},{0xe8bd4070,~0u},{0xea000000,0xff000000}});
    if (!inventory) return false;
    const auto equip_candidates = matches({{0xe92d4070,~0u},{0xe1a04000,~0u},{0xe1a05001,~0u},{0xe3a00004,~0u},{0xe3a01000,~0u},{0xeb000000,0xff000000},{0xe3500000,~0u},{0x0a000000,0xff000000},{0xe5d01000,0xfffff000}});
    u32 equip = 0;
    for (u32 candidate : equip_candidates) {
        for (u32 offset = 0x24; offset < 0x48; offset += 4) {
            const u32 instruction = word(candidate + offset);
            const s32 displacement = static_cast<s32>(instruction << 8) >> 6;
            if ((instruction >> 24) == 0xea && candidate + offset + 8 + displacement == *inventory) {
                if (equip) return false;
                equip = candidate;
            }
        }
    }
    if (!equip) return false;
    auto entry = unique({{0xe92d41f0,~0u},{0xe1a04000,~0u},{0xe2806c01,~0u},{0xe5d00171,~0u},{0xe2866071,~0u},{0xe1a00d00,~0u}});
    const auto alternate = unique({{0xe92d4070,~0u},{0xe1a04000,~0u},{0xed2d8b02,~0u},{0xe5d001a9,~0u},{0xe35000e0,~0u}});
    if (alternate) entry = alternate;
    if (!entry) return false;
    const auto pointers = matches({{0xe59f0000,0xfffff000},{0xe5900000,0xfffff000},{0xe2800a06,~0u},{0xe2800e00,0xffffff00},{0xeb000000,0xff000000},{0xe1b00fa0,~0u}});
    if (pointers.empty()) return false;
    const auto literal = [&](u32 a) -> std::optional<u32> {
        const auto instruction = word(a);
        if ((instruction & 0x0f7f0000) != 0x051f0000) return std::nullopt;
        const u32 offset = instruction & 0xfff;
        const u32 address = a + 8 + ((instruction & 0x800000) ? offset : -offset);
        if (!Valid(memory, process, address, 4)) return std::nullopt;
        return memory.Read32(process, address);
    };
    const auto global = literal(pointers.front());
    const auto table_match = unique({{0xe3500000,0xffffff00},{0x359f1000,0xfffff000},{0x30800080,~0u},{0x30810100,~0u}});
    if (!global || !table_match) return false;
    const u32 count = word(*table_match) & 0xff;
    const auto table = literal(*table_match + 4);
    const u32 cave = segment.addr + static_cast<u32>(code.size()*4) - 0xc0;
    if (!table || count <= 21 || !Valid(memory, process, *table, std::min(count,32u)*12)) return false;
    for (u32 a = cave; a < cave + 0x40; a += 4) if (word(a)) return false;
    state.entry = *entry;
    state.original = word(*entry);
    state.equip = equip;
    state.global = *global;
    state.pointer_offset = word(pointers.front()+4) & 0xfff;
    const u32 immediate = word(pointers.front()+12);
    const unsigned rotation = (immediate >> 7) & 0x1e;
    const u32 byte = immediate & 0xff;
    state.inventory_offset = 0x6000 + (rotation ? (byte >> rotation) | (byte << (32-rotation)) : byte);
    state.cave = cave;
    for (u32 i=0;i<std::min(count,32u);++i) {
        state.ranges[i] = {memory.Read16(process,*table+i*12), memory.Read16(process,*table+i*12+2)};
    }
    const std::array<u32,16> hook{0xe92d500f,0xef0000ad,0xe3500000,0x0a000004,0xe3a01000,0xe3a02000,Branch(cave+0x18,equip,true),0xef0000ad,0xe1a00000,0xe8bd500f,state.original,Branch(cave+0x2c,*entry+4),0,0,0,0};
    for (size_t i=0;i<hook.size();++i) memory.Write32(process,cave+static_cast<u32>(i*4),hook[i]);
    memory.Write32(process,*entry,Branch(*entry,cave));
    system.InvalidateCacheRange(cave,0x40);
    system.InvalidateCacheRange(*entry,4);
    state.located = true;
    LOG_INFO(Core, "Leaf ToolRing located: entry={:08X}, equipment={:08X}, cave={:08X}",*entry,equip,cave);
    return true;
}
std::vector<Tool> Tools() {
    std::lock_guard lock(mutex);
    std::vector<Tool> tools;
    if (!state.located || !state.ready || Clock::now()-state.sampled > std::chrono::seconds(1)) return tools;
    auto add = [&](int slot,u32 item) { if (tools.size()<8) if (auto t=Classify(item,slot)) tools.push_back(*t); };
    add(-1,state.held);
    std::array<bool,16> added{};
    for (int i=0;i<16;++i) {
        if (state.flags[i]&0xfd) continue;
        if (auto t=Classify(state.items[i],i)) {
            const bool duplicate = std::any_of(tools.begin(),tools.end(),[&](const Tool& other){return t->kind==other.kind && t->variant==other.variant;});
            if (!duplicate) {add(i,state.items[i]);added[i]=true;}
        }
    }
    for (int i=0;i<16;++i) if (!added[i] && !(state.flags[i]&0xfd)) add(i,state.items[i]);
    return tools;
}
bool Select(int slot) {
    std::lock_guard lock(mutex);
    if (!state.located || slot < -1 || slot > 15) return false;
    state.pending = slot;
    return true;
}
bool HandleSvc(Core::System& system,u32 immediate) {
    if (immediate != 0xad) return false;
    std::lock_guard lock(mutex);
    if (!state.located) return false;
    auto process = system.Kernel().GetCurrentProcess();
    if (!process || !process->codeset) return false;
    auto& cpu = system.GetRunningCore();
    const u32 pc = cpu.GetPC();
    const bool returning = pc==state.cave+0x1c || pc==state.cave+0x20;
    if (!returning && pc!=state.cave+4 && pc!=state.cave+8) return false;
    auto& memory = system.Memory();
    if (state.global > UINT32_MAX-state.pointer_offset || !Valid(memory,*process,state.global+state.pointer_offset,4)) return false;
    const u32 base = memory.Read32(*process,state.global+state.pointer_offset);
    if (base > UINT32_MAX-state.inventory_offset) return false;
    const u32 inventory = base+state.inventory_offset;
    if (!Valid(memory,*process,base,0x2a) || !Valid(memory,*process,inventory,0x50)) return false;
    if (returning) {
        if (cpu.GetReg(0) && state.returning) {
            const u32 item = memory.Read16(*process,base+0x26) | (static_cast<u32>(memory.Read16(*process,base+0x28))<<16);
            memory.Write16(*process,inventory+static_cast<u32>(*state.returning)*4,static_cast<u16>(item));
            memory.Write16(*process,inventory+static_cast<u32>(*state.returning)*4+2,static_cast<u16>(item>>16));
        }
        state.returning.reset();
        return true;
    }
    const u32 player = cpu.GetReg(0);
    if (!Valid(memory,*process,player,0x1aa)) {cpu.SetReg(0,0);return true;}
    const auto now = Clock::now();
    if (state.player && state.player!=player) state.changed=now;
    state.player=player;
    state.ready=memory.Read8(*process,player+0x1a9)==6 && now-state.changed>std::chrono::seconds(2);
    state.sampled=now;
    for (u32 i=0;i<16;++i) {
        state.items[i]=memory.Read32(*process,inventory+i*4);
        state.flags[i]=memory.Read8(*process,inventory+0x40+i);
    }
    state.held=memory.Read16(*process,base+0x26) | (static_cast<u32>(memory.Read16(*process,base+0x28))<<16);
    cpu.SetReg(0,0);
    const auto pending=std::exchange(state.pending,std::nullopt);
    if (!pending || !state.ready) return true;
    int slot=*pending;
    u32 item=0x7ffe;
    if (slot>=0) {
        if ((state.flags[slot]&0xfd) || !Classify(state.items[slot],slot)) return true;
        item=state.items[slot];
    } else {
        if ((state.held&0x7fff)==0x7ffe) return true;
        slot=-1;
        for (int i=0;i<16;++i) if ((state.items[i]&0x7fff)==0x7ffe && !(state.flags[i]&0xfd)) {slot=i;break;}
        if (slot<0) return true;
    }
    memory.Write32(*process,state.cave+0x30,item);
    state.returning=slot;
    cpu.SetReg(0,state.cave+0x30);
    return true;
}
}
