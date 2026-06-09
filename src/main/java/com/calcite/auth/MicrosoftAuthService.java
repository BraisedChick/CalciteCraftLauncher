package com.calcite.auth;

import android.util.Log;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * Microsoft 正版认证服务
 * <p>
 * 实现完整的 Microsoft Device Code OAuth 流程 + Minecraft 认证链：
 * 1. Microsoft OAuth (Device Code Flow) → access_token + refresh_token
 * 2. Xbox Live Auth → XBL Token
 * 3. XSTS Auth → XSTS Token + User Hash (uhs)
 * 4. Minecraft Auth (login_with_xbox) → MC Access Token
 * 5. Minecraft Profile → Player Name + UUID
 * <p>
 * 参考：Botcraft Authentifier.cpp, FCL MicrosoftService.java
 */
public class MicrosoftAuthService {
    private static final String TAG = "MicrosoftAuth";

    // Azure AD 公共客户端 ID（需替换为自行注册的应用 ID）
    // 注册方式：https://portal.azure.com → Azure Active Directory → App registrations → New registration
    // 类型：Mobile and desktop applications，重定向 URI：https://login.microsoftonline.com/common/oauth2/nativeclient
    public static final String CLIENT_ID = "a0ad834d-e78a-4881-87f6-390aa0f4b283";

    private static final String SCOPE = "XboxLive.signin offline_access";

    // Microsoft OAuth 端点
    private static final String DEVICE_CODE_URL = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
    private static final String TOKEN_URL = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";

    // Xbox Live 端点
    private static final String XBL_AUTH_URL = "https://user.auth.xboxlive.com/user/authenticate";
    private static final String XSTS_AUTH_URL = "https://xsts.auth.xboxlive.com/xsts/authorize";

    // Minecraft 端点
    private static final String MC_LOGIN_URL = "https://api.minecraftservices.com/authentication/login_with_xbox";
    private static final String MC_PROFILE_URL = "https://api.minecraftservices.com/minecraft/profile";
    private static final String MC_STORE_URL = "https://api.minecraftservices.com/entitlements/mcstore";
    private static final String SESSION_JOIN_URL = "https://sessionserver.mojang.com/session/minecraft/join";

    // 回调接口
    public interface AuthCallback {
        /**
         * Device Code 流程启动，需要用户在浏览器中输入代码
         *
         * @param userCode        用户需要输入的代码
         * @param verificationURI 验证网址
         */
        void onDeviceCodeReceived(String userCode, String verificationURI);

        /**
         * 认证进度更新
         */
        void onProgress(String message);

        /**
         * 认证成功
         */
        void onSuccess(AuthResult result);

        /**
         * 认证失败
         */
        void onError(String message);
    }

    /**
     * 执行完整的 Microsoft 正版登录流程
     * 必须在后台线程调用
     */
    public void authenticate(AuthCallback callback) {
        try {
            // Step 1: Microsoft Device Code Flow
            callback.onProgress("正在获取设备代码...");
            JSONObject deviceCodeResp = requestDeviceCode();
            String userCode = deviceCodeResp.getString("user_code");
            String verificationURI = deviceCodeResp.getString("verification_uri");
            String deviceCode = deviceCodeResp.getString("device_code");
            int interval = deviceCodeResp.optInt("interval", 5);

            callback.onDeviceCodeReceived(userCode, verificationURI);

            // Step 2: 等待用户完成授权，轮询获取 token
            callback.onProgress("等待浏览器授权...");
            JSONObject tokenResp = pollForToken(deviceCode, interval);
            String msAccessToken = tokenResp.getString("access_token");
            String msRefreshToken = tokenResp.getString("refresh_token");

            // Step 3: Xbox Live Auth
            callback.onProgress("正在验证 Xbox Live...");
            JSONObject xblResp = authenticateXBL(msAccessToken);
            String xblToken = xblResp.getString("Token");
            String uhs = xblResp.getJSONObject("DisplayClaims")
                    .getJSONArray("xui").getJSONObject(0).getString("uhs");

            // Step 4: XSTS Auth
            callback.onProgress("正在获取 XSTS 令牌...");
            JSONObject xstsResp = authenticateXSTS(xblToken);
            String xstsToken = xstsResp.getString("Token");
            String xstsUhs = xstsResp.getJSONObject("DisplayClaims")
                    .getJSONArray("xui").getJSONObject(0).getString("uhs");

            // Step 5: Minecraft Auth
            callback.onProgress("正在登录 Minecraft...");
            JSONObject mcResp = authenticateMinecraft(xstsUhs, xstsToken);
            String mcAccessToken = mcResp.getString("access_token");
            String tokenType = mcResp.optString("token_type", "Bearer");
            int expiresIn = mcResp.optInt("expires_in", 86400);
            long notAfter = System.currentTimeMillis() + (long) expiresIn * 1000;

            // Step 6: 检查 MC 所有权
            callback.onProgress("正在验证游戏所有权...");
            checkMinecraftOwnership(mcAccessToken);

            // Step 7: 获取 MC Profile
            callback.onProgress("正在获取玩家信息...");
            JSONObject profileResp = getMinecraftProfile(mcAccessToken);
            String playerName = profileResp.getString("name");
            String uuid = profileResp.getString("id");

            AuthResult result = new AuthResult(
                    playerName, uuid, mcAccessToken,
                    msRefreshToken, tokenType, notAfter
            );

            Log.i(TAG, "Authentication successful: " + playerName + " (" + uuid + ")");
            callback.onSuccess(result);

        } catch (AuthCancelledException e) {
            callback.onError("登录已取消");
        } catch (AuthExpiredException e) {
            callback.onError("登录已超时，请重试");
        } catch (AuthDeclinedException e) {
            callback.onError("授权已被拒绝");
        } catch (NoMinecraftProfileException e) {
            callback.onError("该账号未拥有 Minecraft Java 版");
        } catch (XboxAuthorizationException e) {
            String msg = getXboxErrorMessage(e.getErrorCode());
            callback.onError("Xbox 授权失败: " + msg);
        } catch (Exception e) {
            Log.e(TAG, "Authentication failed", e);
            callback.onError("登录失败: " + e.getMessage());
        }
    }

