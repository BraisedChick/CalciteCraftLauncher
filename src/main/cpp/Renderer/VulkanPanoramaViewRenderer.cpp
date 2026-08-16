#include "VulkanPanoramaViewRenderer.h"
#include <android/log.h>
#include <cstring>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include "3rdparty/vk_mem_alloc.h"
#include "TextureLoader.h"
#define LOG_TAG "VulkanPanorama"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
extern AAssetManager* g_assetManager;
// 构造函数
VulkanPanoramaViewRenderer::VulkanPanoramaViewRenderer(VkDevice dev,
                                                       VmaAllocator alloc,
                                                       VkPhysicalDevice physDev,
                                                       VkRenderPass rp,
                                                       VkCommandPool cmdPool,
                                                       VkQueue queue)
        : device(dev),
          allocator(alloc),
          physicalDevice(physDev),
          renderPass(rp),
          commandPool(cmdPool),
          graphicsQueue(queue) {
}

VulkanPanoramaViewRenderer::~VulkanPanoramaViewRenderer() {
    destroy();
}

void VulkanPanoramaViewRenderer::destroy() {
    if (!ready) return;

    if (panoramaPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, panoramaPipeline, nullptr);
        panoramaPipeline = VK_NULL_HANDLE;
    }
    if (panoramaPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, panoramaPipelineLayout, nullptr);
        panoramaPipelineLayout = VK_NULL_HANDLE;
    }
    if (panoramaDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, panoramaDescriptorPool, nullptr);
        panoramaDescriptorPool = VK_NULL_HANDLE;
    }
    panoramaDescriptorSet = VK_NULL_HANDLE;  // 随池销毁
    if (panoramaSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, panoramaSetLayout, nullptr);
        panoramaSetLayout = VK_NULL_HANDLE;
    }
    if (panoramaVertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, panoramaVertexBuffer, panoramaVertexMemory);
        panoramaVertexBuffer = VK_NULL_HANDLE;
        panoramaVertexMemory = VK_NULL_HANDLE;
    }
    if (panoramaIndexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, panoramaIndexBuffer, panoramaIndexMemory);
        panoramaIndexBuffer = VK_NULL_HANDLE;
        panoramaIndexMemory = VK_NULL_HANDLE;
    }
    if (panoramaSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, panoramaSampler, nullptr);
        panoramaSampler = VK_NULL_HANDLE;
    }
    if (panoramaImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, panoramaImageView, nullptr);
        panoramaImageView = VK_NULL_HANDLE;
    }
    if (panoramaImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, panoramaImage, panoramaImageMemory);
        panoramaImage = VK_NULL_HANDLE;
        panoramaImageMemory = VK_NULL_HANDLE;
    }
    ready = false;
}

void VulkanPanoramaViewRenderer::updateExtent(uint32_t width, uint32_t height) {
    extentWidth = width;
    extentHeight = height;
}

bool VulkanPanoramaViewRenderer::recreatePipelines(VkRenderPass newRenderPass) {
    if (!ready) return false;
    renderPass = newRenderPass;

    // 销毁旧管线
    if (panoramaPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, panoramaPipeline, nullptr);
        panoramaPipeline = VK_NULL_HANDLE;
    }
    // 管线布局和描述符不变，只需重新创建管线
    return createPipelines();
}

// ========== 辅助函数（与 VulkanRenderer 中相同逻辑，但此处独立） ==========

bool VulkanPanoramaViewRenderer::createHostBuffer(VkBufferUsageFlags usage,
                                                  const void* src, VkDeviceSize size,
                                                  VkBuffer& outBuf, VmaAllocation& outAlloc) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaInfo{};
    vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vmaInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    if (vmaCreateBuffer(allocator, &bufferInfo, &vmaInfo, &outBuf, &outAlloc, nullptr) != VK_SUCCESS) {
        return false;
    }
    if (src && vmaCopyMemoryToAllocation(allocator, src, outAlloc, 0, size) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, outBuf, outAlloc);
        outBuf = VK_NULL_HANDLE;
        outAlloc = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanPanoramaViewRenderer::createDeviceImage(uint32_t width, uint32_t height, uint32_t layers,
                                                   VkImageCreateFlags flags,
                                                   VkImage& outImage, VmaAllocation& outAlloc) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = flags;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layers;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaInfo{};
    vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
    return vmaCreateImage(allocator, &imageInfo, &vmaInfo, &outImage, &outAlloc, nullptr) == VK_SUCCESS;
}

