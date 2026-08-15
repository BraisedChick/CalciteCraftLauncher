#pragma once
#include <vector>
#include <functional>
#include "imgui.h"
#include "PlayerInventory.h"
#include "ClientEngine/GameEngine.h"

// 拖拽状态管理，独立于具体界面
class DragHelper {
public:
    DragHelper();

    // 重置所有状态
    void reset();

    // 处理拖拽输入（应在每个渲染帧调用）
    // 参数：
    //   io - ImGui IO
    //   inv - 玩家物品栏
    //   engine - 游戏引擎（用于发送网络包）
    //   getSlotAtMouse - 返回鼠标下槽位索引的回调（界面自定义）
    //   containerId - 当前打开的容器ID（0表示玩家背包）
    void processInput(ImGuiIO& io, PlayerInventory& inv, GameEngine* engine,
                      const std::function<int()>& getSlotAtMouse,
                      int containerId);

    // 渲染拖拽预测（高亮和剩余物品）
    // 参数：
    //   io - ImGui IO
    //   inv - 玩家物品栏
    //   renderItem - 渲染单个物品的回调（界面提供）
    //   getSlotScreenPos - 根据槽位索引获取屏幕坐标的回调
    //   containerId - 当前容器ID
    //   invSlotSize - 槽位像素大小（统一为50）
    void renderPrediction(ImGuiIO& io, PlayerInventory& inv,
                          const std::function<void(float, float, const InvSlot&)>& renderItem,
                          const std::function<void(int, float&, float&)>& getSlotScreenPos,
                          int containerId,
                          float invSlotSize = 50.0f);

    // 检查是否正在拖拽
    bool isDragging() const { return m_isDragging; }

    void startDrag(int slot);
private:
    bool m_isDragging;
    int m_quickcraftStatus;
    int m_quickcraftStartSlot;
    std::vector<int> m_quickcraftSlots;


};