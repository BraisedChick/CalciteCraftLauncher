#include "MusicManager.h"
#include "TextureLoader.h"
#include <android/log.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

#define MINIAUDIO_IMPLEMENTATION
#include "3rdparty/miniaudio.h"

// minivorbis OGG 解码 API（实现在 minivorbis.c 中）
#include "3rdparty/minivorbis.h"

#include "miniz.h"

#define LOG_TAG "MusicManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

MusicManager& MusicManager::getInstance() {
    static MusicManager instance;
    return instance;
}

bool MusicManager::init() {
    if (initialized) return true;

    tempDir = "/data/data/com.calcite/cache";
    // 该目录由 Android 系统自动创建，无需 mkdir

    // 初始化 miniaudio 引擎
    ma_engine* eng = new ma_engine();
    ma_engine_config config = ma_engine_config_init();
    config.channels = 2;
    config.sampleRate = 44100;

    ma_result result = ma_engine_init(&config, eng);
    if (result != MA_SUCCESS) {
        LOGE("Failed to init miniaudio engine: %d", result);
        delete eng;
        return false;
    }

    engine = eng;
    rng.seed((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());

    // 初始延迟 5 秒后开始播放
    nextPlayTime = 5.0f;

    initialized = true;
    LOGI("MusicManager initialized, tempDir=%s", tempDir.c_str());
    return true;
}

void MusicManager::shutdown() {
    if (!initialized) return;

    stopPlaying();

    if (engine) {
        ma_engine_uninit(static_cast<ma_engine*>(engine));
        delete static_cast<ma_engine*>(engine);
        engine = nullptr;
    }

    initialized = false;
    LOGI("MusicManager shutdown");
}

static float getCurrentTime() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now.time_since_epoch()).count();
}

void MusicManager::tick() {
    if (!initialized || !engine) return;
    if (currentScene == MusicScene::NONE) return;

    float now = getCurrentTime();

    // 检查当前音乐是否播完
    if (currentSound) {
        ma_sound* snd = static_cast<ma_sound*>(currentSound);
        if (!ma_sound_is_playing(snd)) {
            // 播放完毕，设置下次播放延迟
            float delay = getNextDelay();
            nextPlayTime = now + delay;
            LOGI("Music finished, next play in %.1fs", delay);

            stopPlaying();  // 释放所有资源
        }
    }

    // 到达播放时间且当前没有在播放
    if (!currentSound && now >= nextPlayTime) {
        std::string music = pickRandomMusic();
        if (!music.empty()) {
            startPlaying(music);
        }
    }
}

void MusicManager::setScene(MusicScene scene) {
    if (scene == currentScene) return;

    MusicScene prevScene = currentScene;
    currentScene = scene;

    if (scene == MusicScene::NONE) {
        stopPlaying();
        return;
    }

    // 根据 replace 规则处理
    bool shouldReplace = true; // MENU 的 replace=true
    if (scene == MusicScene::GAME || scene == MusicScene::CREATIVE) {
        shouldReplace = false; // GAME/CREATIVE 的 replace=false
    }

    if (shouldReplace || !currentSound) {
        // 停止当前音乐，设短延迟后开始新场景音乐
        stopPlaying();
        float delay = getNextDelay();
        nextPlayTime = getCurrentTime() + delay;
        LOGI("Scene changed to %d, next play in %.1fs", (int)scene, delay);
    }
    // replace=false 时，等当前曲自然结束后切换
}

void MusicManager::setVolume(float volume) {
    musicVolume = std::max(0.0f, std::min(1.0f, volume));
    if (engine) {
        ma_engine_set_volume(static_cast<ma_engine*>(engine), musicVolume);
    }
}

