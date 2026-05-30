cmake_minimum_required(VERSION 3.18.1)
project("minecraftclient")

find_library(log-lib log)
find_library(android-lib android)
find_library(z-lib z)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

# 添加 GLM 头文件路径（header-only 库）
# GLM 结构：CMAKE_CURRENT_SOURCE_DIR/glm/glm/glm.hpp
# 所以需要包含 CMAKE_CURRENT_SOURCE_DIR/glm
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/glm)
message(STATUS "Using local GLM from: ${CMAKE_CURRENT_SOURCE_DIR}/glm")

# 添加 ProtocolCraft 子目录
set(PROTOCOLCRAFT_STATIC ON CACHE BOOL "Build protocolCraft as static library" FORCE)
set(PROTOCOL_VERSION 758 CACHE STRING "Minecraft protocol version (758 = 1.18.2)" FORCE)

# 设置 Botcraft 输出目录，防止协议库输出到根目录 /lib/
set(BOTCRAFT_OUTPUT_DIR ${CMAKE_BINARY_DIR})
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft ${CMAKE_BINARY_DIR}/protocolCraft)

set(SOURCES
    ClientEngine.cpp
    Compression.cpp
    NetworkManager.cpp
    native-lib.cpp
    utils.cpp
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
    BiomeColorManager.cpp
    GameUI.cpp
    ItemTextureManager.cpp
    PlayerInventory.cpp
    miniz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

add_library(minecraftclient SHARED ${SOURCES})

target_link_libraries(minecraftclient
    ${log-lib}
    ${android-lib}
    ${z-lib}
    ${egl-lib}
    ${gles-lib}
    vulkan
    protocolCraft  # 链接 ProtocolCraft 库
)

target_include_directories(minecraftclient PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/protocolCraft/include
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)
