#pragma once

#include <string>
#include <vector>
#include <random>
#include <chrono>

#include "MinivorbisDataSource.h"

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
    void* engine = nullptr;
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
