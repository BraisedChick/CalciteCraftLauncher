#include "InventoryScreen.h"

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "ResourcepackManager.h"
#include "BlockRegistry.h"
#include "PlayerInventory.h"
#include "ClientEngine.h"

#include <android/log.h>
#include <cstdio>
#include <algorithm>

#define LOG_TAG "InventoryScreen"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

void InventoryScreen::render(int mouseX, int mouseY) {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    const float INV_SLOT = 50.0f;
    const float TEX_SLOT = 18.0f;
    const float TEX_LEFT = 7.0f;
    const float TEX_TOP = 83.0f;
    const float TEX_HOTBAR = 141.0f;
    const float TEX_CONTAINER_W = 176.0f;
    const float TEX_CONTAINER_H = 166.0f;
    float S = INV_SLOT / TEX_SLOT;

    const float TEX_CRAFT_LEFT = 98.0f;
    const float TEX_CRAFT_TOP = 18.0f;
    const float TEX_RESULT_LEFT = 154.0f;
    const float TEX_RESULT_TOP = 28.0f;

    float containerW = TEX_CONTAINER_W * S;
    float containerH = TEX_CONTAINER_H * S;
    float containerX = (w - containerW) * 0.5f;
    float containerY = h * 0.5f - containerH * 0.5f;

    float gridX = containerX + TEX_LEFT * S;
    float gridY = containerY + TEX_TOP * S;
    float hotbarY = containerY + TEX_HOTBAR * S;

    // 背景遮罩
    ImGui::GetForegroundDrawList()->AddRectFilled(
        ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 160));

    // 容器纹理
    GLuint bgTex = ResourcepackManager::getInstance().getGuiTexture("container/inventory");
    if (bgTex != 0) {
        ImGui::GetForegroundDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            glBindSampler(0, 0);
        }, nullptr);
        ImGui::GetForegroundDrawList()->AddImage(
            (ImTextureID)(intptr_t)bgTex,
            ImVec2(containerX, containerY),
            ImVec2(containerX + containerW, containerY + containerH),
            ImVec2(0, 0),
            ImVec2(TEX_CONTAINER_W / 256.0f, TEX_CONTAINER_H / 256.0f));
    }

    // 标题
    const char* title = "背包";
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2(containerX + (containerW - titleSize.x) * 0.5f,
               containerY + 8.0f * S),
        IM_COL32(55, 55, 55, 255), title);

    auto& inv = PlayerInventory::getInstance();
    InvSlot hotbar[9];
    inv.getHotbarSlots(hotbar);

    auto renderItem = [&](float sx, float sy, const InvSlot& slot) {
        if (!slot.present || slot.itemId <= 0) return;
        std::string itemName = BlockRegistry::getInstance().getItemName(slot.itemId);
        if (itemName.empty()) return;
        GLuint tex = ResourcepackManager::getInstance().getItemTexture(itemName);
        if (tex == 0) return;
        ImGui::GetForegroundDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            glBindSampler(0, 0);
        }, nullptr);
        float pad = 5.0f;
        float iconSize = INV_SLOT - pad * 2;
        ImGui::GetForegroundDrawList()->AddImage(
            (ImTextureID)(intptr_t)tex,
            ImVec2(sx + pad, sy + pad),
            ImVec2(sx + pad + iconSize, sy + pad + iconSize));
        if (slot.count > 1) {
            char countStr[8];
            snprintf(countStr, sizeof(countStr), "%d", slot.count);
            ImVec2 textSize = ImGui::CalcTextSize(countStr);
            ImGui::GetForegroundDrawList()->AddText(
                ImVec2(sx + INV_SLOT - textSize.x - 3,
                       sy + INV_SLOT - textSize.y - 2),
                IM_COL32(255, 255, 255, 255), countStr);
        }
    };

    auto getSlotAtMouse = [&]() -> int {
        float mx = io.MousePos.x;
        float my = io.MousePos.y;
        float craftX = containerX + TEX_CRAFT_LEFT * S;
        float craftY = containerY + TEX_CRAFT_TOP * S;
        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 2; col++) {
                float sx = craftX + col * INV_SLOT;
                float sy = craftY + row * INV_SLOT;
                if (mx >= sx && mx < sx + INV_SLOT && my >= sy && my < sy + INV_SLOT)
                    return 1 + row * 2 + col;
            }
        }
        float resultX = containerX + TEX_RESULT_LEFT * S;
        float resultY = containerY + TEX_RESULT_TOP * S;
        if (mx >= resultX && mx < resultX + INV_SLOT && my >= resultY && my < resultY + INV_SLOT)
            return 0;
        for (int row = 0; row < 3; row++) {
            float rowY = gridY + row * INV_SLOT;
            for (int col = 0; col < 9; col++) {
                float sx = gridX + col * INV_SLOT;
                if (mx >= sx && mx < sx + INV_SLOT && my >= rowY && my < rowY + INV_SLOT)
                    return 9 + row * 9 + col;
            }
        }
        for (int i = 0; i < 9; i++) {
            float sx = gridX + i * INV_SLOT;
            if (mx >= sx && mx < sx + INV_SLOT && my >= hotbarY && my < hotbarY + INV_SLOT)
                return 36 + i;
        }
        return -1;
    };

    // 全局拖拽检测
    auto* engine = ClientEngine::getInstance();
    if (engine) {
        if (isDraggingSlot && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            int hoveredSlot = getSlotAtMouse();
            if (hoveredSlot >= 0) {
                bool alreadyAdded = false;
                for (int s : quickcraftSlots) {
                    if (s == hoveredSlot) { alreadyAdded = true; break; }
                }
                const InvSlot& cursorItem = inv.getCursorItem();
                if (!alreadyAdded && (int)quickcraftSlots.size() < cursorItem.count) {
                    quickcraftSlots.push_back(hoveredSlot);
                }
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && isDraggingSlot) {
            if (quickcraftSlots.size() > 1) {
                engine->sendContainerQuickCraft(0, -999, 0);
                for (int slot : quickcraftSlots)
                    engine->sendContainerQuickCraft(1, slot, 0);
                engine->sendContainerQuickCraft(2, -999, 0);

                const InvSlot& cursorItem = inv.getCursorItem();
                int totalItems = cursorItem.count;
                const int MAX_STACK = 64;
                int validSlotCount = 0;
                for (int slot : quickcraftSlots) {
                    const InvSlot& slotItem = inv.getSlot(slot);
                    if (!slotItem.present || slotItem.itemId <= 0 || slotItem.itemId == cursorItem.itemId)
                        validSlotCount++;
                }
                int perSlot = (validSlotCount > 0) ? (totalItems / validSlotCount) : 0;
                int distributed = 0;
                for (int slot : quickcraftSlots) {
                    const InvSlot& slotItem = inv.getSlot(slot);
                    int availableSpace = MAX_STACK;
                    if (slotItem.present && slotItem.itemId > 0) {
                        if (slotItem.itemId == cursorItem.itemId)
                            availableSpace = MAX_STACK - slotItem.count;
                        else
                            availableSpace = 0;
                    }
                    int actualPut = std::min(perSlot, availableSpace);
                    distributed += actualPut;
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
            } else {
                if (quickcraftStartSlot >= 0) {
                    engine->sendContainerClick(quickcraftStartSlot, 0);
                }
            }
            isDraggingSlot = false;
            quickcraftStatus = 0;
            quickcraftSlots.clear();
            quickcraftStartSlot = -1;
        }
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##InventoryClick", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoNav);

    auto handleSlotClick = [&](float sx, float sy, int containerSlot, const char* id) {
        ImGui::SetCursorScreenPos(ImVec2(sx, sy));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##%s", id);
        ImGui::InvisibleButton(btnId, ImVec2(INV_SLOT, INV_SLOT));
        auto* eng = ClientEngine::getInstance();
        if (!eng) { ImGui::PopStyleColor(3); return; }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            const InvSlot& cursor = inv.getCursorItem();
            if (cursor.present && cursor.count > 1) {
                isDraggingSlot = true;
                quickcraftStartSlot = containerSlot;
                quickcraftStatus = 0;
                quickcraftSlots.clear();
            } else {
                eng->sendContainerClick(containerSlot, 0);
            }
        }
        ImGui::PopStyleColor(3);
    };

    // 主背包格（3x9）
    for (int row = 0; row < 3; row++) {
        float rowY = gridY + row * INV_SLOT;
        for (int col = 0; col < 9; col++) {
            float sx = gridX + col * INV_SLOT;
            int displayIndex = row * 9 + col;
            int containerSlot = 9 + displayIndex;
            renderItem(sx, rowY, inv.getMainSlot(displayIndex));
            char id[16];
            snprintf(id, sizeof(id), "main_%d", displayIndex);
            handleSlotClick(sx, rowY, containerSlot, id);
        }
    }

    // 快捷栏（1x9）
    for (int i = 0; i < 9; i++) {
        float sx = gridX + i * INV_SLOT;
        int containerSlot = 36 + i;
        if (i == inv.getSelectedSlot()) {
            ImGui::GetForegroundDrawList()->AddRect(
                ImVec2(sx - 2, hotbarY - 2),
                ImVec2(sx + INV_SLOT + 2, hotbarY + INV_SLOT + 2),
                IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.5f);
        }
        renderItem(sx, hotbarY, hotbar[i]);
        char id[16];
        snprintf(id, sizeof(id), "hot_%d", i);
        handleSlotClick(sx, hotbarY, containerSlot, id);
    }

    // 合成格子（2x2）
    float craftX = containerX + TEX_CRAFT_LEFT * S - 3.0f;
    float craftY = containerY + TEX_CRAFT_TOP * S - 2.0f;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 2; col++) {
            float sx = craftX + col * INV_SLOT;
            float sy = craftY + row * INV_SLOT;
            int craftIdx = row * 2 + col;
            int containerSlot = 1 + craftIdx;
            renderItem(sx, sy, inv.getCraftSlot(craftIdx));
            char id[16];
            snprintf(id, sizeof(id), "craft_%d", craftIdx);
            handleSlotClick(sx, sy, containerSlot, id);
        }
    }

    // 合成结果槽
    float resultX = containerX + TEX_RESULT_LEFT * S;
    float resultY = containerY + TEX_RESULT_TOP * S;
    renderItem(resultX, resultY, inv.getCraftResult());
    handleSlotClick(resultX, resultY, 0, "craft_result");

    // 拖拽预测渲染
    if (isDraggingSlot && !quickcraftSlots.empty()) {
        const InvSlot& cursorItem = inv.getCursorItem();
        int totalItems = cursorItem.count;
        const int MAX_STACK = 64;
        int validSlotCount = 0;
        for (int slot : quickcraftSlots) {
            const InvSlot& slotItem = inv.getSlot(slot);
            if (!slotItem.present || slotItem.itemId <= 0 || slotItem.itemId == cursorItem.itemId)
                validSlotCount++;
        }
        int perSlot = (validSlotCount > 0) ? (totalItems / validSlotCount) : 0;
        int distributedItems = 0;

        for (int i = 0; i < (int)quickcraftSlots.size(); i++) {
            int slot = quickcraftSlots[i];
            float sx, sy;
            if (slot >= 1 && slot <= 4) {
                int craftIdx = slot - 1;
                sx = craftX + (craftIdx % 2) * INV_SLOT;
                sy = craftY + (craftIdx / 2) * INV_SLOT;
            } else if (slot >= 9 && slot < 36) {
                int idx = slot - 9;
                sx = gridX + (idx % 9) * INV_SLOT;
                sy = gridY + (idx / 9) * INV_SLOT;
            } else if (slot >= 36 && slot < 45) {
                sx = gridX + (slot - 36) * INV_SLOT;
                sy = hotbarY;
            } else { continue; }

            const InvSlot& slotItem = inv.getSlot(slot);
            int availableSpace = MAX_STACK;
            if (slotItem.present && slotItem.itemId > 0) {
                availableSpace = (slotItem.itemId == cursorItem.itemId) ? MAX_STACK - slotItem.count : 0;
            }
            int countForThisSlot = std::min(perSlot, availableSpace);

            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(sx, sy), ImVec2(sx + INV_SLOT, sy + INV_SLOT),
                IM_COL32(255, 255, 0, 80));
            ImGui::GetForegroundDrawList()->AddRect(
                ImVec2(sx, sy), ImVec2(sx + INV_SLOT, sy + INV_SLOT),
                IM_COL32(255, 255, 0, 200), 0.0f, 0, 2.0f);

            if (cursorItem.present && cursorItem.itemId > 0 && countForThisSlot > 0) {
                std::string itemName = BlockRegistry::getInstance().getItemName(cursorItem.itemId);
                GLuint tex = ResourcepackManager::getInstance().getItemTexture(itemName);
                if (tex != 0) {
                    float pad = 5.0f;
                    float iconSize = INV_SLOT - pad * 2;
                    ImGui::GetForegroundDrawList()->AddImage(
                        (ImTextureID)(intptr_t)tex,
                        ImVec2(sx + pad, sy + pad),
                        ImVec2(sx + pad + iconSize, sy + pad + iconSize),
                        ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32(255, 255, 255, 160));
                    int finalCount = slotItem.count + countForThisSlot;
                    if (finalCount > 1) {
                        char countStr[8];
                        snprintf(countStr, sizeof(countStr), "%d", finalCount);
                        ImVec2 textSize = ImGui::CalcTextSize(countStr);
                        ImGui::GetForegroundDrawList()->AddText(
                            ImVec2(sx + INV_SLOT - textSize.x - 3,
                                   sy + INV_SLOT - textSize.y - 2),
                            IM_COL32(255, 255, 0, 255), countStr);
                    }
                    distributedItems += countForThisSlot;
                }
            }
        }

        if (cursorItem.present && cursorItem.itemId > 0) {
            int remainingItems = totalItems - distributedItems;
            if (remainingItems > 0) {
                float mx = io.MousePos.x - INV_SLOT * 0.5f;
                float my = io.MousePos.y - INV_SLOT * 0.5f;
                InvSlot tempCursor = cursorItem;
                tempCursor.count = (int8_t)remainingItems;
                renderItem(mx, my, tempCursor);
            }
        }
    } else {
        const InvSlot& cursor = inv.getCursorItem();
        if (cursor.present && cursor.itemId > 0) {
            float mx = io.MousePos.x - INV_SLOT * 0.5f;
            float my = io.MousePos.y - INV_SLOT * 0.5f;
            renderItem(mx, my, cursor);
        }
    }

    ImGui::End();
}
