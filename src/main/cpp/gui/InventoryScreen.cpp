#include "InventoryScreen.h"

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "ResourcepackManager.h"
#include "BlockRegistry.h"
#include "PlayerInventory.h"
#include "ClientEngine/ClientEngine.h"
#include "ClientEngine/GameEngine.h"
#include "GameUI.h"
#include "GuiUtils.h"

#include <android/log.h>
#include <cstdio>
#include <algorithm>

#define LOG_TAG "InventoryScreen"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

void InventoryScreen::render(int mouseX, int mouseY) {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    int containerType = GameUI::getInstance().getOpenContainerType();
    if (containerType == 2 || containerType == 5) {
        // 直接传递 containerType，让 renderChest 自己判断是单箱还是双箱
        renderChest(w, h, containerType);
    } else if (containerType == 11) {
        renderCraftingTable(w, h);
    } else if (containerType == 13) {
        // 13 = furnace（1.18.2 与 1.19.4 相同）
        renderFurnace(w, h);
    } else {
        renderPlayerInventory(w, h);
    }
}

void InventoryScreen::renderPlayerInventory(float w, float h) {
    ImGuiIO& io = ImGui::GetIO();

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

    // 容器纹理（双后端：GL=ResourcepackManager，Vulkan=VulkanRenderer）
    ImTextureID bgTex = getGuiTextureId("container/inventory");
    if (bgTex != 0) {
        addNearestSamplerCallback(ImGui::GetForegroundDrawList());
        ImGui::GetForegroundDrawList()->AddImage(
            bgTex,
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

    auto* gameForInv = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!gameForInv) return;
    auto& inv = *gameForInv->getInventory();
    InvSlot hotbar[9];
    inv.getHotbarSlots(hotbar);

    auto renderItem = [&](float sx, float sy, const InvSlot& slot) {
        if (!slot.present || slot.itemId <= 0) return;
        std::string itemName = ClientEngine::getInstance()->getBlockRegistry()->getItemName(slot.itemId);
        if (itemName.empty()) return;
        ImTextureID tex = getItemIconTexture(itemName);
        if (tex == 0) return;
        addNearestSamplerCallback(ImGui::GetForegroundDrawList());
        float pad = 5.0f;
        float iconSize = INV_SLOT - pad * 2;
        ImGui::GetForegroundDrawList()->AddImage(
            tex,
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

        // 耐久条
        if (slot.maxDamage > 0) {
            float pad = 5.0f;
            float durBarY = sy + INV_SLOT - 7.0f;
            float durBarH = 3.0f;
            float durBarX = sx + pad;
            float durBarW = INV_SLOT - pad * 2;
            float ratio = 1.0f - (float)slot.damage / (float)slot.maxDamage;
            ratio = std::max(0.0f, std::min(1.0f, ratio));

            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW, durBarY + durBarH),
                IM_COL32(0, 0, 0, 150), 1.0f);

            ImU32 durColor;
            if (ratio > 0.5f) durColor = IM_COL32(64, 255, 64, 255);
            else if (ratio > 0.25f) durColor = IM_COL32(255, 255, 64, 255);
            else durColor = IM_COL32(255, 64, 64, 255);

            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW * ratio, durBarY + durBarH),
                durColor, 1.0f);
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
    auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
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
        auto* eng = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
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
                std::string itemName = ClientEngine::getInstance()->getBlockRegistry()->getItemName(cursorItem.itemId);
                ImTextureID tex = getItemIconTexture(itemName);
                if (tex != 0) {
                    float pad = 5.0f;
                    float iconSize = INV_SLOT - pad * 2;
                    ImGui::GetForegroundDrawList()->AddImage(
                        tex,
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

void InventoryScreen::renderCraftingTable(float w, float h) {
    ImGuiIO& io = ImGui::GetIO();

    const float INV_SLOT = 50.0f;
    const float TEX_SLOT = 18.0f;
    float S = INV_SLOT / TEX_SLOT;

    // 工作台容器布局：
    // Slot 0 = 合成结果
    // Slots 1-9 = 3x3 合成格
    // Slots 10-36 = 主背包 (3x9)
    // Slots 37-45 = 快捷栏 (1x9)
    const float TEX_LEFT = 7.0f;       // 主背包左偏移
    const float TEX_TOP = 84.0f;       // 主背包上偏移
    const float TEX_HOTBAR = 142.0f;   // 快捷栏上偏移
    const float TEX_CONTAINER_W = 176.0f;
    const float TEX_CONTAINER_H = 166.0f;

    // 3x3 合成格和结果槽在纹理中的位置
    const float TEX_CRAFT_LEFT = 29.0f;
    const float TEX_CRAFT_TOP = 17.0f;
    const float TEX_RESULT_LEFT = 124.0f;
    const float TEX_RESULT_TOP = 35.0f;

    const float ITEM_Y_OFFSET = -3.0f; // 物品图标上移4像素

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

    // 工作台纹理（双后端）
    ImTextureID bgTex = getGuiTextureId("container/crafting_table");
    if (bgTex != 0) {
        addNearestSamplerCallback(ImGui::GetForegroundDrawList());
        ImGui::GetForegroundDrawList()->AddImage(
            bgTex,
            ImVec2(containerX, containerY),
            ImVec2(containerX + containerW, containerY + containerH),
            ImVec2(0, 0),
            ImVec2(TEX_CONTAINER_W / 256.0f, TEX_CONTAINER_H / 256.0f));
    }

    // 标题
    const char* title = "工作台";
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2(containerX + (containerW - titleSize.x) * 0.5f,
               containerY + 6.0f * S),
        IM_COL32(55, 55, 55, 255), title);

    auto* gameForInv2 = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!gameForInv2) return;
    auto& inv = *gameForInv2->getInventory();
    const auto& containerSlots = inv.getContainerSlots();
    InvSlot hotbar[9];
    inv.getHotbarSlots(hotbar);

    auto renderItem = [&](float sx, float sy, const InvSlot& slot) {
        if (!slot.present || slot.itemId <= 0) return;
        std::string itemName = ClientEngine::getInstance()->getBlockRegistry()->getItemName(slot.itemId);
        if (itemName.empty()) return;
        ImTextureID tex = getItemIconTexture(itemName);
        if (tex == 0) return;
        addNearestSamplerCallback(ImGui::GetForegroundDrawList());
        float pad = 5.0f;
        float iconSize = INV_SLOT - pad * 2;
        ImGui::GetForegroundDrawList()->AddImage(
            tex,
            ImVec2(sx + pad, sy + pad + ITEM_Y_OFFSET),
            ImVec2(sx + pad + iconSize, sy + pad + iconSize + ITEM_Y_OFFSET));
        if (slot.count > 1) {
            char countStr[8];
            snprintf(countStr, sizeof(countStr), "%d", slot.count);
            ImVec2 textSize = ImGui::CalcTextSize(countStr);
            ImGui::GetForegroundDrawList()->AddText(
                ImVec2(sx + INV_SLOT - textSize.x - 3,
                       sy + INV_SLOT - textSize.y - 2 + ITEM_Y_OFFSET),
                IM_COL32(255, 255, 255, 255), countStr);
        }

        // 耐久条
        if (slot.maxDamage > 0) {
            float pad = 5.0f;
            float durBarY = sy + INV_SLOT - 7.0f + ITEM_Y_OFFSET;
            float durBarH = 3.0f;
            float durBarX = sx + pad;
            float durBarW = INV_SLOT - pad * 2;
            float ratio = 1.0f - (float)slot.damage / (float)slot.maxDamage;
            ratio = std::max(0.0f, std::min(1.0f, ratio));

            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW, durBarY + durBarH),
                IM_COL32(0, 0, 0, 150), 1.0f);

            ImU32 durColor;
            if (ratio > 0.5f) durColor = IM_COL32(64, 255, 64, 255);
            else if (ratio > 0.25f) durColor = IM_COL32(255, 255, 64, 255);
            else durColor = IM_COL32(255, 64, 64, 255);

            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW * ratio, durBarY + durBarH),
                durColor, 1.0f);
        }
    };

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##CraftingTableClick", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoNav);

    int containerId = GameUI::getInstance().getOpenContainerId();

    // 全局拖拽检测（与背包一致）
    auto getCraftSlotAtMouse = [&]() -> int {
        float mx = io.MousePos.x, my = io.MousePos.y;
        float craftX = containerX + TEX_CRAFT_LEFT * S;
        float craftY = containerY + TEX_CRAFT_TOP * S;
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++) {
                float sx = craftX + col * INV_SLOT, sy = craftY + row * INV_SLOT;
                if (mx >= sx && mx < sx + INV_SLOT && my >= sy && my < sy + INV_SLOT)
                    return 1 + row * 3 + col;
            }
        float resultX = containerX + TEX_RESULT_LEFT * S, resultY = containerY + TEX_RESULT_TOP * S;
        if (mx >= resultX && mx < resultX + INV_SLOT && my >= resultY && my < resultY + INV_SLOT) return 0;
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 9; col++) {
                float sx = gridX + col * INV_SLOT, rowY = gridY + row * INV_SLOT;
                if (mx >= sx && mx < sx + INV_SLOT && my >= rowY && my < rowY + INV_SLOT)
                    return 10 + row * 9 + col;
            }
        for (int i = 0; i < 9; i++) {
            float sx = gridX + i * INV_SLOT;
            if (mx >= sx && mx < sx + INV_SLOT && my >= hotbarY && my < hotbarY + INV_SLOT)
                return 37 + i;
        }
        return -1;
    };

    auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (engine) {
        if (isDraggingSlot && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            int hoveredSlot = getCraftSlotAtMouse();
            if (hoveredSlot >= 0) {
                bool alreadyAdded = false;
                for (int s : quickcraftSlots) if (s == hoveredSlot) { alreadyAdded = true; break; }
                const InvSlot& cursorItem = inv.getCursorItem();
                if (!alreadyAdded && (int)quickcraftSlots.size() < cursorItem.count)
                    quickcraftSlots.push_back(hoveredSlot);
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && isDraggingSlot) {
            if (quickcraftSlots.size() > 1) {
                engine->sendContainerQuickCraft(0, -999, 0);
                for (int slot : quickcraftSlots) engine->sendContainerQuickCraft(1, slot, 0);
                engine->sendContainerQuickCraft(2, -999, 0);
                const InvSlot& cursorItem = inv.getCursorItem();
                int totalItems = cursorItem.count;
                int validSlotCount = 0;
                for (int slot : quickcraftSlots) {
                    const InvSlot& slotItem = (containerId > 0) ? inv.getContainerSlot(slot) : inv.getSlot(slot);
                    if (!slotItem.present || slotItem.itemId <= 0 || slotItem.itemId == cursorItem.itemId) validSlotCount++;
                }
                int perSlot = (validSlotCount > 0) ? (totalItems / validSlotCount) : 0;
                int distributed = 0;
                for (int slot : quickcraftSlots) {
                    const InvSlot& slotItem = (containerId > 0) ? inv.getContainerSlot(slot) : inv.getSlot(slot);
                    int availableSpace = 64;
                    if (slotItem.present && slotItem.itemId > 0)
                        availableSpace = (slotItem.itemId == cursorItem.itemId) ? 64 - slotItem.count : 0;
                    distributed += std::min(perSlot, availableSpace);
                }
                int remaining = totalItems - distributed;
                InvSlot updatedCursor = cursorItem;
                if (remaining > 0) { updatedCursor.count = (int8_t)remaining; }
                else { updatedCursor.present = false; updatedCursor.itemId = 0; updatedCursor.count = 0; }
                inv.setCursorItem(updatedCursor);
            } else if (quickcraftStartSlot >= 0) {
                engine->sendContainerClick(quickcraftStartSlot, 0);
            }
            isDraggingSlot = false; quickcraftStatus = 0; quickcraftSlots.clear(); quickcraftStartSlot = -1;
        }
    }

    auto handleSlotClick = [&](float sx, float sy, int containerSlot, const char* id) {
        ImGui::SetCursorScreenPos(ImVec2(sx, sy));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##%s", id);
        ImGui::InvisibleButton(btnId, ImVec2(INV_SLOT, INV_SLOT));
        auto* eng = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
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
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            eng->sendContainerClick(containerSlot, 1);
        }
        ImGui::PopStyleColor(3);
    };

    // 获取容器槽位的辅助函数
    auto getContainerSlot = [&](int index) -> InvSlot {
        if (index >= 0 && index < (int)containerSlots.size()) {
            return containerSlots[index];
        }
        return InvSlot{};
    };

    // 3x3 合成格 (slots 1-9)
    float craftX = containerX + TEX_CRAFT_LEFT * S;
    float craftY = containerY + TEX_CRAFT_TOP * S;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            float sx = craftX + col * INV_SLOT;
            float sy = craftY + row * INV_SLOT;
            int slotIdx = 1 + row * 3 + col;
            renderItem(sx, sy, getContainerSlot(slotIdx));
            char id[16];
            snprintf(id, sizeof(id), "craft_%d", slotIdx);
            handleSlotClick(sx, sy, slotIdx, id);
        }
    }

    // 合成结果槽 (slot 0)
    float resultX = containerX + TEX_RESULT_LEFT * S;
    float resultY = containerY + TEX_RESULT_TOP * S;
    renderItem(resultX, resultY, getContainerSlot(0));
    handleSlotClick(resultX, resultY, 0, "craft_result");

    // 主背包格 3x9 (slots 10-36)
    for (int row = 0; row < 3; row++) {
        float rowY = gridY + row * INV_SLOT;
        for (int col = 0; col < 9; col++) {
            float sx = gridX + col * INV_SLOT;
            int slotIdx = 10 + row * 9 + col;
            renderItem(sx, rowY, getContainerSlot(slotIdx));
            char id[16];
            snprintf(id, sizeof(id), "main_%d", slotIdx);
            handleSlotClick(sx, rowY, slotIdx, id);
        }
    }

    // 快捷栏 1x9 (slots 37-45)
    for (int i = 0; i < 9; i++) {
        float sx = gridX + i * INV_SLOT;
        int slotIdx = 37 + i;
        if (i == inv.getSelectedSlot()) {
            ImGui::GetForegroundDrawList()->AddRect(
                ImVec2(sx - 2, hotbarY - 2),
                ImVec2(sx + INV_SLOT + 2, hotbarY + INV_SLOT + 2),
                IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.5f);
        }
        renderItem(sx, hotbarY, getContainerSlot(slotIdx));
        char id[16];
        snprintf(id, sizeof(id), "hot_%d", i);
        handleSlotClick(sx, hotbarY, slotIdx, id);
    }

    // 光标上的物品
    const InvSlot& cursor = inv.getCursorItem();
    if (cursor.present && cursor.itemId > 0) {
        float mx = io.MousePos.x - INV_SLOT * 0.5f;
        float my = io.MousePos.y - INV_SLOT * 0.5f;
        renderItem(mx, my, cursor);
    }

    ImGui::End();
}

