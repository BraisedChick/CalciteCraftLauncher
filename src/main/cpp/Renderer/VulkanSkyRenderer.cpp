#include "VulkanSkyRenderer.h"
#include <android/log.h>
#include <vector>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include "Camera.h"
#include "SkyRenderer.h"
#include "TextureLoader.h"
#include "3rdparty/vk_mem_alloc.h"

#define LOG_TAG "VulkanSkyRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern AAssetManager* g_assetManager;  // 由 VulkanRenderer 设置

struct SkyColorPushConstant {
    glm::mat4 mvp;
    glm::vec4 color;
};
VulkanSkyRenderer::VulkanSkyRenderer(VkDevice device, VmaAllocator allocator,
                                     VkPhysicalDevice physicalDevice, VkRenderPass renderPass,
                                     VkCommandPool commandPool, VkQueue graphicsQueue)
        : device(device), allocator(allocator), physicalDevice(physicalDevice),
          renderPass(renderPass), commandPool(commandPool), graphicsQueue(graphicsQueue) {
    // 所有成员变量都有类内初始化器，无需额外赋值
}
// ---------- 辅助函数（与之前相同） ----------
static std::vector<char> readFile(const std::string& filename) {
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
}

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

bool VulkanSkyRenderer::createHostBuffer(VkBufferUsageFlags usage, const void* src, VkDeviceSize size,
                                         VkBuffer& outBuf, VmaAllocation& outAlloc) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaInfo{};
    vmaInfo.usage = VMA_MEMORY_USAGE_AUTO;
    vmaInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    if (vmaCreateBuffer(allocator, &bufferInfo, &vmaInfo, &outBuf, &outAlloc, nullptr) != VK_SUCCESS)
        return false;
    if (src && vmaCopyMemoryToAllocation(allocator, src, outAlloc, 0, size) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, outBuf, outAlloc);
        outBuf = VK_NULL_HANDLE;
        outAlloc = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanSkyRenderer::createDeviceImage(uint32_t width, uint32_t height, uint32_t layers,
                                          VkImageCreateFlags flags, VkImage& outImage, VmaAllocation& outAlloc) {
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

bool VulkanSkyRenderer::createRgbaImageView(VkImage image, VkImageViewType type, uint32_t layers, VkImageView& outView) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = type;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
    return vkCreateImageView(device, &viewInfo, nullptr, &outView) == VK_SUCCESS;
}

VkCommandBuffer VulkanSkyRenderer::beginOneTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void VulkanSkyRenderer::endOneTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

bool VulkanSkyRenderer::uploadPixelsToImage(const uint8_t* pixels, uint32_t width, uint32_t height,
                                            uint32_t layers, VkImage image) {
    VkDeviceSize layerBytes = (VkDeviceSize)width * height * 4;
    VkBuffer stagingBuffer;
    VmaAllocation stagingMemory;
    if (!createHostBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, pixels, layerBytes * layers,
                          stagingBuffer, stagingMemory))
        return false;

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

bool VulkanSkyRenderer::loadCelestialTexture(const std::string& path, VkImage& outImage,
                                             VmaAllocation& outAlloc, VkImageView& outView) {
    TextureData tex = TextureLoader::loadImage(path);
    if (!tex.data) {
        LOGE("Failed to load texture: %s", path.c_str());
        return false;
    }
    if (!createDeviceImage(tex.width, tex.height, 1, 0, outImage, outAlloc) ||
        !uploadPixelsToImage(tex.data, tex.width, tex.height, 1, outImage) ||
        !createRgbaImageView(outImage, VK_IMAGE_VIEW_TYPE_2D, 1, outView)) {
        LOGE("Failed to upload texture: %s", path.c_str());
        if (outImage) vmaDestroyImage(allocator, outImage, outAlloc);
        if (outView) vkDestroyImageView(device, outView, nullptr);
        return false;
    }
    LOGI("Loaded celestial texture: %s (%dx%d)", path.c_str(), tex.width, tex.height);
    return true;
}

