# Leaf 1.1.5 ToolRing native reconstruction

Source binary: `libcitra-android.so` extracted from the Leaf 1.1.5 beta Android APK (AArch64 ELF).

## Verified symbol locations

- `Leaf::ToolRing::Locate(Core::System&, Kernel::Process&)`: `0x020B7624`
- `Leaf::ToolRing::Select(int)`: `0x020B88E8`
- `Leaf::ToolRing::HandleSvc(Core::System&, unsigned int)`: `0x020B8944`

## Locate: verified instructions

- `0x020B7700`: loads a pointer from the process object using offset `0x12030`.
- `0x020B7708`: reads a pair of 32-bit values at offsets `0x40` and `0x44` from that object.
- `0x020B7738`: obtains the memory-system pointer from the Core::System object at offset `0x328`.
- `0x020B7748`: calls the native `Memory::MemorySystem::ReadBlock(Kernel::Process const&, unsigned int, void*, unsigned long)` implementation.
- `0x020B7B5C` through `0x020B7B6C`: constructs address `0x12100` and calls `Memory::MemorySystem::Read32(unsigned int)`.

These are binary-specific C++ object offsets and must not be copied into Azahar: the target build may have different object layouts. Translate them to the corresponding named C++ interfaces instead.

## Integration requirements

- Locate the game's inventory and equipment structures by reproducing the native pattern-matching and validation logic.
- Check ACNL title identification before running game-specific hooks.
- Do not write to guest memory until both source and destination have been located and validated.
- Service call `0xAD` is currently unused in Azahar's `src/core/hle/kernel/svc.cpp`; Leaf's native handler hooks it.
- Keep the tool-ring state and memory write logic separate to avoid unsafe writes from the controller/input thread.

This document records disassembly observations, not a completed port.

## Native port (2026-11-09)

`tool_ring.cpp` now translates Locate, Apply, HandleSvc, Tools, and Select from the APK. It scans the process code segment using Leaf's masked ARM signatures, follows PC-relative literal loads through named memory interfaces, validates the zero-filled code cave, installs the 16-word trampoline, and intercepts SVC 0xAD only at that trampoline's call sites. Equipment changes occur on the emulation thread. Invalid guest pointers skip the equipment call.

The snapshot contains 16 four-byte inventory items, 16 slot flags, and the held item. Leaf's eligibility checks are retained: player state byte `+0x1A9 == 6`, a two-second player transition delay, one-second snapshot expiry, slot flags restricted to 0 or 2, eight displayed entries, and duplicate kind/variant suppression before filling remaining entries. Selecting the held item finds an empty eligible pocket and puts it away. The second SVC returns the previous held item to the selected pocket only when the game's equipment routine succeeds.

The hook layout is: push at +0x00; first SVC +0x04; compare +0x08; skip branch +0x0C; argument setup +0x10/+0x14; game equipment BL +0x18; second SVC +0x1C; nop +0x20; pop +0x24; displaced original instruction +0x28; return branch +0x2C; selected-item literal +0x30.

Tool kinds and icon IDs were recovered from the APK's table at 0xF1AA30 and LeafToolRing.java: axe 14 (319,319,350,322), net 15 (325,325,353,326), rod 16 (323,323,352,324), shovel 17 (317,317,351,318), slingshot 18 (363,363,365,364), watering can 19 (327,327,354,328), megaphone 21 (482).

The OpenGL/GLES frontend draws LeafCursorView's eight-slot layout: center (width/2,height*0.56), ring radius height*0.22, disc radius height*0.062, selected scale 1.2. Icons are read from the user's game RomFS, `Layout/ItemWin/item_icon.arc`, entries `icn_NNN.bclim`; no game images are bundled. Current icon decoding supports the 32x32 ETC1A4 files verified in the supplied archive. The Android pointing-hand graphic, text bubble, are not ported. The first IPA incorrectly wired ring drawing only to OpenGL, which is disabled on Apple targets; that build therefore had no software-frame overlay. The corrected implementation composites the ring into the row-major XRGB8888 software framebuffer before presentation, preserving row pitch and the X byte.

