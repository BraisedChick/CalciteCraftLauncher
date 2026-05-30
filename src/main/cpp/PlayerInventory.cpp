#include "PlayerInventory.h"
#include <android/log.h>

#define LOG_TAG "PlayerInventory"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

PlayerInventory::PlayerInventory() {
    // 预初始化 41 个空槽（对应无合成格的玩家物品栏布局）
    slots.resize(41);
    for (auto& hb : hotbar) hb = InvSlot{};
}

void PlayerInventory::setContent(int containerId, const std::vector<InvSlot>& items) {
    if (containerId != 0) return;
    std::lock_guard<std::mutex> lock(mutex);
    slots = items;

    // 从完整物品栏中提取快捷栏（9 格）到独立 hotbar 数组
    if (slots.size() >= 45) {
        // Vanilla 1.18.2 完整布局（46 slots）：快捷栏在 36-44
        for (int i = 0; i < 9 && (36 + i) < (int)slots.size(); i++) {
            hotbar[i] = slots[36 + i];
        }
        LOGI("Inventory updated: %zu slots (vanilla layout, hotbar from slots[36-44])", slots.size());
    } else if (slots.size() >= 41) {
        // 41-slot 布局（无合成格）：快捷栏在 0-8
        for (int i = 0; i < 9; i++) {
            hotbar[i] = slots[i];
        }
        LOGI("Inventory updated: %zu slots (compact layout, hotbar from slots[0-8])", slots.size());
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
        LOGI("Inventory updated: %zu slots (unknown layout)", slots.size());
    }
}

void PlayerInventory::setSlot(int containerId, int slot, const InvSlot& item) {
    std::lock_guard<std::mutex> lock(mutex);
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

int PlayerInventory::getHotbarStart() const {
    // 用于 setSlot 中同步 hotbar 的偏移量计算
    if (slots.size() >= 45) return 36;
    if (slots.size() >= 41) return 0;
    if (slots.size() >= 36) return (int)slots.size() - 9;
    return 0;
}
