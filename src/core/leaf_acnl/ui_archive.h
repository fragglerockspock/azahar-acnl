#pragma once
#include <cstddef>
#include <string_view>
#include <vector>
#include "common/common_types.h"
namespace LeafACNL {
struct UiArchiveChanges {
    size_t top_roots = 0, hidden_panes = 0, keyed_textures = 0;
    bool Any() const { return top_roots || hidden_panes || keyed_textures; }
};
UiArchiveChanges PatchUiArchive(std::string_view path, std::vector<u8>& bytes, float top_scale);
}
