#include "MultiplayerScreen.h"
#include "GuiUtils.h"

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "TextureLoader.h"
#include "ServerList.h"
#include "MinecraftVersion.h"
#include "stb_image.h"

#include <android/log.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <thread>
#include <chrono>

#define LOG_TAG "MultiplayerScreen"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define SERVERS_FILE_PATH "/data/data/com.calcite/servers.txt"

void MultiplayerScreen::init(int width, int height) {
    loadServerList();
    pingAllServers();
}

void MultiplayerScreen::removed() {
    saveServerList();
}

void MultiplayerScreen::resetGLResources() {
    defaultServerIconTexID = 0;
    for (int i = 0; i < 5; i++) { pingTex[i] = 0; pingingTex[i] = 0; }
    unreachableTexID = 0;
}

void MultiplayerScreen::render(int mouseX, int mouseY) {
    if (showingAddServer) {
        renderAddServer();
    } else {
        renderServerList();
    }
}

void MultiplayerScreen::renderServerList() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Multiplayer", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPos(ImVec2(w * 0.5f - 80.0f, 25));
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "多人游戏");

    float listStartY = 75.0f;
    float listEndY = h - 90.0f;
    float listHeight = listEndY - listStartY;
    float listWidth = w * 0.7f;
    float listLeft = w * 0.5f - listWidth * 0.5f;

    ImGui::SetCursorPos(ImVec2(listLeft, listStartY));
    ImGui::BeginChild("ServerList", ImVec2(listWidth, listHeight), true);

    if (servers.empty()) {
        ImGui::SetCursorPos(ImVec2(w * 0.5f - 120.0f, listHeight * 0.4f));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "暂无保存的服务器");
    } else {
        // 懒加载默认服务器图标
        if (defaultServerIconTexID == 0) {
            TextureData tex = TextureLoader::loadPNG("misc/unknown_server.png");
            if (tex.data && tex.width > 0 && tex.height > 0) {
                glGenTextures(1, &defaultServerIconTexID);
                glBindTexture(GL_TEXTURE_2D, defaultServerIconTexID);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }
        }

        // 懒加载延迟信号图标
        if (pingTex[0] == 0) {
            auto loadPingTex = [](GLuint& texID, const char* path) {
                TextureData tex = TextureLoader::loadPNG(path);
                if (tex.data && tex.width > 0 && tex.height > 0) {
                    glGenTextures(1, &texID);
                    glBindTexture(GL_TEXTURE_2D, texID);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height,
                                 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
            };
            for (int i = 0; i < 5; i++) {
                char path[64];
                snprintf(path, sizeof(path), "gui/sprites/server_list/ping_%d.png", i + 1);
                loadPingTex(pingTex[i], path);
                snprintf(path, sizeof(path), "gui/sprites/server_list/pinging_%d.png", i + 1);
                loadPingTex(pingingTex[i], path);
            }
            loadPingTex(unreachableTexID, "gui/sprites/server_list/unreachable.png");
        }

        if (ImGui::BeginTable("servers", 1,
            ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthStretch);

            for (size_t i = 0; i < servers.size(); i++) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 80.0f);
                ImGui::TableNextColumn();
                ImGui::PushID((int)i);

                bool isSelected = (selectedServer == (int)i);

                // 懒上传服务器图标纹理
                if (!servers[i].faviconPngData.empty() && servers[i].iconTextureID == 0) {
                    int iw, ih, ich;
                    uint8_t* pixels = stbi_load_from_memory(
                        servers[i].faviconPngData.data(), (int)servers[i].faviconPngData.size(),
                        &iw, &ih, &ich, 4);
                    if (pixels && iw > 0 && ih > 0) {
                        glGenTextures(1, &servers[i].iconTextureID);
                        glBindTexture(GL_TEXTURE_2D, servers[i].iconTextureID);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        stbi_image_free(pixels);
                    }
                    servers[i].faviconPngData.clear();
                    servers[i].faviconPngData.shrink_to_fit();
                }

                std::string line1 = servers[i].name;
                std::string rightInfo;
                std::string motdLine1, motdLine2;
                bool motdRed = false;
                if (servers[i].pinging) {
                    motdLine1 = "(正在获取信息...)";
                } else if (servers[i].pinged) {
                    if (!servers[i].motd.empty()) {
                        size_t nlPos = servers[i].motd.find('\n');
                        if (nlPos != std::string::npos) {
                            motdLine1 = servers[i].motd.substr(0, nlPos);
                            motdLine2 = servers[i].motd.substr(nlPos + 1);
                            while (!motdLine2.empty() && (motdLine2[0] == ' ' || motdLine2[0] == '\t'))
                                motdLine2.erase(0, 1);
                        } else {
                            motdLine1 = servers[i].motd;
                        }
                    }
                    if (servers[i].latencyMs >= 0) {
                        char info[64];
                        snprintf(info, sizeof(info), "%d/%d", servers[i].onlinePlayers, servers[i].maxPlayers);
                        rightInfo = info;
                    } else {
                        char info[64];
                        snprintf(info, sizeof(info), "%d/%d", servers[i].onlinePlayers, servers[i].maxPlayers);
                        rightInfo = info;
                    }
                } else if (servers[i].pingFailed) {
                    motdLine1 = "\xe6\x97\xa0\xe6\xb3\x95\xe8\xbf\x9e\xe6\x8e\xa5\xe5\x88\xb0\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8";
                    motdRed = true;
                }

                float iconSize = 64.0f;
                float textOffsetX = iconSize + 8.0f;
                GLuint displayIcon = (servers[i].iconTextureID != 0) ? servers[i].iconTextureID : defaultServerIconTexID;

                ImVec2 startPos = ImGui::GetCursorPos();
                float availWidth = ImGui::GetContentRegionAvail().x;

                if (ImGui::Selectable("##sel", isSelected,
                    ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, 74.0f)))
                {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        connectToServer(servers[i]);
                    } else {
                        selectedServer = (int)i;
                    }
                }

                if (displayIcon != 0) {
                    ImGui::SetCursorPos(ImVec2(startPos.x + 2, startPos.y + 5));
                    ImGui::Image((ImTextureID)(intptr_t)displayIcon, ImVec2(iconSize, iconSize));
                }

                float textLeft = startPos.x + textOffsetX;
                float rightEdge = startPos.x + availWidth;

                ImGui::SetCursorPos(ImVec2(textLeft, startPos.y - 2));
                ImGui::Text("%s", line1.c_str());

                float pingIconWidth = (pingTex[0] != 0 || unreachableTexID != 0) ? 36.0f : 0.0f;
                if (!rightInfo.empty()) {
                    ImVec2 infoSize = ImGui::CalcTextSize(rightInfo.c_str());
                    ImGui::SetCursorPos(ImVec2(rightEdge - infoSize.x - pingIconWidth - 4, startPos.y + 3));
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", rightInfo.c_str());
                }

                {
                    GLuint displayPingTex = 0;
                    if (servers[i].pinging) {
                        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
                        int frame = (int)((ms / 100 + (int)i * 2) % 8);
                        if (frame > 4) frame = 8 - frame;
                        displayPingTex = pingingTex[frame];
                    } else if (servers[i].pinged && servers[i].latencyMs >= 0) {
                        if (servers[i].latencyMs < 150) displayPingTex = pingTex[4];
                        else if (servers[i].latencyMs < 300) displayPingTex = pingTex[3];
                        else if (servers[i].latencyMs < 600) displayPingTex = pingTex[2];
                        else if (servers[i].latencyMs < 1000) displayPingTex = pingTex[1];
                        else displayPingTex = pingTex[0];
                    } else {
                        displayPingTex = unreachableTexID;
                    }

                    if (displayPingTex != 0) {
                        float iconDisplaySize = 28.0f;
                        float iconX = rightEdge - iconDisplaySize - 4;
                        ImGui::SetCursorPos(ImVec2(iconX, startPos.y + 8));
                        ImGui::Image((ImTextureID)(intptr_t)displayPingTex,
                                     ImVec2(iconDisplaySize, iconDisplaySize * 0.8f));
                        ImGui::SetCursorPos(ImVec2(iconX, startPos.y + 8));
                        ImGui::InvisibleButton("##ping", ImVec2(iconDisplaySize, iconDisplaySize * 0.8f));
                        if (ImGui::IsItemHovered() && servers[i].pinged && servers[i].latencyMs >= 0) {
                            char tooltip[32];
                            snprintf(tooltip, sizeof(tooltip), "%dms", servers[i].latencyMs);
                            ImVec2 mousePos = ImGui::GetMousePos();
                            ImGui::SetNextWindowPos(ImVec2(mousePos.x, mousePos.y - 15));
                            ImGui::SetTooltip("%s", tooltip);
                        }
                    }
                }

                if (motdRed) {
                    ImGui::SetCursorPos(ImVec2(textLeft, startPos.y + 28));
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", motdLine1.c_str());
                } else {
                    ImGui::SetCursorPos(ImVec2(textLeft, startPos.y + 28));
                    ImVec2 screenPos = ImGui::GetCursorScreenPos();
                    drawMcText(screenPos.x, screenPos.y, motdLine1, IM_COL32(128, 128, 128, 255));
                }

                if (!motdLine2.empty()) {
                    ImGui::SetCursorPos(ImVec2(textLeft, startPos.y + 46));
                    ImVec2 screenPos = ImGui::GetCursorScreenPos();
                    drawMcText(screenPos.x, screenPos.y, motdLine2, IM_COL32(128, 128, 128, 255));
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::EndChild();

    // 底部按钮
    float btnW = 120.0f;
    float btnGap = 10.0f;
    float totalW = btnW * 5 + btnGap * 4;
    float startX = w * 0.5f - totalW * 0.5f;
    float btnY = h - 70.0f;

    bool noSel = (selectedServer < 0);
    ImGui::SetCursorPos(ImVec2(startX, btnY));
    if (McButton("\xe8\xbf\x9e\xe6\x8e\xa5\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8", ImVec2(btnW, 50), !noSel)) {
        if (selectedServer >= 0 && selectedServer < (int)servers.size())
            connectToServer(servers[selectedServer]);
    }

    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap), btnY));
    if (McButton("\xe6\xb7\xbb\xe5\x8a\xa0\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8", ImVec2(btnW, 50))) {
        memset(addServerName, 0, sizeof(addServerName));
        memset(addServerIp, 0, sizeof(addServerIp));
        strncpy(addServerPort, "25565", sizeof(addServerPort) - 1);
        addServerPort[sizeof(addServerPort) - 1] = '\0';
        editingServerIndex = -1;
        showingAddServer = true;
    }

    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap) * 2, btnY));
    if (McButton("\xe7\xbc\x96\xe8\xbe\x91", ImVec2(btnW, 50), !noSel)) {
        if (selectedServer >= 0 && selectedServer < (int)servers.size()) {
            const auto& s = servers[selectedServer];
            strncpy(addServerName, s.name.c_str(), sizeof(addServerName) - 1);
            addServerName[sizeof(addServerName) - 1] = '\0';
            strncpy(addServerIp, s.ip.c_str(), sizeof(addServerIp) - 1);
            addServerIp[sizeof(addServerIp) - 1] = '\0';
            snprintf(addServerPort, sizeof(addServerPort), "%d", s.port);
            editingServerIndex = selectedServer;
            showingAddServer = true;
        }
    }

    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap) * 3, btnY));
    if (McButton("\xe5\x88\xa0\xe9\x99\xa4", ImVec2(btnW, 50), !noSel)) {
        if (selectedServer >= 0 && selectedServer < (int)servers.size()) {
            servers.erase(servers.begin() + selectedServer);
            selectedServer = -1;
            saveServerList();
        }
    }

    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap) * 4, btnY));
    if (McButton("\xe5\x8f\x96\xe6\xb6\x88", ImVec2(btnW, 50))) {
        if (backCallback) backCallback();
    }

    ImGui::End();
}

