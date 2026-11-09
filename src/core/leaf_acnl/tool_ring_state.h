#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace LeafACNL {

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
