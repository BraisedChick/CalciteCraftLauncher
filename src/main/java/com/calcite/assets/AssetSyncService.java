package com.calcite.assets;

import android.content.Context;
import android.util.Log;

import com.calcite.MainActivity;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import okhttp3.Request;
import okhttp3.Response;

/**
 * 资源管理
 * 元数据链：version_manifest → 版本 JSON(sha1 校验) → assetIndex JSON → objects 差量下载
 * 本地布局（root = getExternalFilesDir("assets")）：
 *   meta/version_manifest.json   24h TTL 缓存
 *   versions/<id>.json           按 manifest 中 sha1 校验
 *   indexes/<assetId>.json       资产索引（不强判 sha1，对齐 FCL）
 *   objects/<hash前2位>/<hash>   内容寻址对象库，本地存在即跳过（天然续传/跨版本去重）
 *   current_index.txt            同步成功后写入的 assetId（C++ AssetStore 据此建库）
 *   synced_version.txt           同步成功后写入的原版版本号（Java 侧快速判断是否需同步）
 *   vanilla/                     最新正式版 client.jar 解包出的 assets/minecraft（剥前缀，sounds 归哈希库不入此）
 *   vanilla_version.txt          vanilla/ 目录对应的 jar 版本 id（C++ TextureLoader 原版兜底源）
 */
public class AssetSyncService {
    private static final String TAG = "AssetSyncService";

    private static final String MANIFEST_URL =
            "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
    private static final String OBJECTS_BASE_URL =
            "https://resources.download.minecraft.net/";

    private static final long MANIFEST_TTL_MS = 24L * 3600 * 1000; // 24h
    private static final int CONCURRENCY = 6;   // 对齐 FCL getConcurrency()
    private static final int MAX_RETRY = 3;     // 对齐 FCL FileDownloadTask retry=3

    private static final AtomicBoolean cancelRequested = new AtomicBoolean(false);

    public interface Callback {
        /** 元数据解析完成：objects 总数、需下载文件数、需下载字节数（后台线程回调） */
        void onCheckComplete(int totalObjects, int needCount, long needBytes);
        /** 下载进度（后台线程回调，500ms 节流；UI 层自行 runOnUiThread）；currentFile 为最近开始下载的对象路径 */
        void onProgress(int doneFiles, int totalFiles, long bytesDone, long needBytes, long speedBps, String currentFile);
        /** 最终结果；成功 = 全部对象就绪且 current_index.txt 已写入 */
        void onResult(boolean success, String message);
    }

    /** 请求中止当前同步（已入库对象保留，下次续传） */
    public static void cancel() {
        cancelRequested.set(true);
    }

    /** 快速本地检查：该原版版本的资产是否已同步完成（不发起任何网络请求） */
    public static boolean isVersionSynced(Context ctx, String versionName) {
        File root = ctx.getExternalFilesDir("assets");
        if (root == null) return false;
        File indexMarker = new File(root, "current_index.txt");
        File versionMarker = new File(root, "synced_version.txt");
        if (!indexMarker.exists() || !versionMarker.exists()) return false;
        String synced = readFirstLine(versionMarker);
        if (synced == null || !synced.equals(versionName)) return false;
        // index 文件本体也必须还在
        String assetId = readFirstLine(indexMarker);
        if (assetId == null || assetId.isEmpty()) return false;
        if (!new File(root, "indexes/" + assetId + ".json").exists()) return false;
        // vanilla 底层也必须存在（解包中断/丢失后需重新走同步对话框）
        if (!new File(root, "vanilla_version.txt").exists()) return false;
        File[] vanillaFiles = new File(root, "vanilla").listFiles();
        return vanillaFiles != null && vanillaFiles.length > 0;
    }

    /** 后台线程执行完整同步链 */
    public static void sync(Context ctx, String versionName, Callback cb) {
        cancelRequested.set(false);
        final Context appCtx = ctx.getApplicationContext();
        new Thread(() -> {
            try {
                runSync(appCtx, versionName, cb);
            } catch (Exception e) {
                Log.e(TAG, "Asset sync failed: " + e.getMessage(), e);
                cb.onResult(false, e.getMessage());
            }
        }, "AssetSync").start();
    }

    // ===== 同步主流程 =====

