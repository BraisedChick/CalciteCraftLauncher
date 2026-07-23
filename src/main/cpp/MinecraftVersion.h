#pragma once
#include <cstdint>
#include <string>

// Minecraft 协议版本常量
namespace ProtocolVersion {
    // 主要版本对应的协议号
    constexpr int V1_8   = 47;
    constexpr int V1_9   = 107;
    constexpr int V1_10  = 210;
    constexpr int V1_11  = 315;
    constexpr int V1_12  = 340;
    constexpr int V1_13  = 404;
    constexpr int V1_14  = 498;
    constexpr int V1_15  = 578;
    constexpr int V1_16  = 735;
    constexpr int V1_16_2 = 751;
    constexpr int V1_17  = 755;
    constexpr int V1_17_1 = 756;
    constexpr int V1_18  = 757;
    constexpr int V1_18_2 = 758;
    constexpr int V1_19  = 759;
    constexpr int V1_19_1 = 760;
    constexpr int V1_19_3 = 761;
    constexpr int V1_19_4 = 762;
    constexpr int V1_20  = 763;
    constexpr int V1_20_2 = 764;
    constexpr int V1_20_5 = 766;
    constexpr int V1_21  = 767;
    constexpr int V1_21_2 = 768;
    constexpr int V1_21_4 = 769;
    constexpr int V1_21_5 = 770;
    constexpr int V1_21_8 = 772;
    constexpr int V1_21_11 = 774;
}

// 编译期：协议版本号 → 显示名称
constexpr const char* getProtocolVersionName(int version) {
    switch (version) {
        case 47:   return "1.8";
        case 107:  return "1.9";
        case 210:  return "1.10";
        case 315:  return "1.11";
        case 340:  return "1.12";
        case 404:  return "1.13";
        case 498:  return "1.14";
        case 578:  return "1.15";
        case 735:  return "1.16";
        case 751:  return "1.16.2";
        case 755:  return "1.17";
        case 756:  return "1.17.1";
        case 757:  return "1.18";
        case 758:  return "1.18.2";
        case 759:  return "1.19";
        case 760:  return "1.19.1";
        case 761:  return "1.19.3";
        case 762:  return "1.19.4";
        case 763:  return "1.20";
        case 764:  return "1.20.2";
        case 766:  return "1.20.5";
        case 767:  return "1.21";
        case 768:  return "1.21.2";
        case 769:  return "1.21.4";
        case 770:  return "1.21.5";
        case 772:  return "1.21.8";
        case 774:  return "1.21.11";
        default:   return (version > 774) ? "1.22+ (future)" : "Unknown";
    }
}
