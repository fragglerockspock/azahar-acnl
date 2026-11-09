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

The OpenGL/GLES frontend draws LeafCursorView's eight-slot layout: center (width/2,height*0.56), ring radius height*0.22, disc radius height*0.062, selected scale 1.2. Icons are read from the user's game RomFS, `Layout/ItemWin/item_icon.arc`, entries `icn_NNN.bclim`; no game images are bundled. Current icon decoding supports the 32x32 ETC1A4 files verified in the supplied archive. The Android pointing-hand graphic, text bubble, and Vulkan/software ring presentation are not ported. The iOS package uses OpenGL/GLES.

R2 toggles with 200ms debounce; D-pad steps around eight positions; left-stick magnitude >=0.5 selects by angle; A confirms; B cancels. While open, controller inputs are consumed before guest HID sees them. The frontend also sends text labels through libretro notifications.

### Verification

A local C++17 harness compiled the actual backend with `-Wall -Wextra -Werror` against an in-memory guest fixture. Checks passed for signature discovery, branch destinations, trampoline installation, archive icon lookup using the supplied item_icon.arc, inventory snapshots, equipment request literal, successful old-equipment return, locked-slot rejection, invalid-pointer skip, reset, and invalid selection rejection. This is synthetic integration verification, not an iPhone gameplay test. iOS compilation and device behavior must be tracked separately.

### Single-screen audit

Existing `LayeredFS` changes only apply horizontal RootPane scale compensation. They do not reproduce Leaf's map, pocket/inventory, keyboard, or other scene grafting. Leaf has additional native `LayoutPatch::GraftWindow`, `Ui::Initialize`, `Ui::FrameRegions`, `Ui::Buttons`, `UiPatcher`, and `PocketStacks` components. The existing camera and RootPane changes were retained. The ToolRing port must not be described as the complete single-screen Leaf port.