void MultiplayerScreen::renderAddServer() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("AddServer", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    float titleW = 200.0f;
    ImGui::SetCursorPos(ImVec2(w * 0.5f - titleW * 0.5f, h * 0.12f));
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", editingServerIndex >= 0 ? "编辑服务器" : "添加服务器");

    ImGui::SetCursorPosX(w * 0.5f - 200.0f);
    ImGui::BeginGroup();
    float inputW = 400.0f;
    ImGui::PushItemWidth(inputW);
    ImGui::Text("名称");
    ImGui::InputText("##name", addServerName, sizeof(addServerName));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);
    ImGui::Text("地址");
    ImGui::InputText("##ip", addServerIp, sizeof(addServerIp));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);
    ImGui::Text("端口");
    ImGui::InputText("##port", addServerPort, sizeof(addServerPort));
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    float btnWForm = 140.0f;
    float btnGapForm = 30.0f;
    float btnFormY = ImGui::GetCursorPosY() + 20.0f;
    float btnFormStartX = w * 0.5f - (btnWForm * 2 + btnGapForm) * 0.5f;

    ImGui::SetCursorPos(ImVec2(btnFormStartX, btnFormY));
    if (McButton("\xe4\xbf\x9d\xe5\xad\x98", ImVec2(btnWForm, 44)) && strlen(addServerName) > 0 && strlen(addServerIp) > 0) {
        ServerInfo server;
        server.name = addServerName;
        server.ip = addServerIp;
        int port = 25565;
        try { port = std::stoi(addServerPort); } catch (...) { port = 25565; }
        if (port <= 0) port = 25565;
        if (port > 65535) port = 25565;
        server.port = port;
        if (editingServerIndex >= 0 && editingServerIndex < (int)servers.size()) {
            servers[editingServerIndex] = server;
            selectedServer = editingServerIndex;
        } else {
            servers.push_back(server);
            selectedServer = (int)servers.size() - 1;
        }
        saveServerList();
        showingAddServer = false;
    }

    ImGui::SetCursorPos(ImVec2(btnFormStartX + btnWForm + btnGapForm, btnFormY));
    if (McButton("\xe5\x8f\x96\xe6\xb6\x88", ImVec2(btnWForm, 44))) {
        showingAddServer = false;
    }

    ImGui::End();
}

