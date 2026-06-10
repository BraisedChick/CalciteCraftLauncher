/**
 * AES-128-CFB8 流加密器
 * 支持两种模式：
 * 1. ARMv8 Crypto Extensions（硬件加速）- 如果 CPU 支持
 * 2. 纯 C++ 实现（回退方案）- 所有 ARM64 CPU
 *
 * 运行时自动检测 CPU 特性并选择最优实现
 */

#include "AESEncrypter.h"
#include "utils.h"
#include <chrono>

// ARM64 CPU 特性检测
#if defined(__aarch64__) || defined(_M_ARM64)
    #include <sys/auxv.h>
    #include <asm/hwcap.h>
#endif

// ARMv8 硬件加速函数（在 AESEncrypter_armv8.cpp 中实现）
extern "C" void AES128_EncryptBlock_ARMv8(const uint8_t input[16],
                                           const uint8_t expandedKey[176],
                                           uint8_t output[16]);

// 检测 CPU 是否支持 ARMv8 Crypto Extensions
static bool HasARMCryptoSupport() {
    #if defined(__aarch64__) || defined(_M_ARM64)
        // 读取硬件能力标志
        unsigned long hwcap = getauxval(AT_HWCAP);
        // HWCAP_AES 表示支持 AES 指令
        return (hwcap & HWCAP_AES) != 0;
    #else
        return false;
    #endif
}

// AES 加密块函数指针类型
typedef void (*AESEncryptBlockFunc)(const uint8_t input[16],
                                     const uint8_t expandedKey[176],
                                     uint8_t output[16]);

// 全局函数指针和状态
static AESEncryptBlockFunc g_aesEncryptBlockFunc = nullptr;
static bool g_hardwareAESChecked = false;

// ============================================================
// AES S-Box（FIPS 197 标准替换盒）
// ============================================================
static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

// AES 轮常量（AES-128 只用前 10 个）
static const uint8_t rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

// ============================================================
// AES-128 密钥扩展
// ============================================================
void AESEncrypter::KeyExpansion(const uint8_t key[16], uint8_t out[EXPANDED_KEY_SIZE]) {
    // 复制原始密钥到前 16 字节
    for (int i = 0; i < 16; ++i) {
        out[i] = key[i];
    }

    // 扩展 4 字节为一组，共需 44 组（44 * 4 = 176 字节）
    for (int i = 4; i < 44; ++i) {
        uint8_t temp[4] = {
            out[(i - 1) * 4 + 0],
            out[(i - 1) * 4 + 1],
            out[(i - 1) * 4 + 2],
            out[(i - 1) * 4 + 3]
        };

        if (i % 4 == 0) {
            // RotWord: 循环左移 1 字节
            uint8_t t = temp[0];
            temp[0] = temp[1];
            temp[1] = temp[2];
            temp[2] = temp[3];
            temp[3] = t;

            // SubWord: S-Box 替换
            temp[0] = sbox[temp[0]];
            temp[1] = sbox[temp[1]];
            temp[2] = sbox[temp[2]];
            temp[3] = sbox[temp[3]];

            // Rcon 异或
            temp[0] ^= rcon[i / 4];
        }

        out[i * 4 + 0] = out[(i - 4) * 4 + 0] ^ temp[0];
        out[i * 4 + 1] = out[(i - 4) * 4 + 1] ^ temp[1];
        out[i * 4 + 2] = out[(i - 4) * 4 + 2] ^ temp[2];
        out[i * 4 + 3] = out[(i - 4) * 4 + 3] ^ temp[3];
    }
}

// ============================================================
// AES-128 单块加密（ECB 模式加密一个 16 字节块）
// 使用函数指针调度：首次调用时检测并选择最优实现
// ============================================================
void AESEncrypter::AES128_EncryptBlock(const uint8_t input[16],
                                        const uint8_t expandedKey[EXPANDED_KEY_SIZE],
                                        uint8_t output[16]) {
    // 首次调用时检测 CPU 特性并设置函数指针
    if (!g_hardwareAESChecked) {
        if (HasARMCryptoSupport()) {
            g_aesEncryptBlockFunc = AES128_EncryptBlock_ARMv8;
            LOGI("AES: Using ARMv8 Crypto Extensions (hardware acceleration)");
        } else {
            // 使用纯 C++ 实现（内联在下方）
            g_aesEncryptBlockFunc = nullptr;  // nullptr 表示使用内联实现
            LOGI("AES: Using pure C++ implementation");
        }
        g_hardwareAESChecked = true;
    }
    
    // 如果设置了硬件加速函数指针，直接调用
    if (g_aesEncryptBlockFunc) {
        g_aesEncryptBlockFunc(input, expandedKey, output);
        return;
    }
    
    // 回退到纯 C++ 实现（内联）
    uint8_t state[16];
    for (int i = 0; i < 16; ++i) {
        state[i] = input[i];
    }

    // 初始轮密钥加
    for (int i = 0; i < 16; ++i) {
        state[i] ^= expandedKey[i];
    }

    // 主循环（9 轮）
    for (int round = 1; round <= AES128_ROUNDS - 1; ++round) {
        // SubBytes: S-Box 替换
        for (int i = 0; i < 16; ++i) {
            state[i] = sbox[state[i]];
        }

        // ShiftRows: 行移位
        // state 按列优先排列：state[col*4 + row]
        // Row 0: 不移
        // Row 1: 左移 1
        uint8_t t = state[1];
        state[1]  = state[5];  state[5]  = state[9];  state[9]  = state[13]; state[13] = t;
        // Row 2: 左移 2
        t = state[2]; state[2] = state[10]; state[10] = t;
        t = state[6]; state[6] = state[14]; state[14] = t;
        // Row 3: 左移 3 (= 右移 1)
        t = state[15];
        state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = t;

        // MixColumns: 列混合
        for (int c = 0; c < 4; ++c) {
            uint8_t s0 = state[c * 4 + 0];
            uint8_t s1 = state[c * 4 + 1];
            uint8_t s2 = state[c * 4 + 2];
            uint8_t s3 = state[c * 4 + 3];

            // xtime(a) = (a << 1) ^ ((a >> 7) * 0x1b)  — GF(2^8) 乘 2
            auto xt = [](uint8_t a) -> uint8_t {
                return (uint8_t)((a << 1) ^ ((a >> 7) * 0x1b));
            };

            state[c * 4 + 0] = xt(s0) ^ (xt(s1) ^ s1) ^ s2 ^ s3;
            state[c * 4 + 1] = s0 ^ xt(s1) ^ (xt(s2) ^ s2) ^ s3;
            state[c * 4 + 2] = s0 ^ s1 ^ xt(s2) ^ (xt(s3) ^ s3);
            state[c * 4 + 3] = (xt(s0) ^ s0) ^ s1 ^ s2 ^ xt(s3);
        }

        // AddRoundKey
        for (int i = 0; i < 16; ++i) {
            state[i] ^= expandedKey[round * 16 + i];
        }
    }

    // 最后一轮（无 MixColumns）
    // SubBytes
    for (int i = 0; i < 16; ++i) {
        state[i] = sbox[state[i]];
    }

    // ShiftRows
    uint8_t t = state[1];
    state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = t;
    t = state[2]; state[2] = state[10]; state[10] = t;
    t = state[6]; state[6] = state[14]; state[14] = t;
    t = state[15];
    state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = t;

    // AddRoundKey
    for (int i = 0; i < 16; ++i) {
        state[i] ^= expandedKey[AES128_ROUNDS * 16 + i];
    }

    for (int i = 0; i < 16; ++i) {
        output[i] = state[i];
    }
}

