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

    // 主背包格访问（索引 0-26 对应 slots[9]~slots[35]）
    const InvSlot& getMainSlot(int index) const;
    int getMainSlotCount() const { return 27; }

    // 完整 slots 访问（用于背包交互）
    const InvSlot& getSlot(int index) const;
    void setLocalSlot(int index, const InvSlot& item);
    int getSlotCount() const { return (int)slots.size(); }

    // 容器状态 ID（服务器每次 SetContent/SetSlot 递增）
    void setStateId(int id) { stateId = id; }
    int getStateId() const { return stateId; }

    // 光标上持有的物品（鼠标拿着的）
    void setCursorItem(const InvSlot& item);
    const InvSlot& getCursorItem() const { return cursorItem; }

private:
    PlayerInventory();

    int getHotbarStart() const;

    std::vector<InvSlot> slots;       // 完整物品栏（容器 0 的全部格）
    std::array<InvSlot, 9> hotbar;    // 快捷栏专用（独立于 slots 布局）
    int selectedSlot = 0;
    int stateId = 0;
    InvSlot cursorItem;
    mutable std::mutex mutex;
};
