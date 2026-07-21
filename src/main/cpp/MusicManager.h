#pragma once

#include <string>
#include <vector>
#include <random>
#include <chrono>

// 音乐场景类型（对应原版 Minecraft 的 Music 结构）
enum class MusicScene {
    MENU,       // 主菜单: 20-600 ticks (1-30秒), replace=true
    GAME,       // 游戏内: 12000-24000 ticks (10-20分钟), replace=false
    CREATIVE,   // 创造模式: 12000-24000 ticks (10-20分钟), replace=false
    NONE        // 无音乐
};

class MusicManager {
public:
    static MusicManager& getInstance();

    bool init();
    void shutdown();

    // 每帧调用，驱动音乐状态机（完全复刻原版 MusicManager.tick() 逻辑）
    void tick();

    void playClickSound();
    void playOneShot(const std::string& resourcePath);

    void setScene(MusicScene scene);
    MusicScene getScene() const { return currentScene; }

    // 音量控制 (0.0 ~ 1.0)
    void setVolume(float volume);
    float getVolume() const { return musicVolume; }

    bool isInitialized() const { return initialized; }

private:
    MusicManager() = default;
    ~MusicManager() = default;

    // 场景配置（对应原版 Music 结构）
    struct SceneConfig {
        float minDelaySec;   // 最小曲间间隔（秒）
        float maxDelaySec;   // 最大曲间间隔（秒）
        bool  replaceCurrentMusic; // 是否可替换当前曲
    };
    static SceneConfig getConfig(MusicScene scene);

    std::string extractOggToTemp(const std::string& resourcePath);
    void startPlaying(const std::string& resourcePath);
    void stopPlaying();
    void loadClickSound();
    std::vector<std::string> getMusicFiles() const;
    std::string pickRandomMusic() const;

    // 在 [min, max] 区间随机取延迟
    float randomDelay(float minSec, float maxSec) const;

    bool initialized = false;
    void* engine = nullptr;          // ma_engine*
    void* currentSound = nullptr;    // ma_sound* (music)
    void* audioBuffer = nullptr;     // ma_audio_buffer* (music PCM)
    short* pcmData = nullptr;        // decoded music PCM data

    short* clickPcmData = nullptr;
    size_t clickPcmSize = 0;
    int clickChannels = 0;
    int clickSampleRate = 0;
    bool clickSoundLoaded = false;

    struct OneShotSound {
        void* sound;   // ma_sound*
        void* buffer;  // ma_audio_buffer*
    };
    std::vector<OneShotSound> activeOneShots;

    MusicScene currentScene = MusicScene::MENU;
    std::string currentMusicPath;

    // ==== 原版 MusicManager 状态 ====
    static constexpr float STARTING_DELAY = 5.0f;  // same as 100 ticks
    float nextSongDelay = STARTING_DELAY;          // 倒计时（秒），<=0 时播下一首
    float musicVolume = 1.0f;

    mutable std::mt19937 rng;
};
