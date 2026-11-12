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


Fix2 recovery: user reports Fix6 produces a magenta scene and removes top UI. Commit7e32dd1 restores the exact Fix2 tree (cf1ffdadf2b99d52e508f5fb3465b04f2a8ecc9f), preserving all failed commits in history. The recovery candidate changes only layout compensation scope and shared-texture classification, plus its diagnostic label and tests. GPU transfer handling, frame tracking, Vulkan presentation, software composition, tool ring and camera code remain byte-identical to Fix2. There are no keyed clears, wallpaper rewrites, hidden panes or custom Leaf shaders. Bottom320x240 layout roots retain unit scale; only authored400x240 top layout roots receive the existing widescreen X correction. Shared im_cmn_ navigation sprites remain MenuTab evidence but cannot independently classify a dialogue window. Original flicker and opaque UI rectangles are not claimed fixed in this recovery. Device verification remains pending.


Fix2S follows the phone-confirmed Fix2 Recovery baseline. It addresses two demonstrated defects: host-refresh epochs could reset pending guest UI draws, and the stock present pipeline used constant-alpha blending rather than source-alpha blending. Pending targets are now reset on an actual full target clear or reuse after completion. All existing transfer observations remain enabled, including copies; immutable completion records are latched per destination. Bottom buffer swaps also complete direct-rendered UI targets. Host refreshes never reset pending observations. Explicit completed blank frames remove the overlay.

The alpha path uses Leaf's recovered magenta unmix formula (alpha=1-clamp(min(r,b)-g), threshold.06, softness.08), with premultiplied four-texel filtering before source-alpha blending. The stock112-byte uniform ABI and normal presentation pipeline remain unchanged; screen_id_r=-1 selects keying only when screen_id_l==2 during an overlay draw. An additional blend-state pipeline reuses the already-compiled stock vertex/fragment pair. Pipeline failure leaves the opaque fallback usable and disables keyed guest clears. Software composition uses the same key equation and preserves destination alpha.

Clears are no longer inferred from transfer dimensions or broad overlap. Each PICA draw supplies the actual color-buffer address, GetWidth/GetHeight and PICA color format. A clear can be changed only after non-common MenuTab/Map/Choice texture evidence, bottom geometry (240/256x320 or320x240/256), exact full-range equality, and alpha-pipeline readiness. PICA color format2 is RGB5A1 and3 is RGB565, unlike the GPU transfer-format order. Geometry/format changes revoke a target's qualification. Partial ranges, depth/unrelated buffers, top shapes400x240/240x400 and top reuse are rejected. There is no post-transfer fill and no BCLIM texture recoloring.

Recovered pane masks hide MenuTab im_tab_05/B_back_00/P_post_00/B_post_00, choice P_kuchi_00/P_kuchi_sh_00, and mvmnt_undscrn P_base_00/P_base_01. Only visibility bit0 at pane+8 is changed. The failed previous patch also wrote255 at pane+9 on P_pattern_00; that byte is origin, not alpha. This implementation preserves all origins, alpha bytes, animation data and texture payloads. Existing top-layout X compensation remains isolated to400x240 roots.

Tests cover 180 refreshes between partial draws, copy completion, unrelated top transfers, explicit blank completion and bottom buffer-swap fallback. Clear tests cover all five PICA pixel formats, full/partial/depth/top ranges and address reuse. Alpha tests verify magenta transparency, opaque black/white/green, anti-aliased white and unchanged scene pixels. All79 supplied archives pass the sanitizer scan with52 top-scale changes and exactly8 visibility-only mask changes. CI additionally renders the compiled production shaders on Metal: normal magenta must remain opaque, keyed UI must reveal the scene, opaque UI must remain visible, and filtered edges must blend without a magenta fringe. These are controlled tests; iPhone gameplay remains pending.


Fix2T device evidence: IMG_5936 shows REG246/D214994/H16595/T5263/TAG0/K0/AON. Shader startup and texture matching work; transfers occur, but Fix2S never selects a scene or keys a clear. Fix2S removed Fix2's most-recent-draw fallback, and its tests incorrectly expected no overlay when matching draw targets lacked an LCD record. The fallback is now latched when PICA ProcessCmdList finishes. Matched draw evidence is copied before later clears, target reconfiguration or unrelated transfers can destroy it. Host refreshes do not publish partial command lists or reset this snapshot. Unknown-source copies cannot overwrite recognized UI with an unproven blank. Genuine completed blanks from known UI targets still clear the scene by sequence order.

