package com.minecraft;

import android.app.Activity;
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
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.RadioGroup;
import android.widget.RadioButton;
import android.widget.Toast;

public class MainActivity extends Activity {
    static {
        System.loadLibrary("minecraftclient");
    }

    private EditText etGameName;
    private EditText etServerIP;
    private EditText etPort;
    private Button btnStartGame;
    private RadioGroup rendererSelector;
    private RadioButton radioOpenGL;
    private RadioButton radioVulkan;
    private LinearLayout elevationButtons;
    private Button btnUp;
    private Button btnDown;
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

    // 渲染状态
    private boolean g_rendering = false;

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        // 这个方法不再使用，触摸事件由 RendererSurfaceView 处理
        return super.onTouchEvent(event);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        android.util.Log.i("MainActivity", "========================================");
        android.util.Log.i("MainActivity", "onCreate started - TEST LOG");
        android.util.Log.i("MainActivity", "========================================");

        // 复制 blocks.json 从 assets 到 files 目录
        copyBlocksJsonFromAssets();

        // 强制横屏
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);

        // 启用沉浸式模式（隐藏状态栏和导航栏）
        enableImmersiveMode();

        setContentView(R.layout.activity_main);

        android.util.Log.i("MainActivity", "Content view set");

        // 获取启动器传递的参数
        Intent intent = getIntent();
        String username = intent.getStringExtra("username");
        String serverIP = intent.getStringExtra("server_ip");
        int port = intent.getIntExtra("port", 25565);
        int protocolVersion = intent.getIntExtra("protocol_version", 758);
        boolean useVulkan = intent.getBooleanExtra("use_vulkan", false);
        
        android.util.Log.i("MainActivity", "Received from launcher:");
        android.util.Log.i("MainActivity", "  Username: " + username);
        android.util.Log.i("MainActivity", "  Server: " + serverIP + ":" + port);
        android.util.Log.i("MainActivity", "  Protocol Version: " + protocolVersion);
        android.util.Log.i("MainActivity", "  Use Vulkan: " + useVulkan);

        // 初始化视图
        etGameName = findViewById(R.id.etGameName);
        etServerIP = findViewById(R.id.etServerIP);
        etPort = findViewById(R.id.etPort);
        btnStartGame = findViewById(R.id.btnStartGame);
        rendererSelector = findViewById(R.id.rendererSelector);
        radioOpenGL = findViewById(R.id.radioOpenGL);
        radioVulkan = findViewById(R.id.radioVulkan);
        rendererSurfaceView = findViewById(R.id.gameSurface);
        joystickContainer = findViewById(R.id.joystickContainer);
        joystickKnob = findViewById(R.id.joystickKnob);
        
        // 初始化上升/下降按钮
        elevationButtons = findViewById(R.id.elevationButtons);
        btnUp = findViewById(R.id.btnUp);
        btnDown = findViewById(R.id.btnDown);

        // 隐藏渲染表面
        rendererSurfaceView.setVisibility(View.GONE);

        // 设置从启动器接收的值
        if (username != null && !username.isEmpty()) {
            etGameName.setText(username);
        } else {
            etGameName.setText("Player");
        }
        
        if (serverIP != null && !serverIP.isEmpty()) {
            etServerIP.setText(serverIP);
        } else {
            etServerIP.setText("127.0.0.1");
        }
        
        etPort.setText(String.valueOf(port));
        
        // 设置渲染器选择
        if (useVulkan) {
            radioVulkan.setChecked(true);
        } else {
            radioOpenGL.setChecked(true);
        }

        // 保存协议版本号（供 C++ 层使用）
        saveProtocolVersion(protocolVersion);

        // 如果从启动器启动，自动开始游戏
        if (username != null && !username.isEmpty() && serverIP != null && !serverIP.isEmpty()) {
            android.util.Log.i("MainActivity", "Auto-starting game from launcher...");
            autoStartGame(username, serverIP, port, useVulkan);
        } else {
            // 否则显示登录界面，等待用户手动输入
            setupManualLogin();
        }

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

        // 设置触摸监听器（视角控制）
        rendererSurfaceView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                float x = event.getX();
                float y = event.getY();

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

                            // 计算角度变化并传递给 C++ 层
                            float pitchDelta = dy * 0.005f;
                            float yawDelta = dx * 0.005f;  // 向右滑动让视角右转

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
                        joystickCenterX = 75; // 摇杆容器中心X
                        joystickCenterY = 75; // 摇杆容器中心Y
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
                        // 重置摇杆位置
                        joystickKnob.setTranslationX(0);
                        joystickKnob.setTranslationY(0);
                        // 通知 C++ 层摇杆归零
                        setJoystickInput(0, 0);
                        break;
                }

                return true;
            }
        });
    }
    
    // 自动启动游戏（从启动器调用）
    private void autoStartGame(String username, String serverIP, int port, boolean useVulkan) {
        // 设置 AssetManager
        setAssetManager(getAssets());

        // 连接到服务器（在后台线程中）
        new Thread(new Runnable() {
            @Override
            public void run() {
                android.util.Log.i("MainActivity", "Connecting to server...");
                boolean connected = connectToServer(serverIP, port, username);
                if (connected) {
                    android.util.Log.i("MainActivity", "Connected successfully!");
                    android.util.Log.i("MainActivity", "Waiting for player position from server...");
                } else {
                    android.util.Log.e("MainActivity", "Failed to connect");
                }
            }
        }).start();

        // 隐藏登录界面
        findViewById(R.id.loginLayout).setVisibility(View.GONE);

        // 显示渲染表面（这会触发 surfaceCreated 回调）
        rendererSurfaceView.setVisibility(View.VISIBLE);

        // 根据用户选择设置渲染器类型
        setRendererType(useVulkan);
        android.util.Log.i("MainActivity", "Renderer type set to: " + (useVulkan ? "Vulkan" : "OpenGL ES"));

        // 显示上升/下降按钮
        elevationButtons.setVisibility(View.VISIBLE);

        // 显示摇杆
        joystickContainer.setVisibility(View.VISIBLE);

        android.util.Log.i("MainActivity", "Auto-start completed, waiting for surface...");
    }
    
    // 手动登录设置（保留原有逻辑）
    private void setupManualLogin() {
        btnStartGame.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                String gameName = etGameName.getText().toString().trim();
                String serverIP = etServerIP.getText().toString().trim();
                String portStr = etPort.getText().toString().trim();

                if (gameName.isEmpty()) {
                    Toast.makeText(MainActivity.this, "请输入游戏名", Toast.LENGTH_SHORT).show();
                    return;
                }

                if (serverIP.isEmpty()) {
                    Toast.makeText(MainActivity.this, "请输入服务器IP", Toast.LENGTH_SHORT).show();
                    return;
                }

                int port = 25565;
                try {
                    port = Integer.parseInt(portStr);
                } catch (NumberFormatException e) {
                    Toast.makeText(MainActivity.this, "端口号无效", Toast.LENGTH_SHORT).show();
                    return;
                }

                // 保存连接信息
                saveConnectionInfo(gameName, serverIP, port);

                // 自动启动游戏
                boolean useVulkan = radioVulkan.isChecked();
                autoStartGame(gameName, serverIP, port, useVulkan);
            }
        });
    }

    private void updateJoystickPosition(float x, float y) {
        float maxDistance = 45; // 最大移动距离

        float dx = x - joystickCenterX;
        float dy = y - joystickCenterY;
        float distance = (float) Math.sqrt(dx * dx + dy * dy);

        // 限制在圆形范围内
        if (distance > maxDistance) {
            float ratio = maxDistance / distance;
            dx *= ratio;
            dy *= ratio;
        }

        // 更新摇杆 knob 的位置
        joystickKnob.setTranslationX(dx);
        joystickKnob.setTranslationY(dy);

        // 归一化输出 (-1 到 1)
        joystickDX = dx / maxDistance;
        joystickDY = dy / maxDistance;
        
        // 通知 C++ 层摇杆输入
        setJoystickInput(joystickDX, joystickDY);
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

    /**
     * 启用沉浸式模式（隐藏状态栏和导航栏）
     * 下拉时显示，5秒后自动隐藏
     */
    private void enableImmersiveMode() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ 使用 WindowInsetsController
            getWindow().getDecorView().post(new Runnable() {
                @Override
                public void run() {
                    WindowInsetsController controller = getWindow().getInsetsController();
                    if (controller != null) {
                        // 隐藏状态栏和导航栏
                        controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                        
                        // 设置行为：下拉时显示，松手后自动隐藏
                        controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                        );
                        
                        android.util.Log.i("MainActivity", "Immersive mode enabled (API 30+)");
                    }
                }
            });
        } else {
            // Android 10 及以下使用传统的 SYSTEM_UI_FLAG
            View decorView = getWindow().getDecorView();
            int uiOptions = View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                          | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                          | View.SYSTEM_UI_FLAG_FULLSCREEN
                          | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                          | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                          | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN;
            
            decorView.setSystemUiVisibility(uiOptions);
            
            // 监听系统 UI 可见性变化，确保始终隐藏
            decorView.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
                @Override
                public void onSystemUiVisibilityChange(int visibility) {
                    if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                        // 状态栏可见，重新隐藏
                        decorView.setSystemUiVisibility(uiOptions);
                    }
                }
            });
            
            android.util.Log.i("MainActivity", "Immersive mode enabled (API < 30)");
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // 当窗口重新获得焦点时，重新启用沉浸式模式
        if (hasFocus) {
            enableImmersiveMode();
        }
    }

    private native void initRenderer(android.view.Surface surface);
    private native void cleanupRenderer();
    private native void setAssetManager(AssetManager assetManager);
    private native void updateCameraAngle(float pitchDelta, float yawDelta);  // 相对角度变化
    private native void resizeRenderer(int width, int height);
    private native void setRendererType(boolean useVulkan);
    private native void setProtocolVersion(int version);  // 设置协议版本
    private native void addBlock(int x, int y, int z);
    private native void removeBlock(int x, int y, int z);
    private native boolean connectToServer(String address, int port, String username);
    private native void syncCameraToPlayer();  // 同步摄像机到玩家位置
    
    // 新的输入控制接口
    private native void setKeyState(int key, boolean pressed);  // 按键状态
    private native void setJoystickInput(float dx, float dy);   // 摇杆输入
    
    // 从 C++ 层获取玩家位置并更新 Java 层变量（仅用于调试）
    public void updateJavaCameraPosition(float x, float y, float z, float yaw, float pitch) {
        android.util.Log.i("MainActivity", "Camera synced from C++: (" + x + ", " + y + ", " + z + ")");
    }

    private void saveConnectionInfo(String gameName, String serverIP, int port) {
        // 保留用于后续使用
    }
    
    private void copyBlocksJsonFromAssets() {
        AssetManager assetManager = getAssets();
        java.io.File destFile = new java.io.File(getFilesDir(), "blocks.json");
        
        // 如果文件已存在，跳过复制
        if (destFile.exists()) {
            android.util.Log.i("MainActivity", "blocks.json already exists in files directory");
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
            
            android.util.Log.i("MainActivity", "blocks.json copied to: " + destFile.getAbsolutePath());
        } catch (Exception e) {
            android.util.Log.e("MainActivity", "Failed to copy blocks.json", e);
        }
    }
    
    private void saveProtocolVersion(int version) {
        // 调用 C++ 层设置协议版本
        setProtocolVersion(version);
        android.util.Log.i("MainActivity", "Protocol version saved: " + version);
    }

    public void onVulkanSurfaceCreated(android.view.Surface surface) {
        android.util.Log.i("MainActivity", "onVulkanSurfaceCreated: calling initRenderer");
        try {
            initRenderer(surface);
            android.util.Log.i("MainActivity", "initRenderer returned successfully");
        } catch (UnsatisfiedLinkError e) {
            android.util.Log.e("MainActivity", "UnsatisfiedLinkError in initRenderer", e);
        } catch (Exception e) {
            android.util.Log.e("MainActivity", "Exception in initRenderer", e);
        }
    }

    public void onSurfaceChanged(int width, int height) {
        android.util.Log.i("MainActivity", "onSurfaceChanged: calling resizeRenderer");
        try {
            resizeRenderer(width, height);
            android.util.Log.i("MainActivity", "resizeRenderer returned successfully");
        } catch (Exception e) {
            android.util.Log.e("MainActivity", "Exception in resizeRenderer", e);
        }
    }

    public void onVulkanSurfaceDestroyed() {
        android.util.Log.i("MainActivity", "onVulkanSurfaceDestroyed: calling cleanupRenderer");
        try {
            cleanupRenderer();
            android.util.Log.i("MainActivity", "cleanupRenderer returned successfully");
        } catch (Exception e) {
            android.util.Log.e("MainActivity", "Exception in cleanupRenderer", e);
        }
    }
}