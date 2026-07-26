#define MINIVORBIS_DATA_SOURCE_IMPL
#include "MusicManager.h"
#include <android/log.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

#define MINIAUDIO_IMPLEMENTATION
#include "3rdparty/miniaudio.h"

#include "3rdparty/minivorbis/minivorbis.h"
#include "imgui.h"

#define LOG_TAG "MusicManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// 两段式销毁：uninit 内部会关闭设备流并 join 后台音频线程，之后释放内存才安全
void MaEngineDeleter::operator()(ma_engine* engine) const noexcept {
    if (engine) {
        ma_engine_uninit(engine);
        delete engine;
    }
}

MusicManager::MusicManager() {
    // 空构造，所有初始化在 init() 中完成
}

MusicManager::~MusicManager() {
    // 兜底清理：若外部未显式调用 shutdown()，析构时自愈（幂等）
    // 否则 ma_engine 不会 uninit，miniaudio 后台音频线程会在退出后继续播放
    shutdown();
}
MusicManager::SceneConfig MusicManager::getConfig(MusicScene scene) {
    switch (scene) {
        case MusicScene::MENU:     return {1.0f, 30.0f, true};
        case MusicScene::GAME:
        case MusicScene::CREATIVE: return {600.0f, 1200.0f, false};
        default:                   return {0.0f, 0.0f, false};
    }
}

float MusicManager::randomDelay(float minSec, float maxSec) const {
    if (minSec >= maxSec) return minSec;
    return std::uniform_real_distribution<float>(minSec, maxSec)(rng);
}