Viewport dimensions are now recorded separately from storage dimensions. Clear qualification uses the viewport, while the range uses the stored allocation and actual copied input span. A padded256x512 allocation with a240x320 viewport is a regression fixture, not a measured device dimension. The next diagnostic line reports both V and B so the real values are observable. A top240x400 viewport on the same storage is rejected; a target observed serving the top screen cannot subsequently receive a key clear without storage reconfiguration. Native post-display-transfer source clearing is restored only for an exact identified UI source, matching input format/stride, enough copied rows to cover the viewport, and non-overlapping destination. The copied span is retained as the exact permitted range for later game fills. Texture-copy flags are never interpreted as color formats. Shader, blend state, pane masks, camera and112-byte ABI remain identical to the Metal-tested Fix2S version.

Regression tests now require visible HUD/menu evidence after command completion with no LCD association or buffer-swap callback. They cover repeated unknown LCD copies, clearing/reusing the source before presentation,180 unchanged refreshes, genuine blank completion, padded storage versus viewport, top/shared-source rejection, copied-span bounds/overlap/format rejection and all five GPU-to-PICA pixel-format mappings. These fixtures exercise the missing production route; successful synthetic tests still do not establish iPhone gameplay behavior.


Fix2U is based on the user's visible Fix2T screenshots. They show V240x320/B256x416, K0/AON, a displaced duplicate of the player/world inside choice rectangles, inventory tabs moving to the centered panel, and stale rectangles after choices close. Fresh APK disassembly resolves the lifecycle error: GPU::MemoryTransfer at0x2179030 checks output-height320, deactivates the clear phase in that branch, and otherwise records source/input-span/format before calling LeafClearForBottomScreen via PLT0x22C5420 at0x21790C4. The clear prepares the shared scratch buffer AFTER the TOP copy and BEFORE bottom UI drawing; it is not a clear reserved for bottom-owned buffers. GPU::MemoryFill at0x2178A24 checks that phase before overriding overlapping fills. The prior top_seen rejection blocked the exact shared-buffer case in the phone screenshots.

The port now arms only after a completed display transfer from an observed top viewport, validates source storage/format/stride/span and non-overlap, clears that source to key, and permits only exact full/learned UI fill ranges during the top-to-bottom phase. A bottom display transfer closes the phase even if its source is untracked, preserving normal clears for the next top scene. A bottom copy from the phase source publishes an authoritative completed frame, including empty frames, independently of LCD-address matching. No host timeout is used. Shader, blend state, camera, pane masks and112-byte presentation ABI remain unchanged from Fix2T.

BuildScenes at0x20C4E50 was independently executed again in AArch64 emulation. Its14 records include title_menu, priority4. Its0x50 texture-rule vector contains selWin/cmn_slct_win.arc:im_cmn_arrow32_00.bclim and Title/opng_titl.arc:my_opn_bg_01.bclim. Native patcher strings additionally identify selWin/cmn_slct_win.arc:my_csw* and Title/opng_titl.arc:my_csw* corner assets. The recovered region is source(20,20)-(300,220), target(.5,.55), origin(.5,.5), scale.85, with no fit. This scene and these archive registrations are now ported. Title backdrop context is carried from the top copy into the bottom phase; its combination with the shared arrow activates the menu. CSW corner assets also identify the menu; a shared arrow alone does not activate it. The522x240 destination golden is(142,47)-(380,217).

The native tab_screen scene centers a4:3 panel; the user's stationary-tab preference intentionally changes that placement. Inventory is now three non-overlapping source regions: proportion-preserving body below row25, the same top-left tabs used in the town HUD, and a top-right control strip. Touch inversion uses each actual region. Main-scene camera aspect is unchanged.

Tests exercise all five transfer/PICA format mappings and the full shared256x416 lifecycle: copy the top image, clear only the scratch source, draw/copy bottom UI, close its clear phase, preserve the next top clear, then copy a truly blank bottom and remove the old choice without LCD association. The old-world marker remains in the saved top image and is absent from transparent bottom pixels. Other checks cover overlap/range rejection, unknown-source bottom phase closure, real supplied title texture keys, native title geometry, anchored inventory tabs and inverse touch. Device gameplay remains pending verification.


Fix2V follows the user's confirmed Fix2U improvement and attached recording. K16408/AON confirms source clearing now executes. The recording still shows inventory wallpaper, a top-right crop carrying scenery, rapid scene changes, and magenta outside the authored fade bounds. Completed display-copy records now take precedence over newer partial command-list observations. Exact LCD destinations select their matching immutable records; missing LCD association uses only the last authoritative bottom copy once that route is established. A partial UI draw or unrelated texture copy cannot change that fallback. This preserves the Fix2U clear phase and shader. No frame timeout or arbitrary hold duration is introduced.

