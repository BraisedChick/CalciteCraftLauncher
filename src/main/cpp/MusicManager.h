#pragma once

#include <string>
#include <vector>
#include <random>
#include <chrono>

// 音乐场景类型
enum class MusicScene {
    MENU,       // 主菜单 (1-30秒间隔, replace=true)
    GAME,       // 游戏内 (10-20分钟间隔, replace=false)
    CREATIVE,   // 创造模式 (10-20分钟间隔, replace=false)
    NONE        // 无音乐
};

class MusicManager {
public:
    static MusicManager& getInstance();

    // 初始化 miniaudio 引擎（应用启动时调用一次）
    bool init();
    void shutdown();

    // 每帧调用，驱动音乐状态机 + 清理已完成的音效
    void tick();

    // 播放 UI 按钮点击音效（random/click_stereo.ogg）
    void playClickSound();

    // 场景切换时调用
    void setScene(MusicScene scene);
    MusicScene getScene() const { return currentScene; }

    // 音量控制 (0.0 ~ 1.0)
    void setVolume(float volume);
    float getVolume() const { return musicVolume; }

    bool isInitialized() const { return initialized; }

private:
    MusicManager() = default;
    ~MusicManager() = default;

    // 从本地 sounds 目录读取 OGG 到临时文件（由 Java 层从 API 下载），返回临时文件路径
    std::string extractOggToTemp(const std::string& resourcePath);

    // 开始播放一首音乐
    void startPlaying(const std::string& resourcePath);

    // 停止当前音乐
    void stopPlaying();

    // 预加载按钮点击音效（init 时调用）
    void loadClickSound();

    // 获取当前场景的音乐文件列表
    std::vector<std::string> getMusicFiles() const;

    // 计算下次播放延迟（秒）
    float getNextDelay() const;

    // 随机选一首当前场景的音乐
    std::string pickRandomMusic() const;

    bool initialized = false;
    void* engine = nullptr;      // ma_engine*
    void* currentSound = nullptr; // ma_sound* (music)
    void* audioBuffer = nullptr;  // ma_audio_buffer* (music PCM)
    short* pcmData = nullptr;    // decoded music PCM data

    // 按钮点击音效（预解码 PCM 缓存）
    short* clickPcmData = nullptr;
    size_t clickPcmSize = 0;
    int clickChannels = 0;
    int clickSampleRate = 0;
    bool clickSoundLoaded = false;

    // one-shot 音效跟踪（tick 中清理）
    struct OneShotSound {
        void* sound;       // ma_sound*
        void* buffer;      // ma_audio_buffer*
    };
    std::vector<OneShotSound> activeOneShots;

    MusicScene currentScene = MusicScene::MENU;
    std::string currentMusicPath;  // 当前正在播放的资源路径

    float nextPlayTime = 0.0f;   // 下次播放的时间戳（秒）
    float musicVolume = 1.0f;

    mutable std::mt19937 rng;
};
