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
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/glm)
message(STATUS "Using local GLM from: ${CMAKE_CURRENT_SOURCE_DIR}/glm")

# ============================================================
# 协议版本列表：在此添加新版本号即可自动生成对应的 libmc_<版本>.so
# ============================================================
set(SUPPORTED_VERSIONS 758)

# ============================================================
# protocolCraft 源文件（从 protocolCraft/CMakeLists.txt 同步）
# ============================================================
set(PROTOCOLCRAFT_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/BasePacket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/BinaryReadWrite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/NetworkType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/PacketFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Chat/Chat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/CommandNode/BrigadierProperty.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Components/DataComponents.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Components/Subtypes/ConsumeEffects/ConsumeEffect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Components/Subtypes/ItemAttributeModifiers/Display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Debug/DebugSubscription.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/NBT/NBT.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/NBT/Tag.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Particles/Particle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Recipes/Recipe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Recipes/RecipeDisplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Recipes/SlotDisplay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Vibrations/PositionSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Types/Waypoints/TrackedWaypoint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Utilities/GZip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Utilities/Json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/src/Utilities/Plugins/PluginLoader.cpp
)

# ============================================================
# 应用源文件
# ============================================================
set(APP_SOURCES
    ClientEngine.cpp
    Compression.cpp
    NetworkManager.cpp
    native-lib.cpp
    utils.cpp
    AESEncrypter.cpp
    ChunkManager.cpp
    ChunkParser.cpp
    MinecraftVersion.cpp
    VulkanRenderer.cpp
    GLRenderer.cpp
    MeshGenerator.cpp
    TextureLoader.cpp
    BlockRegistry.cpp
    CameraController.cpp
    Collision.cpp
    TextureAtlas.cpp
    BiomeColorManager.cpp
    GameUI.cpp
    ResourcepackManager.cpp
    PlayerInventory.cpp
    Raycast.cpp
    miniz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

# ============================================================
# 为每个协议版本生成独立的 .so
# ============================================================
foreach(PVER ${SUPPORTED_VERSIONS})
    add_library(mc_${PVER} SHARED ${APP_SOURCES} ${PROTOCOLCRAFT_SRC})

    target_compile_definitions(mc_${PVER} PRIVATE PROTOCOL_VERSION=${PVER})

    target_include_directories(mc_${PVER} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/include
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
    )

    target_link_libraries(mc_${PVER}
        ${log-lib}
        ${android-lib}
        ${z-lib}
        ${egl-lib}
        ${gles-lib}
        vulkan
    )

    set_target_properties(mc_${PVER} PROPERTIES OUTPUT_NAME "mc_${PVER}")

    message(STATUS "Configured protocol version ${PVER} -> libmc_${PVER}.so")
endforeach()