    /**
     * 使用 refresh token 刷新认证
     */
    public void refresh(String refreshToken, AuthCallback callback) {
        try {
            callback.onProgress("正在刷新令牌...");
            JSONObject tokenResp = refreshTokenFlow(refreshToken);
            String msAccessToken = tokenResp.getString("access_token");
            String msRefreshToken = tokenResp.getString("refresh_token");

            callback.onProgress("正在验证 Xbox Live...");
            JSONObject xblResp = authenticateXBL(msAccessToken);
            String xblToken = xblResp.getString("Token");
            String uhs = xblResp.getJSONObject("DisplayClaims")
                    .getJSONArray("xui").getJSONObject(0).getString("uhs");

            callback.onProgress("正在获取 XSTS 令牌...");
            JSONObject xstsResp = authenticateXSTS(xblToken);
            String xstsToken = xstsResp.getString("Token");
            String xstsUhs = xstsResp.getJSONObject("DisplayClaims")
                    .getJSONArray("xui").getJSONObject(0).getString("uhs");

            callback.onProgress("正在登录 Minecraft...");
            JSONObject mcResp = authenticateMinecraft(xstsUhs, xstsToken);
            String mcAccessToken = mcResp.getString("access_token");
            String tokenType = mcResp.optString("token_type", "Bearer");
            int expiresIn = mcResp.optInt("expires_in", 86400);
            long notAfter = System.currentTimeMillis() + (long) expiresIn * 1000;

            callback.onProgress("正在获取玩家信息...");
            JSONObject profileResp = getMinecraftProfile(mcAccessToken);
            String playerName = profileResp.getString("name");
            String uuid = profileResp.getString("id");

            AuthResult result = new AuthResult(
                    playerName, uuid, mcAccessToken,
                    msRefreshToken, tokenType, notAfter
            );

            callback.onSuccess(result);
        } catch (Exception e) {
            Log.e(TAG, "Token refresh failed", e);
            callback.onError("刷新令牌失败: " + e.getMessage());
        }
    }

    /**
     * 加入服务器会话（用于正版验证在线模式服务器）
     * 当 C++ 层收到 Encryption Request 时调用
     *
     * @param accessToken MC 访问令牌
     * @param uuid        玩家 UUID（无横线）
     * @param serverHash  服务器哈希（SHA1 hexdigest）
     */
    public static boolean joinServer(String accessToken, String uuid, String serverHash) {
        try {
            JSONObject body = new JSONObject();
            body.put("accessToken", accessToken);
            body.put("selectedProfile", uuid);
            body.put("serverId", serverHash);

            HttpURLConnection conn = (HttpURLConnection) new URL(SESSION_JOIN_URL).openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            conn.setDoOutput(true);
            conn.setConnectTimeout(10000);
            conn.setReadTimeout(10000);

            byte[] data = body.toString().getBytes(StandardCharsets.UTF_8);
            try (OutputStream os = conn.getOutputStream()) {
                os.write(data);
            }

            int code = conn.getResponseCode();
            conn.disconnect();

            Log.i(TAG, "Session join response: " + code);
            return code == 204 || code == 200;
        } catch (Exception e) {
            Log.e(TAG, "Session join failed", e);
            return false;
        }
    }

    // ========== Step 1: Device Code Flow ==========