R2 toggles with 200ms debounce; D-pad steps around eight positions; left-stick magnitude >=0.5 selects by angle; A confirms; B cancels. While open, controller inputs are consumed before guest HID sees them. The frontend also sends text labels through libretro notifications.

### Verification

`python tools/test_leaf_tool_ring.py` runs the reproducible synthetic guest checks (requires g++). The C++17 harness compiles the actual backend with `-Wall -Wextra -Werror` against an in-memory guest fixture. Checks passed for signature discovery, branch destinations, trampoline installation, archive icon lookup using the supplied item_icon.arc, inventory snapshots, equipment request literal, successful old-equipment return, locked-slot rejection, invalid-pointer skip, reset, and invalid selection rejection. This is synthetic integration verification, not an iPhone gameplay test. iOS compilation and device behavior must be tracked separately.

### Single-screen audit

Existing `LayeredFS` changes only apply horizontal RootPane scale compensation. They do not reproduce Leaf's map, pocket/inventory, keyboard, or other scene grafting. Leaf has additional native `LayoutPatch::GraftWindow`, `Ui::Initialize`, `Ui::FrameRegions`, `Ui::Buttons`, `UiPatcher`, and `PocketStacks` components. The existing camera and RootPane changes were retained. The ToolRing port must not be described as the complete single-screen Leaf port.

### Dialogue inspection

The APK has native references to `Layout/msgWin/msg_win.arc` at 0x20BB34C, `Layout/common/cmn_dialog_A.arc` at 0x20BB6A4, and `choice_bubble` at 0x20C5000. It exports `Leaf::Ui::NotifyConfirm()` (0x20CB87C). These establish dialogue/choice UI handling, not a universal A-button prompt. `LeafCursorView.drawButton` draws close and expand/collapse symbols; the verified explicit A hint in Android strings is `leaf_tool_ring_put_away` = `A: put away`. A normal-dialogue "Press A" overlay has not been established by this inspection.


## R2 visibility correction

The first ToolRing IPA lacked the overlay shader string because `ENABLE_OPENGL` is disabled by the Apple CMake configuration. `RenderToolRing` had its complete body behind that flag, and the software SwapBuffers path never called it. Native inventory/equipment code existing in the binary did not establish that the ring could be displayed.

The corrected path shares the CPU ring rasterization and explicitly composites RGBA overlay pixels into XRGB8888 frames, after the top-screen blit and before UploadVideoFrame. It retains the OpenGL path separately. R2 can also show a four-second native status label when there are no eligible tools; failures now distinguish missing signatures, an occupied code cave, a player hook not firing, and an unavailable player state. Confirm/cancel/toggle buttons remain blocked until physical release, preventing a held A from leaking into gameplay after closing the ring.

The regression harness now compiles the actual software overlay without `ENABLE_OPENGL` and extracts the actual controller-polling and CheckInput functions from environment.cpp. Tests exercise R2 opening, stick direction, D-pad stepping, A confirmation, release blocking, an empty-inventory diagnostic, and B cancellation. Pixel assertions verify visible ring output at widths 400, 522, and 928, correct BGRA channel order, unchanged X bytes, untouched row padding, and no compositing when closed. Guest-memory checks remain in the same harness. Texture sampling is stubbed in this host harness; actual ETC1A4 decoding is compiled by the iOS build and still requires device verification.


The visibility correction also covers Vulkan/MoltenVK rather than assuming which renderer Manic chooses. The same CPU RGBA overlay becomes run-length colored triangle quads; a dedicated alpha-blended Vulkan pipeline draws those vertices in DrawScreens before the presentation render pass ends. Icons, disc positions, and diagnostic text therefore use the same rasterization on both software and Vulkan. The host test reconstructs the complete RGBA overlay from the generated triangle mesh and asserts exact pixel equality, buffer bounds, packed RGBA vertex colors, and a mesh size below the dedicated four-MiB stream buffer limit. This validates mesh generation, not Vulkan driver behavior on an iPhone.


## Missing icon correction

