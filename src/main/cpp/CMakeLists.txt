cmake_minimum_required(VERSION 3.18.1)
project("minecraftclient")

find_library(log-lib log)
find_library(android-lib android)
find_library(z-lib z)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

# GLM（header-only）
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/glm)
message(STATUS "Using local GLM from: ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/glm")

# ============================================================
# 协议版本列表：在此添加新版本号即可自动生成对应的 libmc_<版本>.so
# ============================================================
set(SUPPORTED_VERSIONS 758 762)

# ============================================================
# protocolCraft 源文件（从 protocolCraft/CMakeLists.txt 同步）
# ============================================================
set(PROTOCOLCRAFT_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/BasePacket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/BinaryReadWrite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/NetworkType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/PacketFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Chat/Chat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/CommandNode/BrigadierProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Components/DataComponents.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Components/Subtypes/ConsumeEffects/ConsumeEffect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Components/Subtypes/ItemAttributeModifiers/Display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Debug/DebugSubscription.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/NBT/NBT.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/NBT/Tag.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Particles/Particle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Recipes/Recipe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Recipes/RecipeDisplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Recipes/SlotDisplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Vibrations/PositionSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Types/Waypoints/TrackedWaypoint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Utilities/GZip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Utilities/Json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/src/Utilities/Plugins/PluginLoader.cpp
)

# ============================================================
# 应用源文件
# ============================================================
set(APP_SOURCES
    ClientEngine/ClientEngine.cpp
    ClientEngine/GameEngine.cpp
    NetworkManager/handlers/LoginHandler.cpp
    NetworkManager/handlers/WorldHandler.cpp
    NetworkManager/handlers/EntityHandler.cpp
    NetworkManager/handlers/InventoryHandler.cpp
    NetworkManager/handlers/ChatHandler.cpp
    NetworkManager/handlers/PlayerStatusHandler.cpp
    Compression.cpp
    NetworkManager/NetworkManager.cpp
    native-lib.cpp
    AESEncrypter.cpp
    AESEncrypter_armv8.cpp
    ChunkManager.cpp
    ChunkParser.cpp
    Renderer/VulkanRenderer.cpp
    Renderer/GLRenderer.cpp
    Renderer/ChunkMeshScheduler.cpp
    Renderer/ChunkOcclusionCuller.cpp
    Light.cpp
    MeshGenerator.cpp
    CrackOverlayMesh.cpp
    PanoramaView.cpp
    SkyRenderer.cpp
    TextureLoader.cpp
    BlockRegistry.cpp
    Camera.cpp
    Collision.cpp
    TextureAtlas.cpp
    BlockIconRasterizer.cpp
    BiomeColorManager.cpp
    gui/GameUI.cpp
    ServerList.cpp
    MusicManager.cpp
    gui/ScreenManager.cpp
    gui/TitleScreen.cpp
    gui/MultiplayerScreen.cpp
    gui/ConnectingScreen.cpp
    gui/DisconnectedScreen.cpp
    gui/HudScreen.cpp
    gui/DeathScreen.cpp
    gui/InventoryScreen.cpp
    gui/PauseScreen.cpp
    gui/ChatScreen.cpp
    ResourcepackManager.cpp
    PlayerInventory.cpp
    Raycast.cpp
    Entity.cpp
    EntityManager.cpp
    EntityRenderer.cpp
    3rdparty/miniz/miniz.c
    3rdparty/minivorbis/minivorbis.c
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/backends/imgui_impl_opengl3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/backends/imgui_impl_vulkan.cpp
)

# ============================================================
# 为每个协议版本生成独立的 .so
# ============================================================
foreach(PVER ${SUPPORTED_VERSIONS})
    add_library(mc_${PVER} SHARED ${APP_SOURCES} ${PROTOCOLCRAFT_SRC})

    target_compile_definitions(mc_${PVER} PRIVATE PROTOCOL_VERSION=${PVER})

    target_include_directories(mc_${PVER} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/protocolCraft/include
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # 仅为 AESEncrypter_armv8.cpp 启用 ARMv8 Crypto Extensions
    # 主文件 AESEncrypter.cpp 保持默认编译标志，通过函数指针调用
    set_source_files_properties(AESEncrypter_armv8.cpp PROPERTIES
        COMPILE_FLAGS "-march=armv8-a+crypto"
    )

    target_link_libraries(mc_${PVER}
        ${log-lib}
        ${android-lib}
        ${z-lib}
        ${egl-lib}
        ${gles-lib}
        vulkan
        OpenSLES
    )

    set_target_properties(mc_${PVER} PROPERTIES OUTPUT_NAME "mc_${PVER}")

    message(STATUS "Configured protocol version ${PVER} -> libmc_${PVER}.so")
endforeach()
