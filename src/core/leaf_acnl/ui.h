#pragma once

#include <array>
#include <cstddef>
#include <string>
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
UiRect UiLandscapeBounds(UiRect raw);
size_t UiTextureSize(u32 format, u32 width, u32 height);
std::string UiStatus();
void ResetUi();
bool IsUiArchivePath(std::string_view path);
void RegisterUiArchive(std::string_view path, const std::vector<u8>& bytes);
bool UiEnabled();
u32 UiTexture(const u8* data, size_t size, u32 width, u32 height);
void UiTarget(u32 address, u32 width, u32 height, u32 format,
              u32 view_width = 0, u32 view_height = 0);
void UiCommandComplete();
bool UiAfterDisplayTransfer(u32 source, u32 destination, u32 width, u32 height,
                            u32 output_width, u32 output_height, u32 format,
                            u32& end, u32& value, u32& control);
bool UiPrepareFill(u32 start, u32 end, u32& value, u32& control);
void UiPresent(u32 bottom);
void SetUiAlphaReady(bool ready);
std::array<float,4> UiKeyPixel(float r, float g, float b);
void UiDraw(u32 target, u32 tags, UiRect bounds = {});
void UiTransfer(u32 source, u32 destination);
void FinishUiFrame(u32 bottom);
std::vector<UiRegion> UiRegions(float width, float height);
bool UiTouch(float x, float y, float width, float height, float& touch_x, float& touch_y);
void CompositeUi(u8* output, size_t pitch, u32 width, u32 height,
                 const u8* bottom, size_t bottom_size, u32 bottom_stride);
}
