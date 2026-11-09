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
