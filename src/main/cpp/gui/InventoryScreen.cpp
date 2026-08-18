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

// 耐久条渲染
static void renderDurabilityBar(ImDrawList* drawList, float sx, float sy, float invSlotSize,
                                int damage, int maxDamage) {
    if (maxDamage <= 0) return;
    float pad = 5.0f;
    float durBarY = sy + invSlotSize - 7.0f;
    float durBarH = 3.0f;
    float durBarX = sx + pad;
    float durBarW = invSlotSize - pad * 2;
    float ratio = 1.0f - (float)damage / (float)maxDamage;
    ratio = std::max(0.0f, std::min(1.0f, ratio));

    drawList->AddRectFilled(
            ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW, durBarY + durBarH),
            IM_COL32(0, 0, 0, 150), 1.0f);

    ImU32 durColor;
    if (ratio > 0.5f) durColor = IM_COL32(64, 255, 64, 255);
    else if (ratio > 0.25f) durColor = IM_COL32(255, 255, 64, 255);
    else durColor = IM_COL32(255, 64, 64, 255);

    drawList->AddRectFilled(
            ImVec2(durBarX, durBarY), ImVec2(durBarX + durBarW * ratio, durBarY + durBarH),
            durColor, 1.0f);
}
// -------- 主渲染入口 --------
void InventoryScreen::render(int mouseX, int mouseY) {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    int containerType = GameUI::getInstance().getOpenContainerType();
    if (containerType == 2 || containerType == 5) {
        renderChest(w, h, containerType);
    } else if (containerType == 11) {
        renderCraftingTable(w, h);
    } else if (containerType == 13) {
        renderFurnace(w, h);
    } else {
        renderPlayerInventory(w, h);
    }
}