// ---------- 初始化 ----------
bool VulkanSkyRenderer::init() {
    if (initialized) return true;

    // 1. 加载太阳纹理
    if (!loadCelestialTexture("environment/celestial/sun.png", sunTextureImage, sunTextureMemory, sunTextureView)) {
        LOGE("Failed to load sun texture");
        return false;
    }

    // 2. 加载 8 个月相纹理（失败时用 sunTextureView 作为后备，避免空指针）
    for (int phase = 0; phase < 8; ++phase) {
        const char* path = SkyRenderer::getMoonPhasePath(phase);
        if (!loadCelestialTexture(path, moonTextureImages[phase], moonTextureMemories[phase], moonTextureViews[phase])) {
            LOGE("Failed to load moon phase %d, using sun as fallback", phase);
            // 直接复制 sun 的 view 引用（注意：同一个 view，但只读，安全）
            moonTextureViews[phase] = sunTextureView;
            // 图像和内存仍为空，但 view 非空即可
        }
    }

    // 3. 创建采样器
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &skyTextureSampler) != VK_SUCCESS) {
        LOGE("Failed to create sky texture sampler");
        return false;
    }

    // 4. 创建描述符集布局
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &skyTextureSetLayout) != VK_SUCCESS) {
        LOGE("Failed to create sky texture set layout");
        return false;
    }

    // 5. 创建描述符池（支持 9 个 set：1 太阳 + 8 月亮）
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 9;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &skyTextureDescriptorPool) != VK_SUCCESS) {
        LOGE("Failed to create sky texture descriptor pool");
        return false;
    }

    // 6. 分配 9 个描述符集
    VkDescriptorSetLayout layouts[9];
    for (int i = 0; i < 9; ++i) layouts[i] = skyTextureSetLayout;
    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = skyTextureDescriptorPool;
    dsAlloc.descriptorSetCount = 9;
    dsAlloc.pSetLayouts = layouts;
    VkDescriptorSet sets[9];
    if (vkAllocateDescriptorSets(device, &dsAlloc, sets) != VK_SUCCESS) {
        LOGE("Failed to allocate sky descriptor sets");
        return false;
    }
    skyDescriptorSetSun = sets[0];
    for (int i = 0; i < 8; ++i) skyDescriptorSetMoon[i] = sets[1 + i];

