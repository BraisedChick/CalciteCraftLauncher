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
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

public class MainActivity extends Activity {
    static {
        System.loadLibrary("minecraftclient");
    }

    private RendererSurfaceView rendererSurfaceView;
    private FrameLayout joystickContainer;
    private View joystickKnob;

    // 按键码常量（与 C++ CameraController.h 中的定义对应）
    private static final int KEY_W = 0;
    private static final int KEY_S = 1;
    private static final int KEY_A = 2;
    private static final int KEY_D = 3;
    private static final int KEY_UP = 4;
    private static final int KEY_DOWN = 5;

    // 摇杆控制
    private boolean isJoystickActive = false;
    private float joystickCenterX = 0;
    private float joystickCenterY = 0;
    private float joystickDX = 0;
    private float joystickDY = 0;

    // 触摸控制（视角）
    private float lastTouchX = 0;
    private float lastTouchY = 0;
    private boolean isFirstTouch = true;
    private boolean isDragging = false;

    private LinearLayout elevationButtons;
    private Button btnUp;
    private Button btnDown;

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
        joystickContainer = findViewById(R.id.joystickContainer);
        joystickKnob = findViewById(R.id.joystickKnob);
        elevationButtons = findViewById(R.id.elevationButtons);
        btnUp = findViewById(R.id.btnUp);
        btnDown = findViewById(R.id.btnDown);

        // 设置 AssetManager（供 C++ 层加载纹理等资源）
        setAssetManager(getAssets());

        // 设置上升按钮触摸监听
        btnUp.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        setKeyState(KEY_UP, true);
                        return true;
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        setKeyState(KEY_UP, false);
                        return true;
                }
                return false;
            }
        });

        // 设置下降按钮触摸监听
        btnDown.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        setKeyState(KEY_DOWN, true);
                        return true;
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        setKeyState(KEY_DOWN, false);
                        return true;
                }
                return false;
            }
        });

        // 设置触摸监听器（视角控制 + ImGui 转发）
        rendererSurfaceView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                float x = event.getX();
                float y = event.getY();

                // 检查 UI 状态
                if (isUIDisplayed()) {
                    // 菜单界面：触摸事件转发给 ImGui
                    int action;
                    switch (event.getAction()) {
                        case MotionEvent.ACTION_DOWN:    action = 0; break;
                        case MotionEvent.ACTION_UP:      action = 1; break;
                        case MotionEvent.ACTION_MOVE:    action = 2; break;
                        default:                         action = 1; break;
                    }
                    onTouchEventImGui(x, y, action);
                    return true;
                }

                // 游戏中：视角控制
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        lastTouchX = x;
                        lastTouchY = y;
                        isDragging = true;
                        break;

                    case MotionEvent.ACTION_MOVE:
                        if (isDragging) {
                            float dx = x - lastTouchX;
                            float dy = y - lastTouchY;

                            float pitchDelta = dy * 0.005f;
                            float yawDelta = dx * 0.005f;

                            updateCameraAngle(pitchDelta, yawDelta);

                            lastTouchX = x;
                            lastTouchY = y;
                        }
                        break;

                    case MotionEvent.ACTION_UP:
                        isDragging = false;
                        break;
                }

                return true;
            }
        });

        // 设置摇杆触摸监听器
        joystickContainer.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                float x = event.getX();
                float y = event.getY();

                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        isJoystickActive = true;
                        joystickCenterX = 75;
                        joystickCenterY = 75;
                        updateJoystickPosition(x, y);
                        break;

                    case MotionEvent.ACTION_MOVE:
                        if (isJoystickActive) {
                            updateJoystickPosition(x, y);
                        }
                        break;

                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        isJoystickActive = false;
                        joystickDX = 0;
                        joystickDY = 0;
                        joystickKnob.setTranslationX(0);
                        joystickKnob.setTranslationY(0);
                        setJoystickInput(0, 0);
                        break;
                }

                return true;
            }
        });

        android.util.Log.i("MainActivity", "onCreate completed, surface view will trigger initRenderer");
    }

    private void updateJoystickPosition(float x, float y) {
        float maxDistance = 45;

        float dx = x - joystickCenterX;
        float dy = y - joystickCenterY;
        float distance = (float) Math.sqrt(dx * dx + dy * dy);

        if (distance > maxDistance) {
            float ratio = maxDistance / distance;
            dx *= ratio;
            dy *= ratio;
        }

        joystickKnob.setTranslationX(dx);
        joystickKnob.setTranslationY(dy);

        joystickDX = dx / maxDistance;
        joystickDY = dy / maxDistance;

        setJoystickInput(joystickDX, joystickDY);
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
    private native void updateCameraAngle(float pitchDelta, float yawDelta);
    private native void resizeRenderer(int width, int height);
    private native void setRendererType(boolean useVulkan);
    private native void setProtocolVersion(int version);
    private native void setUsername(String username);
    private native void onTouchEventImGui(float x, float y, int action);
    private native boolean isUIDisplayed();
    private native boolean onBackPressedNative();
    native void addImGuiCharacter(int c);
    private native void setKeyState(int key, boolean pressed);
    private native void setJoystickInput(float dx, float dy);

    // ===== UI 控制方法 =====
    public void showInGameUI() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                elevationButtons.setVisibility(View.VISIBLE);
                joystickContainer.setVisibility(View.VISIBLE);
            }
        });
    }

    public void hideInGameUI() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                elevationButtons.setVisibility(View.GONE);
                joystickContainer.setVisibility(View.GONE);
            }
        });
    }

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
