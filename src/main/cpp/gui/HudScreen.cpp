#include "HudScreen.h"

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "GameUI.h"
#include "CameraController.h"
#include "ClientEngine.h"
#include "ResourcepackManager.h"
#include "BlockRegistry.h"
#include "PlayerInventory.h"
#include "EntityRenderer.h"
#include "MinecraftVersion.h"

#include <chrono>
#include <cmath>
#include <algorithm>

// 游戏内 UI 布局常量
#define JOYSTICK_CENTER_X      (ImGui::GetIO().DisplaySize.x * 0.1375f)
#define JOYSTICK_CENTER_Y_OFFSET (ImGui::GetIO().DisplaySize.y * 0.292f)
#define JOYSTICK_RADIUS        (std::min(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y) * 0.153f)
#define JOYSTICK_KNOB_RADIUS   (std::min(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y) * 0.053f)
#define JOYSTICK_MAX_DIST      (std::min(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y) * 0.09f)
#define BTN_RADIUS             (std::min(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y) * 0.053f)
#define BTN_RIGHT_MARGIN       (ImGui::GetIO().DisplaySize.x * 0.056f)
#define BTN_VERTICAL_SPACING   (ImGui::GetIO().DisplaySize.y * 0.118f)

void HudScreen::render(int mouseX, int mouseY) {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;
    auto& gui = GameUI::getInstance();

    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    // ===== 摇杆底座（左下角） =====
    float jx = JOYSTICK_CENTER_X;
    float jy = h - JOYSTICK_CENTER_Y_OFFSET;

    bool joyActive = gui.isJoystickActive();
    ImU32 baseCol = joyActive ? IM_COL32(255, 255, 255, 60) : IM_COL32(255, 255, 255, 30);
    ImU32 baseOutline = joyActive ? IM_COL32(255, 255, 255, 100) : IM_COL32(255, 255, 255, 60);
    draw->AddCircleFilled(ImVec2(jx, jy), JOYSTICK_RADIUS, baseCol);
    draw->AddCircle(ImVec2(jx, jy), JOYSTICK_RADIUS, baseOutline);

    // 摇杆摇柄
    float knobX = joyActive ? jx + gui.getJoystickKnobX() : jx;
    float knobY = joyActive ? jy + gui.getJoystickKnobY() : jy;
    ImU32 knobCol = joyActive ? IM_COL32(255, 255, 255, 180) : IM_COL32(255, 255, 255, 100);
    ImU32 knobOutline = joyActive ? IM_COL32(255, 255, 255, 220) : IM_COL32(255, 255, 255, 160);
    draw->AddCircleFilled(ImVec2(knobX, knobY), JOYSTICK_KNOB_RADIUS, knobCol);
    draw->AddCircle(ImVec2(knobX, knobY), JOYSTICK_KNOB_RADIUS, knobOutline);

    // ===== 上升/下降按钮 =====
    float btnX = w - BTN_RIGHT_MARGIN;
    float btnUpY = h * 0.5f - BTN_VERTICAL_SPACING;
    float btnDownY = h * 0.5f;

    bool upPressed = gui.isButtonUp();
    ImU32 upBg = upPressed ? IM_COL32(255, 255, 255, 140) : IM_COL32(255, 255, 255, 60);
    ImU32 upBorder = upPressed ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 100);
    ImU32 upIcon = upPressed ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 180);
    draw->AddRectFilled(ImVec2(btnX - BTN_RADIUS, btnUpY - BTN_RADIUS), ImVec2(btnX + BTN_RADIUS, btnUpY + BTN_RADIUS), upBg, 4.0f);
    draw->AddRect(ImVec2(btnX - BTN_RADIUS, btnUpY - BTN_RADIUS), ImVec2(btnX + BTN_RADIUS, btnUpY + BTN_RADIUS), upBorder, 4.0f);
    draw->AddTriangleFilled(
        ImVec2(btnX, btnUpY - 10),
        ImVec2(btnX - 10, btnUpY + 6),
        ImVec2(btnX + 10, btnUpY + 6),
        upIcon);

    bool downPressed = gui.isButtonDown();
    ImU32 downBg = downPressed ? IM_COL32(255, 255, 255, 140) : IM_COL32(255, 255, 255, 60);
    ImU32 downBorder = downPressed ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 100);
    ImU32 downIcon = downPressed ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 180);
    draw->AddRectFilled(ImVec2(btnX - BTN_RADIUS, btnDownY - BTN_RADIUS), ImVec2(btnX + BTN_RADIUS, btnDownY + BTN_RADIUS), downBg, 4.0f);
    draw->AddRect(ImVec2(btnX - BTN_RADIUS, btnDownY - BTN_RADIUS), ImVec2(btnX + BTN_RADIUS, btnDownY + BTN_RADIUS), downBorder, 4.0f);
    draw->AddTriangleFilled(
        ImVec2(btnX, btnDownY + 10),
        ImVec2(btnX - 10, btnDownY - 6),
        ImVec2(btnX + 10, btnDownY - 6),
        downIcon);

    // ===== 疾跑按钮 =====
    float btnSprintY = h * 0.5f + BTN_VERTICAL_SPACING;
    bool sprintPressed = gui.isButtonSprint();
    ImU32 sprintBg = sprintPressed ? IM_COL32(255, 255, 255, 140) : IM_COL32(255, 255, 255, 60);
    ImU32 sprintBorder = sprintPressed ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 100);
    ImU32 sprintIcon = sprintPressed ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 180);
    draw->AddRectFilled(ImVec2(btnX - BTN_RADIUS, btnSprintY - BTN_RADIUS), ImVec2(btnX + BTN_RADIUS, btnSprintY + BTN_RADIUS), sprintBg, 4.0f);
    draw->AddRect(ImVec2(btnX - BTN_RADIUS, btnSprintY - BTN_RADIUS), ImVec2(btnX + BTN_RADIUS, btnSprintY + BTN_RADIUS), sprintBorder, 4.0f);
    draw->AddLine(ImVec2(btnX - 8, btnSprintY - 6), ImVec2(btnX + 8, btnSprintY + 2), sprintIcon, 3.0f);
    draw->AddLine(ImVec2(btnX - 8, btnSprintY), ImVec2(btnX + 8, btnSprintY + 6), sprintIcon, 3.0f);
    draw->AddLine(ImVec2(btnX - 8, btnSprintY + 6), ImVec2(btnX + 8, btnSprintY + 10), sprintIcon, 3.0f);

    // ===== 攻击按钮 =====
    float btnAttackY = h * 0.5f - BTN_VERTICAL_SPACING * 2 + h * 0.21f;
    float btnAttackX = btnX - w * 0.125f;
    {
        bool atkPressed = gui.isButtonAttack();
        ImU32 atkBg = atkPressed ? IM_COL32(255, 255, 255, 140) : IM_COL32(255, 255, 255, 60);
        ImU32 atkBorder = atkPressed ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 100);
        ImU32 atkIcon = atkPressed ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 180);
        draw->AddRectFilled(ImVec2(btnAttackX - BTN_RADIUS, btnAttackY - BTN_RADIUS), ImVec2(btnAttackX + BTN_RADIUS, btnAttackY + BTN_RADIUS), atkBg, 4.0f);
        draw->AddRect(ImVec2(btnAttackX - BTN_RADIUS, btnAttackY - BTN_RADIUS), ImVec2(btnAttackX + BTN_RADIUS, btnAttackY + BTN_RADIUS), atkBorder, 4.0f);
        draw->AddLine(ImVec2(btnAttackX - 8, btnAttackY - 8), ImVec2(btnAttackX + 8, btnAttackY + 8), atkIcon, 2.5f);
        draw->AddLine(ImVec2(btnAttackX + 8, btnAttackY - 8), ImVec2(btnAttackX - 8, btnAttackY + 8), atkIcon, 2.5f);
    }

    // ===== 放置按钮 =====
    float btnPlaceY = btnAttackY + BTN_VERTICAL_SPACING;
    {
        bool plcPressed = gui.isButtonPlace();
        ImU32 plcBg = plcPressed ? IM_COL32(255, 255, 255, 140) : IM_COL32(255, 255, 255, 60);
        ImU32 plcBorder = plcPressed ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 100);
        ImU32 plcIcon = plcPressed ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 180);
        draw->AddRectFilled(ImVec2(btnAttackX - BTN_RADIUS, btnPlaceY - BTN_RADIUS), ImVec2(btnAttackX + BTN_RADIUS, btnPlaceY + BTN_RADIUS), plcBg, 4.0f);
        draw->AddRect(ImVec2(btnAttackX - BTN_RADIUS, btnPlaceY - BTN_RADIUS), ImVec2(btnAttackX + BTN_RADIUS, btnPlaceY + BTN_RADIUS), plcBorder, 4.0f);
        draw->AddRect(ImVec2(btnAttackX - 10, btnPlaceY - 10), ImVec2(btnAttackX + 10, btnPlaceY + 10),
                      plcIcon, 0, 0, 2.5f);
    }

    // ===== F3 按钮 =====
    {
        const float F3_W = w * 0.0325f;
        const float F3_H = h * 0.039f;
        const float F3_X = w * 0.25f - F3_W * 0.5f;
        const float F3_Y = h * 0.014f;
        bool showDebug = gui.isDebugInfoVisible();
        ImU32 f3Col = showDebug ? IM_COL32(255, 255, 0, 200) : IM_COL32(255, 255, 255, 80);
        ImU32 f3Bg = showDebug ? IM_COL32(255, 255, 0, 40) : IM_COL32(255, 255, 255, 25);
        draw->AddRectFilled(ImVec2(F3_X, F3_Y), ImVec2(F3_X + F3_W, F3_Y + F3_H), f3Bg, 6.0f);
        draw->AddRect(ImVec2(F3_X, F3_Y), ImVec2(F3_X + F3_W, F3_Y + F3_H), f3Col, 6.0f, 0, 1.5f);
        const char* f3Text = "F3";
        ImVec2 textSize = ImGui::CalcTextSize(f3Text);
        float textX = F3_X + (F3_W - textSize.x) * 0.5f;
        float textY = F3_Y + (F3_H - textSize.y) * 0.5f;
        draw->AddText(ImVec2(textX, textY), f3Col, f3Text);
    }

    // ===== 准星 =====
    float cx = w * 0.5f;
    float cy = h * 0.5f;
    const float CROSS_SIZE = std::min(w, h) * 0.022f;
    const float CROSS_GAP = 0.0f;
    const float CROSS_THICK = 2.5f;
    ImU32 crossCol = IM_COL32(255, 255, 255, 200);
    draw->AddLine(ImVec2(cx - CROSS_SIZE, cy), ImVec2(cx - CROSS_GAP, cy), crossCol, CROSS_THICK);
    draw->AddLine(ImVec2(cx + CROSS_GAP, cy), ImVec2(cx + CROSS_SIZE, cy), crossCol, CROSS_THICK);
    draw->AddLine(ImVec2(cx, cy - CROSS_SIZE), ImVec2(cx, cy - CROSS_GAP), crossCol, CROSS_THICK);
    draw->AddLine(ImVec2(cx, cy + CROSS_GAP), ImVec2(cx, cy + CROSS_SIZE), crossCol, CROSS_THICK);

    // ===== 快捷栏 + HUD（使用 ImGui 窗口） =====
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Hotbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground);

    const float SLOT_SIZE = w * 0.034f;
    const float SLOT_GAP = w * 0.003f;
    const float HOTBAR_Y = h * 0.915f;
    float totalW = 9.0f * SLOT_SIZE + 8.0f * SLOT_GAP;
    float hotbarX = w * 0.5f - totalW * 0.5f;

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(hotbarX - 6, HOTBAR_Y - 6),
        ImVec2(hotbarX + totalW + 6, HOTBAR_Y + SLOT_SIZE + 6),
        IM_COL32(0, 0, 0, 100), 4.0f);

    InvSlot hotbar[9];
    PlayerInventory::getInstance().getHotbarSlots(hotbar);
    int selSlot = PlayerInventory::getInstance().getSelectedSlot();

    for (int i = 0; i < 9; i++) {
        float sx = hotbarX + i * (SLOT_SIZE + SLOT_GAP);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(sx, HOTBAR_Y),
            ImVec2(sx + SLOT_SIZE, HOTBAR_Y + SLOT_SIZE),
            IM_COL32(40, 40, 50, 200), 2.0f);

        if (i == selSlot) {
            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(sx - 2, HOTBAR_Y - 2),
                ImVec2(sx + SLOT_SIZE + 2, HOTBAR_Y + SLOT_SIZE + 2),
                IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.5f);
        }

        if (hotbar[i].present && hotbar[i].itemId > 0) {
            std::string itemName = BlockRegistry::getInstance().getItemName(hotbar[i].itemId);
            if (!itemName.empty()) {
                GLuint tex = ResourcepackManager::getInstance().getItemTexture(itemName);
                if (tex != 0) {
                    ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
                        glBindSampler(0, 0);
                    }, nullptr);
                    float pad = 5.0f;
                    float iconSize = SLOT_SIZE - pad * 2;
                    ImGui::SetCursorScreenPos(ImVec2((int)(sx + pad), (int)(HOTBAR_Y + pad)));
                    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(iconSize, iconSize));
                }
            }

            if (hotbar[i].count > 1) {
                char countStr[8];
                snprintf(countStr, sizeof(countStr), "%d", hotbar[i].count);
                ImVec2 textSize = ImGui::CalcTextSize(countStr);
                ImGui::SetCursorScreenPos(ImVec2(
                    sx + SLOT_SIZE - textSize.x - 4,
                    HOTBAR_Y + SLOT_SIZE - textSize.y - 2));
                ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", countStr);
            }
        }
    }

    // ===== 生命值 + 饥饿值 =====
    {
        auto* engine = ClientEngine::getInstance();
        if (engine && engine->getGameMode() != 1) {
            float healthVal = engine->getHealth();
            int foodVal = engine->getFood();
            const float ICON_SIZE = w * 0.0125f;
            const float GAP = 1.0f;
            const float HUD_Y = HOTBAR_Y - ICON_SIZE - h * 0.025f;

            if (!hudTexturesLoaded) {
                auto& rm = ResourcepackManager::getInstance();
                texHeartContainer = rm.getHudTexture("heart/container");
                texHeartFull = rm.getHudTexture("heart/full");
                texHeartHalf = rm.getHudTexture("heart/half");
                texFoodEmpty = rm.getHudTexture("food_empty");
                texFoodFull = rm.getHudTexture("food_full");
                texFoodHalf = rm.getHudTexture("food_half");
                texExpBarBg = rm.getHudTexture("experience_bar_background");
                texExpBarProgress = rm.getHudTexture("experience_bar_progress");
                hudTexturesLoaded = true;
            }

            for (int i = 0; i < 10; i++) {
                float hx = hotbarX + i * (ICON_SIZE + GAP);
                if (texHeartContainer) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)texHeartContainer,
                        ImVec2(hx, HUD_Y), ImVec2(hx + ICON_SIZE, HUD_Y + ICON_SIZE));
                }
                float remain = healthVal - i * 2.0f;
                if (remain >= 2.0f && texHeartFull) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)texHeartFull,
                        ImVec2(hx, HUD_Y), ImVec2(hx + ICON_SIZE, HUD_Y + ICON_SIZE));
                } else if (remain >= 1.0f && texHeartHalf) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)texHeartHalf,
                        ImVec2(hx, HUD_Y), ImVec2(hx + ICON_SIZE, HUD_Y + ICON_SIZE));
                }
            }

            float totalWFood = 10.0f * (ICON_SIZE + GAP) - GAP;
            float foodStartX = hotbarX + totalW - totalWFood;

            for (int i = 0; i < 10; i++) {
                float fx = foodStartX + i * (ICON_SIZE + GAP);
                if (texFoodEmpty) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)texFoodEmpty,
                        ImVec2(fx, HUD_Y), ImVec2(fx + ICON_SIZE, HUD_Y + ICON_SIZE));
                }
                int remain = foodVal - i * 2;
                if (remain >= 2 && texFoodFull) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)texFoodFull,
                        ImVec2(fx, HUD_Y), ImVec2(fx + ICON_SIZE, HUD_Y + ICON_SIZE));
                } else if (remain >= 1 && texFoodHalf) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)texFoodHalf,
                        ImVec2(fx, HUD_Y), ImVec2(fx + ICON_SIZE, HUD_Y + ICON_SIZE));
                }
            }

            // 经验条
            if (texExpBarBg && texExpBarProgress) {
                float expBarH = 14.0f;
                float expBarW = totalW;
                float expBarX = hotbarX;
                float expBarY = HOTBAR_Y - expBarH - 4.0f;

                ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
                    glBindSampler(0, 0);
                }, nullptr);
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(intptr_t)texExpBarBg,
                    ImVec2(expBarX, expBarY),
                    ImVec2(expBarX + expBarW, expBarY + expBarH),
                    ImVec2(0, 0), ImVec2(1, 1));

                if (engine) {
                    float progress = engine->getExperienceProgress();
                    if (progress > 0.0f && progress <= 1.0f) {
                        ImGui::GetWindowDrawList()->AddImage(
                            (ImTextureID)(intptr_t)texExpBarProgress,
                            ImVec2(expBarX, expBarY),
                            ImVec2(expBarX + expBarW * progress, expBarY + expBarH),
                            ImVec2(0, 0), ImVec2(progress, 1));
                    }

                    int level = engine->getExperienceLevel();
                    if (level > 0) {
                        char levelStr[16];
                        snprintf(levelStr, sizeof(levelStr), "%d", level);
                        ImVec2 textSize = ImGui::CalcTextSize(levelStr);
                        float textX = expBarX + (expBarW - textSize.x) * 0.5f;
                        float textY = expBarY + (expBarH - textSize.y) * 0.5f - 12.0f;
                        ImU32 greenCol = IM_COL32(128, 255, 32, 255);
                        ImU32 shadowCol = IM_COL32(0, 0, 0, 255);
                        ImGui::GetWindowDrawList()->AddText(ImVec2(textX - 1, textY), shadowCol, levelStr);
                        ImGui::GetWindowDrawList()->AddText(ImVec2(textX + 1, textY), shadowCol, levelStr);
                        ImGui::GetWindowDrawList()->AddText(ImVec2(textX, textY - 1), shadowCol, levelStr);
                        ImGui::GetWindowDrawList()->AddText(ImVec2(textX, textY + 1), shadowCol, levelStr);
                        ImGui::GetWindowDrawList()->AddText(ImVec2(textX, textY), greenCol, levelStr);
                    }
                }
            }
        }
    }

    // ===== E 按钮 =====
    {
        float eX = hotbarX + totalW + 10.0f;
        bool invOpen = gui.isInventoryOpen();
        ImU32 eCol = invOpen ? IM_COL32(255, 255, 0, 200) : IM_COL32(255, 255, 255, 180);
        ImU32 eBg = invOpen ? IM_COL32(255, 255, 0, 40) : IM_COL32(40, 40, 50, 180);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(eX, HOTBAR_Y), ImVec2(eX + SLOT_SIZE, HOTBAR_Y + SLOT_SIZE),
            eBg, 4.0f);
        ImGui::GetWindowDrawList()->AddRect(
            ImVec2(eX, HOTBAR_Y), ImVec2(eX + SLOT_SIZE, HOTBAR_Y + SLOT_SIZE),
            eCol, 4.0f, 0, 2.0f);
        const char* eText = "E";
        ImVec2 eTextSize = ImGui::CalcTextSize(eText);
        float eTextX = eX + (SLOT_SIZE - eTextSize.x) * 0.5f;
        float eTextY = HOTBAR_Y + (SLOT_SIZE - eTextSize.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(ImVec2(eTextX, eTextY), eCol, eText);
    }

    ImGui::End();

    // ===== F3 调试信息 =====
    if (gui.isDebugInfoVisible()) {
        static auto lastFpsTime = std::chrono::steady_clock::now();
        static int fpsCounter = 0;
        static float displayFps = 0.0f;
        fpsCounter++;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastFpsTime).count();
        if (elapsed >= 1.0f) {
            displayFps = fpsCounter / elapsed;
            fpsCounter = 0;
            lastFpsTime = now;
        }

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::Begin("DebugInfo", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBackground);

        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Minecraft %s", VersionManager::getInstance().getVersionName().c_str());
        ImGui::Text("FPS: %.0f", displayFps);
        ImGui::Text("E: %d/%d", EntityRenderer::getInstance().getRenderedCount(),
                     EntityRenderer::getInstance().getTotalCount());
        ImGui::Text("");

        auto pos = CameraController::getInstance().getPosition();
        ImGui::Text("XYZ: %.1f / %.1f / %.1f", pos.x, pos.y, pos.z);

        float yaw = CameraController::getInstance().getYaw();
        float yawDeg = glm::degrees(yaw);
        static const char* directions[] = {"South", "West", "North", "East"};
        int dirIdx = ((int)(yawDeg + 45) / 90) % 4;
        if (dirIdx < 0) dirIdx += 4;
        ImGui::Text("Facing: %s (%.1f / %.1f)",
            directions[dirIdx],
            yawDeg,
            glm::degrees(CameraController::getInstance().getPitch()));

        ImGui::End();
    }
}