    private JSONObject requestDeviceCode() throws Exception {
        String body = "client_id=" + CLIENT_ID
                + "&scope=" + java.net.URLEncoder.encode(SCOPE, "UTF-8");

        String response = postForm(DEVICE_CODE_URL, body);
        return new JSONObject(response);
    }

    private JSONObject pollForToken(String deviceCode, int interval) throws Exception {
        long startTime = System.currentTimeMillis();
        long timeout = 15 * 60 * 1000; // 15 分钟超时

        while (System.currentTimeMillis() - startTime < timeout) {
            Thread.sleep((interval + 1) * 1000L);

            String body = "client_id=" + CLIENT_ID
                    + "&scope=" + java.net.URLEncoder.encode(SCOPE, "UTF-8")
                    + "&grant_type=urn:ietf:params:oauth:grant-type:device_code"
                    + "&device_code=" + deviceCode;

            try {
                String response = postForm(TOKEN_URL, body);
                return new JSONObject(response);
            } catch (FormPostErrorException e) {
                JSONObject errorJson = e.getErrorJson();
                String error = errorJson.optString("error", "");

                switch (error) {
                    case "authorization_pending":
                        continue;
                    case "slow_down":
                        interval += 5;
                        continue;
                    case "authorization_declined":
                        throw new AuthDeclinedException();
                    case "expired_token":
                        throw new AuthExpiredException();
                    default:
                        throw new Exception("OAuth error: " + error + " - "
                                + errorJson.optString("error_description", ""));
                }
            }
        }
        throw new AuthExpiredException();
    }

    private JSONObject refreshTokenFlow(String refreshToken) throws Exception {
        String body = "client_id=" + CLIENT_ID
                + "&refresh_token=" + refreshToken
                + "&grant_type=refresh_token"
                + "&redirect_uri=https://login.microsoftonline.com/common/oauth2/nativeclient";

        String response = postForm(TOKEN_URL, body);
        JSONObject json = new JSONObject(response);
        if (json.has("error")) {
            throw new Exception("Token refresh error: " + json.getString("error"));
        }
        return json;
    }

    // ========== Step 2: Xbox Live Auth ==========

    private JSONObject authenticateXBL(String msAccessToken) throws Exception {
        JSONObject props = new JSONObject();
        props.put("AuthMethod", "RPS");
        props.put("SiteName", "user.auth.xboxlive.com");
        props.put("RpsTicket", "d=" + msAccessToken);

        JSONObject body = new JSONObject();
        body.put("Properties", props);
        body.put("RelyingParty", "http://auth.xboxlive.com");
        body.put("TokenType", "JWT");

        String response = postJson(XBL_AUTH_URL, body.toString(), null);
        JSONObject json = new JSONObject(response);

        // 检查 Xbox 错误
        if (json.has("XErr")) {
            throw new XboxAuthorizationException(json.getLong("XErr"), json.optString("Redirect", ""));
        }
        return json;
    }

    // ========== Step 3: XSTS Auth ==========

    private JSONObject authenticateXSTS(String xblToken) throws Exception {
        JSONObject props = new JSONObject();
        props.put("SandboxId", "RETAIL");
        props.put("UserTokens", new org.json.JSONArray().put(xblToken));

        JSONObject body = new JSONObject();
        body.put("Properties", props);
        body.put("RelyingParty", "rp://api.minecraftservices.com/");
        body.put("TokenType", "JWT");

        String response = postJson(XSTS_AUTH_URL, body.toString(), null);
        JSONObject json = new JSONObject(response);

        // 检查 XSTS 错误
        if (json.has("XErr")) {
            throw new XboxAuthorizationException(json.getLong("XErr"), json.optString("Redirect", ""));
        }
        return json;
    }

    // ========== Step 4: Minecraft Auth ==========

    private JSONObject authenticateMinecraft(String uhs, String xstsToken) throws Exception {
        JSONObject body = new JSONObject();
        body.put("identityToken", "XBL3.0 x=" + uhs + ";" + xstsToken);

        String response = postJson(MC_LOGIN_URL, body.toString(), null);
        return new JSONObject(response);
    }

    // ========== Step 5: Check MC Ownership ==========

    private void checkMinecraftOwnership(String mcAccessToken) throws Exception {
        HttpURLConnection conn = (HttpURLConnection) new URL(MC_STORE_URL).openConnection();
        conn.setRequestMethod("GET");
        conn.setRequestProperty("Authorization", "Bearer " + mcAccessToken);
        conn.setRequestProperty("Accept", "application/json");
        conn.setConnectTimeout(10000);
        conn.setReadTimeout(10000);

        int code = conn.getResponseCode();
        if (code != 200) {
            conn.disconnect();
            throw new NoMinecraftProfileException();
        }
        conn.disconnect();
    }

    // ========== Step 6: Get MC Profile ==========