    private static void runSync(Context ctx, String versionName, Callback cb) throws Exception {
        File root = ctx.getExternalFilesDir("assets");
        if (root == null) throw new IllegalStateException("external assets dir unavailable");
        File metaDir = new File(root, "meta");
        File versionsDir = new File(root, "versions");
        File indexesDir = new File(root, "indexes");
        File objectsDir = new File(root, "objects");
        for (File d : new File[]{metaDir, versionsDir, indexesDir, objectsDir}) {
            if (!d.exists() && !d.mkdirs()) throw new IllegalStateException("mkdirs failed: " + d);
        }

        // 1) version_manifest（24h 内用本地缓存）
        File manifestFile = new File(metaDir, "version_manifest.json");
        long age = System.currentTimeMillis() - manifestFile.lastModified();
        if (!manifestFile.exists() || age > MANIFEST_TTL_MS) {
            downloadSimple(MANIFEST_URL, manifestFile);
        }
        String versionUrl;
        String versionSha1;
        String latestId = null;   // 最新正式版（vanilla 底层 jar 来源）
        String latestUrl = null;
        String latestSha1 = "";
        try {
            JSONObject manifest = new JSONObject(slurp(manifestFile));
            versionUrl = null;
            versionSha1 = "";
            Object versions = manifest.opt("versions");
            if (!(versions instanceof org.json.JSONArray)) {
                throw new IllegalStateException("manifest has no versions array");
            }
            org.json.JSONArray arr = (org.json.JSONArray) versions;
            JSONObject latestObj = manifest.optJSONObject("latest");
            String latestRelease = latestObj != null ? latestObj.optString("release") : "";
            for (int i = 0; i < arr.length(); i++) {
                JSONObject v = arr.getJSONObject(i);
                String id = v.optString("id");
                if (versionName.equals(id)) {
                    versionUrl = v.optString("url");
                    versionSha1 = v.optString("sha1"); // v2 提供，v1 为空
                }
                if (id.equals(latestRelease)) {
                    latestId = id;
                    latestUrl = v.optString("url");
                    latestSha1 = v.optString("sha1");
                }
                if (versionUrl != null && latestId != null) break;
            }
        } catch (Exception e) {
            // 缓存损坏：删除后下次启动重下
            manifestFile.delete();
            throw e;
        }
        if (versionUrl == null || versionUrl.isEmpty()) {
            throw new IllegalStateException("version '" + versionName + "' not in manifest");
        }

        // 2) 版本 JSON：按 manifest 给的 sha1 校验（sha1 缺失时仅验证可解析）
        File versionFile = new File(versionsDir, versionName + ".json");
        boolean versionOk = versionFile.exists()
                && (versionSha1.isEmpty() || versionSha1.equalsIgnoreCase(sha1Hex(versionFile)));
        if (!versionOk) {
            if (versionFile.exists()) versionFile.delete();
            downloadVerified(versionUrl, versionFile, versionSha1.isEmpty() ? null : versionSha1);
        }

        // 2.5) vanilla 底层元信息：本地已解包则整步跳过；否则备好最新正式版 client.jar 的下载信息
        File vanillaDir = new File(root, "vanilla");
        String vanillaVer = readFirstLine(new File(root, "vanilla_version.txt"));
        boolean vanillaNeeded = vanillaVer == null || vanillaVer.isEmpty() || !vanillaDir.isDirectory();
        String jarUrl = null;
        String jarSha1 = "";
        long jarSize = 0;
        File jarFile = null;
        if (vanillaNeeded) {
            if (latestId == null || latestUrl == null || latestUrl.isEmpty()) {
                throw new IllegalStateException("latest release not in manifest");
            }
            File latestVersionFile = new File(versionsDir, latestId + ".json");
            boolean lvOk = latestVersionFile.exists()
                    && (latestSha1.isEmpty() || latestSha1.equalsIgnoreCase(sha1Hex(latestVersionFile)));
            if (!lvOk) {
                if (latestVersionFile.exists()) latestVersionFile.delete();
                downloadVerified(latestUrl, latestVersionFile, latestSha1.isEmpty() ? null : latestSha1);
            }
            JSONObject downloads = new JSONObject(slurp(latestVersionFile)).optJSONObject("downloads");
            JSONObject client = downloads != null ? downloads.optJSONObject("client") : null;
            if (client == null) throw new IllegalStateException("latest version json has no downloads.client");
            jarUrl = client.optString("url");
            jarSha1 = client.optString("sha1");
            jarSize = client.optLong("size");
            if (jarUrl.isEmpty()) throw new IllegalStateException("client jar url missing");
            jarFile = new File(root, "client-" + latestId + ".jar");
        }

        // 3) 资产索引：对齐 FCL——本地已存在时不强判 sha1（Mojang 会随时改 index 内容）
        JSONObject versionJson = new JSONObject(slurp(versionFile));
        JSONObject assetIndex = versionJson.optJSONObject("assetIndex");
        if (assetIndex == null) throw new IllegalStateException("version json has no assetIndex");
        String assetId = assetIndex.optString("id");
        String indexUrl = assetIndex.optString("url");
        String indexSha1 = assetIndex.optString("sha1");
        if (assetId.isEmpty() || indexUrl.isEmpty()) {
            throw new IllegalStateException("assetIndex missing id/url");
        }

        File indexFile = new File(indexesDir, assetId + ".json");
        boolean indexValid = false;
        if (indexFile.exists()) {
            // URL 路径中含 sha1 则比对文件哈希，否则仅验证 JSON 可解析
            boolean urlCarriesSha1 = !indexSha1.isEmpty() && indexUrl.contains(indexSha1);
            indexValid = !urlCarriesSha1 || indexSha1.equalsIgnoreCase(sha1Hex(indexFile));
            if (indexValid) {
                try {
                    new JSONObject(slurp(indexFile));
                } catch (Exception e) {
                    indexValid = false;
                }
            }
        }
        if (!indexValid) {
            if (indexFile.exists()) indexFile.delete();
            // index 的 sha1 字段可能过时（Mojang 会更新 index 内容），下载后不强制校验哈希，
            // 仅要求 JSON 可解析（downloadVerified 传 null 即只查可解析性由调用方保证）
            downloadSimple(indexUrl, indexFile);
            new JSONObject(slurp(indexFile)); // 不可解析则抛异常终止
        }

        // 4) 遍历 objects，构建差量下载队列（本地存在即跳过；顺带对比 size）
        JSONObject indexJson = new JSONObject(slurp(indexFile));
        JSONObject objects = indexJson.optJSONObject("objects");
        if (objects == null) throw new IllegalStateException("asset index has no objects map");

        final List<String> pendingHashes = new ArrayList<>();
        final List<String> pendingPaths = new ArrayList<>(); // 供 UI 展示的真实资产路径
        final List<File> pendingTargets = new ArrayList<>();
        long needBytes = 0;
        java.util.Iterator<String> keys = objects.keys();
        while (keys.hasNext()) {
            String key = keys.next(); // 命名空间前缀两种格式："minecraft/"(1.18 实际)或 "minecraft:"(新版)，也有的 key 无前缀
            JSONObject obj = objects.getJSONObject(key);
            String hash = obj.optString("hash");
            long size = obj.optLong("size");
            if (hash.length() < 2) continue;
            File target = new File(new File(objectsDir, hash.substring(0, 2)), hash);
            if (target.exists() && (size <= 0 || target.length() == size)) continue;
            pendingHashes.add(hash);
            pendingPaths.add(key.startsWith("minecraft:") || key.startsWith("minecraft/")
                    ? key.substring("minecraft/".length()) : key);
            pendingTargets.add(target);
            if (size > 0) needBytes += size;
        }

        int total = objects.length();
        int needCount = pendingHashes.size();
        // 总量口径：jar 需下载时计入 1 个文件与自身字节数，vanilla 步骤复用同一进度回调
        final int totalFilesToReport = needCount + (vanillaNeeded ? 1 : 0);
        final long totalNeedBytes = needBytes + (vanillaNeeded ? jarSize : 0);
        cb.onCheckComplete(total, totalFilesToReport, totalNeedBytes);

        if (cancelRequested.get()) {
            cb.onResult(false, "已取消下载");
            return;
        }

        // 5) 并发下载：.tmp → 边下边算 SHA-1 → 校验通过原子 rename 入库
        final AtomicInteger doneFiles = new AtomicInteger(0);
        final AtomicLong bytesDone = new AtomicLong(0);
        // 当前下载/处理对象路径（并发任务与 vanilla 步骤共用展示位）
        final AtomicReference<String> currentFile = new AtomicReference<>("");
        if (needCount > 0) {
            final AtomicInteger failedFiles = new AtomicInteger(0);
            final AtomicBoolean finished = new AtomicBoolean(false);
            final long reportNeedBytes = totalNeedBytes;
            final int reportTotal = totalFilesToReport;

            // 进度上报线程（500ms 刷新，瞬时速度按增量计算）
            Thread reporter = new Thread(() -> {
                long lastTime = System.currentTimeMillis();
                long lastBytes = 0;
                while (!finished.get()) {
                    try { Thread.sleep(500); } catch (InterruptedException ignored) { }
                    long now = System.currentTimeMillis();
                    long b = bytesDone.get();
                    long speed = now > lastTime ? (b - lastBytes) * 1000 / (now - lastTime) : 0;
                    lastBytes = b;
                    lastTime = now;
                    cb.onProgress(doneFiles.get(), reportTotal, b, reportNeedBytes, speed, currentFile.get());
                    if (cancelRequested.get()) break;
                }
            }, "AssetSyncProgress");

            ExecutorService pool = Executors.newFixedThreadPool(CONCURRENCY);
            CountDownLatch latch = new CountDownLatch(needCount);
            for (int i = 0; i < needCount; i++) {
                final String hash = pendingHashes.get(i);
                final String displayPath = pendingPaths.get(i);
                final File target = pendingTargets.get(i);
                pool.execute(() -> {
                    boolean ok = downloadObject(hash, displayPath, target, bytesDone, currentFile);
                    if (ok) doneFiles.incrementAndGet();
                    else failedFiles.incrementAndGet();
                    latch.countDown();
                });
            }

            reporter.start();
            latch.await();
            finished.set(true);
            reporter.join(2000);
            pool.shutdown();

            if (cancelRequested.get()) {
                cb.onResult(false, "已取消下载");
                return;
            }
            if (failedFiles.get() > 0) {
                // 不写 current_index.txt：store 保持未就绪/上一状态，已入库对象下次续传时哈希命中
                cb.onResult(false, failedFiles.get() + " 个文件下载失败");
                return;
            }
        }

        // 5.5) vanilla 底层：下载最新正式版 client.jar（sha1 强校验）并解包 assets/minecraft 到 vanilla/
        if (vanillaNeeded) {
            if (cancelRequested.get()) {
                cb.onResult(false, "已取消下载");
                return;
            }
            final AtomicLong jarBytesDone = new AtomicLong(0);
            final AtomicBoolean jarDownloaded = new AtomicBoolean(false);
            final AtomicBoolean vanillaFinished = new AtomicBoolean(false);
            final long baseBytes = bytesDone.get();
            final int baseDone = doneFiles.get();
            currentFile.set("client-" + latestId + ".jar");
            Thread vreporter = new Thread(() -> {
                long lastTime = System.currentTimeMillis();
                long lastBytes = baseBytes;
                while (!vanillaFinished.get()) {
                    try { Thread.sleep(500); } catch (InterruptedException ignored) { }
                    long now = System.currentTimeMillis();
                    long b = baseBytes + jarBytesDone.get();
                    long speed = now > lastTime ? (b - lastBytes) * 1000 / (now - lastTime) : 0;
                    lastBytes = b;
                    lastTime = now;
                    int done = baseDone + (jarDownloaded.get() ? 1 : 0);
                    cb.onProgress(done, totalFilesToReport, b, totalNeedBytes, speed, currentFile.get());
                }
            }, "VanillaSyncProgress");
            vreporter.start();
            try {
                File tmpJar = new File(root, jarFile.getName() + ".tmp");
                if (jarFile.exists()) jarFile.delete();
                MessageDigest md = MessageDigest.getInstance("SHA-1");
                Request request = new Request.Builder().url(jarUrl).build();
                try (Response response = MainActivity.httpClient.newCall(request).execute()) {
                    if (!response.isSuccessful()) {
                        throw new IOException("HTTP " + response.code() + " for " + jarUrl);
                    }
                    try (InputStream is = response.body().byteStream();
                         FileOutputStream fos = new FileOutputStream(tmpJar)) {
                        byte[] buffer = new byte[8192];
                        int n;
                        while ((n = is.read(buffer)) != -1) {
                            if (cancelRequested.get()) {
                                tmpJar.delete();
                                cb.onResult(false, "已取消下载");
                                return;
                            }
                            md.update(buffer, 0, n);
                            fos.write(buffer, 0, n);
                            jarBytesDone.addAndGet(n);
                        }
                    }
                }
                if (!jarSha1.isEmpty() && !jarSha1.equalsIgnoreCase(toHex(md.digest()))) {
                    tmpJar.delete();
                    throw new IOException("sha1 mismatch for client jar");
                }
                if (!tmpJar.renameTo(jarFile)) throw new IOException("rename failed for " + jarFile);
                jarDownloaded.set(true);

                // 解包：清空旧 vanilla → 写 vanilla.tmp → 整体成功后原子 rename
                currentFile.set("解压原版资源 (vanilla assets)");
                File vanillaTmp = new File(root, "vanilla.tmp");
                deleteRecursive(vanillaTmp);
                deleteRecursive(vanillaDir);
                unzipVanilla(jarFile, vanillaTmp);
                if (cancelRequested.get()) {
                    deleteRecursive(vanillaTmp);
                    jarFile.delete();
                    cb.onResult(false, "已取消下载");
                    return;
                }
                if (!vanillaTmp.renameTo(vanillaDir)) throw new IOException("rename failed for vanilla dir");
                jarFile.delete();
                writeFile(new File(root, "vanilla_version.txt"), latestId);
            } finally {
                vanillaFinished.set(true);
            }
        }

        // 6) 全部就绪：写就绪标记（C++ AssetStore 读 current_index.txt 建库）
        writeFile(new File(root, "current_index.txt"), assetId);
        writeFile(new File(root, "synced_version.txt"), versionName);
        cb.onResult(true, null);
    }