// 7. 填充所有描述符集（初始化一次，永不改变）
    auto writeSet = [&](VkDescriptorSet set, VkImageView view) {
        VkDescriptorImageInfo info{ skyTextureSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &info;
        return write;
    };

    VkWriteDescriptorSet writes[9];
    writes[0] = writeSet(skyDescriptorSetSun, sunTextureView);
    for (int i = 0; i < 8; ++i) {
        writes[1 + i] = writeSet(skyDescriptorSetMoon[i], moonTextureViews[i]);
    }
    vkUpdateDescriptorSets(device, 9, writes, 0, nullptr);
    // 7. 编译着色器
    auto vertCode = readFile("shaders/sky_color_vert.spv");
    auto fragCode = readFile("shaders/sky_color_frag.spv");
    auto texVertCode = readFile("shaders/sky_texture_vert.spv");
    auto texFragCode = readFile("shaders/sky_texture_frag.spv");
    if (vertCode.empty() || fragCode.empty() || texVertCode.empty() || texFragCode.empty()) {
        LOGE("Sky shaders missing");
        return false;
    }
    VkShaderModule vertModule = createShaderModule(device, vertCode);
    VkShaderModule fragModule = createShaderModule(device, fragCode);
    VkShaderModule texVertModule = createShaderModule(device, texVertCode);
    VkShaderModule texFragModule = createShaderModule(device, texFragCode);
    if (!vertModule || !fragModule || !texVertModule || !texFragModule) {
        LOGE("Failed to create shader modules");
        // 清理已创建的部分...
        return false;
    }

    // 8. 创建管线布局
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(SkyColorPushConstant);

    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device, &plInfo, nullptr, &skyColorPipelineLayout) != VK_SUCCESS) {
        LOGE("Failed to create sky color pipeline layout");
        return false;
    }

    VkPipelineLayoutCreateInfo texPlInfo = plInfo;
    texPlInfo.setLayoutCount = 1;
    texPlInfo.pSetLayouts = &skyTextureSetLayout;
    if (vkCreatePipelineLayout(device, &texPlInfo, nullptr, &skyTexturePipelineLayout) != VK_SUCCESS) {
        LOGE("Failed to create sky texture pipeline layout");
        return false;
    }

    // 9. 创建管线
    VkVertexInputBindingDescription posBinding{};
    posBinding.binding = 0;
    posBinding.stride = 3 * sizeof(float);
    posBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription posAttr{};
    posAttr.binding = 0;
    posAttr.location = 0;
    posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
    posAttr.offset = 0;

    VkVertexInputBindingDescription texBinding{};
    texBinding.binding = 0;
    texBinding.stride = 5 * sizeof(float);
    texBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription texAttrs[2] = {};
    texAttrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    texAttrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 3 * sizeof(float)};

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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendAttachmentState additiveBlend = blendAttachment;
    additiveBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    additiveBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;

    auto buildColorPipeline = [&](VkPipeline& outPipeline) -> bool {
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &posBinding;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &posAttr;

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
        pipelineInfo.layout = skyColorPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline) == VK_SUCCESS;
    };

    auto buildTexturePipeline = [&](VkPipeline& outPipeline) -> bool {
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = texVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = texFragModule;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &texBinding;
        vertexInput.vertexAttributeDescriptionCount = 2;
        vertexInput.pVertexAttributeDescriptions = texAttrs;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &additiveBlend;

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
        pipelineInfo.layout = skyTexturePipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline) == VK_SUCCESS;
    };

    if (!buildColorPipeline(skyColorPipeline) || !buildTexturePipeline(skyTexturePipeline)) {
        LOGE("Failed to create sky pipelines");
        return false;
    }

    // 销毁着色器模块（管线已持有编译代码）
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, texVertModule, nullptr);
    vkDestroyShaderModule(device, texFragModule, nullptr);

    // 10. 创建天空圆盘 VBO（上半部分，三角形扇转列表）
    const auto& topVerts = SkyRenderer::getTopSkyVertices();
    std::vector<float> topTriList;
    uint32_t fanCount = (uint32_t)(topVerts.size() / 3);
    if (fanCount >= 3) {
        for (uint32_t i = 1; i < fanCount - 1; ++i) {
            topTriList.push_back(topVerts[0]);
            topTriList.push_back(topVerts[1]);
            topTriList.push_back(topVerts[2]);
            topTriList.push_back(topVerts[i * 3]);
            topTriList.push_back(topVerts[i * 3 + 1]);
            topTriList.push_back(topVerts[i * 3 + 2]);
            topTriList.push_back(topVerts[(i + 1) * 3]);
            topTriList.push_back(topVerts[(i + 1) * 3 + 1]);
            topTriList.push_back(topVerts[(i + 1) * 3 + 2]);
        }
    }
    skyTopVertexCount = (uint32_t)(topTriList.size() / 3);
    if (skyTopVertexCount == 0 ||
        !createHostBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, topTriList.data(),
                          topTriList.size() * sizeof(float), skyTopVertexBuffer, skyTopVertexMemory)) {
        LOGE("Failed to create sky top vertex buffer");
        return false;
    }

    // 9. 创建太阳 VBO 和 EBO
    const auto& sunVerts = SkyRenderer::getSunVertices();
    std::vector<float> sunVertexData;
    for (const auto& v : sunVerts) {
        sunVertexData.push_back(v.x);
        sunVertexData.push_back(v.y);
        sunVertexData.push_back(v.z);
        sunVertexData.push_back(v.u);
        sunVertexData.push_back(v.v);
    }
    if (!createHostBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sunVertexData.data(),
                          sunVertexData.size() * sizeof(float), skySunVertexBuffer, skySunVertexMemory)) {
        LOGE("Failed to create sun vertex buffer");
        return false;
    }
    const auto& sunIdx = SkyRenderer::getSunIndices();
    skySunIndexCount = (uint32_t)sunIdx.size();
    if (!createHostBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sunIdx.data(),
                          sunIdx.size() * sizeof(uint16_t), skySunIndexBuffer, skySunIndexMemory)) {
        LOGE("Failed to create sun index buffer");
        return false;
    }

    // 11. 创建月亮 VBO 和 EBO
    const auto& moonVerts = SkyRenderer::getMoonVertices();
    std::vector<float> moonVertexData;
    for (const auto& v : moonVerts) {
        moonVertexData.push_back(v.x);
        moonVertexData.push_back(v.y);
        moonVertexData.push_back(v.z);
        moonVertexData.push_back(v.u);
        moonVertexData.push_back(v.v);
    }
    if (!createHostBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, moonVertexData.data(),
                          moonVertexData.size() * sizeof(float), skyMoonVertexBuffer, skyMoonVertexMemory)) {
        LOGE("Failed to create moon vertex buffer");
        return false;
    }
    const auto& moonIdx = SkyRenderer::getMoonIndices();
    skyMoonIndexCount = (uint32_t)moonIdx.size();
    if (!createHostBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, moonIdx.data(),
                          moonIdx.size() * sizeof(uint16_t), skyMoonIndexBuffer, skyMoonIndexMemory)) {
        LOGE("Failed to create moon index buffer");
        return false;
    }

    // 12. 创建星星 VBO 和 EBO
    int starCount = 0;
    const auto& starVerts = SkyRenderer::getStarVertices(starCount);
    const auto& starIdx = SkyRenderer::getStarIndices(starCount);
    skyStarsIndexCount = (uint32_t)starIdx.size();
    if (!createHostBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, starVerts.data(),
                          starVerts.size() * sizeof(float), skyStarsVertexBuffer, skyStarsVertexMemory) ||
        !createHostBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, starIdx.data(),
                          starIdx.size() * sizeof(uint16_t), skyStarsIndexBuffer, skyStarsIndexMemory)) {
        LOGE("Failed to create stars buffers");
        return false;
    }

    initialized = true;
    LOGI("VulkanSkyRenderer initialized with pre-created descriptor sets");
    return true;
}