// -------- 玩家背包渲染（含合成格）--------
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
    const float TEX_ARMOR_X = 7.0f;
    const float TEX_ARMOR_Y_START = 8.0f;

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

    // 渲染装备槽位
    auto renderArmorSlot = [&](int equipmentSlotIndex, float sx, float sy, const InvSlot& slot) {
        // 背景图已经包含了装备槽位的纹理，我们只需要渲染物品
        if (slot.present && slot.itemId > 0) {
            std::string itemName = ClientEngine::getInstance()->getBlockRegistry()->getItemName(slot.itemId);
            if (!itemName.empty()) {
                ImTextureID tex = getItemIconTexture(itemName);
                if (tex != 0) {
                    addNearestSamplerCallback(ImGui::GetForegroundDrawList());
                    float pad = 4.0f;
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
                }
            }
        }
    };

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
        renderDurabilityBar(ImGui::GetForegroundDrawList(), sx, sy, INV_SLOT, slot.damage, slot.maxDamage);
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

    auto getSlotScreenPos = [&](int slot, float& sx, float& sy) {
        if (slot == 0) {
            sx = containerX + TEX_RESULT_LEFT * S;
            sy = containerY + TEX_RESULT_TOP * S;
        } else if (slot >= 1 && slot <= 4) {
            int idx = slot - 1;
            sx = containerX + TEX_CRAFT_LEFT * S - 3.0f + (idx % 2) * INV_SLOT;
            sy = containerY + TEX_CRAFT_TOP * S - 2.0f + (idx / 2) * INV_SLOT;
        } else if (slot >= 9 && slot <= 35) {
            int idx = slot - 9;
            sx = gridX + (idx % 9) * INV_SLOT;
            sy = gridY + (idx / 9) * INV_SLOT;
        } else if (slot >= 36 && slot <= 44) {
            sx = gridX + (slot - 36) * INV_SLOT;
            sy = hotbarY;
        } else {
            sx = sy = 0;
        }
    };

    int containerId = GameUI::getInstance().getOpenContainerId();

    // ---- 处理拖拽输入 ----
    auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (engine) {
        m_dragHelper.processInput(io, inv, engine, getSlotAtMouse, containerId);
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
                m_dragHelper.startDrag(containerSlot);
            } else {
                eng->sendContainerClick(containerSlot, 0);
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            eng->sendContainerClick(containerSlot, 1);
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

    // 装备槽位（5-8）
    float armorStartX = containerX + TEX_ARMOR_X * S;
    for (int i = 0; i < 4; i++) {
        float armorY = containerY + (TEX_ARMOR_Y_START + i * 18.0f) * S;
        renderArmorSlot(i, armorStartX, armorY, inv.getArmorSlot(i));

        // 处理装备槽点击
        ImGui::SetCursorScreenPos(ImVec2(armorStartX, armorY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
        char armorId[16];
        snprintf(armorId, sizeof(armorId), "armor_%d", i);
        ImGui::InvisibleButton(armorId, ImVec2(INV_SLOT, INV_SLOT));
        auto* eng = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
        if (eng) {
            int armorSlotIdx = 5 + i;  // 5-8 对应装备槽位
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                eng->sendContainerClick(armorSlotIdx, 0);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                eng->sendContainerClick(armorSlotIdx, 1);
            }
        }
        ImGui::PopStyleColor(3);
    }

    // ---- 拖拽预测渲染 ----
    m_dragHelper.renderPrediction(io, inv, renderItem, getSlotScreenPos, containerId, INV_SLOT);

    ImGui::End();
}

// -------- 工作台渲染 --------
void InventoryScreen::renderCraftingTable(float w, float h) {
    ImGuiIO& io = ImGui::GetIO();

    const float INV_SLOT = 50.0f;
    const float TEX_SLOT = 18.0f;
    float S = INV_SLOT / TEX_SLOT;

    const float TEX_LEFT = 7.0f;
    const float TEX_TOP = 84.0f;
    const float TEX_HOTBAR = 142.0f;
    const float TEX_CONTAINER_W = 176.0f;
    const float TEX_CONTAINER_H = 166.0f;
    const float TEX_CRAFT_LEFT = 29.0f;
    const float TEX_CRAFT_TOP = 17.0f;
    const float TEX_RESULT_LEFT = 124.0f;
    const float TEX_RESULT_TOP = 35.0f;

    const float ITEM_Y_OFFSET = -3.0f; // 物品图标上移3像素

    float containerW = TEX_CONTAINER_W * S;
    float containerH = TEX_CONTAINER_H * S;
    float containerX = (w - containerW) * 0.5f;
    float containerY = h * 0.5f - containerH * 0.5f;

    float gridX = containerX + TEX_LEFT * S;
    float gridY = containerY + TEX_TOP * S;
    float hotbarY = containerY + TEX_HOTBAR * S;

    ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 160));

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
        renderDurabilityBar(ImGui::GetForegroundDrawList(), sx, sy, INV_SLOT, slot.damage, slot.maxDamage);
    };

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

    auto getCraftSlotScreenPos = [&](int slot, float& sx, float& sy) {
        if (slot == 0) {
            sx = containerX + TEX_RESULT_LEFT * S;
            sy = containerY + TEX_RESULT_TOP * S;
        } else if (slot >= 1 && slot <= 9) {
            int idx = slot - 1;
            sx = containerX + TEX_CRAFT_LEFT * S + (idx % 3) * INV_SLOT;
            sy = containerY + TEX_CRAFT_TOP * S + (idx / 3) * INV_SLOT;
        } else if (slot >= 10 && slot <= 36) {
            int idx = slot - 10;
            sx = gridX + (idx % 9) * INV_SLOT;
            sy = gridY + (idx / 9) * INV_SLOT;
        } else if (slot >= 37 && slot <= 45) {
            sx = gridX + (slot - 37) * INV_SLOT;
            sy = hotbarY;
        } else {
            sx = sy = 0;
        }
    };

    int containerId = GameUI::getInstance().getOpenContainerId();

    auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (engine) {
        m_dragHelper.processInput(io, inv, engine, getCraftSlotAtMouse, containerId);
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##CraftingTableClick", nullptr,
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
                m_dragHelper.startDrag(containerSlot);
            } else {
                eng->sendContainerClick(containerSlot, 0);
            }
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

    // 3x3 合成格
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

    // 合成结果槽
    float resultX = containerX + TEX_RESULT_LEFT * S;
    float resultY = containerY + TEX_RESULT_TOP * S;
    renderItem(resultX, resultY, getContainerSlot(0));
    handleSlotClick(resultX, resultY, 0, "craft_result");

    // 主背包格
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

    // 快捷栏
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

    m_dragHelper.renderPrediction(io, inv, renderItem, getCraftSlotScreenPos, containerId, INV_SLOT);

    ImGui::End();
}

