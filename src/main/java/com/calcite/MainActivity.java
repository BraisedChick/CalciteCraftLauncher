package com.calcite;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.inputmethod.InputMethodManager;

public class MainActivity extends Activity {
    private RendererSurfaceView rendererSurfaceView;

    // 按键码常量（与 C++ 中的定义对应）
    private static final int KEY_W = 0;
    private static final int KEY_S = 1;
    private static final int KEY_A = 2;
    private static final int KEY_D = 3;
    private static final int KEY_UP = 4;
    private static final int KEY_DOWN = 5;

    private static void loadLibraryForProtocol(int protocolVersion) {
        String libName = "mc_" + protocolVersion;
        try {
            System.loadLibrary(libName);
            android.util.Log.i("MainActivity", "Loaded library: " + libName);
        } catch (UnsatisfiedLinkError e) {
            android.util.Log.w("MainActivity", "Library " + libName + " not found, falling back to mc_758");
            System.loadLibrary("mc_758");
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // 先读取 intent，加载对应协议版本的 .so（必须在任何 native 方法调用之前）
        Intent intent = getIntent();
        int protocolVersion = intent.getIntExtra("protocol_version", 758);
        String username = intent.getStringExtra("username");
        loadLibraryForProtocol(protocolVersion);

        android.util.Log.i("MainActivity", "========================================");
        android.util.Log.i("MainActivity", "onCreate started, protocol=" + protocolVersion);
        android.util.Log.i("MainActivity", "========================================");

        // 强制横屏
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);

        // 允许内容延伸到凹槽区域（须在 setContentView 前调用）
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        // 启用沉浸式模式
        enableImmersiveMode();

        // 在 SurfaceView 创建前设置 AssetManager 和 ZIP 路径
        // 否则 initRenderer 触发时资源还没准备好
        setAssetManager(getAssets());

        // 搜索 resourcepack.zip 的优先级路径列表
        String[] zipPaths = {
            // 1. 传统 external storage 路径
            android.os.Environment.getExternalStorageDirectory().getAbsolutePath()
                + "/Android/data/com.calcite/resourcepack.zip",
            // 2. getExternalFilesDir 路径（Android 11+ 推荐）
            getExternalFilesDir(null).getAbsolutePath() + "/resourcepack.zip",
            // 3. 内部存储 data 目录
            getDataDir().getAbsolutePath() + "/resourcepack.zip"
        };

        boolean zipFound = false;
        for (String path : zipPaths) {
            if (new java.io.File(path).exists()) {
                setTextureZipPath(path);
                android.util.Log.i("MainActivity", "ZIP loaded from: " + path);
                zipFound = true;
                break;
            }
        }
        if (!zipFound) {
            android.util.Log.w("MainActivity", "ZIP not found at any path, textures may show as purple");
        }

        setContentView(R.layout.activity_main);

        // 保存用户名到 C++
        if (username != null && !username.isEmpty()) {
            setUsername(username);
        } else {
            setUsername("Player");
        }

        // 保存协议版本
        saveProtocolVersion(protocolVersion);

        // 初始化视图
        rendererSurfaceView = findViewById(R.id.gameSurface);

        // 设置触摸监听器（多点触控支持，统一转发到 C++）
        rendererSurfaceView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                int actionMasked = event.getActionMasked();

                if (actionMasked == MotionEvent.ACTION_MOVE) {
                    // MOVE：遍历所有 active 指针
                    for (int i = 0; i < event.getPointerCount(); i++) {
                        int pid = event.getPointerId(i);
                        float x = event.getX(i);
                        float y = event.getY(i);
                        onTouchEventImGui(pid, x, y, 2);
                    }
                } else {
                    int pointerIndex = event.getActionIndex();
                    int pid = event.getPointerId(pointerIndex);
                    float x = event.getX(pointerIndex);
                    float y = event.getY(pointerIndex);
                    int mappedAction;
                    switch (actionMasked) {
                        case MotionEvent.ACTION_DOWN:
                        case MotionEvent.ACTION_POINTER_DOWN:
                            mappedAction = 0; break;
                        case MotionEvent.ACTION_UP:
                        case MotionEvent.ACTION_POINTER_UP:
                            mappedAction = 1; break;
                        default:
                            mappedAction = 1; break;
                    }
                    onTouchEventImGui(pid, x, y, mappedAction);
                }
                return true;
            }
        });

        android.util.Log.i("MainActivity", "onCreate completed, surface view will trigger initRenderer");
    }

    @Override
    public void onBackPressed() {
        if (!onBackPressedNative()) {
            super.onBackPressed();
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_W:
                setKeyState(KEY_W, true);
                return true;
            case KeyEvent.KEYCODE_S:
                setKeyState(KEY_S, true);
                return true;
            case KeyEvent.KEYCODE_A:
                setKeyState(KEY_A, true);
                return true;
            case KeyEvent.KEYCODE_D:
                setKeyState(KEY_D, true);
                return true;
            case KeyEvent.KEYCODE_SPACE:
                setKeyState(KEY_UP, true);
                return true;
            case KeyEvent.KEYCODE_SHIFT_LEFT:
            case KeyEvent.KEYCODE_SHIFT_RIGHT:
                setKeyState(KEY_DOWN, true);
                return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_W:
                setKeyState(KEY_W, false);
                return true;
            case KeyEvent.KEYCODE_S:
                setKeyState(KEY_S, false);
                return true;
            case KeyEvent.KEYCODE_A:
                setKeyState(KEY_A, false);
                return true;
            case KeyEvent.KEYCODE_D:
                setKeyState(KEY_D, false);
                return true;
            case KeyEvent.KEYCODE_SPACE:
                setKeyState(KEY_UP, false);
                return true;
            case KeyEvent.KEYCODE_SHIFT_LEFT:
            case KeyEvent.KEYCODE_SHIFT_RIGHT:
                setKeyState(KEY_DOWN, false);
                return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    private void enableImmersiveMode() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().getDecorView().post(new Runnable() {
                @Override
                public void run() {
                    WindowInsetsController controller = getWindow().getInsetsController();
                    if (controller != null) {
                        controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                        controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                        );
                    }
                }
            });
        } else {
            View decorView = getWindow().getDecorView();
            int uiOptions = View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                          | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                          | View.SYSTEM_UI_FLAG_FULLSCREEN
                          | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                          | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                          | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN;

            decorView.setSystemUiVisibility(uiOptions);

            decorView.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
                @Override
                public void onSystemUiVisibilityChange(int visibility) {
                    if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                        decorView.setSystemUiVisibility(uiOptions);
                    }
                }
            });
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enableImmersiveMode();
        }
    }

    // ===== Native 方法 =====
    private native void initRenderer(android.view.Surface surface);
    private native void cleanupRenderer();
    private native void setAssetManager(AssetManager assetManager);
    private native void setTextureZipPath(String zipPath);
    private native void resizeRenderer(int width, int height);
    private native void setRendererType(boolean useVulkan);
    private native void setProtocolVersion(int version);
    private native void setUsername(String username);
    private native void onTouchEventImGui(int pointerId, float x, float y, int action);
    private native boolean isUIDisplayed();
    private native boolean onBackPressedNative();
    native void addImGuiCharacter(int c);
    private native void setKeyState(int key, boolean pressed);

    // ImGui 键盘显示/隐藏（从 C++ 渲染线程调用）
    public void showKeyboardImGui(final boolean show) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                if (imm == null) return;
                if (show) {
                    rendererSurfaceView.requestFocus();
                    imm.showSoftInput(rendererSurfaceView, InputMethodManager.SHOW_IMPLICIT);
                } else {
                    imm.hideSoftInputFromWindow(rendererSurfaceView.getWindowToken(), 0);
                }
            }
        });
    }

    private void saveProtocolVersion(int version) {
        setProtocolVersion(version);
    }

    public void onVulkanSurfaceCreated(android.view.Surface surface) {
        try {
            initRenderer(surface);
        } catch (UnsatisfiedLinkError e) {
            android.util.Log.e("MainActivity", "UnsatisfiedLinkError in initRenderer", e);
        } catch (Exception e) {
            android.util.Log.e("MainActivity", "Exception in initRenderer", e);
        }
    }

    public void onSurfaceChanged(int width, int height) {
        try {
            resizeRenderer(width, height);
        } catch (Exception e) {
            android.util.Log.e("MainActivity", "Exception in resizeRenderer", e);
        }
    }

    public void onVulkanSurfaceDestroyed() {
        try {
            cleanupRenderer();
        } catch (Exception e) {
            android.util.Log.e("MainActivity", "Exception in cleanupRenderer", e);
        }
    }
}
