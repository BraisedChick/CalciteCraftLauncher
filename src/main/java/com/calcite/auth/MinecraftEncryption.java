package com.calcite.auth;

import android.util.Log;

import java.security.KeyFactory;
import java.security.MessageDigest;
import java.security.PublicKey;
import java.security.SecureRandom;
import java.security.spec.X509EncodedKeySpec;
import java.util.Arrays;

import javax.crypto.Cipher;

/**
 * Minecraft 服务器加密处理
 * <p>
 * 参考 Botcraft Authentifier.cpp 的 JoinServer 和 AESEncrypter.cpp 的 Init 方法，
 * 使用 Android 内置 javax.crypto 替代 OpenSSL。
 * <p>
 * 流程（参考 https://wiki.vg/Protocol_Encryption）：
 * 1. 生成 16 字节共享密钥
 * 2. 计算 Minecraft 特殊格式 SHA1 服务器哈希
 * 3. 加入 Session Server
 * 4. 用服务器公钥 RSA 加密共享密钥和验证令牌
 */
public class MinecraftEncryption {
    private static final String TAG = "MCrypto";

    /**
     * 加密响应结果，传回 C++ 层
     */
    public static class EncryptionResponse {
        public final byte[] sharedSecret;       // 原始共享密钥（C++ 用于初始化 AES 流加密）
        public final byte[] encryptedSecret;    // RSA 加密后的共享密钥（发送给服务器）
        public final byte[] encryptedVerifyToken; // RSA 加密后的验证令牌（发送给服务器）

        public EncryptionResponse(byte[] sharedSecret, byte[] encryptedSecret, byte[] encryptedVerifyToken) {
            this.sharedSecret = sharedSecret;
            this.encryptedSecret = encryptedSecret;
            this.encryptedVerifyToken = encryptedVerifyToken;
        }
    }

    /**
     * 处理服务器的 Encryption Request
     * <p>
     * 参考 Botcraft AESEncrypter::Init + Authentifier::JoinServer
     *
     * @param serverID      服务器 ID（ASCII 字符串）
     * @param publicKeyDER  服务器公钥（DER 编码的 X.509 SubjectPublicKeyInfo）
     * @param verifyToken   服务器验证令牌
     * @param accessToken   MC 访问令牌
     * @param playerUuid    玩家 UUID（无横线）
     * @return 加密响应数据
     */
    public static EncryptionResponse handleEncryptionRequest(
            String serverID, byte[] publicKeyDER, byte[] verifyToken,
            String accessToken, String playerUuid) throws Exception {

        Log.i(TAG, "Handling encryption request, serverID len=" + serverID.length()
                + ", pubKey len=" + publicKeyDER.length + ", verifyToken len=" + verifyToken.length);

        // 1. 生成 16 字节共享密钥（参考 Botcraft AESEncrypter::Init）
        byte[] sharedSecret = new byte[16];
        new SecureRandom().nextBytes(sharedSecret);

        // 2. 计算 Minecraft 服务器哈希
        //    参考 Botcraft Authentifier::JoinServer 中的 SHA1 计算
        String serverHash = computeServerHash(serverID, sharedSecret, publicKeyDER);
        Log.i(TAG, "Server hash: " + serverHash);

        // 3. 加入 Session Server（参考 Botcraft Authentifier::JoinServer）
        boolean joinResult = MicrosoftAuthService.joinServer(accessToken, playerUuid, serverHash);
        if (!joinResult) {
            throw new Exception("Session server join failed");
        }
        Log.i(TAG, "Session join successful");

        // 4. 用服务器公钥加密共享密钥和验证令牌
        //    参考 Botcraft AESEncrypter::Init 中的 RSA_public_encrypt
        PublicKey publicKey = KeyFactory.getInstance("RSA")
                .generatePublic(new X509EncodedKeySpec(publicKeyDER));

        Cipher cipher = Cipher.getInstance("RSA/ECB/PKCS1Padding");
        cipher.init(Cipher.ENCRYPT_MODE, publicKey);

        byte[] encryptedSecret = cipher.doFinal(sharedSecret);
        byte[] encryptedVerifyToken = cipher.doFinal(verifyToken);

        Log.i(TAG, "Encryption response ready, encSecret len=" + encryptedSecret.length
                + ", encVerifyToken len=" + encryptedVerifyToken.length);

        return new EncryptionResponse(sharedSecret, encryptedSecret, encryptedVerifyToken);
    }

    /**
     * 计算 Minecraft 特殊格式的 SHA1 hexdigest
     * <p>
     * 直接搬自 Botcraft Authentifier::JoinServer 中的 SHA1 计算逻辑：
     * - SHA1(server_id + shared_secret + public_key)
     * - 如果结果为负数（最高位为1），取二进制补码后加负号
     * - 去除前导零
     * <p>
     * 参考：https://wiki.vg/Protocol_Encryption#Client
     */
    private static String computeServerHash(String serverID, byte[] sharedSecret, byte[] publicKey)
            throws Exception {
        MessageDigest sha1 = MessageDigest.getInstance("SHA-1");
        sha1.update(serverID.getBytes("ASCII"));
        sha1.update(sharedSecret);
        sha1.update(publicKey);
        byte[] digest = sha1.digest();

        // 检查是否为负数（最高位为1）— 参考 Botcraft 的 is_negative 判断
        boolean isNegative = (digest[0] & 0x80) != 0;

        if (isNegative) {
            // 取二进制补码 — 参考 Botcraft 的 two's complement 逻辑
            digest = twosComplement(digest);
        }

        // 转为十六进制 — 参考 Botcraft 的 hex 转换
        StringBuilder sb = new StringBuilder();
        for (byte b : digest) {
            sb.append(String.format("%02x", b & 0xFF));
        }
        String hex = sb.toString();

        // 去除前导零 — 参考 Botcraft 的 find_first_not_of('0')
        hex = hex.replaceFirst("^0+", "");
        if (hex.isEmpty()) hex = "0";

        // 如果是负数，添加负号
        if (isNegative) {
            hex = "-" + hex;
        }

        return hex;
    }

    /**
     * 二进制补码（取反加一）— 直接搬自 Botcraft Authentifier::JoinServer
     */
    private static byte[] twosComplement(byte[] data) {
        byte[] result = Arrays.copyOf(data, data.length);

        // 取反
        for (int i = 0; i < result.length; ++i) {
            result[i] = (byte) ~result[i];
        }

        // 加1
        int position = result.length - 1;
        while (position > 0 && result[position] == (byte) 0xFF) {
            result[position] = 0;
            position--;
        }
        result[position] += 1;

        return result;
    }
}
