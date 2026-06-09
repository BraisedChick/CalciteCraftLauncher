package com.calcite.auth;

/**
 * Microsoft 正版认证结果
 * 包含完整的 Minecraft 认证链信息
 */
public class AuthResult {
    private final String playerName;
    private final String uuid;           // 无横线的 Minecraft UUID
    private final String accessToken;    // Minecraft 访问令牌
    private final String refreshToken;   // Microsoft 刷新令牌
    private final String tokenType;      // 令牌类型（通常为 "Bearer"）
    private final long notAfter;         // 访问令牌过期时间（毫秒时间戳）

    public AuthResult(String playerName, String uuid, String accessToken,
                      String refreshToken, String tokenType, long notAfter) {
        this.playerName = playerName;
        this.uuid = uuid;
        this.accessToken = accessToken;
        this.refreshToken = refreshToken;
        this.tokenType = tokenType;
        this.notAfter = notAfter;
    }

    public String getPlayerName() { return playerName; }
    public String getUuid() { return uuid; }
    public String getAccessToken() { return accessToken; }
    public String getRefreshToken() { return refreshToken; }
    public String getTokenType() { return tokenType; }
    public long getNotAfter() { return notAfter; }

    public boolean isExpired() {
        return System.currentTimeMillis() > notAfter;
    }

    @Override
    public String toString() {
        return "AuthResult{name=" + playerName + ", uuid=" + uuid + "}";
    }
}