void MultiplayerScreen::connectToServer(const ServerInfo& server) {
    if (connectCallback) {
        std::string address = server.ip + ":" + std::to_string(server.port);
        connectCallback(server.ip, server.port, address);
    }
}

void MultiplayerScreen::pingServer(int index) {
    if (index < 0 || index >= (int)servers.size()) return;
    if (servers[index].pinging) return;

    servers[index].pinging = true;
    servers[index].pingFailed = false;
    std::string ip = servers[index].ip;
    int port = servers[index].port;
    int protocolVersion = VersionManager::getInstance().getProtocolVersion();
    if (protocolVersion == 0) protocolVersion = 758;

    std::thread([this, index, ip, port, protocolVersion]() {
        PingResult result = ServerList::ping(ip, port, protocolVersion);
        if (index < (int)servers.size()) {
            servers[index].pinging = false;
            if (result.success) {
                servers[index].motd = result.motd;
                servers[index].onlinePlayers = result.onlinePlayers;
                servers[index].maxPlayers = result.maxPlayers;
                servers[index].latencyMs = result.latencyMs;
                servers[index].pinged = true;
                if (!result.faviconPng.empty()) {
                    servers[index].faviconPngData = std::move(result.faviconPng);
                }
                LOGI("Server %d pinged: %s (%d/%d) %dms",
                     index, result.motd.c_str(),
                     result.onlinePlayers, result.maxPlayers, result.latencyMs);
            } else {
                servers[index].pinged = false;
                servers[index].latencyMs = -1;
                servers[index].pingFailed = true;
                LOGI("Server %d ping failed", index);
            }
        }
    }).detach();
}

void MultiplayerScreen::pingAllServers() {
    for (int i = 0; i < (int)servers.size(); i++) {
        pingServer(i);
    }
}

void MultiplayerScreen::loadServerList() {
    servers.clear();
    std::ifstream file(SERVERS_FILE_PATH);
    if (!file.is_open()) {
        LOGI("No server list file found, starting fresh");
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string name, ip, portStr;
        if (!std::getline(ss, name, '\t')) continue;
        if (!std::getline(ss, ip, '\t')) continue;
        if (!std::getline(ss, portStr, '\t')) continue;
        int port = 25565;
        try { port = std::stoi(portStr); } catch (...) { port = 25565; }
        servers.push_back({name, ip, port});
    }
    file.close();
}

void MultiplayerScreen::saveServerList() {
    std::ofstream file(SERVERS_FILE_PATH, std::ios::trunc);
    if (!file.is_open()) {
        LOGE("Failed to save server list");
        return;
    }
    for (const auto& s : servers) {
        file << s.name << '\t' << s.ip << '\t' << s.port << '\n';
    }
    file.close();
    LOGI("Saved %zu servers", servers.size());
}
