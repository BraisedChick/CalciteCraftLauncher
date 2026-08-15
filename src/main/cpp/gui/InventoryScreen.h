#pragma once

#include "Screen.h"
#include <GLES3/gl3.h>
#include <vector>
#include "DragHelper.h"   // 新增

// 背包界面（对应 MC 的 InventoryScreen）
class InventoryScreen : public Screen {
public:
    const char* getName() const override { return "InventoryScreen"; }
    bool isPauseScreen() const override { return false; }
    void render(int mouseX, int mouseY) override;

private:
    void renderPlayerInventory(float w, float h);
    void renderCraftingTable(float w, float h);
    void renderFurnace(float w, float h);
    void renderChest(float w, float h, int containerType);

    // 拖拽辅助（取代原有的三个拖拽成员变量）
    DragHelper m_dragHelper;
};