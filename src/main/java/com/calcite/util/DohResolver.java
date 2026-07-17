package com.calcite.util;

import android.util.Log;

import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.URL;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;

import javax.net.ssl.HttpsURLConnection;

import okhttp3.HttpUrl;
import okhttp3.OkHttpClient;
import okhttp3.dnsoverhttps.DnsOverHttps;

/**
 * DNS over HTTPS 解析器
 * 每次启动应用时 DoH 查询一次 API 服务器 IP，缓存在内存中，后续请求直接用
 */
public class DohResolver {
    private static final String TAG = "DohResolver";

    // 阿里云公共 DNS DoH 端点
    private static final String DOH_HOST = "dns.alidns.com";
    private static final String DOH_URL = "https://" + DOH_HOST + "/dns-query";

    // 阿里云 DNS 的固定 IP（绕过系统 DNS 解析 DoH 服务器自身）
    private static final List<InetAddress> DOH_BOOTSTRAP = Arrays.asList(
            ip("223.5.5.5"),
            ip("223.6.6.6")
    );

    // API 服务器信息
    private static final String SERVER_HOST = "api.calcite.eu.cc";
    private static final int SERVER_PORT = 25000;

    // 内存缓存（warmUp 时更新）
    private static volatile String cachedServerIp = null;

    // OkHttp 客户端 + DnsOverHttps（仅 warmUp 时使用）
    private static OkHttpClient bootstrapClient;
    private static DnsOverHttps dohClient;

    /**
     * 预热：通过 DoH 查询 API 服务器 IP，缓存到内存
     * 应用启动时调用一次即可
     */
    public static void warmUp() {
        // 延迟初始化 OkHttp + DoH 客户端
        if (dohClient == null) {
            bootstrapClient = new OkHttpClient.Builder()
                    .dns(hostname -> {
                        if (DOH_HOST.equals(hostname)) return DOH_BOOTSTRAP;
                        return Arrays.asList(InetAddress.getAllByName(hostname));
                    })
                    .connectTimeout(10, TimeUnit.SECONDS)
                    .readTimeout(10, TimeUnit.SECONDS)
                    .build();

            dohClient = new DnsOverHttps.Builder()
                    .client(bootstrapClient)
                    .url(HttpUrl.get(DOH_URL))
                    .build();
        }

        new Thread(() -> {
            try {
                List<InetAddress> addresses = dohClient.lookup(SERVER_HOST);
                if (!addresses.isEmpty()) {
                    String ip = addresses.get(0).getHostAddress();
                    cachedServerIp = ip;
                    Log.i(TAG, "WarmUp: " + SERVER_HOST + " -> " + ip);
                }
            } catch (Exception e) {
                Log.e(TAG, "WarmUp DoH lookup failed", e);
            }
        }).start();
    }

    /**
     * 获取服务器 IP（从内存缓存返回，不会发起网络请求）
     * 若 warmUp 尚未完成则返回域名本身（fallback 走系统 DNS）
     */
    public static String getServerIp() {
        if (cachedServerIp != null) return cachedServerIp;
        return SERVER_HOST;
    }

    /**
     * 创建使用缓存 IP 直连的 HttpURLConnection
     * 自动设置 Host header + 处理 HTTPS 证书验证
     */
    public static HttpURLConnection openConnection(String urlString) throws Exception {
        String ip = getServerIp();

        String ipUrl = urlString.replaceFirst(
                java.util.regex.Pattern.quote(SERVER_HOST + ":" + SERVER_PORT),
                ip + ":" + SERVER_PORT
        );
        URL url = new URL(ipUrl);
        HttpURLConnection conn = (HttpURLConnection) url.openConnection();

        conn.setRequestProperty("Host", SERVER_HOST + ":" + SERVER_PORT);

        if (conn instanceof HttpsURLConnection) {
            ((HttpsURLConnection) conn).setHostnameVerifier(
                    (hostname, session) ->
                            HttpsURLConnection.getDefaultHostnameVerifier()
                                    .verify(SERVER_HOST, session)
            );
        }

        return conn;
    }

    private static InetAddress ip(String addr) {
        byte[] bytes = new byte[4];
        String[] parts = addr.split("\\.");
        for (int i = 0; i < 4; i++) bytes[i] = (byte) Integer.parseInt(parts[i]);
        try {
            return InetAddress.getByAddress(bytes);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
