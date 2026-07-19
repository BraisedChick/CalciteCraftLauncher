package com.calcite.auth;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.util.Base64;
import android.util.Log;

import com.calcite.util.DohResolver;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public class CalciteApiService {

    private static final String TAG = "CalciteApi";
    public static final String BASE_URL = "https://api.calcite.eu.cc:25000";

    private HttpURLConnection openDoh(String path) throws Exception {
        return DohResolver.openConnection(BASE_URL + path);
    }

    public interface Callback<T> {
        void onSuccess(T result);
        void onError(String message);
    }

    /** 验证码信息 */
    public static class CaptchaResult {
        public final String captchaId;
        public final Bitmap image;

        public CaptchaResult(String captchaId, Bitmap image) {
            this.captchaId = captchaId;
            this.image = image;
        }
    }

    /** 注册/登录结果 */
    public static class AuthResult {
        public final String token;
        public final String username;
        public final String email;

        public AuthResult(String token, String username, String email) {
            this.token = token;
            this.username = username;
            this.email = email;
        }
    }

    /** 排行榜条目 */
    public static class RankEntry {
        public final int rank;
        public final String username;
        public final int totalPlaytimeSeconds;

        public RankEntry(int rank, String username, int totalPlaytimeSeconds) {
            this.rank = rank;
            this.username = username;
            this.totalPlaytimeSeconds = totalPlaytimeSeconds;
        }
    }

    /**
     * GET /captcha — 获取验证码
     */
    public void fetchCaptcha(Callback<CaptchaResult> callback) {
        new Thread(() -> {
            try {
                HttpURLConnection conn = openDoh("/captcha");
                conn.setRequestMethod("GET");
                conn.setConnectTimeout(10000);
                conn.setReadTimeout(10000);

                int code = conn.getResponseCode();
                if (code != 200) {
                    callback.onError("获取验证码失败: HTTP " + code);
                    return;
                }

                String body = readResponse(conn);
                JSONObject json = new JSONObject(body);
                String captchaId = json.getString("captchaId");
                String base64Image = json.getString("image");

                byte[] imageBytes = Base64.decode(base64Image, Base64.DEFAULT);
                Bitmap bitmap = BitmapFactory.decodeByteArray(imageBytes, 0, imageBytes.length);

                callback.onSuccess(new CaptchaResult(captchaId, bitmap));
            } catch (Exception e) {
                Log.e(TAG, "fetchCaptcha failed", e);
                callback.onError("网络错误: " + e.getMessage());
            }
        }).start();
    }

    /**
     * POST /users — 注册
     */
    public void register(String username, String email, String password,
                         String captchaId, String captchaCode,
                         Callback<AuthResult> callback) {
        new Thread(() -> {
            try {
                JSONObject body = new JSONObject();
                body.put("username", username);
                body.put("email", email);
                body.put("password", password);
                body.put("captchaId", captchaId);
                body.put("captchaCode", captchaCode);

                HttpURLConnection conn = openDoh("/users");
                conn.setRequestMethod("POST");
                conn.setRequestProperty("Content-Type", "application/json");
                conn.setConnectTimeout(10000);
                conn.setReadTimeout(10000);
                conn.setDoOutput(true);

                try (OutputStream os = conn.getOutputStream()) {
                    os.write(body.toString().getBytes(StandardCharsets.UTF_8));
                }

                int code = conn.getResponseCode();
                if (code == 201) {
                    String resp = readResponse(conn);
                    JSONObject json = new JSONObject(resp);
                    callback.onSuccess(new AuthResult(
                        json.getString("token"),
                        json.getString("username"),
                        json.getString("email")
                    ));
                } else {
                    String err = readErrorResponse(conn);
                    callback.onError(parseError(err));
                }
            } catch (Exception e) {
                Log.e(TAG, "register failed", e);
                callback.onError("网络错误: " + e.getMessage());
            }
        }).start();
    }

    /**
     * POST /sessions — 登录
     */
    public void login(String username, String password, Callback<AuthResult> callback) {
        new Thread(() -> {
            try {
                JSONObject body = new JSONObject();
                body.put("username", username);
                body.put("password", password);

                HttpURLConnection conn = openDoh("/sessions");
                conn.setRequestMethod("POST");
                conn.setRequestProperty("Content-Type", "application/json");
                conn.setConnectTimeout(10000);
                conn.setReadTimeout(10000);
                conn.setDoOutput(true);

                try (OutputStream os = conn.getOutputStream()) {
                    os.write(body.toString().getBytes(StandardCharsets.UTF_8));
                }

                int code = conn.getResponseCode();
                if (code == 200) {
                    String resp = readResponse(conn);
                    JSONObject json = new JSONObject(resp);
                    callback.onSuccess(new AuthResult(
                        json.getString("token"),
                        json.getString("username"),
                        json.getString("email")
                    ));
                } else {
                    String err = readErrorResponse(conn);
                    callback.onError(parseError(err));
                }
            } catch (Exception e) {
                Log.e(TAG, "login failed", e);
                callback.onError("网络错误: " + e.getMessage());
            }
        }).start();
    }

    /**
     * GET /users/time — 查询游戏时长
     */
    public void fetchPlaytime(String token, Callback<Integer> callback) {
        new Thread(() -> {
            try {
                HttpURLConnection conn = openDoh("/users/time");
                conn.setRequestMethod("GET");
                conn.setRequestProperty("Authorization", "Bearer " + token);
                conn.setConnectTimeout(10000);
                conn.setReadTimeout(10000);

                int code = conn.getResponseCode();
                if (code == 200) {
                    String resp = readResponse(conn);
                    JSONObject json = new JSONObject(resp);
                    int seconds = json.getInt("totalPlaytimeSeconds");
                    callback.onSuccess(seconds);
                } else {
                    String err = readErrorResponse(conn);
                    callback.onError(parseError(err));
                }
            } catch (Exception e) {
                Log.e(TAG, "fetchPlaytime failed", e);
                callback.onError("网络错误: " + e.getMessage());
            }
        }).start();
    }

    /**
     * GET /ranking — 获取游戏时长排行榜（无需认证）
     */
    public void fetchLeaderboard(Callback<List<RankEntry>> callback) {
        new Thread(() -> {
            try {
                HttpURLConnection conn = openDoh("/ranking");
                conn.setRequestMethod("GET");
                conn.setConnectTimeout(10000);
                conn.setReadTimeout(10000);

                int code = conn.getResponseCode();
                if (code == 200) {
                    String resp = readResponse(conn);
                    JSONObject json = new JSONObject(resp);
                    JSONArray arr = json.getJSONArray("ranking");
                    List<RankEntry> list = new ArrayList<>();
                    for (int i = 0; i < arr.length(); i++) {
                        JSONObject item = arr.getJSONObject(i);
                        String name = item.getString("username");
                        int secs = item.getInt("totalPlaytimeSeconds");
                        list.add(new RankEntry(i + 1, name, secs));
                    }
                    callback.onSuccess(list);
                } else {
                    String err = readErrorResponse(conn);
                    callback.onError(parseError(err));
                }
            } catch (Exception e) {
                Log.e(TAG, "fetchLeaderboard failed", e);
                callback.onError("网络错误: " + e.getMessage());
            }
        }).start();
    }

    /**
     * POST /users/time — 心跳上报
     * 每分钟调用一次，需要 Bearer Token 认证
     */
    public void sendHeartbeat(String token, Callback<Void> callback) {
        new Thread(() -> {
            try {
                HttpURLConnection conn = openDoh("/users/time");
                conn.setRequestMethod("POST");
                conn.setRequestProperty("Authorization", "Bearer " + token);
                conn.setRequestProperty("Content-Type", "application/json");
                conn.setConnectTimeout(10000);
                conn.setReadTimeout(10000);
                conn.setDoOutput(true);

                try (OutputStream os = conn.getOutputStream()) {
                    os.write("{}".getBytes(StandardCharsets.UTF_8));
                }

                int code = conn.getResponseCode();
                if (code == 200) {
                    callback.onSuccess(null);
                } else {
                    String err = readErrorResponse(conn);
                    callback.onError(parseError(err));
                }
            } catch (Exception e) {
                Log.e(TAG, "sendHeartbeat failed", e);
                callback.onError("网络错误: " + e.getMessage());
            }
        }).start();
    }

    private String readResponse(HttpURLConnection conn) throws Exception {
        java.io.InputStream is = conn.getInputStream();
        java.util.Scanner s = new java.util.Scanner(is, "UTF-8").useDelimiter("\\A");
        return s.hasNext() ? s.next() : "";
    }

    private String readErrorResponse(HttpURLConnection conn) {
        try {
            java.io.InputStream is = conn.getErrorStream();
            if (is == null) return "";
            java.util.Scanner s = new java.util.Scanner(is, "UTF-8").useDelimiter("\\A");
            return s.hasNext() ? s.next() : "";
        } catch (Exception e) {
            return "";
        }
    }

    private String parseError(String json) {
        try {
            return new JSONObject(json).optString("error", "未知错误");
        } catch (Exception e) {
            return "未知错误";
        }
    }
}
