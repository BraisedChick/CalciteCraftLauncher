/**
 * AES-128 硬件加速实现（ARMv8 Crypto Extensions）
 * 此文件使用 -march=armv8-a+crypto 编译，仅包含硬件加速函数
 */

#include <cstdint>
#include <cstddef>
#include <arm_neon.h>
#include <arm_acle.h>

extern "C" {

/**
 * ARMv8 AES 加密块（使用硬件指令）
 * @param input  输入 16 字节明文块
 * @param expandedKey  扩展密钥 176 字节（11 轮 × 16 字节）
 * @param output  输出 16 字节密文块
 */
void AES128_EncryptBlock_ARMv8(const uint8_t input[16],
                                const uint8_t expandedKey[176],
                                uint8_t output[16]) {
    // 加载输入数据到 NEON 寄存器
    uint8x16_t data = vld1q_u8(input);
    
    // 加载扩展密钥（11 轮，每轮 16 字节）
    uint8x16_t rk[11];
    for (int i = 0; i < 11; ++i) {
        rk[i] = vld1q_u8(expandedKey + i * 16);
    }
    
    // 初始轮密钥加
    data = veorq_u8(data, rk[0]);
    
    // 9 轮完整加密（SubBytes + ShiftRows + MixColumns + AddRoundKey）
    for (int i = 1; i <= 9; ++i) {
        data = vaeseq_u8(data, vdupq_n_u8(0));      // AESE: SubBytes + ShiftRows
        data = vaesmcq_u8(data);                     // AESMC: MixColumns
        data = veorq_u8(data, rk[i]);                // AddRoundKey
    }
    
    // 最后一轮（无 MixColumns）
    data = vaeseq_u8(data, vdupq_n_u8(0));          // SubBytes + ShiftRows
    data = veorq_u8(data, rk[10]);                   // AddRoundKey
    
    // 存储结果
    vst1q_u8(output, data);
}

/**
 * ARMv8 硬件加速的 CFB8 批处理（加密/解密通用）
 * 轮密钥仅在循环外加载一次常驻 NEON 寄存器，
 * 移位寄存器用 vextq 单指令完成左移+补入，
 * 相比逐字节调用单块函数省去每字节的调用开销与 176 字节密钥重装
 *
 * 支持 input == output 原地处理（先读入字节再写输出）
 *
 * @param input       输入数据（加密时=明文，解密时=密文）
 * @param output      输出数据（加密时=密文，解密时=明文）
 * @param length      数据长度（字节）
 * @param expandedKey 扩展密钥（176 字节）
 * @param shiftReg    移位寄存器（16 字节），会被原地更新
 * @param isEncrypt   true=加密（反馈密文输出），false=解密（反馈密文输入）
 */
void AES128_CFB8_Process_ARMv8(const uint8_t* input,
                                uint8_t* output,
                                size_t length,
                                const uint8_t expandedKey[176],
                                uint8_t shiftReg[16],
                                bool isEncrypt) {
    if (length == 0) return;

    // 预加载全部轮密钥（仅此一次）
    uint8x16_t rk[11];
    for (int i = 0; i < 11; ++i) {
        rk[i] = vld1q_u8(expandedKey + i * 16);
    }

    // 加载当前移位寄存器
    uint8x16_t state = vld1q_u8(shiftReg);

    for (size_t i = 0; i < length; ++i) {
        // AES 加密当前移位寄存器（CFB 解密也是用加密方向）
        uint8x16_t data = veorq_u8(state, rk[0]);
        for (int round = 1; round <= 9; ++round) {
            data = vaeseq_u8(data, vdupq_n_u8(0));   // SubBytes + ShiftRows
            data = vaesmcq_u8(data);                  // MixColumns
            data = veorq_u8(data, rk[round]);         // AddRoundKey
        }
        data = vaeseq_u8(data, vdupq_n_u8(0));        // 最后一轮（无 MixColumns）
        data = veorq_u8(data, rk[10]);

        // 先读入字节再写输出，保证 input == output 原地处理时反馈字节不被覆盖
        const uint8_t inByte = input[i];
        const uint8_t result = inByte ^ vgetq_lane_u8(data, 0);
        output[i] = result;

        // 移位寄存器反馈密文：加密时是输出，解密时是输入
        const uint8_t feedback = isEncrypt ? result : inByte;
        // vextq_u8(a, b, 1) = a[1..15] + b[0]：单指令完成左移 1 字节 + 末尾补入
        state = vextq_u8(state, vdupq_n_u8(feedback), 1);
    }

    // 保存更新后的移位寄存器
    vst1q_u8(shiftReg, state);
}

} // extern "C"