std::string MusicManager::extractOggToTemp(const std::string& resourcePath) {
    // resourcePath 如 "music/menu/menu1"
    // ZIP 中路径可能是: "sounds/music/menu/menu1.ogg" 或 "music/menu/menu1.ogg"
    std::string tempPath = tempDir + "/mc_music_" + resourcePath.substr(resourcePath.rfind('/') + 1) + ".ogg";

    // 检查临时文件是否已存在
    FILE* testFile = fopen(tempPath.c_str(), "rb");
    if (testFile) {
        fclose(testFile);
        return tempPath;
    }

    // 从 ZIP 读取 OGG 数据
    mz_zip_archive* zip = static_cast<mz_zip_archive*>(TextureLoader::getZipHandle());
    if (!zip) {
        LOGE("extractOggToTemp: ZIP handle is NULL");
        return "";
    }

    // 尝试多种 ZIP 路径
    int fileIndex = -1;
    std::string zipPath;
    const char* prefixes[] = {"sounds/", "", nullptr};
    for (int i = 0; prefixes[i]; i++) {
        zipPath = std::string(prefixes[i]) + resourcePath + ".ogg";
        fileIndex = mz_zip_reader_locate_file(zip, zipPath.c_str(), nullptr, 0);
        if (fileIndex >= 0) {
            LOGI("Found music in ZIP at: %s (index=%d)", zipPath.c_str(), fileIndex);
            break;
        }
    }

    if (fileIndex < 0) {
        LOGE("extractOggToTemp: file not found in ZIP. Tried: sounds/%s.ogg, %s.ogg",
             resourcePath.c_str(), resourcePath.c_str());
        // 列出 ZIP 中包含 music 的文件帮助调试
        int numFiles = mz_zip_reader_get_num_files(zip);
        for (int i = 0; i < numFiles; i++) {
            char name[256];
            mz_zip_reader_get_filename(zip, i, name, sizeof(name));
            if (strstr(name, "menu") || strstr(name, "music")) {
                LOGE("  ZIP contains: %s", name);
            }
        }
        return "";
    }

    size_t uncompSize = 0;
    unsigned char* data = static_cast<unsigned char*>(
        mz_zip_reader_extract_to_heap(zip, fileIndex, &uncompSize, 0));
    if (!data || uncompSize == 0) {
        LOGE("extractOggToTemp: extract failed for %s (data=%p, size=%zu)", zipPath.c_str(), data, uncompSize);
        return "";
    }

    LOGI("Extracted OGG from ZIP: %s (%zu bytes)", zipPath.c_str(), uncompSize);

    // 写入临时文件
    FILE* outFile = fopen(tempPath.c_str(), "wb");
    if (!outFile) {
        LOGE("extractOggToTemp: fopen failed for %s", tempPath.c_str());
        mz_free(data);
        return "";
    }
    size_t written = fwrite(data, 1, uncompSize, outFile);
    fclose(outFile);
    mz_free(data);

    if (written != uncompSize) {
        LOGE("extractOggToTemp: write incomplete %zu/%zu", written, uncompSize);
        return "";
    }

    LOGI("Music temp file written: %s (%zu bytes)", tempPath.c_str(), written);
    return tempPath;
}

void MusicManager::startPlaying(const std::string& resourcePath) {
    if (!engine) return;

    std::string filePath = extractOggToTemp(resourcePath);
    if (filePath.empty()) {
        LOGE("Cannot play music: %s", resourcePath.c_str());
        return;
    }

    // 用 minivorbis 解码 OGG -> PCM
    OggVorbis_File vf;
    int err = ov_fopen(filePath.c_str(), &vf);
    if (err != 0) {
        LOGE("ov_fopen failed for %s (error %d)", filePath.c_str(), err);
        return;
    }

    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi) {
        LOGE("ov_info failed for %s", filePath.c_str());
        ov_clear(&vf);
        return;
    }

    int channels = vi->channels;
    long sampleRate = vi->rate;
    LOGI("Decoding OGG: %s (%d ch, %ld Hz)", filePath.c_str(), channels, sampleRate);

    // 解码全部 PCM 数据
    std::vector<short> pcm;
    pcm.reserve(sampleRate * channels * 30); // 预估30秒
    char readBuf[4096];
    int bitstream = 0;
    long bytesRead;
    while ((bytesRead = ov_read(&vf, readBuf, sizeof(readBuf), 0, 2, 1, &bitstream)) > 0) {
        short* samples = reinterpret_cast<short*>(readBuf);
        int numSamples = bytesRead / 2;
        pcm.insert(pcm.end(), samples, samples + numSamples);
    }
    ov_clear(&vf);

    if (pcm.empty()) {
        LOGE("No PCM data decoded from %s", filePath.c_str());
        return;
    }

    LOGI("Decoded PCM: %zu samples (%.1fs)", pcm.size(), (float)pcm.size() / (channels * sampleRate));

    // 保存 PCM 数据（ma_audio_buffer 需要引用）
    delete[] pcmData;
    pcmData = new short[pcm.size()];
    memcpy(pcmData, pcm.data(), pcm.size() * sizeof(short));

    // 创建 ma_audio_buffer
    ma_audio_buffer_config bufConfig = ma_audio_buffer_config_init(
        ma_format_s16,
        channels,
        pcm.size() / channels,
        pcmData,
        nullptr
    );
    bufConfig.sampleRate = sampleRate;

    ma_audio_buffer* buf = new ma_audio_buffer();
    ma_result result = ma_audio_buffer_init(&bufConfig, buf);
    if (result != MA_SUCCESS) {
        LOGE("ma_audio_buffer_init failed (error %d)", result);
        delete buf;
        delete[] pcmData;
        pcmData = nullptr;
        return;
    }

    // 创建 ma_sound 从 audio buffer
    ma_sound* snd = new ma_sound();
    result = ma_sound_init_from_data_source(
        static_cast<ma_engine*>(engine),
        buf,
        0,
        nullptr,
        snd);

    if (result != MA_SUCCESS) {
        LOGE("ma_sound_init_from_data_source failed (error %d)", result);
        ma_audio_buffer_uninit(buf);
        delete buf;
        delete snd;
        delete[] pcmData;
        pcmData = nullptr;
        return;
    }

    ma_sound_set_volume(snd, musicVolume);
    ma_sound_start(snd);

    currentSound = snd;
    audioBuffer = buf;
    currentMusicPath = resourcePath;
    LOGI("Playing music: %s", resourcePath.c_str());
}

