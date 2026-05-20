#pragma once
#include <vector>
#include <cstdint>

class Compression {
public:
    static void setEnabled(bool enabled);
    static bool isEnabled();
    static void setReceiveEnabled(bool enabled);
    static bool isReceiveEnabled();
    static void setThreshold(int threshold);
    static int getThreshold();
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input, int uncompressedLength);
};