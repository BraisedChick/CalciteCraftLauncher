#include "Compression.h"
#include <zlib.h>
#include "utils.h"

static bool g_compressionEnabled = false;
static bool g_receiveCompressionEnabled = false;
static int g_compressionThreshold = -1;

void Compression::setEnabled(bool enabled) {
    g_compressionEnabled = enabled;
}
bool Compression::isEnabled() {
    return g_compressionEnabled;
}

void Compression::setReceiveEnabled(bool enabled) {
    g_receiveCompressionEnabled = enabled;
}
bool Compression::isReceiveEnabled() {
    return g_receiveCompressionEnabled;
}

void Compression::setThreshold(int threshold) { g_compressionThreshold = threshold; }
int Compression::getThreshold() { return g_compressionThreshold; }

std::vector<uint8_t> Compression::compress(const std::vector<uint8_t>& input) {
    uLongf destLen = compressBound(input.size());
    std::vector<uint8_t> output(destLen);
    int ret = ::compress(output.data(), &destLen, input.data(), input.size());
    if (ret != Z_OK) {
        LOGE("Compression failed: %d", ret);
        return {};
    }
    output.resize(destLen);
    return output;
}

std::vector<uint8_t> Compression::decompress(const std::vector<uint8_t>& input, int uncompressedLength) {
    return decompress(input.data(), input.size(), uncompressedLength);
}

std::vector<uint8_t> Compression::decompress(const uint8_t* data, size_t size, int uncompressedLength) {
    std::vector<uint8_t> output(uncompressedLength);
    uLongf destLen = uncompressedLength;
    int ret = uncompress(output.data(), &destLen, data, size);
    if (ret != Z_OK) {
        LOGE("Decompression failed: %d", ret);
        return {};
    }
    return output;
}