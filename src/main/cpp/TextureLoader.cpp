#include "TextureLoader.h"
#include <android/log.h>
#include <cstring>
#include <vector>

#define LOG_TAG "TextureLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 包含 stb_image
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// 包含 miniz（ZIP 读取支持，实现代码在 miniz.c 中）
#include "miniz.h"

AAssetManager* TextureLoader::g_assetManager = nullptr;
std::string TextureLoader::g_zipPath = "";

void TextureLoader::setAssetManager(AAssetManager* assetManager) {
    g_assetManager = assetManager;
}

void TextureLoader::setZipPath(const std::string& path) {
    g_zipPath = path;
    LOGI("Texture ZIP path set to: %s", path.c_str());
}

TextureData TextureLoader::loadFromZip(const std::string& filename) {
    TextureData result;

    if (g_zipPath.empty()) {
        return result;
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    LOGI("Attempting to open ZIP: %s", g_zipPath.c_str());

    if (!mz_zip_reader_init_file(&zip, g_zipPath.c_str(), 0)) {
        LOGE("Failed to open ZIP file: %s", g_zipPath.c_str());
        return result;
    }

    // 在 ZIP 中查找 blocks/<filename>
    std::string blocksPath = "blocks/" + filename;
    int fileIndex = mz_zip_reader_locate_file(&zip, blocksPath.c_str(), nullptr, 0);

    if (fileIndex < 0) {
        mz_zip_reader_end(&zip);
        return result;
    }

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip, fileIndex, &stat)) {
        LOGE("Failed to stat file in ZIP: %s", filename.c_str());
        mz_zip_reader_end(&zip);
        return result;
    }

    size_t uncompSize = 0;
    unsigned char* fileData = static_cast<unsigned char*>(
        mz_zip_reader_extract_file_to_heap(&zip, stat.m_filename, &uncompSize, 0));

    if (!fileData || uncompSize == 0) {
        LOGE("Failed to extract from ZIP: %s", filename.c_str());
        mz_zip_reader_end(&zip);
        return result;
    }

    // 使用 stb_image 解码
    int width, height, channels;
    unsigned char* imageData = stbi_load_from_memory(
        fileData,
        static_cast<int>(uncompSize),
        &width,
        &height,
        &channels,
        4  // 强制 RGBA
    );

    mz_free(fileData);
    mz_zip_reader_end(&zip);

    if (!imageData) {
        LOGE("Failed to decode PNG from ZIP: %s - %s", filename.c_str(), stbi_failure_reason());
        return result;
    }

    result.width = width;
    result.height = height;
    result.channels = 4;
    result.data = new unsigned char[width * height * 4];
    std::memcpy(result.data, imageData, width * height * 4);

    stbi_image_free(imageData);

    LOGI("Loaded texture from ZIP: %s (%dx%d)", filename.c_str(), width, height);
    return result;
}

TextureData TextureLoader::loadPNG(const std::string& filename) {
    TextureData result;

    if (g_zipPath.empty()) {
        LOGE("ZIP path not set, cannot load texture: %s", filename.c_str());
        return result;
    }

    result = loadFromZip(filename);
    if (result.data == nullptr) {
        LOGE("Failed to load texture from ZIP: %s", filename.c_str());
    }
    return result;
}

TextureData TextureLoader::loadImage(const std::string& filename) {
    LOGI("loadImage called with: %s", filename.c_str());

    // 根据文件扩展名选择加载方式
    if (filename.size() >= 4) {
        std::string ext = filename.substr(filename.size() - 4);
        LOGI("File extension: %s", ext.c_str());

        if (ext == ".png") {
            LOGI("Detected PNG format, calling loadPNG");
            return loadPNG(filename);
        }
    }

    LOGE("Unsupported image format or invalid filename: %s", filename.c_str());
    return TextureData();
}
