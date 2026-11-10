#pragma once

#include <vector>
#include <string>
#include <string_view>
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
std::string ToolRingStatus();
void ResetToolRing();
inline bool IsToolIconArchivePath(std::string_view path) {
    constexpr std::string_view suffix="Layout/ItemWin/item_icon.arc";
    return path.size()>=suffix.size() && path.substr(path.size()-suffix.size())==suffix;
}
void SetToolIconArchive(const std::vector<u8>& bytes);
std::vector<u8> ToolIcon(u16 icon);
}
