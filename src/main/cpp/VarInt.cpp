#include "VarInt.h"

std::vector<uint8_t> VarInt::encode(int value) {
    std::vector<uint8_t> out;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) byte |= 0x80;
        out.push_back(byte);
    } while (value != 0);
    return out;
}

int VarInt::decode(const std::vector<uint8_t>& data, size_t& pos) {
    int result = 0;
    int shift = 0;
    while (pos < data.size()) {
        uint8_t byte = data[pos++];
        result |= (byte & 0x7F) << shift;
        shift += 7;
        if ((byte & 0x80) == 0) return result;
    }
    return -1;
}

std::vector<uint8_t> VarInt::encodeVarLong(long long value) {
    std::vector<uint8_t> out;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) byte |= 0x80;
        out.push_back(byte);
    } while (value != 0);
    return out;
}