package com.calcite;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.database.Cursor;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import androidx.core.content.FileProvider;
import android.widget.ListView;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.Spinner;
import android.widget.Toast;

import com.calcite.account.Account;
import com.calcite.account.AccountListAdapter;
import com.calcite.ui.MioButton;
import com.calcite.ui.MioTextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public class LauncherActivity extends Activity {

    private static final String PREFS_NAME = "launcher_prefs";
    private static final String KEY_USERNAME = "username";
    private static final String KEY_VERSION = "version";
    private static final String KEY_RENDERER = "renderer";
    private static final String KEY_LOGIN_TYPE = "login_type";
    private static final String KEY_ACCOUNTS = "accounts";

    private String username = "Player";
    private String loginType = "offline";
    private int versionIndex = 0;
    private boolean useVulkan = false;

    private List<Account> accounts = new ArrayList<>();
    private AccountListAdapter accountAdapter;

    private View pageHome;
    private View pageAccount;
    private View pageVersion;
    private View pageRenderer;
    private View pageSettings;
    private View currentPage;

    // 右侧启动面板
    private MioTextView panelDisplayName;
    private MioTextView panelLoginType;
    private MioTextView panelVersion;
    private MioTextView panelRenderer;

    private static final int REQUEST_OPEN_DIR = 1001;
    private static final int REQUEST_PICK_FILE = 1002;

    private static final String[] VERSIONS = {
        "1.8", "1.12",
        "1.13", "1.14", "1.15", "1.16", "1.17",
        "1.18", "1.19", "1.20", "1.21",
        "26.1", "26.2"
    };
    private static final int[] PROTOCOL_VERSIONS = {
        47, 340,
        404, 498, 578, 735, 755,
        758, 759, 763, 767,
        800, 850
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);

        enableImmersiveMode();
        loadPreferences();
        loadAccounts();
        initViews();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
            );
        }
    }

    private void enableImmersiveMode() {
        getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(
            visibility -> {
                if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                    getWindow().getDecorView().setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                    );
                }
            }
        );
    }

    private void initViews() {
        MioButton btnHome = findViewById(R.id.btnHome);
        MioButton btnAccount = findViewById(R.id.btnAccount);
        MioButton btnVersion = findViewById(R.id.btnVersion);
        MioButton btnRenderer = findViewById(R.id.btnRenderer);
        MioButton btnSettings = findViewById(R.id.btnSettings);

        pageHome = findViewById(R.id.pageHome);
        pageAccount = findViewById(R.id.pageAccount);
        pageVersion = findViewById(R.id.pageVersion);
        pageRenderer = findViewById(R.id.pageRenderer);
        pageSettings = findViewById(R.id.pageSettings);
        currentPage = pageHome;

        // 右侧启动面板
        panelDisplayName = findViewById(R.id.panelDisplayName);
        panelLoginType = findViewById(R.id.panelLoginType);
        panelVersion = findViewById(R.id.panelVersion);
        panelRenderer = findViewById(R.id.panelRenderer);
        findViewById(R.id.btnLaunch).setOnClickListener(v -> launchGame());

        // 各设置页
        fillAccountPage();
        fillVersionPage();
        fillRendererPage();
        fillSettingsPage();

        // 左侧按钮
        btnHome.setOnClickListener(v -> goHome());
        btnAccount.setOnClickListener(v -> switchPage(pageAccount));
        btnVersion.setOnClickListener(v -> switchPage(pageVersion));
        btnRenderer.setOnClickListener(v -> switchPage(pageRenderer));
        btnSettings.setOnClickListener(v -> switchPage(pageSettings));

        updatePanelDisplay();
    }

    private void switchPage(View target) {
        if (currentPage == target) return;
        currentPage.setVisibility(View.GONE);
        target.setVisibility(View.VISIBLE);
        currentPage = target;
    }

    private void goHome() {
        updatePanelDisplay();
        if (currentPage != pageHome) {
            currentPage.setVisibility(View.GONE);
            pageHome.setVisibility(View.VISIBLE);
            currentPage = pageHome;
        }
    }

    private void updatePanelDisplay() {
        Account selected = accountAdapter != null ? accountAdapter.getSelectedAccount() : null;
        if (selected != null) {
            panelDisplayName.setText(selected.getName());
            panelLoginType.setText(selected.getTypeDisplay());
            username = selected.getName();
            loginType = selected.getType();
        } else {
            panelDisplayName.setText("未设置");
            panelLoginType.setText("暂无账号");
        }
        panelVersion.setText(VERSIONS[versionIndex]);
        panelRenderer.setText(useVulkan ? "Vulkan" : "OpenGL");
    }

    // ===== 账号管理 =====

    private void fillAccountPage() {
        pageAccount.findViewById(R.id.btnCreateOffline).setOnClickListener(v -> showCreateOfflineDialog());
        pageAccount.findViewById(R.id.btnCreatePremium).setOnClickListener(v -> {
            Toast.makeText(this, "正版登录功能暂未实现", Toast.LENGTH_SHORT).show();
        });

        ListView listView = pageAccount.findViewById(R.id.accountList);
        accountAdapter = new AccountListAdapter(this, accounts);
        accountAdapter.setOnAccountDeleteListener(position -> {
            accounts.remove(position);
            saveAccounts();
            accountAdapter.setSelectedPosition(accountAdapter.getSelectedPosition());
            accountAdapter.notifyDataSetChanged();
            updatePanelDisplay();
        });
        listView.setAdapter(accountAdapter);
    }

    private void showCreateOfflineDialog() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        View view = getLayoutInflater().inflate(R.layout.dialog_create_offline, null);
        builder.setView(view);

        AlertDialog dialog = builder.create();
        if (dialog.getWindow() != null) {
            dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        }
        dialog.show();

        EditText etUsername = view.findViewById(R.id.etOfflineUsername);
        view.findViewById(R.id.btnConfirmOffline).setOnClickListener(v -> {
            String name = etUsername.getText().toString().trim();
            if (name.isEmpty()) {
                Toast.makeText(this, "用户名不能为空", Toast.LENGTH_SHORT).show();
                return;
            }
            String uuid = UUID.nameUUIDFromBytes(("OfflinePlayer:" + name).getBytes()).toString();
            accounts.add(new Account(name, "offline", uuid));
            saveAccounts();
            accountAdapter.notifyDataSetChanged();
            accountAdapter.setSelectedPosition(accounts.size() - 1);
            username = name;
            loginType = "offline";
            savePreferences();
            updatePanelDisplay();
            Toast.makeText(this, "账号已创建", Toast.LENGTH_SHORT).show();
            dialog.dismiss();
        });

        view.findViewById(R.id.btnCancelOffline).setOnClickListener(v -> dialog.dismiss());
    }

    private void loadAccounts() {
        accounts.clear();
        String json = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getString(KEY_ACCOUNTS, "");
        if (!json.isEmpty()) {
            try {
                JSONArray array = new JSONArray(json);
                for (int i = 0; i < array.length(); i++) {
                    JSONObject obj = array.getJSONObject(i);
                    accounts.add(new Account(
                        obj.getString("name"),
                        obj.getString("type"),
                        obj.getString("uuid")
                    ));
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
        if (accounts.isEmpty()) {
            String savedName = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getString(KEY_USERNAME, "Player");
            String uuid = UUID.nameUUIDFromBytes(("OfflinePlayer:" + savedName).getBytes()).toString();
            accounts.add(new Account(savedName, "offline", uuid));
            saveAccounts();
        }
    }

    private void saveAccounts() {
        try {
            JSONArray array = new JSONArray();
            for (Account acc : accounts) {
                JSONObject obj = new JSONObject();
                obj.put("name", acc.getName());
                obj.put("type", acc.getType());
                obj.put("uuid", acc.getUuid());
                array.put(obj);
            }
            getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                .edit()
                .putString(KEY_ACCOUNTS, array.toString())
                .apply();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    // ===== 版本设置 =====

    private void fillVersionPage() {
        Spinner spinnerVersion = pageVersion.findViewById(R.id.spinnerVersion);
        MioButton btnSave = pageVersion.findViewById(R.id.btnSaveVersion);

        ArrayAdapter<String> versionAdapter = new ArrayAdapter<>(this,
            android.R.layout.simple_spinner_item, VERSIONS);
        versionAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinnerVersion.setAdapter(versionAdapter);
        spinnerVersion.setSelection(versionIndex);

        btnSave.setOnClickListener(v -> {
            versionIndex = spinnerVersion.getSelectedItemPosition();
            savePreferences();
            updatePanelDisplay();
            goHome();
        });
    }

    // ===== 渲染器设置 =====

    private void fillRendererPage() {
        RadioGroup rgRenderer = pageRenderer.findViewById(R.id.rgRenderer);
        RadioButton rbOpenGL = pageRenderer.findViewById(R.id.rbOpenGL);
        RadioButton rbVulkan = pageRenderer.findViewById(R.id.rbVulkan);
        MioButton btnSave = pageRenderer.findViewById(R.id.btnSaveRenderer);

        rgRenderer.check(useVulkan ? R.id.rbVulkan : R.id.rbOpenGL);

        rbVulkan.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (isChecked) {
                Toast.makeText(this, "Vulkan 渲染器暂未实现，将使用 OpenGL ES",
                    Toast.LENGTH_LONG).show();
                rbOpenGL.setChecked(true);
            }
        });

        btnSave.setOnClickListener(v -> {
            useVulkan = rbVulkan.isChecked();
            savePreferences();
            updatePanelDisplay();
            goHome();
        });
    }

    // ===== 设置页：资源包管理 =====

    private void fillSettingsPage() {
        pageSettings.findViewById(R.id.btnOpenResourcepacks).setOnClickListener(v -> openResourcepackDir());
        pageSettings.findViewById(R.id.btnAddResourcepack).setOnClickListener(v -> pickResourcepackFile());
    }

    private File getResourcepacksDir() {
        return getExternalFilesDir(null);
    }

    private void openResourcepackDir() {
        File dir = getExternalFilesDir(null);
        Uri uri = FileProvider.getUriForFile(this, "com.calcite.fileprovider", dir);

        Intent intent = new Intent(Intent.ACTION_SEND);
        intent.setDataAndType(uri, "*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);

        startActivity(Intent.createChooser(intent, "选择文件管理器"));
    }

    private void pickResourcepackFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        // 优先过滤 zip 文件
        intent.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{
                "application/zip",
                "application/x-zip-compressed"
        });
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_PICK_FILE);
    }

    private void copyResourcepack(Uri uri) {
        try {
            File destDir = getResourcepacksDir();
            // 通过 ContentResolver 查询真实文件名
            String fileName = "resourcepack.zip";
            try (Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (nameIndex >= 0) {
                        String name = cursor.getString(nameIndex);
                        if (name != null && !name.isEmpty()) fileName = name;
                    }
                }
            }

            File destFile = new File(destDir, fileName);
            int counter = 1;
            while (destFile.exists()) {
                String base = fileName.contains(".") ? fileName.substring(0, fileName.lastIndexOf('.')) : fileName;
                String ext = fileName.contains(".") ? fileName.substring(fileName.lastIndexOf('.')) : "";
                destFile = new File(destDir, base + "_" + counter + ext);
                counter++;
            }

            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(destFile)) {
                if (in == null) {
                    Toast.makeText(this, "无法读取文件", Toast.LENGTH_SHORT).show();
                    return;
                }
                byte[] buf = new byte[8192];
                int len;
                while ((len = in.read(buf)) > 0) {
                    out.write(buf, 0, len);
                }
            }

            Toast.makeText(this, "资源包已添加: " + destFile.getName(), Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            e.printStackTrace();
            Toast.makeText(this, "添加资源包失败: " + e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null) return;

        if (requestCode == REQUEST_PICK_FILE) {
            Uri uri = data.getData();
            if (uri != null) {
                copyResourcepack(uri);
            }
        } else if (requestCode == REQUEST_OPEN_DIR) {
            // 文件管理器已打开，无需处理
        }
    }

    // ===== 持久化 =====

    private void loadPreferences() {
        SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        username = prefs.getString(KEY_USERNAME, "Player");
        loginType = prefs.getString(KEY_LOGIN_TYPE, "offline");
        useVulkan = "vulkan".equals(prefs.getString(KEY_RENDERER, "opengl"));

        String savedVersion = prefs.getString(KEY_VERSION, "1.18");
        for (int i = 0; i < VERSIONS.length; i++) {
            if (VERSIONS[i].equals(savedVersion)) {
                versionIndex = i;
            }
        }
    }

    private void savePreferences() {
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            .edit()
            .putString(KEY_USERNAME, username)
            .putString(KEY_VERSION, VERSIONS[versionIndex])
            .putString(KEY_RENDERER, useVulkan ? "vulkan" : "opengl")
            .putString(KEY_LOGIN_TYPE, loginType)
            .apply();
    }

    // ===== 启动游戏 =====

    private void launchGame() {
        Account selected = accountAdapter != null ? accountAdapter.getSelectedAccount() : null;
        if (selected == null) {
            Toast.makeText(this, "请先创建账号", Toast.LENGTH_SHORT).show();
            return;
        }

        String versionName = VERSIONS[versionIndex];
        int protocolVersion = PROTOCOL_VERSIONS[versionIndex];

        Toast.makeText(this,
            String.format("启动游戏\n用户名: %s\n版本: %s (协议 %d)\n渲染器: %s",
                selected.getName(), versionName, protocolVersion,
                useVulkan ? "Vulkan" : "OpenGL ES"),
            Toast.LENGTH_LONG).show();

        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra("username", selected.getName());
        intent.putExtra("protocol_version", protocolVersion);
        intent.putExtra("use_vulkan", useVulkan);
        intent.setFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivity(intent);
    }
}
