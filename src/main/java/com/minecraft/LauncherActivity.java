package com.minecraft;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

public class LauncherActivity extends Activity {
    
    private static final String PREFS_NAME = "launcher_prefs";
    private static final String KEY_USERNAME = "username";
    private static final String KEY_SERVER_IP = "server_ip";
    private static final String KEY_PORT = "port";
    private static final String KEY_VERSION = "version";
    private static final String KEY_RENDERER = "renderer";
    private static final String KEY_LOGIN_TYPE = "login_type";
    
    // 登录类型
    private RadioGroup rgLoginType;
    private RadioButton rbOffline;
    private RadioButton rbPremium;
    
    // 用户名输入
    private EditText etUsername;
    
    // 服务器配置
    private EditText etServerIP;
    private EditText etPort;
    
    // 版本选择
    private Spinner spinnerVersion;
    
    // 渲染器选择
    private RadioGroup rgRenderer;
    private RadioButton rbOpenGL;
    private RadioButton rbVulkan;
    
    // 启动按钮
    private Button btnLaunch;
    
    // 版本号数组（保留主要版本，1.21.11后使用新命名规则 26.x）
    private static final String[] VERSIONS = {
        "1.8", "1.12",
        "1.13", "1.14", "1.15", "1.16", "1.17",
        "1.18", "1.19", "1.20", "1.21",
        "26.1", "26.2"
    };
    
    // 协议版本号映射
    private static final int[] PROTOCOL_VERSIONS = {
        47, 340,
        404, 498, 578, 735, 755,
        758, 759, 763, 767,  // 1.18 使用 758 (1.18.2)
        800, 850
    };
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);
        
        initViews();
        loadPreferences();
        setupListeners();
    }
    
    private void initViews() {
        // 登录类型
        rgLoginType = findViewById(R.id.rgLoginType);
        rbOffline = findViewById(R.id.rbOffline);
        rbPremium = findViewById(R.id.rbPremium);
        
        // 用户名
        etUsername = findViewById(R.id.etUsername);
        
        // 服务器配置
        etServerIP = findViewById(R.id.etServerIP);
        etPort = findViewById(R.id.etPort);
        
        // 版本选择
        spinnerVersion = findViewById(R.id.spinnerVersion);
        ArrayAdapter<String> versionAdapter = new ArrayAdapter<>(
            this, 
            android.R.layout.simple_spinner_item, 
            VERSIONS
        );
        versionAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinnerVersion.setAdapter(versionAdapter);
        
        // 渲染器选择
        rgRenderer = findViewById(R.id.rgRenderer);
        rbOpenGL = findViewById(R.id.rbOpenGL);
        rbVulkan = findViewById(R.id.rbVulkan);
        
        // 启动按钮
        btnLaunch = findViewById(R.id.btnLaunch);
    }
    
    private void loadPreferences() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        
        // 加载用户名
        String username = prefs.getString(KEY_USERNAME, "Player");
        etUsername.setText(username);
        
        // 加载服务器配置
        String serverIP = prefs.getString(KEY_SERVER_IP, "127.0.0.1");
        etServerIP.setText(serverIP);
        
        String port = prefs.getString(KEY_PORT, "25565");
        etPort.setText(port);
        
        // 加载版本选择
        String savedVersion = prefs.getString(KEY_VERSION, "1.18");
        for (int i = 0; i < VERSIONS.length; i++) {
            if (VERSIONS[i].equals(savedVersion)) {
                spinnerVersion.setSelection(i);
                break;
            }
        }
        
        // 加载渲染器选择
        String renderer = prefs.getString(KEY_RENDERER, "opengl");
        if ("vulkan".equals(renderer)) {
            rbVulkan.setChecked(true);
        } else {
            rbOpenGL.setChecked(true);
        }
        
        // 加载登录类型
        String loginType = prefs.getString(KEY_LOGIN_TYPE, "offline");
        if ("premium".equals(loginType)) {
            rbPremium.setChecked(true);
        } else {
            rbOffline.setChecked(true);
        }
    }
    
    private void savePreferences() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        SharedPreferences.Editor editor = prefs.edit();
        
        // 保存用户名
        editor.putString(KEY_USERNAME, etUsername.getText().toString().trim());
        
        // 保存服务器配置
        editor.putString(KEY_SERVER_IP, etServerIP.getText().toString().trim());
        editor.putString(KEY_PORT, etPort.getText().toString().trim());
        
        // 保存版本选择
        String selectedVersion = VERSIONS[spinnerVersion.getSelectedItemPosition()];
        editor.putString(KEY_VERSION, selectedVersion);
        
        // 保存渲染器选择
        String renderer = rbVulkan.isChecked() ? "vulkan" : "opengl";
        editor.putString(KEY_RENDERER, renderer);
        
        // 保存登录类型
        String loginType = rbPremium.isChecked() ? "premium" : "offline";
        editor.putString(KEY_LOGIN_TYPE, loginType);
        
        editor.apply();
    }
    
    private void setupListeners() {
        // 启动按钮点击事件
        btnLaunch.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                launchGame();
            }
        });
        
        // 正版登录提示（暂未实现）
        rbPremium.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (isChecked) {
                Toast.makeText(LauncherActivity.this, 
                    "正版登录功能暂未实现，将使用离线模式", 
                    Toast.LENGTH_LONG).show();
                rbOffline.setChecked(true);
            }
        });
        
        // Vulkan 渲染器提示（暂未实现）
        rbVulkan.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (isChecked) {
                Toast.makeText(LauncherActivity.this, 
                    "Vulkan 渲染器暂未实现，将使用 OpenGL ES", 
                    Toast.LENGTH_LONG).show();
                rbOpenGL.setChecked(true);
            }
        });
    }
    
    private void launchGame() {
        // 验证输入
        String username = etUsername.getText().toString().trim();
        if (username.isEmpty()) {
            Toast.makeText(this, "请输入用户名", Toast.LENGTH_SHORT).show();
            return;
        }
        
        String serverIP = etServerIP.getText().toString().trim();
        if (serverIP.isEmpty()) {
            Toast.makeText(this, "请输入服务器IP", Toast.LENGTH_SHORT).show();
            return;
        }
        
        String portStr = etPort.getText().toString().trim();
        int port;
        try {
            port = Integer.parseInt(portStr);
            if (port < 1 || port > 65535) {
                throw new NumberFormatException();
            }
        } catch (NumberFormatException e) {
            Toast.makeText(this, "端口号无效 (1-65535)", Toast.LENGTH_SHORT).show();
            return;
        }
        
        // 获取选择的版本和协议号
        int versionIndex = spinnerVersion.getSelectedItemPosition();
        String versionName = VERSIONS[versionIndex];
        int protocolVersion = PROTOCOL_VERSIONS[versionIndex];
        
        // 获取渲染器类型
        boolean useVulkan = rbVulkan.isChecked();
        
        // 保存配置
        savePreferences();
        
        // 显示启动信息
        String message = String.format(
            "启动游戏\n用户名: %s\n版本: %s (协议 %d)\n服务器: %s:%d\n渲染器: %s",
            username, versionName, protocolVersion, serverIP, port,
            useVulkan ? "Vulkan" : "OpenGL ES"
        );
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
        
        // 启动游戏 Activity
        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra("username", username);
        intent.putExtra("server_ip", serverIP);
        intent.putExtra("port", port);
        intent.putExtra("protocol_version", protocolVersion);
        intent.putExtra("use_vulkan", useVulkan);
        startActivity(intent);
        
        // 关闭启动器
        finish();
    }
}
