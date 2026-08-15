#include "DragHelper.h"
#include <algorithm>

DragHelper::DragHelper() : m_isDragging(false), m_quickcraftStatus(0), m_quickcraftStartSlot(-1) {}

void DragHelper::reset() {
    m_isDragging = false;
    m_quickcraftStatus = 0;
    m_quickcraftStartSlot = -1;
    m_quickcraftSlots.clear();
}

void DragHelper::processInput(ImGuiIO& io, PlayerInventory& inv, GameEngine* engine,
                              const std::function<int()>& getSlotAtMouse,
                              int containerId) {
    if (!engine) return;

    // 拖拽进行中：添加新悬停槽位
    if (m_isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        int hoveredSlot = getSlotAtMouse();
        if (hoveredSlot >= 0) {
            // 避免重复添加
            bool alreadyAdded = false;
            for (int s : m_quickcraftSlots) {
                if (s == hoveredSlot) { alreadyAdded = true; break; }
            }
            const InvSlot& cursorItem = inv.getCursorItem();
            if (!alreadyAdded && (int)m_quickcraftSlots.size() < cursorItem.count) {
                m_quickcraftSlots.push_back(hoveredSlot);
            }
        }
    }

    // 释放左键：结束拖拽
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && m_isDragging) {
        if (m_quickcraftSlots.size() > 1) {
            // 快速合成/分配
            engine->sendContainerQuickCraft(0, -999, 0);
            for (int slot : m_quickcraftSlots)
                engine->sendContainerQuickCraft(1, slot, 0);
            engine->sendContainerQuickCraft(2, -999, 0);

            // 本地预测分配
            const InvSlot& cursorItem = inv.getCursorItem();
            int totalItems = cursorItem.count;
            const int MAX_STACK = 64;
            int validSlotCount = 0;
            for (int slot : m_quickcraftSlots) {
                const InvSlot& slotItem = (containerId > 0) ? inv.getContainerSlot(slot) : inv.getSlot(slot);
                if (!slotItem.present || slotItem.itemId <= 0 || slotItem.itemId == cursorItem.itemId)
                    validSlotCount++;
            }
            int perSlot = (validSlotCount > 0) ? (totalItems / validSlotCount) : 0;
            int distributed = 0;
            for (int slot : m_quickcraftSlots) {
                const InvSlot& slotItem = (containerId > 0) ? inv.getContainerSlot(slot) : inv.getSlot(slot);
                int availableSpace = MAX_STACK;
                if (slotItem.present && slotItem.itemId > 0) {
                    availableSpace = (slotItem.itemId == cursorItem.itemId) ? MAX_STACK - slotItem.count : 0;
                }
                int put = std::min(perSlot, availableSpace);
                distributed += put;
                // 实际物品分配由服务端响应完成，本地只更新光标
            }
            int remaining = totalItems - distributed;
            InvSlot updatedCursor = cursorItem;
            if (remaining > 0) {
                updatedCursor.count = (int8_t)remaining;
            } else {
                updatedCursor.present = false;
                updatedCursor.itemId = 0;
                updatedCursor.count = 0;
            }
            inv.setCursorItem(updatedCursor);
        } else if (m_quickcraftStartSlot >= 0) {
            // 普通点击（无拖拽）
            engine->sendContainerClick(m_quickcraftStartSlot, 0);
        }
        // 重置状态
        reset();
    }
}

void DragHelper::renderPrediction(ImGuiIO& io, PlayerInventory& inv,
                                  const std::function<void(float, float, const InvSlot&)>& renderItem,
                                  const std::function<void(int, float&, float&)>& getSlotScreenPos,
                                  int containerId,
                                  float invSlotSize) {
    if (!m_isDragging || m_quickcraftSlots.empty()) {
        // 非拖拽：只绘制光标物品
        const InvSlot& cursor = inv.getCursorItem();
        if (cursor.present && cursor.itemId > 0) {
            float mx = io.MousePos.x - invSlotSize * 0.5f;
            float my = io.MousePos.y - invSlotSize * 0.5f;
            renderItem(mx, my, cursor);
        }
        return;
    }

    // 拖拽预测
    const InvSlot& cursorItem = inv.getCursorItem();
    int totalItems = cursorItem.count;
    const int MAX_STACK = 64;
    int validSlotCount = 0;
    for (int slot : m_quickcraftSlots) {
        const InvSlot& slotItem = (containerId > 0) ? inv.getContainerSlot(slot) : inv.getSlot(slot);
        if (!slotItem.present || slotItem.itemId <= 0 || slotItem.itemId == cursorItem.itemId)
            validSlotCount++;
    }
    int perSlot = (validSlotCount > 0) ? (totalItems / validSlotCount) : 0;
    int distributedItems = 0;

    for (int slot : m_quickcraftSlots) {
        float sx, sy;
        getSlotScreenPos(slot, sx, sy);
        const InvSlot& slotItem = (containerId > 0) ? inv.getContainerSlot(slot) : inv.getSlot(slot);
        int availableSpace = MAX_STACK;
        if (slotItem.present && slotItem.itemId > 0) {
            availableSpace = (slotItem.itemId == cursorItem.itemId) ? MAX_STACK - slotItem.count : 0;
        }
        int countForThisSlot = std::min(perSlot, availableSpace);

        // 高亮背景
        ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(sx, sy), ImVec2(sx + invSlotSize, sy + invSlotSize),
                IM_COL32(255, 255, 0, 80));
        ImGui::GetForegroundDrawList()->AddRect(
                ImVec2(sx, sy), ImVec2(sx + invSlotSize, sy + invSlotSize),
                IM_COL32(255, 255, 0, 200), 0.0f, 0, 2.0f);

        if (cursorItem.present && cursorItem.itemId > 0 && countForThisSlot > 0) {
            // 绘制预测物品（半透明）
            InvSlot tempSlot = slotItem;
            tempSlot.present = true;
            tempSlot.itemId = cursorItem.itemId;
            tempSlot.count += countForThisSlot;
            renderItem(sx, sy, tempSlot);
            distributedItems += countForThisSlot;
        }
    }

    // 剩余光标物品
    int remainingItems = totalItems - distributedItems;
    if (remainingItems > 0) {
        float mx = io.MousePos.x - invSlotSize * 0.5f;
        float my = io.MousePos.y - invSlotSize * 0.5f;
        InvSlot tempCursor = cursorItem;
        tempCursor.count = (int8_t)remainingItems;
        renderItem(mx, my, tempCursor);
    }
}

void DragHelper::startDrag(int slot) {
    reset();
    m_isDragging = true;
    m_quickcraftStartSlot = slot;
    m_quickcraftStatus = 0;
}