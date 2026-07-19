#include "MusicManager.h"
#include <android/log.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

#define MINIAUDIO_IMPLEMENTATION
#include "3rdparty/miniaudio.h"

// minivorbis OGG 解码 API（实现在 minivorbis.c 中）
#include "3rdparty/minivorbis/minivorbis.h"

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
    LOGI("MusicManager initialized");

    // 预加载按钮点击音效
    loadClickSound();

    return true;
}

void MusicManager::shutdown() {
    if (!initialized) return;
    initialized = false;

    LOGI("MusicManager shutdown...");

    // 1. 先停引擎——这会停止音频线程，所有 ma_sound/ma_buffer 内部资源随之释放
    if (engine) {
        ma_engine_uninit(static_cast<ma_engine*>(engine));
        delete static_cast<ma_engine*>(engine);
        engine = nullptr;
    }

    // 2. 音频线程已停，安全释放 C++ 层包装和 PCM 数据
    //    （miniaudio 对象内部资源已在 engine uninit 时释放，只需 delete 包装）
    if (currentSound) {
        delete static_cast<ma_sound*>(currentSound);
        currentSound = nullptr;
    }
    if (audioBuffer) {
        delete static_cast<ma_audio_buffer*>(audioBuffer);
        audioBuffer = nullptr;
    }

    for (auto& os : activeOneShots) {
        if (os.sound) delete static_cast<ma_sound*>(os.sound);
        if (os.buffer) delete static_cast<ma_audio_buffer*>(os.buffer);
    }
    activeOneShots.clear();

    if (pcmData) {
        delete[] pcmData;
        pcmData = nullptr;
    }
    if (clickPcmData) {
        delete[] clickPcmData;
        clickPcmData = nullptr;
    }
    clickSoundLoaded = false;
    currentMusicPath.clear();

    LOGI("MusicManager shutdown complete");
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

    // 清理已完成的 one-shot 音效
    for (auto it = activeOneShots.begin(); it != activeOneShots.end();) {
        ma_sound* snd = static_cast<ma_sound*>(it->sound);
        if (!ma_sound_is_playing(snd)) {
            ma_sound_uninit(snd);
            delete snd;
            if (it->buffer) {
                ma_audio_buffer_uninit(static_cast<ma_audio_buffer*>(it->buffer));
                delete static_cast<ma_audio_buffer*>(it->buffer);
            }
            it = activeOneShots.erase(it);
        } else {
            ++it;
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

    // 从其他场景切换到 GAME/CREATIVE 时，停止当前音乐
    // （菜单音乐不应该在游戏内继续播放）
    if (prevScene != scene && (prevScene == MusicScene::MENU || prevScene == MusicScene::NONE)) {
        stopPlaying();
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
    // resourcePath 如 "music/menu/menu1"（无 .ogg 扩展名）
    // 直接使用下载目录的 OGG 文件，无需复制到临时目录
    std::string soundsDir = "/storage/emulated/0/Android/data/com.calcite/files/sounds";
    std::string path = soundsDir + "/" + resourcePath + ".ogg";

    FILE* test = fopen(path.c_str(), "rb");
    if (test) {
        fclose(test);
        return path;
    }

    LOGE("extractOggToTemp: sound not found at %s (API download may not have completed)", path.c_str());
    return "";
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

void MusicManager::loadClickSound() {
    if (clickSoundLoaded) return;

    // 从本地 sounds 目录读取 click_stereo.ogg 并解码
    std::string filePath = extractOggToTemp("random/click_stereo");
    if (filePath.empty()) {
        // 回退到单声道 click.ogg
        filePath = extractOggToTemp("random/click");
    }
    if (filePath.empty()) {
        LOGW("Click sound not found, button clicks will be silent");
        return;
    }

    // 用 minivorbis 解码 OGG -> PCM
    OggVorbis_File vf;
    int err = ov_fopen(filePath.c_str(), &vf);
    if (err != 0) {
        LOGE("ov_fopen failed for click sound: %d", err);
        return;
    }

    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi) {
        LOGE("ov_info failed for click sound");
        ov_clear(&vf);
        return;
    }

    clickChannels = vi->channels;
    clickSampleRate = (int)vi->rate;

    // 解码全部 PCM 数据
    std::vector<short> pcm;
    pcm.reserve(clickSampleRate * clickChannels); // 短音效
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
        LOGE("No PCM data decoded from click sound");
        return;
    }

    // 保存 PCM 数据
    clickPcmSize = pcm.size();
    clickPcmData = new short[clickPcmSize];
    memcpy(clickPcmData, pcm.data(), clickPcmSize * sizeof(short));
    clickSoundLoaded = true;

    LOGI("Click sound loaded: %d ch, %d Hz, %zu samples (%.0fms)",
         clickChannels, clickSampleRate, clickPcmSize,
         (float)(clickPcmSize / clickChannels) / clickSampleRate * 1000.0f);
}

void MusicManager::playClickSound() {
    if (!initialized || !engine || !clickSoundLoaded) return;

    // 从缓存的 PCM 数据创建 one-shot ma_sound
    ma_audio_buffer_config bufConfig = ma_audio_buffer_config_init(
        ma_format_s16,
        clickChannels,
        clickPcmSize / clickChannels,
        clickPcmData,
        nullptr
    );
    bufConfig.sampleRate = clickSampleRate;

    ma_audio_buffer* buf = new ma_audio_buffer();
    ma_result result = ma_audio_buffer_init(&bufConfig, buf);
    if (result != MA_SUCCESS) {
        LOGE("click ma_audio_buffer_init failed: %d", result);
        delete buf;
        return;
    }

    ma_sound* snd = new ma_sound();
    result = ma_sound_init_from_data_source(
        static_cast<ma_engine*>(engine),
        buf,
        0,   // 默认 flag：自动连接到引擎音频输出
        nullptr,
        snd);

    if (result != MA_SUCCESS) {
        LOGE("click ma_sound_init failed: %d", result);
        ma_audio_buffer_uninit(buf);
        delete buf;
        delete snd;
        return;
    }

    // MC 按钮音量 = 0.25
    ma_sound_set_volume(snd, 0.25f);
    ma_sound_start(snd);

    // 跟踪以便 tick 清理
    activeOneShots.push_back({snd, buf});
}

void MusicManager::playOneShot(const std::string& resourcePath) {
    if (!initialized || !engine) return;

    std::string filePath = extractOggToTemp(resourcePath);
    if (filePath.empty()) {
        LOGW("One-shot sound not found: %s", resourcePath.c_str());
        return;
    }

    // 用 minivorbis 解码 OGG -> PCM
    OggVorbis_File vf;
    int err = ov_fopen(filePath.c_str(), &vf);
    if (err != 0) {
        LOGE("playOneShot ov_fopen failed for %s (error %d)", filePath.c_str(), err);
        return;
    }

    vorbis_info* vi = ov_info(&vf, -1);
    if (!vi) {
        ov_clear(&vf);
        return;
    }

    int channels = vi->channels;
    long sampleRate = vi->rate;

    // 解码全部 PCM 数据
    std::vector<short> pcm;
    pcm.reserve(sampleRate * channels);
    char readBuf[4096];
    int bitstream = 0;
    long bytesRead;
    while ((bytesRead = ov_read(&vf, readBuf, sizeof(readBuf), 0, 2, 1, &bitstream)) > 0) {
        short* samples = reinterpret_cast<short*>(readBuf);
        int numSamples = bytesRead / 2;
        pcm.insert(pcm.end(), samples, samples + numSamples);
    }
    ov_clear(&vf);

    if (pcm.empty()) return;

    // 创建 ma_audio_buffer
    ma_audio_buffer_config bufConfig = ma_audio_buffer_config_init(
        ma_format_s16, channels, (ma_uint64)pcm.size() / channels,
        pcm.data(), nullptr);
    bufConfig.sampleRate = (ma_uint32)sampleRate;

    ma_audio_buffer* buf = new ma_audio_buffer();
    if (ma_audio_buffer_init(&bufConfig, buf) != MA_SUCCESS) {
        LOGE("playOneShot ma_audio_buffer_init failed");
        delete buf;
        return;
    }

    // 创建 ma_sound 从 audio buffer
    ma_sound* snd = new ma_sound();
    if (ma_sound_init_from_data_source(
            static_cast<ma_engine*>(engine), buf, 0, nullptr, snd) != MA_SUCCESS) {
        ma_audio_buffer_uninit(buf);
        delete buf;
        delete snd;
        return;
    }

    ma_sound_set_volume(snd, 1.0f);
    ma_sound_start(snd);

    activeOneShots.push_back({snd, buf});
    LOGI("Playing one-shot: %s", resourcePath.c_str());
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
