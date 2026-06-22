#pragma once

#include "Screen.h"
#include <GLES3/gl3.h>
#include <vector>

// 背包界面（对应 MC 的 InventoryScreen）
class InventoryScreen : public Screen {
public:
    const char* getName() const override { return "InventoryScreen"; }
    bool isPauseScreen() const override { return false; }
    void render(int mouseX, int mouseY) override;

private:
    void renderPlayerInventory(float w, float h);
    void renderCraftingTable(float w, float h);

    // 背包拖拽（Quick Craft）状态
    int quickcraftStatus = 0;
    std::vector<int> quickcraftSlots;
    int quickcraftStartSlot = -1;
    bool isDraggingSlot = false;
};
