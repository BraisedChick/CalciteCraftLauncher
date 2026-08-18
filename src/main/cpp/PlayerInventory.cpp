#include "PlayerInventory.h"
#include <android/log.h>

#define LOG_TAG "PlayerInventory"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

PlayerInventory::PlayerInventory() {
    // 预初始化 46 个空槽（对应完整玩家物品栏布局，包含合成格）
    // 布局：Slot 0=合成结果, Slots 1-4=合成格, Slots 5-8=装备, Slots 9-35=主背包, Slots 36-44=快捷栏, Slot 45=副手
    slots.resize(46);
    for (auto& hb : hotbar) hb = InvSlot{};
}

void PlayerInventory::setContent(int containerId, const std::vector<InvSlot>& items) {
    if (containerId != 0) {
        // 外部容器（工作台、熔炉等）
        std::lock_guard<std::mutex> lock(mutex);
        containerSlots = items;
        LOGI("Container %d content: %zu slots", containerId, items.size());
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    slots = items;

    // 从完整物品栏中提取快捷栏（9 格）到独立 hotbar 数组
    if (slots.size() >= 45) {
        // Vanilla 1.18.2 完整布局（46 slots）：快捷栏在 36-44
        for (int i = 0; i < 9 && (36 + i) < (int)slots.size(); i++) {
            hotbar[i] = slots[36 + i];
        }
    } else if (slots.size() >= 41) {
        // 41-slot 布局（无合成格）：快捷栏在 0-8
        for (int i = 0; i < 9; i++) {
            hotbar[i] = slots[i];
        }
    } else if (slots.size() >= 36) {
        // 36-slot 布局（只有主背包 + 快捷栏）：快捷栏在最后 9 格
        int start = (int)slots.size() - 9;
        for (int i = 0; i < 9; i++) {
            hotbar[i] = slots[start + i];
        }
        LOGI("Inventory updated: %zu slots (minimal layout, hotbar from slots[%d-%d])",
             slots.size(), start, start + 8);
    } else {
        // 未知布局：尝试最后 9 格
        for (int i = 0; i < 9 && i < (int)slots.size(); i++) {
            hotbar[i] = slots[slots.size() - 1 - i];
        }
    }
}

void PlayerInventory::setSlot(int containerId, int slot, const InvSlot& item) {
    std::lock_guard<std::mutex> lock(mutex);
    if (containerId > 0) {
        // 外部容器槽位更新
        if (slot >= 0 && slot < (int)containerSlots.size()) {
            containerSlots[slot] = item;
        }
        return;
    }
    if (containerId == 0) {
        // 直接更新物品栏
        if (slot >= 0 && slot < (int)slots.size()) {
            slots[slot] = item;
        }
        // 如果更新的格属于快捷栏区域，同步到 hotbar
        int hotbarStart = getHotbarStart();
        if (slot >= hotbarStart && slot < hotbarStart + 9) {
            int hbIdx = slot - hotbarStart;
            if (hbIdx >= 0 && hbIdx < 9) {
                hotbar[hbIdx] = item;
            }
        }
    } else if (containerId == -2) {
        // containerId = -2：玩家在游戏中（未打开 GUI）时的物品更新
        // slot 0-8 = 快捷栏，直接更新 hotbar
        if (slot >= 0 && slot < 9) {
            hotbar[slot] = item;
        }
        // 也更新 slots（如果大小足够）
        if (slot >= 0 && slot < (int)slots.size()) {
            slots[slot] = item;
        }
    }

    // 更新装备槽位时的额外处理
    if (containerId == 0) {
        // 检查是否是装备槽位（5-8）
        if (slot >= 5 && slot <= 8) {
            int equipmentSlot = slot - 5;
            if (equipmentSlot >= 0 && equipmentSlot < 4) {
                // 更新装备槽位
                if (slot >= 0 && slot < (int)slots.size()) {
                    slots[slot] = item;
                }
            }
        }
    }
    // containerId = -1 表示鼠标上的物品（游标），暂不处理
}

const InvSlot& PlayerInventory::getHotbarSlot(int index) const {
    static InvSlot empty;
    if (index < 0 || index > 8) return empty;
    std::lock_guard<std::mutex> lock(mutex);
    return hotbar[index];
}

void PlayerInventory::getHotbarSlots(InvSlot out[9]) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (int i = 0; i < 9; i++) {
        out[i] = hotbar[i];
    }
}