Device testing confirmed the ring is visible but the icons were absent. The LayeredFS path filter compared a 27-character suffix to the 28-character string `Layout/ItemWin/item_icon.arc`, so SetToolIconArchive was never called. The filter now uses IsToolIconArchivePath and derives the comparison length from the string itself. Ring control, equipment hooks, camera expansion, and UI scaling are unchanged.

The host regression checks the actual archive path, rejects nearby incorrect names, sends a synthetic BCLIM archive through the path predicate and production archive reader, and asserts that the sampled icon color reaches the visible software framebuffer. Texture sampling remains stubbed for that integration test. An optional LEAF_TOOL_ICON_ARCHIVE path additionally checks all 19 distinct supported tool-icon IDs against the supplied game archive without embedding game graphics in the repository.


## Bottom-screen scene port, first pass (2026-11-10)

Native evidence was recovered from the supplied APK's arm64 libcitra-android.so. The anonymous BuildScenes function at 0x20C4E50 was executed in an isolated AArch64 emulator with allocator/string imports stubbed. Its returned vector has fourteen SceneConfig records of 0x120 bytes. These are configuration records, distinct from Ui::Initialize's runtime scene records of 0x270 bytes. Ui::FrameRegions is at 0x20CBCD4. UiProbe::TextureKey at 0x20BFBF8 hashes the raw texture payload with XXH3_64bits and XORs width<<48 and height<<32. UiPatcher::BuildTextureKeys (0x20BC4A4) resolves BCLIM assets using archive-relative names; no Android screenshot recognition is involved.

Recovered scenes relevant to this pass:

| Scene | Priority | Visible texture rule | Placement |
| --- | ---: | --- | --- |
| choice | 5 | selWin/slct_win.arc: | source (40,1)-(280,216), target (0.965,0.54), origin (1,1), scale 0.8, bounds padding 3 |
| choice_map | 6 | choice plus Menu/map_*; excludes my_men_btn_00 | source (0,0)-(320,224), target (0.5,0.485), origin (0.5,1), scale 0.52 |
| map_pick | 3 | map plus common/cmn_btn.arc:my_cbn_btn.bclim | three separately placed map/accept/reject crops |
| map_show | 2 | map_village/island/lobby/exb; excludes menu_tab | source (0,0)-(320,224), target (0.5,0.485), origin (0.5,1), scale 0.5 |
| tab_screen | 0 | Menu/menu_tab.arc: | source (0,1)-(320,240), centered, fit, backdrop 0.45 |
| town_hud | 1 | menu_tab plus my_men_btn_00 | hud_tabs top left; hud_map bottom right, scale 0.42, (-4,-4) offset, source y>=26 |

The anonymous region geometry function at 0x20CF2E4 was also executed directly with these recovered records and a (0,0)-(522,240) display rectangle. Its exact integer destination rectangles are golden fixtures in test_leaf_ui.py. RegionConfig +0x18 is the source rectangle, +0x28 the target position, +0x30 the origin, +0x38 offsets, +0x40 scale; it is not a vector at +0x18. Scale multiplies display-height/240 and the cropped source dimensions. Each destination edge is rounded and clamped independently. UiProbe::EndFrame at 0x20C1034 converts raw draw bounds using (320-maxY,240-maxX,320-minY,240-minX); that conversion is used before cropping. A native TextureKey call for bytes 0..63 at 8x8 returned 0x618FEB9889B0ED55, also a golden fixture.

The new ui.cpp ports those texture-key rules and a first subset of their region presentation. RegisterUiArchive reads matching BCLIM payloads from the user's RomFS. PicaCore probes texture0 before accelerated DrawArrays can return, runs Leaf's small-batch vertex-shader bounds calculation, and records bounds against the guest render target. GPU display transfers propagate that visibility to LCD buffers. FinishUiFrame selects the displayed bottom buffer, including double buffering. The software libretro presenter and Vulkan presenter composite supported bottom regions before ToolRing. Native X/D-pad/A/B/L/R input remains unchanged. UiTouch reverses the exact displayed region transform into normalized 320x240 coordinates; the single-screen libretro pointer calls TouchNormalized only inside a visible region, releasing outside or while ToolRing is open. This implements direct visible-region touch rather than a cursor on a hidden screen. No dialogue prompt was invented.

