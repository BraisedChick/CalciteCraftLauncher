package com.calcite;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.util.concurrent.TimeUnit;

public class LogManager {

    private static Process sLogcatProcess = null;
    private static final Object LOCK = new Object();

    /**
     * 启动 logcat 日志捕获（点击图标时调用）
     * 特性：
     * 1. 若进程已存在则直接返回，不重复启动
     * 2. 仅当首次启动时，才执行日志轮转（保留上次日志）
     */
    public static void startLogcatCapture(Context context) {
        synchronized (LOCK) {
            // 如果进程还活着，说明之前已经启动过了，直接返回（避免轮转覆盖当前日志）
            if (sLogcatProcess != null && sLogcatProcess.isAlive()) {
                Log.i("LogManager", "Logcat already running, skip restart");
                return;
            }

            // 只有在这里（首次启动）才执行轮转，避免因旋转屏幕等重建导致日志丢失
            rotateLogs(context);

            try {
                String logPath = context.getExternalFilesDir(null).getAbsolutePath() + "/logs/client.log";
                new File(logPath).getParentFile().mkdirs();

                // 清空旧缓冲区（仅当进程未启动时执行一次）
                Runtime.getRuntime().exec(new String[]{"logcat", "-c"});

                // 启动重定向进程
                sLogcatProcess = Runtime.getRuntime().exec(new String[]{"logcat", "-f", logPath, "*:V"});
                Log.i("LogManager", "Logcat capture started: " + logPath);

            } catch (Exception e) {
                Log.e("LogManager", "Failed to start logcat capture", e);
            }
        }
    }

    /**
     * 停止日志捕获（仅在应用彻底退出时调用，即 LauncherActivity 销毁）
     */
    public static void stopLogcatCapture() {
        synchronized (LOCK) {
            if (sLogcatProcess != null) {
                sLogcatProcess.destroy();
                try {
                    // 等待进程结束，避免僵尸进程
                    sLogcatProcess.waitFor(500, TimeUnit.MILLISECONDS);
                } catch (InterruptedException ignored) {}
                sLogcatProcess = null;
                Log.i("LogManager", "Logcat capture stopped");
            }
        }
    }

    /**
     * 检查日志进程是否正在运行
     */
    public static boolean isLogcatRunning() {
        synchronized (LOCK) {
            return sLogcatProcess != null && sLogcatProcess.isAlive();
        }
    }

    // ========== 私有辅助方法 ==========

    /**
     * 轮转日志：client.1.log → 删除, client.log → client.1.log
     * 仅当进程未启动时调用，保证数据完整性
     */
    private static void rotateLogs(Context context) {
        try {
            File logsDir = new File(context.getExternalFilesDir(null).getAbsolutePath() + "/logs/");
            File logFile = new File(logsDir, "client.log");
            File logBak = new File(logsDir, "client1.log");
            if (logBak.exists()) {
                logBak.delete();
            }
            if (logFile.exists()) {
                logFile.renameTo(logBak);
            }
            Log.i("LogManager", "Logs rotated");
        } catch (Exception ignored) {
            // 轮转失败不影响主流程
        }
    }
}