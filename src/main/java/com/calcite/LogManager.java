package com.calcite;

import android.content.Context;
import android.util.Log;

import java.io.File;

public class LogManager {

    private static Process sLogcatProcess = null;

    /**
     * 轮转日志：client.1.log → 删除, client.log → client.1.log
     */
    public static void rotateLogs(Context context) {
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
        } catch (Exception ignored) {
            // 轮转失败不影响主流程
        }
    }

    /**
     * 启动 logcat 日志捕获（如果尚未启动）
     */
    public static void startLogcatCapture(Context context) {
        // 如果进程已经存在且存活，则不再重复启动
        if (sLogcatProcess != null && sLogcatProcess.isAlive()) {
            Log.i("LogManager", "Logcat already running");
            return;
        }

        try {
            String logPath = context.getExternalFilesDir(null).getAbsolutePath() + "/logs/client.log";
            new File(logPath).getParentFile().mkdirs();

            // 清空旧缓冲区
            Runtime.getRuntime().exec(new String[]{"logcat", "-c"});

            // 启动重定向进程
            sLogcatProcess = Runtime.getRuntime().exec(new String[]{"logcat", "-f", logPath, "*:V"});
            Log.i("LogManager", "Logcat capture started: " + logPath);

        } catch (Exception e) {
            Log.e("LogManager", "Failed to start logcat capture", e);
        }
    }

    /**
     * 停止 logcat 捕获
     */
    public static void stopLogcatCapture() {
        if (sLogcatProcess != null) {
            sLogcatProcess.destroy();
            sLogcatProcess = null;
            Log.i("LogManager", "Logcat capture stopped");
        }
    }

    /**
     * 获取 logcat 进程（供外部判断是否已在运行）
     */
    public static Process getLogcatProcess() {
        return sLogcatProcess;
    }
}