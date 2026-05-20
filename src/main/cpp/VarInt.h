#pragma once
#include <vector>
#include <cstdint>

class VarInt {
public:
    static std::vector<uint8_t> encode(int value);
    static int decode(const std::vector<uint8_t>& data, size_t& pos);
    static std::vector<uint8_t> encodeVarLong(long long value);
};