// ============================================================
// 构造 / 析构
// ============================================================
AESEncrypter::AESEncrypter() {
    memset(expandedKey, 0, sizeof(expandedKey));
    encShiftReg.fill(0);
    decShiftReg.fill(0);
}

AESEncrypter::~AESEncrypter() = default;

// ============================================================
// Init: 用共享密钥初始化 AES-128-CFB8
// 与 Botcraft/OpenSSL 行为一致：key = sharedSecret, IV = sharedSecret
// ============================================================
void AESEncrypter::Init(const std::vector<unsigned char>& sharedSecret) {
    if (sharedSecret.size() != 16) {
        LOGE("AESEncrypter: shared secret must be 16 bytes, got %zu", sharedSecret.size());
        return;
    }

    // 密钥扩展
    KeyExpansion(sharedSecret.data(), expandedKey);

    // 初始化移位寄存器 = IV = sharedSecret
    for (int i = 0; i < AES_BLOCK_SIZE; ++i) {
        encShiftReg[i] = sharedSecret[i];
        decShiftReg[i] = sharedSecret[i];
    }

    initialized = true;
    LOGI("AESEncrypter: AES-128-CFB8 initialized (pure C++, no OpenSSL)");
}

// ============================================================
// CFB8 加密：逐字节处理
// ============================================================
void AESEncrypter::encryptCFB8(const uint8_t* input, uint8_t* output, size_t length) {
    uint8_t encryptedBlock[16];

    for (size_t i = 0; i < length; ++i) {
        // 加密当前移位寄存器
        AES128_EncryptBlock(encShiftReg.data(), expandedKey, encryptedBlock);

        // 明文字节 XOR 密钥流首字节 → 密文字节
        output[i] = input[i] ^ encryptedBlock[0];

        // 移位寄存器左移 1 字节，末尾补入密文字节
        for (int j = 0; j < AES_BLOCK_SIZE - 1; ++j) {
            encShiftReg[j] = encShiftReg[j + 1];
        }
        encShiftReg[AES_BLOCK_SIZE - 1] = output[i];
    }
}

// ============================================================
// CFB8 解密：逐字节处理
// 注意：解密也用 AES 加密（不是解密）！这是 CFB 模式的特性
// ============================================================
void AESEncrypter::decryptCFB8(const uint8_t* input, uint8_t* output, size_t length) {
    uint8_t encryptedBlock[16];

    for (size_t i = 0; i < length; ++i) {
        // 加密当前移位寄存器（注意：仍然是加密操作）
        AES128_EncryptBlock(decShiftReg.data(), expandedKey, encryptedBlock);

        // 密文字节 XOR 密钥流首字节 → 明文字节
        output[i] = input[i] ^ encryptedBlock[0];

        // 移位寄存器左移 1 字节，末尾补入密文字节（不是明文！）
        for (int j = 0; j < AES_BLOCK_SIZE - 1; ++j) {
            decShiftReg[j] = decShiftReg[j + 1];
        }
        decShiftReg[AES_BLOCK_SIZE - 1] = input[i];
    }
}

// ============================================================
// Encrypt / Decrypt 公开接口
// ============================================================
std::vector<unsigned char> AESEncrypter::Encrypt(const std::vector<unsigned char>& in) {
    if (!initialized) {
        LOGW("AESEncrypter: trying to encrypt while not initialized");
        return in;
    }
    
    std::vector<unsigned char> output(in.size());
    encryptCFB8(in.data(), output.data(), in.size());
    return output;
}

std::vector<unsigned char> AESEncrypter::Decrypt(const std::vector<unsigned char>& in) {
    if (!initialized) {
        LOGW("AESEncrypter: trying to decrypt while not initialized");
        return in;
    }
    
    std::vector<unsigned char> output(in.size());
    decryptCFB8(in.data(), output.data(), in.size());
    return output;
}
