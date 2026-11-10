#pragma once

#include <cstddef>
#include <string_view>
#include <vector>
#include "common/common_types.h"

namespace LeafACNL {
struct UiRect {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool Valid() const;
};
struct UiRegion {
    UiRect source;
    UiRect destination;
    float shade = 0;
};
void ResetUi();
bool IsUiArchivePath(std::string_view path);
void RegisterUiArchive(std::string_view path, const std::vector<u8>& bytes);
bool UiEnabled();
u32 UiTexture(const u8* data, size_t size, u32 width, u32 height);
void UiDraw(u32 target, u32 tags, UiRect bounds = {});
void UiTransfer(u32 source, u32 destination);
void FinishUiFrame(u32 bottom);
std::vector<UiRegion> UiRegions(float width, float height);
void CompositeUi(u8* output, size_t pitch, u32 width, u32 height,
                 const u8* bottom, size_t bottom_size, u32 bottom_stride);
}