    // ===== 下载工具 =====

    /** 单对象下载：3 次重试，URL 候选列表结构（默认仅官方源，预留镜像扩展位） */
    private static boolean downloadObject(String hash, String displayPath, File target,
                                          AtomicLong bytesDone, AtomicReference<String> currentFile) {
        String twoHex = hash.substring(0, 2);
        List<String> urlCandidates = new ArrayList<>();
        urlCandidates.add(OBJECTS_BASE_URL + twoHex + "/" + hash);

        File parent = target.getParentFile();
        if (!parent.exists() && !parent.mkdirs()) return false;
        File tmp = new File(parent, target.getName() + ".tmp." + Thread.currentThread().getId());

        for (int attempt = 0; attempt < MAX_RETRY; attempt++) {
            if (cancelRequested.get()) {
                tmp.delete();
                return false;
            }
            currentFile.set(displayPath); // 每次重试都刷新 UI 展示的文件名
            for (String url : urlCandidates) {
                try {
                    Request request = new Request.Builder().url(url).build();
                    try (Response response = MainActivity.httpClient.newCall(request).execute()) {
                        if (!response.isSuccessful()) continue;
                        MessageDigest md = MessageDigest.getInstance("SHA-1");
                        long read = 0;
                        try (InputStream is = response.body().byteStream();
                             FileOutputStream fos = new FileOutputStream(tmp)) {
                            byte[] buffer = new byte[8192];
                            int n;
                            while ((n = is.read(buffer)) != -1) {
                                md.update(buffer, 0, n);
                                fos.write(buffer, 0, n);
                                read += n;
                            }
                        }
                        String got = toHex(md.digest());
                        if (got.equalsIgnoreCase(hash)) {
                            bytesDone.addAndGet(read);
                            // 已存在（其他线程刚下完）也视为成功，tmp 直接丢弃
                            if (target.exists() || tmp.renameTo(target)) {
                                tmp.delete();
                                return true;
                            }
                        }
                        tmp.delete();
                    }
                } catch (Exception e) {
                    tmp.delete();
                    // 网络异常，重试下一轮
                }
            }
        }
        tmp.delete();
        return false;
    }