bool VulkanPanoramaViewRenderer::createRgbaImageView(VkImage image, VkImageViewType type,
                                                     uint32_t layers, VkImageView& outView) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = type;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
    return vkCreateImageView(device, &viewInfo, nullptr, &outView) == VK_SUCCESS;
}

VkCommandBuffer VulkanPanoramaViewRenderer::beginOneTimeCommands() {
    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool = commandPool;
    cbAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cbAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void VulkanPanoramaViewRenderer::endOneTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

bool VulkanPanoramaViewRenderer::uploadPixelsToImage(const uint8_t* pixels, uint32_t width,
                                                     uint32_t height, uint32_t layers,
                                                     VkImage image) {
    VkDeviceSize layerBytes = (VkDeviceSize)width * height * 4;
    VkBuffer stagingBuffer;
    VmaAllocation stagingMemory;
    if (!createHostBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels, layerBytes * layers,
                          stagingBuffer, stagingMemory)) {
        return false;
    }

    VkCommandBuffer cmd = beginOneTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    std::vector<VkBufferImageCopy> regions(layers);
    for (uint32_t i = 0; i < layers; i++) {
        regions[i] = {};
        regions[i].bufferOffset = layerBytes * i;
        regions[i].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, i, 1 };
        regions[i].imageExtent = { width, height, 1 };
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           layers, regions.data());

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    endOneTimeCommands(cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingMemory);
    return true;
}

// ========== 全景图初始化 ==========
bool VulkanPanoramaViewRenderer::init() {
    if (ready) return true;

    // 1. 加载6个面像素
    PanoramaView::FacePixels faces[6];
    int loadedCount = panoramaView.loadFacePixels(faces);
    LOGI("Panorama faces loaded: %d/6", loadedCount);

    int faceSize = 16;
    for (int i = 0; i < 6; i++) {
        if (faces[i].loaded) { faceSize = faces[i].width; break; }
    }
    VkDeviceSize faceBytes = (VkDeviceSize)faceSize * faceSize * 4;
    std::vector<uint8_t> allPixels((size_t)faceBytes * 6);
    for (int i = 0; i < 6; i++) {
        uint8_t* dst = allPixels.data() + (size_t)faceBytes * i;
        const PanoramaView::FacePixels& f = faces[i];
        if (f.width == faceSize && f.height == faceSize) {
            memcpy(dst, f.rgba.data(), (size_t)faceBytes);
        } else {
            // 重采样
            for (int y = 0; y < faceSize; y++) {
                int sy = y * f.height / faceSize;
                for (int x = 0; x < faceSize; x++) {
                    int sx = x * f.width / faceSize;
                    memcpy(dst + ((size_t)y * faceSize + x) * 4,
                           f.rgba.data() + ((size_t)sy * f.width + sx) * 4, 4);
                }
            }
        }
    }

    // 2. 创建 cubemap 图像
    if (!createDeviceImage((uint32_t)faceSize, (uint32_t)faceSize, 6,
                           VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                           panoramaImage, panoramaImageMemory)) {
        LOGE("Failed to create panorama cubemap image");
        destroyResources();
        return false;
    }
    if (!uploadPixelsToImage(allPixels.data(), (uint32_t)faceSize, (uint32_t)faceSize, 6, panoramaImage)) {
        LOGE("Failed to upload panorama pixels");
        destroyResources();
        return false;
    }
    if (!createRgbaImageView(panoramaImage, VK_IMAGE_VIEW_TYPE_CUBE, 6, panoramaImageView)) {
        LOGE("Failed to create panorama image view");
        destroyResources();
        return false;
    }

    // 3. 采样器
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &panoramaSampler) != VK_SUCCESS) {
        LOGE("Failed to create panorama sampler");
        destroyResources();
        return false;
    }

    // 4. 顶点/索引缓冲
    size_t cubeVertFloats = 0, cubeIdxCount = 0;
    const float* cubeVerts = PanoramaView::cubeVertices(cubeVertFloats);
    const uint16_t* cubeIdx = PanoramaView::cubeIndices(cubeIdxCount);
    panoramaIndexCount = (uint32_t)cubeIdxCount;

    if (!createHostBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, cubeVerts,
                          cubeVertFloats * sizeof(float), panoramaVertexBuffer, panoramaVertexMemory) ||
        !createHostBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, cubeIdx,
                          cubeIdxCount * sizeof(uint16_t), panoramaIndexBuffer, panoramaIndexMemory)) {
        LOGE("Failed to create panorama geometry buffers");
        destroyResources();
        return false;
    }

    // 5. 描述符
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &panoramaSetLayout) != VK_SUCCESS) {
        LOGE("Failed to create panorama set layout");
        destroyResources();
        return false;
    }

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &panoramaDescriptorPool) != VK_SUCCESS) {
        LOGE("Failed to create panorama descriptor pool");
        destroyResources();
        return false;
    }

    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = panoramaDescriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &panoramaSetLayout;
    if (vkAllocateDescriptorSets(device, &dsAlloc, &panoramaDescriptorSet) != VK_SUCCESS) {
        LOGE("Failed to allocate panorama descriptor set");
        destroyResources();
        return false;
    }

    VkDescriptorImageInfo imageDescInfo{};
    imageDescInfo.sampler = panoramaSampler;
    imageDescInfo.imageView = panoramaImageView;
    imageDescInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = panoramaDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // 6. 管线（依赖当前 renderPass）
    if (!createPipelines()) {
        LOGE("Failed to create panorama pipelines");
        destroyResources();
        return false;
    }

    ready = true;
    LOGI("Panorama initialized, faceSize=%d", faceSize);
    return true;
}