// -------- 熔炉渲染 --------
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

    const float TEX_INPUT_LEFT = 55.0f;
    const float TEX_INPUT_TOP  = 16.0f;
    const float TEX_FUEL_LEFT  = 55.0f;
    const float TEX_FUEL_TOP   = 52.0f;
    const float TEX_OUT_LEFT   = 115.0f;
    const float TEX_OUT_TOP    = 34.0f;

    const float ITEM_Y_OFFSET = -2.0f;

    float containerW = TEX_CONTAINER_W * S;
    float containerH = TEX_CONTAINER_H * S;
    float containerX = (w - containerW) * 0.5f;
    float containerY = h * 0.5f - containerH * 0.5f;

    float gridX = containerX + TEX_LEFT * S;
    float gridY = containerY + TEX_TOP * S;
    float hotbarY = containerY + TEX_HOTBAR * S;

    ImGui::GetForegroundDrawList()->AddRectFilled(
        ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 160));

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
        renderDurabilityBar(ImGui::GetForegroundDrawList(), sx, sy, INV_SLOT, slot.damage, slot.maxDamage);
    };

    auto getFurnaceSlotAtMouse = [&]() -> int {
        float mx = io.MousePos.x, my = io.MousePos.y;
        float inputX = containerX + TEX_INPUT_LEFT * S;
        float inputY = containerY + TEX_INPUT_TOP * S;
        if (mx >= inputX && mx < inputX + INV_SLOT && my >= inputY && my < inputY + INV_SLOT)
            return 0;
        float fuelX = containerX + TEX_FUEL_LEFT * S;
        float fuelY = containerY + TEX_FUEL_TOP * S;
        if (mx >= fuelX && mx < fuelX + INV_SLOT && my >= fuelY && my < fuelY + INV_SLOT)
            return 1;
        float outX = containerX + TEX_OUT_LEFT * S;
        float outY = containerY + TEX_OUT_TOP * S;
        if (mx >= outX && mx < outX + INV_SLOT && my >= outY && my < outY + INV_SLOT)
            return 2;
        for (int row = 0; row < 3; row++) {
            float rowY = gridY + row * INV_SLOT;
            for (int col = 0; col < 9; col++) {
                float sx = gridX + col * INV_SLOT;
                if (mx >= sx && mx < sx + INV_SLOT && my >= rowY && my < rowY + INV_SLOT)
                    return 3 + row * 9 + col;
            }
        }
        for (int i = 0; i < 9; i++) {
            float sx = gridX + i * INV_SLOT;
            if (mx >= sx && mx < sx + INV_SLOT && my >= hotbarY && my < hotbarY + INV_SLOT)
                return 30 + i;
        }
        return -1;
    };

    auto getFurnaceSlotScreenPos = [&](int slot, float& sx, float& sy) {
        if (slot == 0) {
            sx = containerX + TEX_INPUT_LEFT * S;
            sy = containerY + TEX_INPUT_TOP * S;
        } else if (slot == 1) {
            sx = containerX + TEX_FUEL_LEFT * S;
            sy = containerY + TEX_FUEL_TOP * S;
        } else if (slot == 2) {
            sx = containerX + TEX_OUT_LEFT * S;
            sy = containerY + TEX_OUT_TOP * S;
        } else if (slot >= 3 && slot <= 29) {
            int idx = slot - 3;
            sx = gridX + (idx % 9) * INV_SLOT;
            sy = gridY + (idx / 9) * INV_SLOT;
        } else if (slot >= 30 && slot <= 38) {
            sx = gridX + (slot - 30) * INV_SLOT;
            sy = hotbarY;
        } else {
            sx = sy = 0;
        }
    };

    int containerId = GameUI::getInstance().getOpenContainerId();

    auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (engine) {
        m_dragHelper.processInput(io, inv, engine, getFurnaceSlotAtMouse, containerId);
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##FurnaceClick", nullptr,
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
                m_dragHelper.startDrag(containerSlot);
            } else {
                eng->sendContainerClick(containerSlot, 0);
            }
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
    float inputX = containerX + TEX_INPUT_LEFT * S;
    float inputY = containerY + TEX_INPUT_TOP * S;
    renderItem(inputX, inputY, getContainerSlot(0));
    handleSlotClick(inputX, inputY, 0, "furnace_in");

    float fuelX = containerX + TEX_FUEL_LEFT * S;
    float fuelY = containerY + TEX_FUEL_TOP * S;
    renderItem(fuelX, fuelY, getContainerSlot(1));
    handleSlotClick(fuelX, fuelY, 1, "furnace_fuel");

    float outX = containerX + TEX_OUT_LEFT * S;
    float outY = containerY + TEX_OUT_TOP * S;
    renderItem(outX, outY, getContainerSlot(2));
    handleSlotClick(outX, outY, 2, "furnace_out");

    // 主背包
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

    // 快捷栏
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

    m_dragHelper.renderPrediction(io, inv, renderItem, getFurnaceSlotScreenPos, containerId, INV_SLOT);

    ImGui::End();
}