This is not the full Leaf single-screen port. Layout grafts, magenta-key texture patches/transparency, fades, Android expand/collapse controls, keyboard, board, CAT tasks, coupons, letters, lighting, and other scene-specific regions remain unported. Dialogue without a choice texture does not activate a new overlay; a universal A-button hint remains unestablished. Crop/placement and menu navigation still require actual iPhone tests. Existing widescreen and RootPane scaling were retained.

Verification: tools/test_leaf_ui.py compiles production archive parsing, XXH3 hashing, scene routing, transfer tracking and software composition with warnings treated as errors. It includes eight native geometry golden fixtures, a native XXH3 texture-key fixture, inverse touch coordinates for menus/map buttons, double-buffer switches, unchanged LCD frames, clearing stale scenes after untextured draws, dialogue bounds unions/padding, map and menu priorities, channel conversion and row padding. LEAF_MENU_TAB_ARCHIVE can test the actual supplied menu_tab.arc without committing game data. tools/test_leaf_tool_ring.py remains the ToolRing regression check. Host tests do not establish GPU driver or gameplay correctness.


## No-visible-change device report: compressed texture probe correction

The first bottom-UI IPA produced no visible change in device testing. Inspection found that ProbeLeafUi used TexturingRegs::NibblesPerPixel, which does not handle ETC1 or ETC1A4 in this fork and returns zero for both. Leaf::UiProbe::BitsPerPixel at 0x20BF7DC instead uses the complete table at 0xF1AB08: 32,24,16,16,16,16,16,8,8,8,4,4,4,8. UiTextureSize now uses that table, including the two compressed formats. The regression suite checks every format and passes synthetic compressed BCLIM payloads through archive registration and runtime matching using the production byte-size function. This fixes a demonstrated code defect; it does not establish that it was the only cause of the on-device failure.

Leaf's EndFrame also falls back to the most recently observed render target when the displayed bottom address has no corresponding draw record (0x20C0ACC onward). The port now retains draw sequence numbers and supports that fallback for currently drawn, recognized UI textures. An explicitly tracked blank LCD frame still clears the scene. Tests cover the missing-address route and its clearing on the next untextured frame.

Opening the working R2 ring now shows a short UI FIX 2 status line. REG is the number of registered texture keys, D the observed draw count, H matching draws, T transfers, TAG the selected displayed scene bits. This uses the existing software/Vulkan overlay path and remains hidden when the ring is closed. These counters distinguish an unloaded registry, inactive probe, texture mismatch, and framebuffer-routing failure during device testing.


## Flicker, proportions and cut-out correction

Device screenshots from UI FIX 2 confirmed visibility but showed strong flicker, tall/narrow shapes, cream rectangles around HUD tabs, and inventory sometimes placed like a choice bubble. The last condition was reproduced as a signature ambiguity: im_cmn_arrow32_00.bclim is byte-identical in menu_tab.arc and slct_win.arc. Shared im_cmn_ navigation sprites no longer count as evidence of a dialogue-choice body; the actual choice-window textures still do.

The old per-VBlank epoch reset could clear a render target midway through the game's draw sequence. Pending draw observations now accumulate until a completed bottom display transfer publishes an immutable snapshot. VBlanks never clear it. Top-screen transfers (240x400) and texture copies cannot publish a bottom snapshot. The selected LCD buffer retains its last completed snapshot between game frames, including repeated refreshes and partial next-frame draws. A completed blank bottom transfer removes the UI immediately. This is transfer synchronization, not a timer that hides missing observations.

Bottom-screen CLYT canvases are 320x240 and must not receive top-screen widescreen compensation. PatchUiArchive parses archive entries and CLYT section boundaries, changing RootPane X only on authored 400x240 canvases. Real menu_tab/map/choice layouts keep X and Y scale 1. The working camera modification is unchanged, as is compensation on authored top layouts such as msg_win's 400x240 canvas.