    /** 下载并按 sha1 校验（sha1Hex 为 null 时不校验），原子 rename 落盘 */
    private static void downloadVerified(String url, File dest, String expectedSha1) throws Exception {
        File tmp = new File(dest.getParentFile(), dest.getName() + ".tmp");
        MessageDigest md = MessageDigest.getInstance("SHA-1");
        Request request = new Request.Builder().url(url).build();
        try (Response response = MainActivity.httpClient.newCall(request).execute()) {
            if (!response.isSuccessful()) {
                throw new java.io.IOException("HTTP " + response.code() + " for " + url);
            }
            try (InputStream is = response.body().byteStream();
                 FileOutputStream fos = new FileOutputStream(tmp)) {
                byte[] buffer = new byte[8192];
                int n;
                while ((n = is.read(buffer)) != -1) {
                    md.update(buffer, 0, n);
                    fos.write(buffer, 0, n);
                }
            }
        }
        if (expectedSha1 != null && !expectedSha1.equalsIgnoreCase(toHex(md.digest()))) {
            tmp.delete();
            throw new java.io.IOException("sha1 mismatch for " + url);
        }
        if (dest.exists()) dest.delete();
        if (!tmp.renameTo(dest)) throw new java.io.IOException("rename failed for " + dest);
    }

    /** 元数据小文件直下（manifest/version json 由上层逻辑负责校验有效性） */
    private static void downloadSimple(String url, File dest) throws Exception {
        downloadVerified(url, dest, null);
    }