The supplied ItemWin/item_scrn.arc has three background pictures P_bg_C/P_bg_T/P_bg_B on a320x240 canvas. Their visibility flags are masked, leaving item pictures, slot markers, bells, character and letter controls intact. Inventory texture registration supplies its own scene bit512 rather than relying only on menu_tab draws. The extra right-hand25-row crop is removed; its current contents produced the reported upper-right artifact. The body and the original top-left tabs remain separately mapped with inverse touch coordinates. This is a layout-only adaptation of the supplied game assets, following Leaf's pane-mask technique; it does not claim recovered native patch-table equivalence for these three additional names.

The supplied Wipe/fade.arc contains P_fade_00, a400x240 picture. Existing top-root aspect compensation preserves that authored width, leaving the expanded camera sides uncovered. Only this named picture's width is expanded by the inverse correction; other panes and UI scales retain their current behavior. No replacement texture is required.

Manic's ControllerView.hitTest returns its buttonsView for empty space unless allowTapThroughIfButtonNotHit is enabled. Azahar had not enabled it. The packaging frontend now uses that existing path for Azahar, forwarding empty-space touches to CocoaView, whose sendEvent feeds cocoa_input RETRO_DEVICE_POINTER; core UiTouch then inverts the displayed region. Onscreen controls keep hit-test priority. This requires rebuilding the frontend, so its cached app key is advanced. Phone touch behavior and dynamic flicker remain device checks, not claims established by the host tests. The slightly transparent pink tab is not changed: the current recovered magenta-unmix formula can reduce alpha for magenta-rich foreground colors, and changing that formula globally needs separate edge/color validation.


Fix2W follows phone confirmation that Fix2V tap-through works and inventory wallpaper removal helps, while UI still flickers and the pink tab loses opacity. The new recording shows item action menus crossing the inventory/tab source split, profile-help windows changing during transitions, and magenta at the circular door wipe's outer edge. Local workspace sources were refreshed from the actual Fix2V GitHub commit691c690199615d36eff2701fe07d58fc5e346e93 before editing.

Fix2V latched scene metadata but RendererVulkan::PrepareRendertarget independently read the current bottom LCD address/configuration. The fallback therefore selected completed tags without also selecting that completed copy's image. Each authoritative display-copy record now carries validated destination address, dimensions, byte stride and GPU output pixel format. FinishUiFrame selects those together; the single-screen software/Vulkan renderers load the exact selected descriptor. Capture is limited to non-scaled linear outputs without the special crop/flip offset case. Unsupported outputs retain the existing path. This records an image address, not a private immutable GPU pixel snapshot; it does not claim source-cache aliasing or driver issues have been ruled out. LCD bottom colour-fill blanks suppress stale overlays explicitly, and normal dual-screen presentation keeps its existing framebuffer selection. R2 reports IMG ON/OFF for this route. The Fix2U top-to-bottom clear timing is unchanged.

ItemWin/itm_slct_win.arc textures now identify item popups, with draw bounds from the existing native-style vertex probe. The popup source rectangle is subtracted from the inventory and anchored-tab slices, then drawn whole in the inventory body's coordinate space. Its source-to-destination transform stays fixed as bounds animate; inverse touch uses that same final region. Shared im_cmn_/my_cmn_ textures cannot independently identify this popup. Registration was checked against the supplied archive. This addresses the split visible in the recording without a timer or a fabricated touchscreen menu.

The pink-tab repair is an explicit scoped compositor adaptation, not claimed to be Leaf's unmodified formula: only the second52-pixel tab strip selects a red/blue-balance gate for chroma spill. Pure magenta and balanced filtered edges retain key removal, while asymmetric pink foreground RGB stays opaque. Other UI regions and the normal main screen retain the existing shader path. The existing112-byte push-constant ABI is unchanged; screen_id_r=-2 selects the scoped mode, versus-1 for normal Leaf keying. Software composition uses the same equation. Apple Metal pixel tests now include the pink mode's key and foreground colour in both shader compilation variants.

The supplied Wipe/wipe_circle.arc has static black side panels P_L/P_R (width240), P_T/P_B (width400), and a separate animated opening P_C. The black-panel widths receive the inverse aspect correction used by the flat fade; P_C and the camera remain unchanged. Regression tests check real archive changes, alternating completed image descriptors including unknown LCD association,120 buffer swaps, LCD blank/unblank, popup exclusion and hit mapping, pink RGB/alpha, ToolRing, and the original layout-scaling constraints. Gameplay and dynamic flicker reduction remain pending device verification.


The same popup-splitting defect also applies to the Town Pass Card help bubbles visible in the recording. The supplied common/cmn_dialog_A.arc is320x240 and contains my_cmn_ballon_XXL.bclim; Leaf's UiPatcher references this archive at0x20BB6A4 (recorded above). This specific balloon texture now marks a common dialog, using the same whole-popup presentation and source exclusion as item action menus. Shared stripe/base/corner textures do not independently activate it. Actual archive registration and a dialog crossing row25 are regression fixtures.