Native patch recipes were recovered by executing the descriptor initializer at 0x20BB05C in an isolated AArch64 emulator. Menu/mvmnt_undscrn.arc's flag at descriptor+8 requests FillBclimWithKey for every supported BCLIM and hides P_base_00/P_base_01. menu_tab hides im_tab_05, B_back_00, P_post_00, B_post_00. slct_win hides P_kuchi_00/P_kuchi_sh_00 and sets P_pattern_00 alpha to 255. These exact masks are now applied to the user's RomFS archives in memory; no game assets are bundled. The mvmnt canvas itself is authored 400 pixels wide, but its now-keyed wallpaper is invisible.

FillBclimWithKey at 0x20AAB4C writes ETC1 block bytes 00 00 00 00 00 FF 00 FF, or an ETC1A4 block with eight FF alpha bytes followed by that block. Footer metadata is preserved. GPU::LeafClearForBottomScreen at 0x217A098 records the bottom transfer's input range and clears that color buffer after copying to the LCD destination. GPU::MemoryFill at 0x21789B8 overrides later overlapping color fills with magenta in the source pixel format. Both operations are translated: RGBA8 FF00FFFF, RGB8 FF00FF, RGB565 F81F, RGB5A1 F83F, RGBA4 F0FF. Unrelated fill ranges and depth buffers are left alone.

The complete Leaf Vulkan presentation shader source was recovered from the APK's native read-only data and transcribed to vulkan_leaf_ui.frag, with attribution to the GPL Azahar/Leaf shader here. It unmixes each texel from magenta, neutralizes Leaf's lavender rim into a faint shadow, and filters premultiplied keyed texels before converting to straight alpha. A dedicated alpha-blended UI pipeline uses this shader; the ordinary game presentation pipeline is unchanged. Software composition translates the same key/unmix/rim formula and filters premultiplied samples before blending. Config+0x48/+0x4C are key threshold/softness (0.06/0.08), as demonstrated by DrawBottomRegions at 0x220B110 loading the resulting region+0x28/+0x2C into leaf_params.y/z; earlier notes incorrectly described these as fade durations. Choice opacity remains 0.96.

Tests cover pending/completed frame separation, repeated VBlanks, top/bottom transfer separation, immediate blank-frame clearing, shared arrow rejection, every pixel-format clear value and range guard, transparent magenta, opaque colors, white antialiased edges, Leaf's rim color, and magenta-free bilinear edge compositing. test_leaf_ui_archive.py checks native mask bytes and 400/320 canvas separation; LEAF_LAYOUT_ZIP optionally verifies the supplied four archives, including all eleven wallpaper BCLIMs. The iOS workflow additionally compiles both descriptor-indexing variants of the recovered shader to SPIR-V before packaging. Physical-device behavior remains unverified until the next test.


## Immediate ROM-load crash report after FIX 3

Device feedback reports an exit before any game image. The exact fault is not established without a device crash trace. FIX 3 introduced a new optional graphics pipeline and enlarged the shared presentation push-constant ABI from the previously working 112 bytes to 128 bytes during renderer initialization. FIX 4 restores the 112-byte ABI for every presentation pipeline. The Leaf-only shader reads its choice-opacity selector from the otherwise unused right-screen field during a bottom-UI draw, then restores that field. Its threshold, softness, key/unmix/rim math, and premultiplied filtering remain unchanged.

The Leaf shader now uses constant bottom-texture index 2 for textureSize and texelFetch instead of dynamic texture selection and helper switches; it is compiled without descriptor-indexing feature defines. An empty shader compilation result is checked before module creation. Optional keyed-pipeline initialization errors are logged and caught instead of asserted; if unavailable, the base renderer remains active and the R2 status line reports GPU OFF. This is startup hardening and an iOS compatibility change, not proof of the crash's sole cause. The new marker is UI FIX 4.

Verification adds an optional AddressSanitizer/UBSan scan across every archive in the supplied Layout ZIP. CI translates the actual vertex/fragment SPIR-V to iOS Metal, compiles the generated fragment with the iPhone SDK, and attempts a Metal render-pipeline smoke test on the build host when a Metal device exists. These checks do not replace an iPhone ROM-load test.
