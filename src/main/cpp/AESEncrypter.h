#pragma once

#include <vector>
#include <cstdint>
#include <array>

/**
 * AES-128-CFB8 流加密器
 * 纯 C++ 实现，不依赖 OpenSSL/BoringSSL
 *
 * 用于 Minecraft 在线模式服务器的加密通信：
 * - Init(): 用共享密钥初始化 AES-128-CFB8 上下文
 * - Encrypt()/Decrypt(): 对网络数据流进行加解密
 *
 * 算法说明：
 * AES-128-CFB8 是一种流加密模式，每次处理 1 字节：
 * 1. 加密当前移位寄存器（16字节）得到密钥流块
 * 2. 取密钥流块第 1 字节与明文字节 XOR → 密文字节
 * 3. 移位寄存器左移 1 字节，末尾补入密文字节（加密时）或密文字节（解密时）
 */
class AESEncrypter {
public:
    AESEncrypter();
    ~AESEncrypter();

    /**
     * 用共享密钥初始化加密上下文
     * 共享密钥同时用作 AES-128 的密钥和 CFB8 的 IV（与 Botcraft/OpenSSL 行为一致）
     *
     * @param sharedSecret 16 字节共享密钥（由 Java 层生成并传回）
     */
    void Init(const std::vector<unsigned char>& sharedSecret);

    /** 加密数据（发送前调用）*/
    std::vector<unsigned char> Encrypt(const std::vector<unsigned char>& in);

    /** 解密数据（接收后调用）*/
    std::vector<unsigned char> Decrypt(const std::vector<unsigned char>& in);

    bool isInitialized() const { return initialized; }

private:
    static constexpr int AES_BLOCK_SIZE = 16;
    static constexpr int AES128_ROUNDS = 10;
    static constexpr int EXPANDED_KEY_SIZE = 176; // (AES128_ROUNDS + 1) * 16

    uint8_t expandedKey[EXPANDED_KEY_SIZE];

    // CFB8 加密移位寄存器（发送方向）
    std::array<uint8_t, AES_BLOCK_SIZE> encShiftReg;
    // CFB8 解密移位寄存器（接收方向）
    std::array<uint8_t, AES_BLOCK_SIZE> decShiftReg;

    bool initialized = false;

    // AES-128 内部函数
    static void KeyExpansion(const uint8_t key[16], uint8_t out[EXPANDED_KEY_SIZE]);
    static void AES128_EncryptBlock(const uint8_t input[16], const uint8_t expandedKey[EXPANDED_KEY_SIZE], uint8_t output[16]);

    // CFB8 流加解密（Minecraft 协议标准模式）
    void encryptCFB8(const uint8_t* input, uint8_t* output, size_t length);
    void decryptCFB8(const uint8_t* input, uint8_t* output, size_t length);
};
