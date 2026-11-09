#pragma once

#include <vector>
#include "common/common_types.h"

namespace Core { class System; }
namespace Kernel { class Process; }
namespace LeafACNL {
struct Tool {
    int slot;
    u8 kind;
    u8 variant;
};
bool Locate(Core::System& system, Kernel::Process& process);
bool HandleSvc(Core::System& system, u32 immediate);
std::vector<Tool> Tools();
bool Select(int slot);
void ResetToolRing();
}