void MusicManager::stopPlaying() {
    if (currentSound) {
        ma_sound* snd = static_cast<ma_sound*>(currentSound);
        ma_sound_stop(snd);
        ma_sound_uninit(snd);
        delete snd;
        currentSound = nullptr;
    }
    if (audioBuffer) {
        ma_audio_buffer_uninit(static_cast<ma_audio_buffer*>(audioBuffer));
        delete static_cast<ma_audio_buffer*>(audioBuffer);
        audioBuffer = nullptr;
    }
    if (pcmData) {
        delete[] pcmData;
        pcmData = nullptr;
    }
    currentMusicPath.clear();
}

std::vector<std::string> MusicManager::getMusicFiles() const {
    switch (currentScene) {
        case MusicScene::MENU:
            return {
                "music/menu/menu1",
                "music/menu/menu2",
                "music/menu/menu3",
                "music/menu/menu4"
            };
        case MusicScene::GAME:
            return {
                "music/game/calm1", "music/game/calm2", "music/game/calm3",
                "music/game/hal1", "music/game/hal2", "music/game/hal3", "music/game/hal4",
                "music/game/piano1", "music/game/piano2", "music/game/piano3",
                "music/game/nuance1", "music/game/nuance2",
                "music/game/an_ordinary_day", "music/game/comforting_memories",
                "music/game/floating_dream", "music/game/infinite_amethyst",
                "music/game/left_to_bloom", "music/game/one_more_day",
                "music/game/stand_tall", "music/game/wending"
            };
        case MusicScene::CREATIVE:
            return {
                "music/game/creative/creative1", "music/game/creative/creative2",
                "music/game/creative/creative3", "music/game/creative/creative4",
                "music/game/creative/creative5", "music/game/creative/creative6"
            };
        default:
            return {};
    }
}

float MusicManager::getNextDelay() const {
    std::uniform_real_distribution<float> dist;
    switch (currentScene) {
        case MusicScene::MENU:
            // 1-30 秒
            dist = std::uniform_real_distribution<float>(1.0f, 30.0f);
            break;
        case MusicScene::GAME:
        case MusicScene::CREATIVE:
            // 10-20 分钟
            dist = std::uniform_real_distribution<float>(600.0f, 1200.0f);
            break;
        default:
            return 999.0f;
    }
    return dist(rng);
}

std::string MusicManager::pickRandomMusic() const {
    auto files = getMusicFiles();
    if (files.empty()) return "";

    // 避免连续播放同一首
    std::uniform_int_distribution<int> dist(0, (int)files.size() - 1);
    int idx = dist(rng);

    // 如果随机到同一首，换一个
    if (files.size() > 1 && files[idx] == currentMusicPath) {
        idx = (idx + 1) % (int)files.size();
    }

    return files[idx];
}