// ---------- updateExtent ----------
void VulkanSkyRenderer::updateExtent(uint32_t width, uint32_t height) {
    extent.width = width;
    extent.height = height;
}

// ---------- render ----------
void VulkanSkyRenderer::render(VkCommandBuffer cmd, const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                               float skyR, float skyG, float skyB, const SkyRenderParams& params) {
    if (!initialized) return;

    // 直接从结构体提取
    float normalizedTime = params.normalizedTime;
    float starBrightness = params.starBrightness;
    int moonPhase = params.moonPhase;

    float sunAlpha = SkyRenderer::getCelestialAlpha(false);   // 太阳
    float moonAlpha = SkyRenderer::getCelestialAlpha(true);   // 月亮
    // Y 翻转
    glm::mat4 yFlip(1.0f);
    yFlip[1][1] = -1.0f;
    float aspect = (float)extent.width / (float)extent.height;
    glm::mat4 skyProj = yFlip * Camera::computeProjectionMatrix(70.0f, aspect, 0.1f, 1024.0f);
    glm::mat4 skyView = glm::mat4(glm::mat3(viewMatrix));

    VkViewport viewport{};
    viewport.width = (float)extent.width;
    viewport.height = (float)extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, extent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ---- 1. 天空圆盘 ----
    {
        glm::mat4 skyMVP = skyProj * skyView;
        SkyColorPushConstant pc{ skyMVP, glm::vec4(skyR, skyG, skyB, 1.0f) };
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyColorPipeline);
        vkCmdPushConstants(cmd, skyColorPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(SkyColorPushConstant), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &skyTopVertexBuffer, &offset);
        vkCmdDraw(cmd, skyTopVertexCount, 1, 0, 0);
    }

    // ---- 2. 天体旋转 ----
    glm::mat4 celestialRot = SkyRenderer::getCelestialRotation(normalizedTime);
    glm::mat4 celestialMVP = skyProj * skyView * celestialRot;

    // ---- 3. 太阳 ----
    if (sunAlpha > 0.0f && sunTextureView != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyTexturePipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyTexturePipelineLayout,
                                0, 1, &skyDescriptorSetSun, 0, nullptr);
        SkyColorPushConstant pc{ celestialMVP, glm::vec4(1.0f, 1.0f, 1.0f, sunAlpha) };
        vkCmdPushConstants(cmd, skyTexturePipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(SkyColorPushConstant), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &skySunVertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, skySunIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, skySunIndexCount, 1, 0, 0, 0);
    }

    // ---- 4. 月亮 ----
    if (moonAlpha > 0.0f) {
        int idx = (moonPhase % 8 + 8) % 8;
        VkDescriptorSet moonSet = skyDescriptorSetMoon[idx];
        if (moonSet != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyTexturePipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyTexturePipelineLayout,
                                    0, 1, &moonSet, 0, nullptr);
            SkyColorPushConstant pc{ celestialMVP, glm::vec4(1.0f, 1.0f, 1.0f, moonAlpha) };
            vkCmdPushConstants(cmd, skyTexturePipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(SkyColorPushConstant), &pc);
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &skyMoonVertexBuffer, &offset);
            vkCmdBindIndexBuffer(cmd, skyMoonIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cmd, skyMoonIndexCount, 1, 0, 0, 0);
        }
    }

    // ---- 5. 星星 ----
    if (starBrightness > 0.01f) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyColorPipeline);
        SkyColorPushConstant pc{ celestialMVP, glm::vec4(starBrightness, starBrightness, starBrightness, starBrightness) };
        vkCmdPushConstants(cmd, skyColorPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(SkyColorPushConstant), &pc);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &skyStarsVertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, skyStarsIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, skyStarsIndexCount, 1, 0, 0, 0);
    }
}

