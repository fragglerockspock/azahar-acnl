#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace LeafACNL {

struct EquipmentTransfer {
    std::uint32_t source_first;
    std::uint32_t source_second;
    std::uint32_t destination_first;
    std::uint32_t destination_second;
};

inline std::optional<EquipmentTransfer> ComputeEquipmentTransfer(
    std::uint32_t source_base, std::uint32_t destination_base, int slot) {
    if (slot < 0 || slot > 15 || source_base > UINT32_MAX - 0x28 ||
        destination_base > UINT32_MAX - 0x3e) {
        return std::nullopt;
    }
    const auto destination = destination_base + static_cast<std::uint32_t>(slot) * 4;
    return EquipmentTransfer{source_base + 0x26, source_base + 0x28,
                             destination, destination + 2};
}

class ToolRingState {
public:
    void Reset() {
        std::lock_guard<std::mutex> lock(mutex);
        located = false;
        pending = false;
        selected_slot = -1;
        inventory.fill(0x7FFE);
    }

    void SetLocated(bool value) {
        std::lock_guard<std::mutex> lock(mutex);
        located = value;
        if (!value) {
            pending = false;
            selected_slot = -1;
        }
    }

    void SetInventory(const std::array<std::uint16_t, 16>& values) {
        std::lock_guard<std::mutex> lock(mutex);
        inventory = values;
    }

    std::array<std::uint16_t, 16> Inventory() const {
        std::lock_guard<std::mutex> lock(mutex);
        return inventory;
    }

    bool Select(int slot) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!located || slot < -1 || slot > 15) {
            return false;
        }
        selected_slot = slot;
        pending = true;
        return true;
    }

    std::optional<int> TakePendingSelection() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!located || !pending) {
            return std::nullopt;
        }
        pending = false;
        return selected_slot;
    }

private:
    mutable std::mutex mutex;
    bool located = false;
    bool pending = false;
    int selected_slot = -1;
    std::array<std::uint16_t, 16> inventory{};
};

}
