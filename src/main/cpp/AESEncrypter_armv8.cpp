/**
 * AES-128 硬件加速实现（ARMv8 Crypto Extensions）
 * 此文件使用 -march=armv8-a+crypto 编译，仅包含硬件加速函数
 */

#include <cstdint>
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

} // extern "C"
