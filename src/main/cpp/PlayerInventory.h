#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <mutex>

struct InvSlot {
    bool present = false;
    int32_t itemId = 0;
    int8_t count = 0;
};

class PlayerInventory {
public:
    static PlayerInventory& getInstance() {
        static PlayerInventory instance;
        return instance;
    }

    // 容器 ID=0 为玩家物品栏
    void setContent(int containerId, const std::vector<InvSlot>& items);
    void setSlot(int containerId, int slot, const InvSlot& item);

    // 快捷栏 (0-8)
    const InvSlot& getHotbarSlot(int index) const;
    int getSelectedSlot() const { return selectedSlot; }
    void setSelectedSlot(int slot) {
        if (slot >= 0 && slot <= 8) selectedSlot = slot;
    }

    // 快捷栏 9 格（用于渲染）
    void getHotbarSlots(InvSlot out[9]) const;

private:
    PlayerInventory();

    int getHotbarStart() const;

    std::vector<InvSlot> slots;       // 完整物品栏（容器 0 的全部格）
    std::array<InvSlot, 9> hotbar;    // 快捷栏专用（独立于 slots 布局）
    int selectedSlot = 0;
    mutable std::mutex mutex;
};