bool MusicManager::init() {
    if (initialized) return true;
    // init 成功前不交给 unique_ptr：失败时引擎未初始化，删除器里的 uninit 不可调用
    auto* eng = new ma_engine();
    ma_engine_config config = ma_engine_config_init();
    config.channels = 2;
    config.sampleRate = 44100;
    if (ma_engine_init(&config, eng) != MA_SUCCESS) {
        LOGE("Failed to init miniaudio engine"); delete eng; return false;
    }
    engine.reset(eng);
    rng.seed((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
    nextSongDelay = STARTING_DELAY;
    initialized = true;
    LOGI("MusicManager initialized, starting delay=%.1fs", nextSongDelay);
    loadClickSound();
    return true;
}

void MusicManager::shutdown() {
    if (!initialized) return;
    initialized = false;
    LOGI("MusicManager shutdown...");
    stopPlaying();
    engine.reset();  // 删除器负责 uninit + 释放
    for (auto& os : activeOneShots) {
        if (os.sound) delete static_cast<ma_sound*>(os.sound);
        if (os.buffer) delete static_cast<ma_audio_buffer*>(os.buffer);
    }
    activeOneShots.clear();
    if (pcmData) { delete[] pcmData; pcmData = nullptr; }
    if (clickPcmData) { delete[] clickPcmData; clickPcmData = nullptr; }
    clickSoundLoaded = false;
    currentMusicPath.clear();
    LOGI("MusicManager shutdown complete");
}

void MusicManager::tick() {
    if (!initialized || !engine || currentScene == MusicScene::NONE) return;
    SceneConfig cfg = getConfig(currentScene);
    if (currentSound) {
        auto* snd = static_cast<ma_sound*>(currentSound);
        if (!ma_sound_is_playing(snd)) {
            float interDelay = randomDelay(cfg.minDelaySec, cfg.maxDelaySec);
            if (interDelay < nextSongDelay) nextSongDelay = interDelay;
            LOGI("Music finished, next play in %.1fs", nextSongDelay);
            stopPlaying();
        }
    }
    if (nextSongDelay > cfg.maxDelaySec) nextSongDelay = cfg.maxDelaySec;
    if (!currentSound) {
        ImGuiIO& io = ImGui::GetIO();
        nextSongDelay -= io.DeltaTime;
        if (nextSongDelay <= 0.0f) {
            std::string music = pickRandomMusic();
            if (!music.empty()) startPlaying(music);
        }
    }
    for (auto it = activeOneShots.begin(); it != activeOneShots.end();) {
        auto* snd = static_cast<ma_sound*>(it->sound);
        if (!ma_sound_is_playing(snd)) {
            ma_sound_uninit(snd); delete snd;
            if (it->buffer) {
                ma_audio_buffer_uninit(static_cast<ma_audio_buffer*>(it->buffer));
                delete static_cast<ma_audio_buffer*>(it->buffer);
            }
            it = activeOneShots.erase(it);
        } else ++it;
    }
}

void MusicManager::setScene(MusicScene scene) {
    if (scene == currentScene) return;
    MusicScene prevScene = currentScene;
    currentScene = scene;
    if (scene == MusicScene::NONE) { stopPlaying(); return; }
    auto cfg = getConfig(scene);
    if (prevScene == MusicScene::MENU || prevScene == MusicScene::NONE) {
        stopPlaying();
        nextSongDelay = STARTING_DELAY;
        LOGI("Scene changed from %d to %d, stop music, next in %.1fs", (int)prevScene, (int)scene, nextSongDelay);
        return;
    }
    if (cfg.replaceCurrentMusic && currentSound) {
        stopPlaying();
        nextSongDelay = std::uniform_real_distribution<float>(0.0f, cfg.minDelaySec * 0.5f)(rng);
        LOGI("Scene changed to %d, replace music, next in %.1fs", (int)scene, nextSongDelay);
    } else if (!currentSound) {
        nextSongDelay = STARTING_DELAY;
        LOGI("Scene changed to %d, next in %.1fs", (int)scene, nextSongDelay);
    }
}

void MusicManager::setVolume(float volume) {
    musicVolume = std::max(0.0f, std::min(1.0f, volume));
    if (engine) ma_engine_set_volume(engine.get(), musicVolume);
}

std::string MusicManager::extractOggToTemp(const std::string& resourcePath) {
    std::string path = std::string("/storage/emulated/0/Android/data/com.calcite/files/sounds/") + resourcePath + ".ogg";
    FILE* f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return path; }
    LOGE("extractOggToTemp: sound not found at %s", path.c_str());
    return "";
}

void MusicManager::startPlaying(const std::string& resourcePath) {
    if (!engine) return;
    std::string filePath = extractOggToTemp(resourcePath);
    if (filePath.empty()) { LOGE("Cannot play music: %s", resourcePath.c_str()); return; }

    auto* ds = new MinivorbisDataSource();
    memset(ds, 0, sizeof(*ds));

    int err = ov_fopen(filePath.c_str(), &ds->vf);
    if (err != 0) { LOGE("ov_fopen failed: %s (err %d)", filePath.c_str(), err); delete ds; return; }

    auto* vi = ov_info(&ds->vf, -1);
    if (!vi) { ov_clear(&ds->vf); delete ds; return; }
    ds->channels = vi->channels;
    ds->sampleRate = (unsigned int)vi->rate;

    ma_data_source_config dsConfig = ma_data_source_config_init();
    dsConfig.vtable = minivorbis_datasource_get_vtable();
    if (ma_data_source_init(&dsConfig, (ma_data_source*)ds) != MA_SUCCESS) {
        LOGE("ma_data_source_init failed"); ov_clear(&ds->vf); delete ds; return;
    }

    auto* snd = new ma_sound();
    if (ma_sound_init_from_data_source(engine.get(), (ma_data_source*)ds, 0, nullptr, snd) != MA_SUCCESS) {
        LOGE("ma_sound_init_from_data_source failed for %s", filePath.c_str());
        ma_data_source_uninit((ma_data_source*)ds); ov_clear(&ds->vf); delete ds; delete snd;
        return;
    }

    ma_sound_set_volume(snd, musicVolume);
    ma_sound_start(snd);

    currentSound = snd;
    currentDataSource = ds;
    currentMusicPath = resourcePath;
    nextSongDelay = 99999.0f;
    LOGI("Playing music (streaming): %s", resourcePath.c_str());
}

void MusicManager::stopPlaying() {
    if (currentSound) {
        auto* snd = static_cast<ma_sound*>(currentSound);
        ma_sound_stop(snd);
        ma_sound_uninit(snd);
        delete snd;
        currentSound = nullptr;
    }
    if (currentDataSource) {
        ov_clear(&currentDataSource->vf);
        ma_data_source_uninit((ma_data_source*)currentDataSource);
        delete currentDataSource;
        currentDataSource = nullptr;
    }
    if (audioBuffer) {
        ma_audio_buffer_uninit(static_cast<ma_audio_buffer*>(audioBuffer));
        delete static_cast<ma_audio_buffer*>(audioBuffer);
        audioBuffer = nullptr;
    }
    if (pcmData) { delete[] pcmData; pcmData = nullptr; }
    currentMusicPath.clear();
    if (nextSongDelay < 99999.0f) nextSongDelay += STARTING_DELAY;
}

void MusicManager::loadClickSound() {
    if (clickSoundLoaded) return;
    std::string filePath = extractOggToTemp("random/click_stereo");
    if (filePath.empty()) filePath = extractOggToTemp("random/click");
    if (filePath.empty()) { LOGW("Click sound not found"); return; }

    OggVorbis_File vf;
    if (ov_fopen(filePath.c_str(), &vf) != 0) return;
    auto* vi = ov_info(&vf, -1);
    if (!vi) { ov_clear(&vf); return; }
    clickChannels = vi->channels;
    clickSampleRate = (int)vi->rate;

    std::vector<short> pcm;
    pcm.reserve(clickSampleRate * clickChannels);
    char buf[4096];
    int bs = 0;
    long n;
    while ((n = ov_read(&vf, buf, sizeof(buf), 0, 2, 1, &bs)) > 0)
        pcm.insert(pcm.end(), (short*)buf, (short*)buf + n / 2);
    ov_clear(&vf);
    if (pcm.empty()) return;

    clickPcmSize = pcm.size();
    clickPcmData = new short[clickPcmSize];
    memcpy(clickPcmData, pcm.data(), clickPcmSize * sizeof(short));
    clickSoundLoaded = true;
    LOGI("Click sound loaded: %dch %dHz %zums", clickChannels, clickSampleRate,
         (clickPcmSize / clickChannels) * 1000 / clickSampleRate);
}

void MusicManager::playClickSound() {
    if (!initialized || !engine || !clickSoundLoaded) return;
    auto cfg = ma_audio_buffer_config_init(ma_format_s16, clickChannels, clickPcmSize / clickChannels, clickPcmData, nullptr);
    cfg.sampleRate = clickSampleRate;
    auto* buf = new ma_audio_buffer();
    if (ma_audio_buffer_init_copy(&cfg, buf) != MA_SUCCESS) { delete buf; return; }
    auto* snd = new ma_sound();
    if (ma_sound_init_from_data_source(engine.get(), buf, 0, nullptr, snd) != MA_SUCCESS) {
        ma_audio_buffer_uninit(buf); delete buf; delete snd; return;
    }
    ma_sound_set_volume(snd, 0.25f);
    ma_sound_start(snd);
    activeOneShots.push_back({snd, buf});
}

void MusicManager::playOneShot(const std::string& resourcePath) {
    if (!initialized || !engine) return;
    std::string filePath = extractOggToTemp(resourcePath);
    if (filePath.empty()) return;
    OggVorbis_File vf;
    if (ov_fopen(filePath.c_str(), &vf) != 0) return;
    auto* vi = ov_info(&vf, -1);
    if (!vi) { ov_clear(&vf); return; }
    int ch = vi->channels;
    long sr = vi->rate;
    std::vector<short> pcm;
    pcm.reserve(sr * ch);
    char buf[4096];
    int bs = 0; long n;
    while ((n = ov_read(&vf, buf, sizeof(buf), 0, 2, 1, &bs)) > 0)
        pcm.insert(pcm.end(), (short*)buf, (short*)buf + n / 2);
    ov_clear(&vf);
    if (pcm.empty()) return;
    auto cfg = ma_audio_buffer_config_init(ma_format_s16, ch, (ma_uint64)pcm.size() / ch, pcm.data(), nullptr);
    cfg.sampleRate = (ma_uint32)sr;
    auto* abuf = new ma_audio_buffer();
    if (ma_audio_buffer_init_copy(&cfg, abuf) != MA_SUCCESS) { delete abuf; return; }
    auto* snd = new ma_sound();
    if (ma_sound_init_from_data_source(engine.get(), abuf, 0, nullptr, snd) != MA_SUCCESS) {
        ma_audio_buffer_uninit(abuf); delete abuf; delete snd; return;
    }
    ma_sound_set_volume(snd, 1.0f);
    ma_sound_start(snd);
    activeOneShots.push_back({snd, abuf});
}

std::vector<std::string> MusicManager::getMusicFiles() const {
    switch (currentScene) {
        case MusicScene::MENU:
            return {"music/menu/menu1","music/menu/menu2","music/menu/menu3","music/menu/menu4"};
        case MusicScene::GAME:
            return {"music/game/calm1","music/game/calm2","music/game/calm3",
                    "music/game/hal1","music/game/hal2","music/game/hal3","music/game/hal4",
                    "music/game/piano1","music/game/piano2","music/game/piano3",
                    "music/game/nuance1","music/game/nuance2",
                    "music/game/an_ordinary_day","music/game/comforting_memories",
                    "music/game/floating_dream","music/game/infinite_amethyst",
                    "music/game/left_to_bloom","music/game/one_more_day",
                    "music/game/stand_tall","music/game/wending"};
        case MusicScene::CREATIVE:
            return {"music/game/creative/creative1","music/game/creative/creative2",
                    "music/game/creative/creative3","music/game/creative/creative4",
                    "music/game/creative/creative5","music/game/creative/creative6"};
        default: return {};
    }
}

std::string MusicManager::pickRandomMusic() const {
    auto files = getMusicFiles();
    if (files.empty()) return "";
    int idx = (int)std::uniform_int_distribution<int>(0, (int)files.size() - 1)(rng);
    if (files.size() > 1 && files[idx] == currentMusicPath)
        idx = (idx + 1) % (int)files.size();
    return files[idx];
}
