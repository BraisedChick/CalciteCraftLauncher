#include "utils.h"
#include <string>
#include <cstdio>

void printBytes(const std::vector<uint8_t>& data, const char* name) {
    LOGI("%s (%zu bytes):", name, data.size());
    std::string hex;
    for (size_t i = 0; i < data.size(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        hex += buf;
        if ((i + 1) % 16 == 0) {
            LOGI("%s", hex.c_str());
            hex.clear();
        }
    }
    if (!hex.empty()) LOGI("%s", hex.c_str());
}