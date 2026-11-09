#pragma once

#include <vector>
#include "common/common_types.h"

namespace Core {
class System;
}
namespace Kernel {
class Process;
}
namespace LeafACNL {
struct Tool {
    int slot;
    u8 kind;
    u8 variant;
    u16 icon;
};
bool Locate(Core::System& system, Kernel::Process& process);
bool HandleSvc(Core::System& system, u32 immediate);
std::vector<Tool> Tools();
bool Select(int slot);
void ResetToolRing();
void SetToolIconArchive(const std::vector<u8>& bytes);
std::vector<u8> ToolIcon(u16 icon);
}
