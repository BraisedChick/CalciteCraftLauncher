/*
MinivorbisDataSource - 使 miniaudio 支持 OGG/Vorbis 流式播放
许可证：MIT-0（无限制授权）
依赖：miniaudio (https://github.com/mackron/miniaudio)
      minivorbis (https://github.com/edubart/minivorbis)

用法（单头文件模式）：
  在 *一个* c/cpp 文件中 #define MINIVORBIS_DATA_SOURCE_IMPL 再 #include 此文件：
    #define MINIVORBIS_DATA_SOURCE_IMPL
    #include "MinivorbisDataSource.h"

  其他文件正常 #include 即可：

  示例：
    MinivorbisDataSource ds;
    memset(&ds, 0, sizeof(ds));
    ov_fopen("music.ogg", &ds.vf);
    vorbis_info* vi = ov_info(&ds.vf, -1);
    ds.channels   = vi->channels;
    ds.sampleRate = (unsigned int)vi->rate;
    ds.base.vtable = minivorbis_datasource_get_vtable();
    ma_data_source_config dsConfig = ma_data_source_config_init();
    dsConfig.vtable = ds.base.vtable;
    ma_data_source_init(&dsConfig, (ma_data_source*)&ds);
    ma_sound_init_from_data_source(engine, (ma_data_source*)&ds, 0, NULL, &sound);
    ma_sound_start(&sound);
*/

#pragma once

#include "3rdparty/miniaudio.h"
#include "3rdparty/minivorbis/minivorbis.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ma_data_source_base base;   // 必须是第一个成员
    OggVorbis_File vf;          // 打开的 OGG 文件句柄
    int channels;
    unsigned int sampleRate;
} MinivorbisDataSource;

// 获取 vtable 指针，线程安全，可缓存
const ma_data_source_vtable* minivorbis_datasource_get_vtable(void);

#ifdef __cplusplus
}
#endif

// ============================================================================
// 实现（仅在定义了 MINIVORBIS_DATA_SOURCE_IMPL 时编译）
// ============================================================================
#ifdef MINIVORBIS_DATA_SOURCE_IMPL

#ifndef MINIVORBIS_DATA_SOURCE_IMPL_DONE
#define MINIVORBIS_DATA_SOURCE_IMPL_DONE

#include <cstring>

static ma_result minivorbis_datasource_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead) {
    auto* ds = (MinivorbisDataSource*)pDataSource;
    int frameSize = ds->channels * (int)sizeof(short);
    char* buf = (pFramesOut != nullptr) ? (char*)pFramesOut : nullptr;
    long bytes = ov_read(&ds->vf, buf, (int)(frameCount * frameSize), 0, 2, 1, nullptr);
    if (bytes <= 0) {
        if (pFramesRead) *pFramesRead = 0;
        return (bytes == 0) ? MA_AT_END : MA_ERROR;
    }
    if (pFramesRead) *pFramesRead = bytes / frameSize;
    return MA_SUCCESS;
}

static ma_result minivorbis_datasource_seek(ma_data_source* pDataSource, ma_uint64 frameIndex) {
    auto* ds = (MinivorbisDataSource*)pDataSource;
    return (ov_pcm_seek(&ds->vf, (ogg_int64_t)frameIndex) == 0) ? MA_SUCCESS : MA_ERROR;
}

static ma_result minivorbis_datasource_get_data_format(ma_data_source* pDataSource, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap) {
    auto* ds = (MinivorbisDataSource*)pDataSource;
    if (pFormat)   *pFormat = ma_format_s16;
    if (pChannels) *pChannels = (ma_uint32)ds->channels;
    if (pSampleRate) *pSampleRate = ds->sampleRate;
    if (pChannelMap && channelMapCap > 0) pChannelMap[0] = MA_CHANNEL_NONE;
    return MA_SUCCESS;
}

static ma_result minivorbis_datasource_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor) {
    auto* ds = (MinivorbisDataSource*)pDataSource;
    *pCursor = (ma_uint64)ov_pcm_tell(&ds->vf);
    return MA_SUCCESS;
}

static ma_result minivorbis_datasource_get_length(ma_data_source* pDataSource, ma_uint64* pLength) {
    auto* ds = (MinivorbisDataSource*)pDataSource;
    ogg_int64_t total = ov_pcm_total(&ds->vf, -1);
    if (total < 0) return MA_NOT_IMPLEMENTED;
    *pLength = (ma_uint64)total;
    return MA_SUCCESS;
}

static ma_data_source_vtable g_minivorbis_ds_vtable = {
    minivorbis_datasource_read,
    minivorbis_datasource_seek,
    minivorbis_datasource_get_data_format,
    minivorbis_datasource_get_cursor,
    minivorbis_datasource_get_length,
    nullptr,   // onSetLooping
    0
};

const ma_data_source_vtable* minivorbis_datasource_get_vtable(void) {
    return &g_minivorbis_ds_vtable;
}

#endif /* MINIVORBIS_DATA_SOURCE_IMPL_DONE */
#endif /* MINIVORBIS_DATA_SOURCE_IMPL */