// ---------- destroy ----------
void VulkanSkyRenderer::destroy() {
    if (!initialized) return;

    // 管线
    if (skyColorPipeline) { vkDestroyPipeline(device, skyColorPipeline, nullptr); skyColorPipeline = VK_NULL_HANDLE; }
    if (skyColorPipelineLayout) { vkDestroyPipelineLayout(device, skyColorPipelineLayout, nullptr); skyColorPipelineLayout = VK_NULL_HANDLE; }
    if (skyTexturePipeline) { vkDestroyPipeline(device, skyTexturePipeline, nullptr); skyTexturePipeline = VK_NULL_HANDLE; }
    if (skyTexturePipelineLayout) { vkDestroyPipelineLayout(device, skyTexturePipelineLayout, nullptr); skyTexturePipelineLayout = VK_NULL_HANDLE; }

    // 描述符
    if (skyTextureDescriptorPool) { vkDestroyDescriptorPool(device, skyTextureDescriptorPool, nullptr); skyTextureDescriptorPool = VK_NULL_HANDLE; }
    if (skyTextureSetLayout) { vkDestroyDescriptorSetLayout(device, skyTextureSetLayout, nullptr); skyTextureSetLayout = VK_NULL_HANDLE; }
    if (skyTextureSampler) { vkDestroySampler(device, skyTextureSampler, nullptr); skyTextureSampler = VK_NULL_HANDLE; }
    skyDescriptorSetSun = VK_NULL_HANDLE;
    for (int i = 0; i < 8; ++i) skyDescriptorSetMoon[i] = VK_NULL_HANDLE;

    // 纹理（注意：如果某月相使用了 sun 的回退，销毁时需避免 double-free）
    // 为安全，只销毁 sun 和自己持有的 moon，若 moon 指向 sun 则跳过销毁。
    auto destroyTex = [&](VkImage& img, VmaAllocation& alloc, VkImageView& view) {
        if (view && view != sunTextureView) {  // 若不是 sun 的回退引用
            vkDestroyImageView(device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (img && img != sunTextureImage) {
            vmaDestroyImage(allocator, img, alloc);
            img = VK_NULL_HANDLE;
            alloc = VK_NULL_HANDLE;
        }
    };
    if (sunTextureView) { vkDestroyImageView(device, sunTextureView, nullptr); sunTextureView = VK_NULL_HANDLE; }
    if (sunTextureImage) { vmaDestroyImage(allocator, sunTextureImage, sunTextureMemory); sunTextureImage = VK_NULL_HANDLE; sunTextureMemory = VK_NULL_HANDLE; }
    for (int i = 0; i < 8; ++i) {
        destroyTex(moonTextureImages[i], moonTextureMemories[i], moonTextureViews[i]);
    }

    // 销毁缓冲（与之前相同）
    auto destroyBuf = [&](VkBuffer& buf, VmaAllocation& alloc) {
        if (buf) { vmaDestroyBuffer(allocator, buf, alloc); buf = VK_NULL_HANDLE; alloc = VK_NULL_HANDLE; }
    };
    destroyBuf(skyTopVertexBuffer, skyTopVertexMemory);
    destroyBuf(skySunVertexBuffer, skySunVertexMemory);
    destroyBuf(skySunIndexBuffer, skySunIndexMemory);
    destroyBuf(skyMoonVertexBuffer, skyMoonVertexMemory);
    destroyBuf(skyMoonIndexBuffer, skyMoonIndexMemory);
    destroyBuf(skyStarsVertexBuffer, skyStarsVertexMemory);
    destroyBuf(skyStarsIndexBuffer, skyStarsIndexMemory);

    initialized = false;
    LOGI("VulkanSkyRenderer destroyed");
}
VulkanSkyRenderer::~VulkanSkyRenderer() {
    destroy();
}