const InvSlot& PlayerInventory::getMainSlot(int index) const {
    static InvSlot empty;
    if (index < 0 || index >= 27) return empty;
    std::lock_guard<std::mutex> lock(mutex);
    int slotIdx = 9 + index;
    if (slotIdx < (int)slots.size()) {
        return slots[slotIdx];
    }
    return empty;
}

int PlayerInventory::getHotbarStart() const {
    if (slots.size() >= 45) return 36;
    if (slots.size() >= 41) return 0;
    if (slots.size() >= 36) return (int)slots.size() - 9;
    return 0;
}

const InvSlot& PlayerInventory::getSlot(int index) const {
    static InvSlot empty;
    std::lock_guard<std::mutex> lock(mutex);
    if (index < 0 || index >= (int)slots.size()) return empty;
    return slots[index];
}

// 获取装备槽位
const InvSlot& PlayerInventory::getArmorSlot(int equipmentSlot) const {
    static InvSlot empty;
    // equipmentSlot: 0=头盔, 1=胸甲, 2=护腿, 3=鞋子
    if (equipmentSlot < 0 || equipmentSlot > 3) return empty;
    int slotIndex = 5 + equipmentSlot;
    if (slotIndex < (int)slots.size()) {
        std::lock_guard<std::mutex> lock(mutex);
        return slots[slotIndex];
    }
    return empty;
}

void PlayerInventory::setLocalSlot(int index, const InvSlot& item) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index >= 0 && index < (int)slots.size()) {
        slots[index] = item;
        // 同步快捷栏
        int hotbarStart = getHotbarStart();
        if (index >= hotbarStart && index < hotbarStart + 9) {
            hotbar[index - hotbarStart] = item;
        }
    }
}

void PlayerInventory::setCursorItem(const InvSlot& item) {
    std::lock_guard<std::mutex> lock(mutex);
    cursorItem = item;
}

const InvSlot& PlayerInventory::getContainerSlot(int index) const {
    static InvSlot empty;
    if (index < 0 || index >= (int)containerSlots.size()) return empty;
    std::lock_guard<std::mutex> lock(mutex);
    return containerSlots[index];
}

void PlayerInventory::setContainerLocalSlot(int index, const InvSlot& item) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index >= 0 && index < (int)containerSlots.size()) {
        containerSlots[index] = item;
    }
}

// 合成格子访问方法
const InvSlot& PlayerInventory::getCraftSlot(int index) const {
    static InvSlot empty;
    if (index < 0 || index >= 4) return empty;
    std::lock_guard<std::mutex> lock(mutex);
    int slotIdx = 1 + index;  // slots 1-4 对应合成格 0-3
    if (slotIdx < (int)slots.size()) {
        return slots[slotIdx];
    }
    return empty;
}

const InvSlot& PlayerInventory::getCraftResult() const {
    static InvSlot empty;
    std::lock_guard<std::mutex> lock(mutex);
    if (0 < (int)slots.size()) {
        return slots[0];
    }
    return empty;
}

void PlayerInventory::setCraftSlot(int index, const InvSlot& item) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index >= 0 && index < 4) {
        int slotIdx = 1 + index;  // slots 1-4 对应合成格 0-3
        if (slotIdx < (int)slots.size()) {
            slots[slotIdx] = item;
        }
    }
}

void PlayerInventory::setCraftResult(const InvSlot& item) {
    std::lock_guard<std::mutex> lock(mutex);
    if (0 < (int)slots.size()) {
        slots[0] = item;
    }
}