// ============================================================
// 熔炉界面（containerType == 13）
// 纹理：textures/gui/container/furnace.png（176x166 区域在 256x256 图集中）
// 容器槽位：0=输入、1=燃料、2=输出；玩家背包槽位 3-38（主背包 27 + 快捷栏 9）
// 注：火焰/箭头进度条依赖 ContainerSetData 包（burnTime/cookTime），
//     当前项目未实现该包解析，故暂不绘制进度指示
// ============================================================
void InventoryScreen::renderFurnace(float w, float h) {
    ImGuiIO& io = ImGui::GetIO();

    const float INV_SLOT = 50.0f;
    const float TEX_SLOT = 18.0f;
    const float TEX_LEFT = 7.0f;
    const float TEX_TOP = 83.0f;
    const float TEX_HOTBAR = 142.0f;
    const float TEX_CONTAINER_W = 176.0f;
    const float TEX_CONTAINER_H = 166.0f;
    float S = INV_SLOT / TEX_SLOT;

    // 熔炉槽位像素坐标（基于 176x166 纹理区域）
    const float TEX_INPUT_LEFT = 55.0f;
    const float TEX_INPUT_TOP  = 16.0f;
    const float TEX_FUEL_LEFT  = 55.0f;
    const float TEX_FUEL_TOP   = 52.0f;
    const float TEX_OUT_LEFT   = 115.0f;
    const float TEX_OUT_TOP    = 34.0f;

    const float ITEM_Y_OFFSET = -3.0f;

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

    // 熔炉纹理（双后端）
    ImTextureID bgTex = getGuiTextureId("container/furnace");
    if (bgTex != 0) {
        addNearestSamplerCallback(ImGui::GetForegroundDrawList());
        ImGui::GetForegroundDrawList()->AddImage(
            bgTex,
            ImVec2(containerX, containerY),
            ImVec2(containerX + containerW, containerY + containerH),
            ImVec2(0, 0),
            ImVec2(TEX_CONTAINER_W / 256.0f, TEX_CONTAINER_H / 256.0f));
    }

    // 标题
    const char* title = "熔炉";
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2(containerX + (containerW - titleSize.x) * 0.5f,
               containerY + 6.0f * S),
        IM_COL32(55, 55, 55, 255), title);

    auto* gameForInv = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!gameForInv) return;
    auto& inv = *gameForInv->getInventory();
    const auto& containerSlots = inv.getContainerSlots();
    InvSlot hotbar[9];
    inv.getHotbarSlots(hotbar);

    auto renderItem = [&](float sx, float sy, const InvSlot& slot) {
        if (!slot.present || slot.itemId <= 0) return;
        std::string itemName = ClientEngine::getInstance()->getBlockRegistry()->getItemName(slot.itemId);
        if (itemName.empty()) return;
        ImTextureID tex = getItemIconTexture(itemName);
        if (tex == 0) return;
        addNearestSamplerCallback(ImGui::GetForegroundDrawList());
        float pad = 5.0f;
        float iconSize = INV_SLOT - pad * 2;
        ImGui::GetForegroundDrawList()->AddImage(
            tex,
            ImVec2(sx + pad, sy + pad + ITEM_Y_OFFSET),
            ImVec2(sx + pad + iconSize, sy + pad + iconSize + ITEM_Y_OFFSET));
        if (slot.count > 1) {
            char countStr[8];
            snprintf(countStr, sizeof(countStr), "%d", slot.count);
            ImVec2 textSize = ImGui::CalcTextSize(countStr);
            ImGui::GetForegroundDrawList()->AddText(
                ImVec2(sx + INV_SLOT - textSize.x - 3,
                       sy + INV_SLOT - textSize.y - 2 + ITEM_Y_OFFSET),
                IM_COL32(255, 255, 255, 255), countStr);
        }
        // 耐久条
        if (slot.maxDamage > 0) {
            float pad = 5.0f;
            float durBarY = sy + INV_SLOT - 7.0f + ITEM_Y_OFFSET;
            float durBarH = 3.0f;
            float durBarX = sx + pad;
            float durBarW = INV_SLOT - pad * 2;
            float ratio = 1.0f - (float)slot.damage / (float)slot.maxDamage;
            ratio = std::max(0.0f, std::min(1.0f, ratio));
            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW, durBarY + durBarH),
                IM_COL32(0, 0, 0, 150), 1.0f);
            ImU32 durColor;
            if (ratio > 0.5f) durColor = IM_COL32(64, 255, 64, 255);
            else if (ratio > 0.25f) durColor = IM_COL32(255, 255, 64, 255);
            else durColor = IM_COL32(255, 64, 64, 255);
            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW * ratio, durBarY + durBarH),
                durColor, 1.0f);
        }
    };

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##FurnaceClick", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoNav);

    // 熔炉槽位坐标（屏幕像素）
    float inputX = containerX + TEX_INPUT_LEFT * S;
    float inputY = containerY + TEX_INPUT_TOP * S;
    float fuelX  = containerX + TEX_FUEL_LEFT * S;
    float fuelY  = containerY + TEX_FUEL_TOP * S;
    float outX   = containerX + TEX_OUT_LEFT * S;
    float outY   = containerY + TEX_OUT_TOP * S;

    auto handleSlotClick = [&](float sx, float sy, int containerSlot, const char* id) {
        ImGui::SetCursorScreenPos(ImVec2(sx, sy));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##%s", id);
        ImGui::InvisibleButton(btnId, ImVec2(INV_SLOT, INV_SLOT));
        auto* eng = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
        if (!eng) { ImGui::PopStyleColor(3); return; }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            eng->sendContainerClick(containerSlot, 0);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            eng->sendContainerClick(containerSlot, 1);
        }
        ImGui::PopStyleColor(3);
    };

    auto getContainerSlot = [&](int index) -> InvSlot {
        if (index >= 0 && index < (int)containerSlots.size()) {
            return containerSlots[index];
        }
        return InvSlot{};
    };

    // 熔炉 3 个容器槽
    renderItem(inputX, inputY, getContainerSlot(0));
    handleSlotClick(inputX, inputY, 0, "furnace_in");

    renderItem(fuelX, fuelY, getContainerSlot(1));
    handleSlotClick(fuelX, fuelY, 1, "furnace_fuel");

    renderItem(outX, outY, getContainerSlot(2));
    handleSlotClick(outX, outY, 2, "furnace_out");

    // 主背包格 3x9 (slots 3-29)
    for (int row = 0; row < 3; row++) {
        float rowY = gridY + row * INV_SLOT;
        for (int col = 0; col < 9; col++) {
            float sx = gridX + col * INV_SLOT;
            int slotIdx = 3 + row * 9 + col;
            renderItem(sx, rowY, getContainerSlot(slotIdx));
            char id[16];
            snprintf(id, sizeof(id), "main_%d", slotIdx);
            handleSlotClick(sx, rowY, slotIdx, id);
        }
    }

    // 快捷栏 1x9 (slots 30-38)
    for (int i = 0; i < 9; i++) {
        float sx = gridX + i * INV_SLOT;
        int slotIdx = 30 + i;
        if (i == inv.getSelectedSlot()) {
            ImGui::GetForegroundDrawList()->AddRect(
                ImVec2(sx - 2, hotbarY - 2),
                ImVec2(sx + INV_SLOT + 2, hotbarY + INV_SLOT + 2),
                IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.5f);
        }
        renderItem(sx, hotbarY, getContainerSlot(slotIdx));
        char id[16];
        snprintf(id, sizeof(id), "hot_%d", i);
        handleSlotClick(sx, hotbarY, slotIdx, id);
    }

    // 光标上的物品
    const InvSlot& cursor = inv.getCursorItem();
    if (cursor.present && cursor.itemId > 0) {
        float mx = io.MousePos.x - INV_SLOT * 0.5f;
        float my = io.MousePos.y - INV_SLOT * 0.5f;
        renderItem(mx, my, cursor);
    }

    ImGui::End();
}
// ============================================================
// 箱子界面（containerType == 2 或 5）2是单格箱子，5是双格箱子
// 纹理：textures/gui/container/generic_54.png（256x256 图集）
// 容器槽位：0-(n*9-1) = 箱子格子（单格=9个，双格=18个）
// 玩家背包槽位 n*9-(n*9+26) = 主背包
// 快捷栏槽位 n*9+27-(n*9+35) = 快捷栏
// ============================================================
void InventoryScreen::renderChest(float w, float h, int containerType) {
    ImGuiIO& io = ImGui::GetIO();

    const float INV_SLOT = 50.0f;
    const float TEX_SLOT = 18.0f;
    float S = INV_SLOT / TEX_SLOT;

    auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!game) return;
    auto& inv = *game->getInventory();
    const auto& containerSlots = inv.getContainerSlots();

    // ---- 根据 containerType 确定行数 ----
    int chestRows = 3;                         // 默认单格箱子
    if (containerType == 5) chestRows = 6;     // 双格箱子

    // ---- 纹理参数 ----
    const float TEX_CONTAINER_W = 176.0f;
    const float CHEST_AREA_H = chestRows * 18.0f + 17.0f;
    const float PLAYER_AREA_H = 96.0f;
    const float PLAYER_UV_Y_START = 126.0f / 256.0f;
    const float PLAYER_UV_Y_END   = (126.0f + 96.0f) / 256.0f;
    const float CHEST_OFFSET = (chestRows-3) * 18.0f;

    float containerW = TEX_CONTAINER_W * S;
    float containerH = (CHEST_AREA_H + PLAYER_AREA_H) * S;
    float containerX = (w - containerW) * 0.5f;
    float containerY = h * 0.5f - containerH * 0.5f;


    // ---- 背景遮罩 ----
    ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 160));

    // ---- 绘制箱子纹理 ----
    ImTextureID bgTex = getGuiTextureId("container/generic_54");
    if (bgTex != 0) {
        addNearestSamplerCallback(ImGui::GetForegroundDrawList());
        // 箱子区域
        ImGui::GetForegroundDrawList()->AddImage(
                bgTex,
                ImVec2(containerX, containerY),
                ImVec2(containerX + containerW, containerY + CHEST_AREA_H * S),
                ImVec2(0.0f, 0.0f),
                ImVec2(TEX_CONTAINER_W / 256.0f, CHEST_AREA_H / 256.0f));
        // 玩家背包区域
        ImGui::GetForegroundDrawList()->AddImage(
                bgTex,
                ImVec2(containerX, containerY + CHEST_AREA_H * S),
                ImVec2(containerX + containerW, containerY + (CHEST_AREA_H + PLAYER_AREA_H) * S),
                ImVec2(0.0f, PLAYER_UV_Y_START),
                ImVec2(TEX_CONTAINER_W / 256.0f, PLAYER_UV_Y_END));
    }

    // ---- 标题 ----
    const char* title = (containerType == 5) ? "大型箱子" : "箱子";
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImGui::GetForegroundDrawList()->AddText(
            ImVec2(containerX + (containerW - titleSize.x) * 0.5f,
                   containerY + 6.0f * S),
            IM_COL32(55, 55, 55, 255), title);

    // ---- 辅助 Lambda（renderItem / handleSlotClick 完全不变） ----
    auto renderItem = [&](float sx, float sy, const InvSlot& slot) {
        if (!slot.present || slot.itemId <= 0) return;
        std::string itemName = ClientEngine::getInstance()->getBlockRegistry()->getItemName(slot.itemId);
        if (itemName.empty()) return;
        ImTextureID tex = getItemIconTexture(itemName);
        if (tex == 0) return;
        addNearestSamplerCallback(ImGui::GetForegroundDrawList());
        float pad = 5.0f;
        float iconSize = INV_SLOT - pad * 2;
        ImGui::GetForegroundDrawList()->AddImage(
                tex,
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
        // 耐久条
        if (slot.maxDamage > 0) {
            float pad = 5.0f;
            float durBarY = sy + INV_SLOT - 7.0f;
            float durBarH = 3.0f;
            float durBarX = sx + pad;
            float durBarW = INV_SLOT - pad * 2;
            float ratio = 1.0f - (float)slot.damage / (float)slot.maxDamage;
            ratio = std::max(0.0f, std::min(1.0f, ratio));
            ImGui::GetForegroundDrawList()->AddRectFilled(
                    ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW, durBarY + durBarH),
                    IM_COL32(0, 0, 0, 150), 1.0f);
            ImU32 durColor;
            if (ratio > 0.5f) durColor = IM_COL32(64, 255, 64, 255);
            else if (ratio > 0.25f) durColor = IM_COL32(255, 255, 64, 255);
            else durColor = IM_COL32(255, 64, 64, 255);
            ImGui::GetForegroundDrawList()->AddRectFilled(
                    ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW * ratio, durBarY + durBarH),
                    durColor, 1.0f);
        }
    };

    auto getContainerSlot = [&](int index) -> InvSlot {
        if (index >= 0 && index < (int)containerSlots.size()) {
            return containerSlots[index];
        }
        return InvSlot{};
    };

    auto handleSlotClick = [&](float sx, float sy, int containerSlot, const char* id) {
        ImGui::SetCursorScreenPos(ImVec2(sx, sy));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##%s", id);
        ImGui::InvisibleButton(btnId, ImVec2(INV_SLOT, INV_SLOT));
        auto* eng = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
        if (!eng) { ImGui::PopStyleColor(3); return; }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            eng->sendContainerClick(containerSlot, 0);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            eng->sendContainerClick(containerSlot, 1);
        }
        ImGui::PopStyleColor(3);
    };

    // ---- ImGui 窗口 ----
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##ChestClick", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoNav);

    // ---- 绘制箱子格子（偏移 7, 17） ----
    float chestStartX = containerX + 7.0f * S;
    float chestStartY = containerY + 17.0f * S;
    for (int row = 0; row < chestRows; row++) {
        for (int col = 0; col < 9; col++) {
            float sx = chestStartX + col * INV_SLOT;
            float sy = chestStartY + row * INV_SLOT;
            int slotIdx = row * 9 + col;
            renderItem(sx, sy, getContainerSlot(slotIdx));
            char id[16];
            snprintf(id, sizeof(id), "chest_%d", slotIdx);
            handleSlotClick(sx, sy, slotIdx, id);
        }
    }

    // ---- 绘制玩家主背包（修正 Y：83 + chestRows * 18） ----
    float playerStartX = containerX + 7.0f * S;
    float playerStartY = containerY + (CHEST_OFFSET + 84.0f) * S;   // 关键修复
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 9; col++) {
            float sx = playerStartX + col * INV_SLOT;
            float sy = playerStartY + row * INV_SLOT;
            int slotIdx = chestRows * 9 + row * 9 + col;
            renderItem(sx, sy, getContainerSlot(slotIdx));
            char id[16];
            snprintf(id, sizeof(id), "main_%d", slotIdx);
            handleSlotClick(sx, sy, slotIdx, id);
        }
    }

    // ---- 绘制快捷栏（修正 Y：141，固定） ----
    float hotbarStartY = containerY + ((CHEST_OFFSET) + 142.0f) * S;                      // 关键修复
    for (int col = 0; col < 9; col++) {
        float sx = playerStartX + col * INV_SLOT;
        float sy = hotbarStartY;
        int slotIdx = chestRows * 9 + 27 + col;
        if (col == inv.getSelectedSlot()) {
            ImGui::GetForegroundDrawList()->AddRect(
                    ImVec2(sx - 2, sy - 2),
                    ImVec2(sx + INV_SLOT + 2, sy + INV_SLOT + 2),
                    IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.5f);
        }
        renderItem(sx, sy, getContainerSlot(slotIdx));
        char id[16];
        snprintf(id, sizeof(id), "hot_%d", col);
        handleSlotClick(sx, sy, slotIdx, id);
    }

    // ---- 光标物品 ----
    const InvSlot& cursor = inv.getCursorItem();
    if (cursor.present && cursor.itemId > 0) {
        float mx = io.MousePos.x - INV_SLOT * 0.5f;
        float my = io.MousePos.y - INV_SLOT * 0.5f;
        renderItem(mx, my, cursor);
    }

    ImGui::End();
}