    private JSONObject getMinecraftProfile(String mcAccessToken) throws Exception {
        HttpURLConnection conn = (HttpURLConnection) new URL(MC_PROFILE_URL).openConnection();
        conn.setRequestMethod("GET");
        conn.setRequestProperty("Authorization", "Bearer " + mcAccessToken);
        conn.setRequestProperty("Accept", "application/json");
        conn.setConnectTimeout(10000);
        conn.setReadTimeout(10000);

        int code = conn.getResponseCode();
        if (code == 404) {
            conn.disconnect();
            throw new NoMinecraftProfileException();
        }
        if (code != 200) {
            conn.disconnect();
            throw new Exception("Profile request failed: " + code);
        }

        String response = readResponse(conn);
        conn.disconnect();
        return new JSONObject(response);
    }

    // ========== HTTP 工具方法 ==========

    private String postForm(String urlStr, String formData) throws Exception {
        HttpURLConnection conn = (HttpURLConnection) new URL(urlStr).openConnection();
        conn.setRequestMethod("POST");
        conn.setRequestProperty("Content-Type", "application/x-www-form-urlencoded");
        conn.setRequestProperty("Accept", "application/json");
        conn.setDoOutput(true);
        conn.setConnectTimeout(10000);
        conn.setReadTimeout(30000);

        try (OutputStream os = conn.getOutputStream()) {
            os.write(formData.getBytes(StandardCharsets.UTF_8));
        }

        int code = conn.getResponseCode();
        String response;
        if (code >= 200 && code < 300) {
            response = readResponse(conn);
        } else {
            String errorBody = readErrorResponse(conn);
            conn.disconnect();
            throw new FormPostErrorException(code, errorBody);
        }
        conn.disconnect();
        return response;
    }

    private String postJson(String urlStr, String jsonBody, String authorization) throws Exception {
        HttpURLConnection conn = (HttpURLConnection) new URL(urlStr).openConnection();
        conn.setRequestMethod("POST");
        conn.setRequestProperty("Content-Type", "application/json");
        conn.setRequestProperty("Accept", "application/json");
        if (authorization != null && !authorization.isEmpty()) {
            conn.setRequestProperty("Authorization", authorization);
        }
        conn.setDoOutput(true);
        conn.setConnectTimeout(10000);
        conn.setReadTimeout(30000);

        try (OutputStream os = conn.getOutputStream()) {
            os.write(jsonBody.getBytes(StandardCharsets.UTF_8));
        }

        int code = conn.getResponseCode();
        String response;
        if (code >= 200 && code < 300) {
            response = readResponse(conn);
        } else {
            String errorBody = readErrorResponse(conn);
            conn.disconnect();
            throw new Exception("HTTP " + code + ": " + errorBody);
        }
        conn.disconnect();
        return response;
    }

    private String readResponse(HttpURLConnection conn) throws Exception {
        BufferedReader reader = new BufferedReader(
                new java.io.InputStreamReader(conn.getInputStream(), StandardCharsets.UTF_8));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = reader.readLine()) != null) {
            sb.append(line);
        }
        reader.close();
        return sb.toString();
    }

    private String readErrorResponse(HttpURLConnection conn) throws Exception {
        try {
            BufferedReader reader = new BufferedReader(
                    new java.io.InputStreamReader(conn.getErrorStream(), StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                sb.append(line);
            }
            reader.close();
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }

    // ========== Xbox 错误码转友好消息 ==========

    private static String getXboxErrorMessage(long errorCode) {
        if (errorCode == 2148916227L) return "账号被封禁";
        if (errorCode == 2148916233L) return "缺少 Xbox 账号，请先注册";
        if (errorCode == 2148916235L) return "所在地区不可用";
        if (errorCode == 2148916238L) return "儿童账号，需添加到家庭";
        return "错误码: " + errorCode;
    }

    // ========== 自定义异常 ==========

    public static class AuthCancelledException extends Exception {}
    public static class AuthExpiredException extends Exception {}
    public static class AuthDeclinedException extends Exception {}
    public static class NoMinecraftProfileException extends Exception {}

    public static class XboxAuthorizationException extends Exception {
        private final long errorCode;
        private final String redirect;

        public XboxAuthorizationException(long errorCode, String redirect) {
            super("Xbox error: " + errorCode);
            this.errorCode = errorCode;
            this.redirect = redirect;
        }

        public long getErrorCode() { return errorCode; }
        public String getRedirect() { return redirect; }
    }

    private static class FormPostErrorException extends Exception {
        private final String errorBody;

        public FormPostErrorException(int code, String errorBody) {
            super("HTTP " + code + ": " + errorBody);
            this.errorBody = errorBody;
        }

        public JSONObject getErrorJson() {
            try {
                return new JSONObject(errorBody);
            } catch (Exception e) {
                return new JSONObject();
            }
        }
    }
}
