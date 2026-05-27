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
void* TextureLoader::g_zip = nullptr;
bool TextureLoader::g_zipOpen = false;

void TextureLoader::setAssetManager(AAssetManager* assetManager) {
    g_assetManager = assetManager;
}

void TextureLoader::setZipPath(const std::string& path) {
    // 关闭之前的 ZIP
    if (g_zipOpen) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(g_zip));
        delete static_cast<mz_zip_archive*>(g_zip);
        g_zip = nullptr;
        g_zipOpen = false;
    }

    g_zipPath = path;
    LOGI("Texture ZIP path set to: %s", path.c_str());

    // 直接打开缓存
    if (!g_zipPath.empty()) {
        mz_zip_archive* zip = new mz_zip_archive();
        memset(zip, 0, sizeof(mz_zip_archive));
        if (mz_zip_reader_init_file(zip, g_zipPath.c_str(), 0)) {
            g_zip = zip;
            g_zipOpen = true;
            LOGI("ZIP opened and cached: %s", g_zipPath.c_str());
        } else {
            delete zip;
            LOGE("Failed to open ZIP file: %s", g_zipPath.c_str());
        }
    }
}

void TextureLoader::closeZip() {
    if (g_zipOpen) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(g_zip));
        delete static_cast<mz_zip_archive*>(g_zip);
        g_zip = nullptr;
        g_zipOpen = false;
        LOGI("ZIP closed");
    }
}

TextureData TextureLoader::loadFromZip(const std::string& filename) {
    TextureData result;

    if (!g_zipOpen || !g_zip) {
        LOGE("ZIP not open, cannot load: %s", filename.c_str());
        return result;
    }

    mz_zip_archive* zip = static_cast<mz_zip_archive*>(g_zip);

    // 在 ZIP 中查找 blocks/<filename> 或 colormap/<filename>
    std::string searchPath = "blocks/" + filename;
    int fileIndex = mz_zip_reader_locate_file(zip, searchPath.c_str(), nullptr, 0);
    if (fileIndex < 0) {
        searchPath = "colormap/" + filename;
        fileIndex = mz_zip_reader_locate_file(zip, searchPath.c_str(), nullptr, 0);
    }

    if (fileIndex < 0) {
        return result;
    }

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(zip, fileIndex, &stat)) {
        LOGE("Failed to stat file in ZIP: %s", filename.c_str());
        return result;
    }

    size_t uncompSize = 0;
    unsigned char* fileData = static_cast<unsigned char*>(
        mz_zip_reader_extract_file_to_heap(zip, stat.m_filename, &uncompSize, 0));

    if (!fileData || uncompSize == 0) {
        LOGE("Failed to extract from ZIP: %s", filename.c_str());
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