// -------- 箱子渲染 --------
void InventoryScreen::renderChest(float w, float h, int containerType) {
    ImGuiIO& io = ImGui::GetIO();

    const float INV_SLOT = 50.0f;
    const float TEX_SLOT = 18.0f;
    float S = INV_SLOT / TEX_SLOT;

    auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!game) return;
    auto& inv = *game->getInventory();
    const auto& containerSlots = inv.getContainerSlots();

    int chestRows = 3;
    if (containerType == 5) chestRows = 6;

    const float TEX_CONTAINER_W = 176.0f;
    const float CHEST_AREA_H = chestRows * 18.0f + 17.0f;
    const float PLAYER_AREA_H = 96.0f;
    const float PLAYER_UV_Y_START = 126.0f / 256.0f;
    const float PLAYER_UV_Y_END   = (126.0f + 96.0f) / 256.0f;
    const float CHEST_OFFSET = (chestRows - 3) * 18.0f;

    float containerW = TEX_CONTAINER_W * S;
    float containerH = (CHEST_AREA_H + PLAYER_AREA_H) * S;
    float containerX = (w - containerW) * 0.5f;
    float containerY = h * 0.5f - containerH * 0.5f;

    ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 160));

    ImTextureID bgTex = getGuiTextureId("container/generic_54");
    if (bgTex != 0) {
        addNearestSamplerCallback(ImGui::GetForegroundDrawList());
        ImGui::GetForegroundDrawList()->AddImage(
                bgTex,
                ImVec2(containerX, containerY),
                ImVec2(containerX + containerW, containerY + CHEST_AREA_H * S),
                ImVec2(0.0f, 0.0f),
                ImVec2(TEX_CONTAINER_W / 256.0f, CHEST_AREA_H / 256.0f));
        ImGui::GetForegroundDrawList()->AddImage(
                bgTex,
                ImVec2(containerX, containerY + CHEST_AREA_H * S),
                ImVec2(containerX + containerW, containerY + (CHEST_AREA_H + PLAYER_AREA_H) * S),
                ImVec2(0.0f, PLAYER_UV_Y_START),
                ImVec2(TEX_CONTAINER_W / 256.0f, PLAYER_UV_Y_END));
    }

    const char* title = (containerType == 5) ? "大型箱子" : "箱子";
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImGui::GetForegroundDrawList()->AddText(
            ImVec2(containerX + (containerW - titleSize.x) * 0.5f,
                   containerY + 6.0f * S),
            IM_COL32(55, 55, 55, 255), title);

    int containerId = GameUI::getInstance().getOpenContainerId();

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
        renderDurabilityBar(ImGui::GetForegroundDrawList(), sx, sy, INV_SLOT, slot.damage, slot.maxDamage);
    };

    auto getChestSlotAtMouse = [&]() -> int {
        float mx = io.MousePos.x, my = io.MousePos.y;
        float chestStartX = containerX + 7.0f * S;
        float chestStartY = containerY + 17.0f * S;
        for (int row = 0; row < chestRows; row++) {
            for (int col = 0; col < 9; col++) {
                float sx = chestStartX + col * INV_SLOT;
                float sy = chestStartY + row * INV_SLOT;
                if (mx >= sx && mx < sx + INV_SLOT && my >= sy && my < sy + INV_SLOT)
                    return row * 9 + col;
            }
        }
        float playerStartX = containerX + 7.0f * S;
        float playerStartY = containerY + (CHEST_OFFSET + 84.0f) * S;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 9; col++) {
                float sx = playerStartX + col * INV_SLOT;
                float sy = playerStartY + row * INV_SLOT;
                if (mx >= sx && mx < sx + INV_SLOT && my >= sy && my < sy + INV_SLOT)
                    return chestRows * 9 + row * 9 + col;
            }
        }
        float hotbarStartY = containerY + (CHEST_OFFSET + 142.0f) * S;
        for (int col = 0; col < 9; col++) {
            float sx = playerStartX + col * INV_SLOT;
            float sy = hotbarStartY;
            if (mx >= sx && mx < sx + INV_SLOT && my >= sy && my < sy + INV_SLOT)
                return chestRows * 9 + 27 + col;
        }
        return -1;
    };

    auto getChestSlotScreenPos = [&](int slot, float& sx, float& sy) {
        if (slot < chestRows * 9) {
            int row = slot / 9, col = slot % 9;
            sx = containerX + 7.0f * S + col * INV_SLOT;
            sy = containerY + 17.0f * S + row * INV_SLOT;
        } else if (slot < chestRows * 9 + 27) {
            int idx = slot - chestRows * 9;
            sx = containerX + 7.0f * S + (idx % 9) * INV_SLOT;
            sy = containerY + (CHEST_OFFSET + 84.0f) * S + (idx / 9) * INV_SLOT;
        } else if (slot < chestRows * 9 + 36) {
            int idx = slot - (chestRows * 9 + 27);
            sx = containerX + 7.0f * S + idx * INV_SLOT;
            sy = containerY + (CHEST_OFFSET + 142.0f) * S;
        } else {
            sx = sy = 0;
        }
    };

    auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (engine) {
        m_dragHelper.processInput(io, inv, engine, getChestSlotAtMouse, containerId);
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##ChestClick", nullptr,
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
                m_dragHelper.startDrag(containerSlot);
            } else {
                eng->sendContainerClick(containerSlot, 0);
            }
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

    // 箱子格子
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

    // 主背包
    float playerStartX = containerX + 7.0f * S;
    float playerStartY = containerY + (CHEST_OFFSET + 84.0f) * S;
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

    // 快捷栏
    float hotbarStartY = containerY + (CHEST_OFFSET + 142.0f) * S;
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

    m_dragHelper.renderPrediction(io, inv, renderItem, getChestSlotScreenPos, containerId, INV_SLOT);

    ImGui::End();
}