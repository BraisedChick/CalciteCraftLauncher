package com.minecraft;

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
    static {
        System.loadLibrary("minecraftclient");
    }

    private RendererSurfaceView rendererSurfaceView;

    // 按键码常量（与 C++ CameraController 中的定义对应）
    private static final int KEY_W = 0;
    private static final int KEY_S = 1;
    private static final int KEY_A = 2;
    private static final int KEY_D = 3;
    private static final int KEY_UP = 4;
    private static final int KEY_DOWN = 5;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        android.util.Log.i("MainActivity", "========================================");
        android.util.Log.i("MainActivity", "onCreate started");
        android.util.Log.i("MainActivity", "========================================");

        // 复制 blocks.json 从 assets 到 files 目录
        copyBlocksJsonFromAssets();

        // 强制横屏
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);

        // 启用沉浸式模式
        enableImmersiveMode();

        setContentView(R.layout.activity_main);

        // 获取启动器传递的参数
        Intent intent = getIntent();
        String username = intent.getStringExtra("username");
        int protocolVersion = intent.getIntExtra("protocol_version", 758);

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

        // 设置 AssetManager（供 C++ 层加载纹理等资源）
        setAssetManager(getAssets());

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

    private void copyBlocksJsonFromAssets() {
        AssetManager assetManager = getAssets();
        java.io.File destFile = new java.io.File(getFilesDir(), "blocks.json");

        if (destFile.exists()) {
            return;
        }

        try {
            java.io.InputStream inputStream = assetManager.open("blocks.json");
            java.io.FileOutputStream outputStream = new java.io.FileOutputStream(destFile);

            byte[] buffer = new byte[8192];
            int bytesRead;
            while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
            }

            inputStream.close();
            outputStream.flush();
            outputStream.close();
        } catch (Exception e) {
            android.util.Log.e("MainActivity", "Failed to copy blocks.json", e);
        }
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