bool VulkanPanoramaViewRenderer::createPipelines() {
    auto readFile = [&](const std::string& filename) -> std::vector<char> {
        if (!g_assetManager) {
            LOGE("Asset manager not set!");
            return {};
        }
        AAsset* asset = AAssetManager_open(g_assetManager, filename.c_str(), AASSET_MODE_BUFFER);
        if (!asset) {
            LOGE("Failed to open asset: %s", filename.c_str());
            return {};
        }
        off_t length = AAsset_getLength(asset);
        const void* buffer = AAsset_getBuffer(asset);
        std::vector<char> data((const char*)buffer, (const char*)buffer + length);
        AAsset_close(asset);
        return data;
    };

    auto vertCode = readFile("shaders/panorama_vert.spv");
    auto fragCode = readFile("shaders/panorama_frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        LOGE("Panorama SPIR-V shaders missing");
        return false;
    }

    auto createShaderModule = [&](const std::vector<char>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        return module;
    };

    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // 管线布局
    if (panoramaPipelineLayout == VK_NULL_HANDLE) {
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(glm::mat4);

        VkPipelineLayoutCreateInfo plInfo{};
        plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &panoramaSetLayout;
        plInfo.pushConstantRangeCount = 1;
        plInfo.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(device, &plInfo, nullptr, &panoramaPipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(device, vertModule, nullptr);
            vkDestroyShaderModule(device, fragModule, nullptr);
            return false;
        }
    }

    // 创建管线
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = 3 * sizeof(float);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrDesc{};
    attrDesc.binding = 0;
    attrDesc.location = 0;
    attrDesc.format = VK_FORMAT_R32G32B32_SFLOAT;
    attrDesc.offset = 0;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attrDesc;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = panoramaPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &panoramaPipeline);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        LOGE("Failed to create panorama pipeline");
        return false;
    }
    return true;
}

void VulkanPanoramaViewRenderer::render(VkCommandBuffer cmd) {
    if (!ready || extentWidth == 0 || extentHeight == 0) return;

    // 计算 MVP（带 Y 翻转）
    glm::mat4 mvp = panoramaView.computeMVP((int)extentWidth, (int)extentHeight);
    glm::mat4 yFlip(1.0f);
    yFlip[1][1] = -1.0f;
    mvp = yFlip * mvp;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, panoramaPipeline);

    VkViewport viewport{};
    viewport.width = (float)extentWidth;
    viewport.height = (float)extentHeight;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, {extentWidth, extentHeight} };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdPushConstants(cmd, panoramaPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, panoramaPipelineLayout,
                            0, 1, &panoramaDescriptorSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &panoramaVertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, panoramaIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, panoramaIndexCount, 1, 0, 0, 0);
}

void VulkanPanoramaViewRenderer::destroyResources() {
    // 与 destroy() 相同，但为了内部调用单独封装
    destroy();
}