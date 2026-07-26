#pragma once

#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <memory>

#include "MinivorbisDataSource.h"

// miniaudio 完整定义仅在 .cpp 中随实现宏引入，此处前置声明供智能指针使用
struct ma_engine;

// ma_engine 专用删除器：先 ma_engine_uninit（同步停音频线程）再释放内存，定义在 .cpp
struct MaEngineDeleter {
    void operator()(ma_engine* engine) const noexcept;
};

enum class MusicScene {
    MENU,
    GAME,
    CREATIVE,
    NONE
};

class MusicManager {
public:
    MusicManager();   // 公开构造
    ~MusicManager();  // 公开析构
    bool init();
    void shutdown();
    void tick();
    void playClickSound();
    void playOneShot(const std::string& resourcePath);
    void setScene(MusicScene scene);
    MusicScene getScene() const { return currentScene; }
    void setVolume(float volume);
    float getVolume() const { return musicVolume; }
    bool isInitialized() const { return initialized; }

private:
    struct SceneConfig {
        float minDelaySec;
        float maxDelaySec;
        bool  replaceCurrentMusic;
    };
    static SceneConfig getConfig(MusicScene scene);

    std::string extractOggToTemp(const std::string& resourcePath);
    void startPlaying(const std::string& resourcePath);
    void stopPlaying();
    void loadClickSound();
    std::vector<std::string> getMusicFiles() const;
    std::string pickRandomMusic() const;
    float randomDelay(float minSec, float maxSec) const;

    MinivorbisDataSource* currentDataSource = nullptr;

    bool initialized = false;
    std::unique_ptr<ma_engine, MaEngineDeleter> engine;
    void* currentSound = nullptr;
    void* audioBuffer = nullptr;
    short* pcmData = nullptr;

    short* clickPcmData = nullptr;
    size_t clickPcmSize = 0;
    int clickChannels = 0;
    int clickSampleRate = 0;
    bool clickSoundLoaded = false;

    struct OneShotSound {
        void* sound;
        void* buffer;
    };
    std::vector<OneShotSound> activeOneShots;

    MusicScene currentScene = MusicScene::MENU;
    std::string currentMusicPath;

    static constexpr float STARTING_DELAY = 5.0f;
    float nextSongDelay = STARTING_DELAY;
    float musicVolume = 1.0f;

    mutable std::mt19937 rng;
};