    // ===== 本地文件工具 =====

    /** 解 jar 的 assets/minecraft/** 到 outDir（剥前缀）；sounds/ 跳过（音效归哈希资产库） */
    private static void unzipVanilla(File jar, File outDir) throws Exception {
        if (!outDir.mkdirs() && !outDir.isDirectory()) throw new IOException("mkdirs failed: " + outDir);
        try (ZipInputStream zis = new ZipInputStream(
                new BufferedInputStream(new FileInputStream(jar), 65536))) {
            ZipEntry entry;
            while ((entry = zis.getNextEntry()) != null) {
                if (cancelRequested.get()) return;
                if (entry.isDirectory()) continue;
                String name = entry.getName();
                if (!name.startsWith("assets/minecraft/")
                        || name.startsWith("assets/minecraft/sounds/")) continue;
                String rel = name.substring("assets/minecraft/".length());
                if (rel.contains("..")) continue; // zip slip 防护
                File out = new File(outDir, rel);
                File parent = out.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                    throw new IOException("mkdirs failed: " + parent);
                }
                try (FileOutputStream fos = new FileOutputStream(out)) {
                    byte[] buffer = new byte[8192];
                    int n;
                    while ((n = zis.read(buffer)) != -1) fos.write(buffer, 0, n);
                }
            }
        }
    }

    private static void deleteRecursive(File f) {
        if (f.isDirectory()) {
            File[] kids = f.listFiles();
            if (kids != null) for (File k : kids) deleteRecursive(k);
        }
        f.delete();
    }

    private static String slurp(File f) throws Exception {
        byte[] buf = new byte[(int) f.length()];
        try (FileInputStream fis = new FileInputStream(f)) {
            int off = 0, n;
            while (off < buf.length && (n = fis.read(buf, off, buf.length - off)) > 0) off += n;
        }
        return new String(buf, "UTF-8");
    }

    private static String readFirstLine(File f) {
        try {
            String s = slurp(f);
            int nl = s.indexOf('\n');
            s = (nl >= 0 ? s.substring(0, nl) : s).trim();
            return s.isEmpty() ? null : s;
        } catch (Exception e) {
            return null;
        }
    }

    private static void writeFile(File f, String content) throws Exception {
        try (Writer w = new OutputStreamWriter(new FileOutputStream(f), "UTF-8")) {
            w.write(content);
        }
    }

    private static String sha1Hex(File f) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-1");
        try (FileInputStream fis = new FileInputStream(f)) {
            byte[] buffer = new byte[8192];
            int n;
            while ((n = fis.read(buffer)) != -1) md.update(buffer, 0, n);
        }
        return toHex(md.digest());
    }

    private static String toHex(byte[] digest) {
        StringBuilder sb = new StringBuilder(digest.length * 2);
        for (byte b : digest) {
            sb.append(Character.forDigit((b >> 4) & 0xF, 16));
            sb.append(Character.forDigit(b & 0xF, 16));
        }
        return sb.toString();
    }
}
