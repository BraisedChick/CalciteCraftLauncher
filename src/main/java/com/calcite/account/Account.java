package com.calcite.account;

import com.calcite.auth.AuthResult;

public class Account {
    private String name;
    private String type; // "offline", "premium", or "calcite"
    private String uuid;

    // 正版账号额外字段
    private String accessToken;    // Minecraft 访问令牌
    private String refreshToken;   // Microsoft 刷新令牌
    private String tokenType;      // 令牌类型（通常为 "Bearer"）
    private long notAfter;         // 访问令牌过期时间（毫秒时间戳）

    // Calcite 账号字段
    private String calciteToken;   // Calcite JWT 令牌
    private String email;          // 注册邮箱

    public Account(String name, String type, String uuid) {
        this.name = name;
        this.type = type;
        this.uuid = uuid;
    }

    /**
     * 从正版认证结果创建 Account
     */
    public static Account fromAuthResult(AuthResult result) {
        Account account = new Account(result.getPlayerName(), "premium", result.getUuid());
        account.accessToken = result.getAccessToken();
        account.refreshToken = result.getRefreshToken();
        account.tokenType = result.getTokenType();
        account.notAfter = result.getNotAfter();
        return account;
    }

    /**
     * 更新认证信息（令牌刷新后调用）
     */
    public void updateAuthResult(AuthResult result) {
        this.name = result.getPlayerName();
        this.uuid = result.getUuid();
        this.accessToken = result.getAccessToken();
        this.refreshToken = result.getRefreshToken();
        this.tokenType = result.getTokenType();
        this.notAfter = result.getNotAfter();
    }

    public String getName() { return name; }
    public String getType() { return type; }
    public String getUuid() { return uuid; }
    public void setName(String name) { this.name = name; }
    public void setType(String type) { this.type = type; }
    public void setUuid(String uuid) { this.uuid = uuid; }

    // 正版账号 getter
    public String getAccessToken() { return accessToken; }
    public String getRefreshToken() { return refreshToken; }
    public String getTokenType() { return tokenType; }
    public long getNotAfter() { return notAfter; }

    public void setAccessToken(String accessToken) { this.accessToken = accessToken; }
    public void setRefreshToken(String refreshToken) { this.refreshToken = refreshToken; }
    public void setTokenType(String tokenType) { this.tokenType = tokenType; }
    public void setNotAfter(long notAfter) { this.notAfter = notAfter; }

    public boolean isPremium() { return "premium".equals(type); }
    public boolean isCalcite() { return "calcite".equals(type); }

    /**
     * 检查正版令牌是否已过期
     */
    public boolean isTokenExpired() {
        return isPremium() && System.currentTimeMillis() > notAfter;
    }

    public String getTypeDisplay() {
        if ("offline".equals(type)) return "离线账号";
        if ("calcite".equals(type)) return "Calcite 账号";
        return "正版账号";
    }

    // Calcite 账号 getter/setter
    public String getCalciteToken() { return calciteToken; }
    public void setCalciteToken(String token) { this.calciteToken = token; }
    public String getEmail() { return email; }
    public void setEmail(String email) { this.email = email; }
}
