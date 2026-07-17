package com.calcite;

import static com.calcite.auth.CalciteApiService.BASE_URL;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.provider.OpenableColumns;
import android.util.Log;
import android.database.Cursor;
import android.view.View;
import android.view.WindowManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import androidx.core.content.FileProvider;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import com.calcite.account.Account;
import com.calcite.account.AccountListAdapter;
import com.calcite.auth.AuthResult;
import com.calcite.auth.CalciteApiService;
import com.calcite.auth.MicrosoftAuthService;
import com.calcite.ui.MioButton;
import com.calcite.ui.MioTextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

public class LauncherActivity extends Activity {

    private static final String PREFS_NAME = "launcher_prefs";
    private static final String KEY_USERNAME = "username";
    private static final String KEY_VERSION = "version";
    private static final String KEY_RENDERER = "renderer";
    private static final String KEY_LOGIN_TYPE = "login_type";
    private static final String KEY_SELECTED_ACCOUNT = "selected_account";
    private static final String KEY_ACCOUNTS = "accounts";
    private static final String KEY_CALCITE_ACCOUNT = "calcite_account";

    private String username = "Player";
    private String loginType = "offline";
    private int versionIndex = 0;
    private boolean useVulkan = false;

    private List<Account> accounts = new ArrayList<>();
    private AccountListAdapter accountAdapter;
    private String selectedAccountUuid = null;

    /** 启动器账号（独立于 MC 账号列表） */
    private Account calciteAccount = null;

    private View pageHome;
    private View pageMy;
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
    private static final int REQUEST_LAUNCH_GAME = 1003;

    private Handler heartbeatHandler = new Handler();
    private Runnable heartbeatTask = null;

    private static final String[] VERSIONS = {
        "1.8", "1.12",
        "1.13", "1.14", "1.15", "1.16", "1.17",
        "1.18.2", "1.19.4", "1.20", "1.21",
        "26.1", "26.2"
    };
    private static final int[] PROTOCOL_VERSIONS = {
        47, 340,
        404, 498, 578, 735, 755,
        758,  762, 763, 767,
        800, 850
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // 允许内容延伸到凹槽区域（须在 setContentView 前调用）
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
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
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
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
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    );
                }
            }
        );
    }

    private void initViews() {
        MioButton btnHome = findViewById(R.id.btnHome);
        MioButton btnMy = findViewById(R.id.btnMy);
        MioButton btnAccount = findViewById(R.id.btnAccount);
        MioButton btnVersion = findViewById(R.id.btnVersion);
        MioButton btnRenderer = findViewById(R.id.btnRenderer);
        MioButton btnSettings = findViewById(R.id.btnSettings);

        pageHome = findViewById(R.id.pageHome);
        pageMy = findViewById(R.id.pageMy);
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
        fillMyPage();
        fillAccountPage();
        fillVersionPage();
        fillRendererPage();
        fillSettingsPage();

        // 左侧按钮
        btnHome.setOnClickListener(v -> goHome());
        btnMy.setOnClickListener(v -> switchPage(pageMy));
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
        if (target == pageMy) {
            updateCalciteStatus();
        }
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

    // ===== 我的页面 =====

    private void fillMyPage() {
        pageMy.findViewById(R.id.btnRegisterCalcite).setOnClickListener(v -> showRegisterDialog());
        pageMy.findViewById(R.id.btnLoginCalcite).setOnClickListener(v -> showLoginDialog());
        pageMy.findViewById(R.id.btnLogoutCalcite).setOnClickListener(v -> logoutCalcite());
        updateCalciteStatus();
    }

    private void updateCalciteStatus() {
        MioTextView tvStatus = pageMy.findViewById(R.id.tvCalciteStatus);
        View buttonRow = pageMy.findViewById(R.id.layoutCalciteButtons);
        View layoutPlaytime = pageMy.findViewById(R.id.layoutPlaytime);
        View btnLogout = pageMy.findViewById(R.id.btnLogoutCalcite);
        if (calciteAccount != null) {
            tvStatus.setText("已登录: " + calciteAccount.getName() + " (" + calciteAccount.getEmail() + ")");
            buttonRow.setVisibility(View.GONE);
            layoutPlaytime.setVisibility(View.VISIBLE);
            btnLogout.setVisibility(View.VISIBLE);
            queryPlaytime();
        } else {
            tvStatus.setText("未登录 Calcite 账号");
            buttonRow.setVisibility(View.VISIBLE);
            layoutPlaytime.setVisibility(View.GONE);
            btnLogout.setVisibility(View.GONE);
        }
    }

    private void logoutCalcite() {
        calciteAccount = null;
        saveCalciteAccount();
        updateCalciteStatus();
        Toast.makeText(this, "已退出登录", Toast.LENGTH_SHORT).show();
    }

    private void queryPlaytime() {
        if (calciteAccount == null || calciteAccount.getCalciteToken() == null) return;

        TextView tvPlaytime = pageMy.findViewById(R.id.tvPlaytime);
        tvPlaytime.setText("加载中...");

        if (calciteApi == null) calciteApi = new CalciteApiService();
        calciteApi.fetchPlaytime(calciteAccount.getCalciteToken(), new CalciteApiService.Callback<Integer>() {
            @Override
            public void onSuccess(Integer totalSeconds) {
                runOnUiThread(() -> {
                    int hours = totalSeconds / 3600;
                    int minutes = (totalSeconds % 3600) / 60;
                    int seconds = totalSeconds % 60;
                    String text;
                    if (hours > 0) {
                        text = String.format("%.1f小时", hours + minutes / 60.0);
                    } else {
                        text = minutes + "分钟";
                    }
                    tvPlaytime.setText(text);
                });
            }

            @Override
            public void onError(String message) {
                runOnUiThread(() -> {
                    tvPlaytime.setText("查询失败");
                    Toast.makeText(LauncherActivity.this, message, Toast.LENGTH_SHORT).show();
                });
            }
        });
    }

    // ===== 账号管理 =====

    private void fillAccountPage() {
        pageAccount.findViewById(R.id.btnCreateOffline).setOnClickListener(v -> showCreateOfflineDialog());
        pageAccount.findViewById(R.id.btnCreatePremium).setOnClickListener(v -> showMicrosoftLoginDialog());

        ListView listView = pageAccount.findViewById(R.id.accountList);
        accountAdapter = new AccountListAdapter(this, accounts);
        // 恢复之前选中的账号
        if (selectedAccountUuid != null) {
            for (int i = 0; i < accounts.size(); i++) {
                if (accounts.get(i).getUuid().equals(selectedAccountUuid)) {
                    accountAdapter.setSelectedPosition(i);
                    break;
                }
            }
        }
        // 选中变化时保存
        accountAdapter.setOnAccountSelectedListener((pos, acc) -> {
            getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                .edit()
                .putString(KEY_SELECTED_ACCOUNT, acc.getUuid())
                .apply();
        });
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
            getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                .edit()
                .putString(KEY_SELECTED_ACCOUNT, uuid)
                .apply();
            username = name;
            loginType = "offline";
            savePreferences();
            updatePanelDisplay();
            Toast.makeText(this, "账号已创建", Toast.LENGTH_SHORT).show();
            dialog.dismiss();
        });

        view.findViewById(R.id.btnCancelOffline).setOnClickListener(v -> dialog.dismiss());
    }

    // ===== Microsoft 正版登录 =====

    private MicrosoftAuthService authService;
    private CalciteApiService calciteApi = null;
    private Thread authThread;
    private volatile boolean authCancelled = false;

    private void showMicrosoftLoginDialog() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        View view = getLayoutInflater().inflate(R.layout.dialog_microsoft_login, null);
        builder.setView(view);
        builder.setCancelable(false);

        AlertDialog dialog = builder.create();
        if (dialog.getWindow() != null) {
            dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        }
        dialog.show();

        TextView tvStatus = view.findViewById(R.id.tvAuthStatus);
        View layoutDeviceCode = view.findViewById(R.id.layoutDeviceCode);
        TextView tvVerificationUrl = view.findViewById(R.id.tvVerificationUrl);
        TextView tvUserCode = view.findViewById(R.id.tvUserCode);
        Button btnCopyCode = view.findViewById(R.id.btnCopyCode);
        ProgressBar progressAuth = view.findViewById(R.id.progressAuth);
        Button btnCancelAuth = view.findViewById(R.id.btnCancelAuth);

        authCancelled = false;
        if (authService == null) {
            authService = new MicrosoftAuthService();
        }

        btnCancelAuth.setOnClickListener(v -> {
            authCancelled = true;
            dialog.dismiss();
        });

        // 复制代码并打开浏览器
        btnCopyCode.setOnClickListener(v -> {
            String code = tvUserCode.getText().toString();
            String url = tvVerificationUrl.getText().toString();
            // 复制到剪贴板
            ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            clipboard.setPrimaryClip(ClipData.newPlainText("Microsoft Login Code", code));
            // 打开浏览器
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
            } catch (Exception e) {
                Toast.makeText(this, "无法打开浏览器，请手动访问: " + url, Toast.LENGTH_LONG).show();
            }
            Toast.makeText(this, "代码已复制到剪贴板", Toast.LENGTH_SHORT).show();
        });

        // 在后台线程执行认证
        authThread = new Thread(() -> {
            authService.authenticate(new MicrosoftAuthService.AuthCallback() {
                @Override
                public void onDeviceCodeReceived(String userCode, String verificationURI) {
                    runOnUiThread(() -> {
                        if (authCancelled) return;
                        tvUserCode.setText(userCode);
                        tvVerificationUrl.setText(verificationURI);
                        layoutDeviceCode.setVisibility(View.VISIBLE);
                        tvStatus.setText("请在浏览器中输入代码完成登录");
                    });
                }

                @Override
                public void onProgress(String message) {
                    runOnUiThread(() -> {
                        if (authCancelled) return;
                        tvStatus.setText(message);
                    });
                }

                @Override
                public void onSuccess(AuthResult result) {
                    runOnUiThread(() -> {
                        if (authCancelled) return;
                        // 创建正版账号
                        Account account = Account.fromAuthResult(result);
                        accounts.add(account);
                        saveAccounts();
                        accountAdapter.notifyDataSetChanged();
                        accountAdapter.setSelectedPosition(accounts.size() - 1);
                        getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                            .edit()
                            .putString(KEY_SELECTED_ACCOUNT, account.getUuid())
                            .apply();
                        username = account.getName();
                        loginType = "premium";
                        savePreferences();
                        updatePanelDisplay();
                        Toast.makeText(LauncherActivity.this,
                            "正版登录成功: " + result.getPlayerName(),
                            Toast.LENGTH_SHORT).show();
                        dialog.dismiss();
                    });
                }

                @Override
                public void onError(String message) {
                    runOnUiThread(() -> {
                        if (authCancelled) return;
                        tvStatus.setText("登录失败");
                        layoutDeviceCode.setVisibility(View.GONE);
                        progressAuth.setVisibility(View.GONE);
                        Toast.makeText(LauncherActivity.this, message, Toast.LENGTH_LONG).show();
                        // 延迟关闭对话框
                        tvStatus.postDelayed(() -> dialog.dismiss(), 1500);
                    });
                }
            });
        });
        authThread.setDaemon(true);
        authThread.start();
    }

    // ===== Calcite 账号注册/登录 =====

    private void showRegisterDialog() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        View view = getLayoutInflater().inflate(R.layout.dialog_register_calcite, null);
        builder.setView(view);

        AlertDialog dialog = builder.create();
        if (dialog.getWindow() != null) {
            dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        }
        dialog.show();

        EditText etUsername = view.findViewById(R.id.etRegUsername);
        EditText etEmail = view.findViewById(R.id.etRegEmail);
        EditText etPassword = view.findViewById(R.id.etRegPassword);
        ImageView ivCaptcha = view.findViewById(R.id.ivCaptcha);
        EditText etCaptcha = view.findViewById(R.id.etCaptchaCode);
        View btnRefresh = view.findViewById(R.id.btnRefreshCaptcha);
        View btnConfirm = view.findViewById(R.id.btnConfirmReg);
        View btnCancel = view.findViewById(R.id.btnCancelReg);

        if (calciteApi == null) calciteApi = new CalciteApiService();

        // 持有当前 captchaId
        final String[] currentCaptchaId = {null};

        // 加载验证码
        Runnable loadCaptcha = () -> calciteApi.fetchCaptcha(new CalciteApiService.Callback<CalciteApiService.CaptchaResult>() {
            @Override
            public void onSuccess(CalciteApiService.CaptchaResult result) {
                runOnUiThread(() -> {
                    currentCaptchaId[0] = result.captchaId;
                    ivCaptcha.setImageBitmap(result.image);
                });
            }

            @Override
            public void onError(String message) {
                runOnUiThread(() -> Toast.makeText(LauncherActivity.this, message, Toast.LENGTH_SHORT).show());
            }
        });
        loadCaptcha.run();

        btnRefresh.setOnClickListener(v -> loadCaptcha.run());

        btnConfirm.setOnClickListener(v -> {
            String name = etUsername.getText().toString().trim();
            String email = etEmail.getText().toString().trim();
            String password = etPassword.getText().toString().trim();
            String captchaCode = etCaptcha.getText().toString().trim();

            if (name.isEmpty() || email.isEmpty() || password.isEmpty() || captchaCode.isEmpty()) {
                Toast.makeText(this, "请填写所有字段", Toast.LENGTH_SHORT).show();
                return;
            }
            if (currentCaptchaId[0] == null) {
                Toast.makeText(this, "验证码未加载，请刷新", Toast.LENGTH_SHORT).show();
                return;
            }

            btnConfirm.setEnabled(false);
            calciteApi.register(name, email, password, currentCaptchaId[0], captchaCode,
                new CalciteApiService.Callback<CalciteApiService.AuthResult>() {
                    @Override
                    public void onSuccess(CalciteApiService.AuthResult result) {
                        runOnUiThread(() -> {
                            String uuid = UUID.nameUUIDFromBytes(("Calcite:" + result.username).getBytes()).toString();
                            calciteAccount = new Account(result.username, "calcite", uuid);
                            calciteAccount.setCalciteToken(result.token);
                            calciteAccount.setEmail(result.email);
                            saveCalciteAccount();
                            updateCalciteStatus();
                            Toast.makeText(LauncherActivity.this, "注册成功: " + result.username, Toast.LENGTH_SHORT).show();
                            dialog.dismiss();
                        });
                    }

                    @Override
                    public void onError(String message) {
                        runOnUiThread(() -> {
                            btnConfirm.setEnabled(true);
                            Toast.makeText(LauncherActivity.this, "注册失败: " + message, Toast.LENGTH_LONG).show();
                        });
                    }
                });
        });

        btnCancel.setOnClickListener(v -> dialog.dismiss());
    }

    private void showLoginDialog() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        View view = getLayoutInflater().inflate(R.layout.dialog_login_calcite, null);
        builder.setView(view);

        AlertDialog dialog = builder.create();
        if (dialog.getWindow() != null) {
            dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        }
        dialog.show();

        EditText etUsername = view.findViewById(R.id.etLoginUsername);
        EditText etPassword = view.findViewById(R.id.etLoginPassword);
        View btnConfirm = view.findViewById(R.id.btnConfirmLogin);
        View btnCancel = view.findViewById(R.id.btnCancelLogin);

        if (calciteApi == null) calciteApi = new CalciteApiService();

        btnConfirm.setOnClickListener(v -> {
            String name = etUsername.getText().toString().trim();
            String password = etPassword.getText().toString().trim();

            if (name.isEmpty() || password.isEmpty()) {
                Toast.makeText(this, "请填写用户名和密码", Toast.LENGTH_SHORT).show();
                return;
            }

            btnConfirm.setEnabled(false);
            calciteApi.login(name, password, new CalciteApiService.Callback<CalciteApiService.AuthResult>() {
                @Override
                public void onSuccess(CalciteApiService.AuthResult result) {
                    runOnUiThread(() -> {
                        String uuid = UUID.nameUUIDFromBytes(("Calcite:" + result.username).getBytes()).toString();
                        calciteAccount = new Account(result.username, "calcite", uuid);
                        calciteAccount.setCalciteToken(result.token);
                        calciteAccount.setEmail(result.email);
                        saveCalciteAccount();
                        updateCalciteStatus();
                        Toast.makeText(LauncherActivity.this, "登录成功: " + result.username, Toast.LENGTH_SHORT).show();
                        dialog.dismiss();
                    });
                }

                @Override
                public void onError(String message) {
                    runOnUiThread(() -> {
                        btnConfirm.setEnabled(true);
                        Toast.makeText(LauncherActivity.this, "登录失败: " + message, Toast.LENGTH_LONG).show();
                    });
                }
            });
        });

        btnCancel.setOnClickListener(v -> dialog.dismiss());
    }

    /**
     * 刷新正版账号令牌（在启动游戏前调用）
     */
    private void refreshPremiumAccount(Account account, Runnable onSuccess, Runnable onError) {
        if (!account.isPremium() || account.getRefreshToken() == null) {
            onError.run();
            return;
        }

        if (!account.isTokenExpired()) {
            onSuccess.run();
            return;
        }

        if (authService == null) {
            authService = new MicrosoftAuthService();
        }

        new Thread(() -> {
            authService.refresh(account.getRefreshToken(), new MicrosoftAuthService.AuthCallback() {
                @Override
                public void onDeviceCodeReceived(String userCode, String verificationURI) {}
                @Override
                public void onProgress(String message) {}

                @Override
                public void onSuccess(AuthResult result) {
                    runOnUiThread(() -> {
                        account.updateAuthResult(result);
                        saveAccounts();
                        onSuccess.run();
                    });
                }

                @Override
                public void onError(String message) {
                    runOnUiThread(() -> {
                        Toast.makeText(LauncherActivity.this,
                            "刷新令牌失败: " + message, Toast.LENGTH_LONG).show();
                        onError.run();
                    });
                }
            });
        }).start();
    }

    private void loadAccounts() {
        accounts.clear();
        String json = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getString(KEY_ACCOUNTS, "");
        if (!json.isEmpty()) {
            try {
                JSONArray array = new JSONArray(json);
                for (int i = 0; i < array.length(); i++) {
                    JSONObject obj = array.getJSONObject(i);
                    Account acc = new Account(
                        obj.getString("name"),
                        obj.getString("type"),
                        obj.getString("uuid")
                    );
                    // calcite 账号由 loadCalciteAccount 单独加载
                    if (acc.isCalcite()) continue;
                    // 恢复正版账号字段
                    if (acc.isPremium()) {
                        acc.setAccessToken(obj.optString("accessToken", ""));
                        acc.setRefreshToken(obj.optString("refreshToken", ""));
                        acc.setTokenType(obj.optString("tokenType", "Bearer"));
                        acc.setNotAfter(obj.optLong("notAfter", 0));
                    }
                    accounts.add(acc);
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
        // 恢复之前选中的账号
        selectedAccountUuid = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getString(KEY_SELECTED_ACCOUNT, null);
        // 恢复 Calcite 启动器账号
        loadCalciteAccount();
    }

    private void saveAccounts() {
        try {
            JSONArray array = new JSONArray();
            for (Account acc : accounts) {
                JSONObject obj = new JSONObject();
                obj.put("name", acc.getName());
                obj.put("type", acc.getType());
                obj.put("uuid", acc.getUuid());
                // 保存正版账号字段
                if (acc.isPremium()) {
                    obj.put("accessToken", acc.getAccessToken() != null ? acc.getAccessToken() : "");
                    obj.put("refreshToken", acc.getRefreshToken() != null ? acc.getRefreshToken() : "");
                    obj.put("tokenType", acc.getTokenType() != null ? acc.getTokenType() : "Bearer");
                    obj.put("notAfter", acc.getNotAfter());
                }
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

    /** 独立保存 Calcite 启动器账号 */
    private void saveCalciteAccount() {
        try {
            if (calciteAccount != null) {
                JSONObject obj = new JSONObject();
                obj.put("name", calciteAccount.getName());
                obj.put("uuid", calciteAccount.getUuid());
                obj.put("calciteToken", calciteAccount.getCalciteToken() != null ? calciteAccount.getCalciteToken() : "");
                obj.put("email", calciteAccount.getEmail() != null ? calciteAccount.getEmail() : "");
                getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                    .edit()
                    .putString(KEY_CALCITE_ACCOUNT, obj.toString())
                    .apply();
            } else {
                getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                    .edit()
                    .remove(KEY_CALCITE_ACCOUNT)
                    .apply();
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    /** 独立加载 Calcite 启动器账号 */
    private void loadCalciteAccount() {
        String json = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getString(KEY_CALCITE_ACCOUNT, "");
        if (!json.isEmpty()) {
            try {
                JSONObject obj = new JSONObject(json);
                calciteAccount = new Account(
                    obj.getString("name"), "calcite", obj.getString("uuid")
                );
                calciteAccount.setCalciteToken(obj.optString("calciteToken", ""));
                calciteAccount.setEmail(obj.optString("email", ""));
            } catch (Exception e) {
                e.printStackTrace();
                calciteAccount = null;
            }
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
        if (requestCode == REQUEST_LAUNCH_GAME) {
            // 游戏退出，停止心跳
            stopHeartbeat();
            return;
        }
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

    private boolean isProtocolSupported(int protocolVersion) {
        try {
            System.loadLibrary("mc_" + protocolVersion);
            return true;
        } catch (UnsatisfiedLinkError e) {
            return false;
        }
    }

    private void launchGame() {
        Account selected = accountAdapter != null ? accountAdapter.getSelectedAccount() : null;
        if (selected == null) {
            Toast.makeText(this, "请先创建账号", Toast.LENGTH_SHORT).show();
            return;
        }

        String versionName = VERSIONS[versionIndex];
        int protocolVersion = PROTOCOL_VERSIONS[versionIndex];

        if (!isProtocolSupported(protocolVersion)) {
            new AlertDialog.Builder(this)
                .setTitle("协议版本不可用")
                .setMessage("协议版本 " + protocolVersion + "（" + versionName + "）的库尚未编译\n"
                    + "请在构建配置的 SUPPORTED_VERSIONS 中添加该版本号后重新编译")
                .setCancelable(false)
                .setPositiveButton("确定", null)
                .show();
            return;
        }

        // 先校验音效文件完整性，通过后再走启动流程
        checkSoundsAndLaunch(selected, versionName, protocolVersion);
    }

    /**
     * 校验音效文件是否已下载完整，不完整则先下载再启动
     */
    private void checkSoundsAndLaunch(Account selected, String versionName, int protocolVersion) {
        String soundsDir = Environment.getExternalStorageDirectory().getAbsolutePath()
            + "/Android/data/com.calcite/files/sounds";

        // 快速检查关键文件是否存在
        if (new File(soundsDir + "/music/menu/menu1.ogg").exists()) {
            doLaunch(selected, versionName, protocolVersion);
            return;
        }

        // 缺失音效：让用户选择下载或直接进入
        new AlertDialog.Builder(this)
            .setTitle("音效资源未下载")
            .setMessage("未检测到音效文件，是否下载？\n（约需下载 100+ 个文件，建议连接 Wi-Fi）")
            .setPositiveButton("下载音效", (d, w) ->
                downloadSoundsBlocking(selected, versionName, protocolVersion, soundsDir))
            .setNegativeButton("直接进入", (d, w) ->
                doLaunch(selected, versionName, protocolVersion))
            .setCancelable(false)
            .show();
    }

    /**
     * 阻塞式下载音效：弹进度条，逐一下载缺失文件，全部完成后才启动游戏
     */
    private void downloadSoundsBlocking(Account selected, String versionName,
                                         int protocolVersion, String soundsDir) {
        // 构建进度条对话框
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(60, 30, 60, 40);

        TextView tvMsg = new TextView(this);
        tvMsg.setText("正在下载音效资源，请稍候...");
        tvMsg.setTextSize(15);
        tvMsg.setTextColor(0xFF333333);
        layout.addView(tvMsg);

        ProgressBar progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        progressBar.setProgress(0);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 40);
        lp.setMargins(0, 24, 0, 0);
        progressBar.setLayoutParams(lp);
        layout.addView(progressBar);

        AlertDialog dialog = new AlertDialog.Builder(this)
            .setTitle("下载音效资源")
            .setView(layout)
            .setCancelable(false)
            .show();

        new Thread(() -> {
            try {
                new File(soundsDir).mkdirs();

                // 1. 获取音效文件列表
                HttpURLConnection conn = (HttpURLConnection)
                    new URL(BASE_URL+"/sounds").openConnection();
                conn.setRequestMethod("GET");
                conn.setConnectTimeout(10000);
                conn.setReadTimeout(10000);

                if (conn.getResponseCode() != 200) {
                    runOnUiThread(() -> {
                        dialog.dismiss();
                        Toast.makeText(this, "获取音效列表失败", Toast.LENGTH_LONG).show();
                    });
                    return;
                }

                String json;
                try (InputStream is = conn.getInputStream()) {
                    java.util.Scanner s = new java.util.Scanner(is, "UTF-8").useDelimiter("\\A");
                    json = s.hasNext() ? s.next() : "[]";
                }
                conn.disconnect();

                // 2. 逐个下载缺失文件，更新进度条
                JSONArray files = new JSONArray(json);
                int total = files.length();
                int completed = 0;

                for (int i = 0; i < total; i++) {
                    String path = files.getString(i);
                    File localFile = new File(soundsDir + "/" + path);

                    if (localFile.exists()) {
                        completed++;
                        continue;
                    }

                    File parent = localFile.getParentFile();
                    if (parent != null && !parent.exists()) parent.mkdirs();

                    String fileUrl = BASE_URL + "/sounds/file?path=" + path;
                    boolean ok = MainActivity.downloadFile(fileUrl, localFile.getAbsolutePath());

                    if (ok) completed++;

                    final int percent = completed * 100 / total;
                    runOnUiThread(() -> progressBar.setProgress(percent));
                }

                // 3. 全部下载完成，启动游戏
                runOnUiThread(() -> {
                    dialog.dismiss();
                    doLaunch(selected, versionName, protocolVersion);
                });
            } catch (Exception e) {
                runOnUiThread(() -> {
                    dialog.dismiss();
                    Toast.makeText(this, "下载音效资源失败: " + e.getMessage(), Toast.LENGTH_LONG).show();
                });
            }
        }).start();
    }

    /**
     * 音效校验通过后的实际启动流程（保留正版令牌刷新逻辑）
     */
    private void doLaunch(Account selected, String versionName, int protocolVersion) {
        // 正版账号：检查令牌是否需要刷新
        if (selected.isPremium()) {
            refreshPremiumAccount(selected, () -> startGameActivity(selected, versionName, protocolVersion),
                () -> Toast.makeText(this, "无法刷新正版令牌，请重新登录", Toast.LENGTH_LONG).show());
            return;
        }

        startGameActivity(selected, versionName, protocolVersion);
    }



    private void startGameActivity(Account selected, String versionName, int protocolVersion) {
        Toast.makeText(this,
            String.format("启动游戏\n用户名: %s\n版本: %s (协议 %d)\n渲染器: %s",
                selected.getName(), versionName, protocolVersion,
                useVulkan ? "Vulkan" : "OpenGL ES"),
            Toast.LENGTH_LONG).show();

        Intent intent = new Intent(this, MainActivity.class);
        intent.putExtra("username", selected.getName());
        intent.putExtra("protocol_version", protocolVersion);
        intent.putExtra("use_vulkan", useVulkan);
        // 传递正版认证信息
        intent.putExtra("login_type", selected.getType());
        if (selected.isPremium()) {
            intent.putExtra("access_token", selected.getAccessToken() != null ? selected.getAccessToken() : "");
            intent.putExtra("uuid", selected.getUuid());
            intent.putExtra("token_type", selected.getTokenType() != null ? selected.getTokenType() : "Bearer");
        }
        intent.setFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivityForResult(intent, REQUEST_LAUNCH_GAME);
        // 若已登录 Calcite 账号，启动心跳上报
        if (calciteAccount != null && calciteAccount.getCalciteToken() != null) {
            startHeartbeat(calciteAccount.getCalciteToken());
        }
    }

    // ===== 心跳上报 =====

    private void startHeartbeat(String token) {
        stopHeartbeat();
        heartbeatTask = new Runnable() {
            @Override
            public void run() {
                if (calciteApi == null) calciteApi = new CalciteApiService();
                calciteApi.sendHeartbeat(token, new CalciteApiService.Callback<Void>() {
                    @Override
                    public void onSuccess(Void result) {
                        Log.d("Heartbeat", "心跳上报成功");
                    }

                    @Override
                    public void onError(String message) {
                        Log.w("Heartbeat", "心跳上报失败: " + message);
                    }
                });
                // 每分钟上报一次
                heartbeatHandler.postDelayed(this, 60000);
            }
        };
        // 立即上报一次
        heartbeatHandler.post(heartbeatTask);
    }

    private void stopHeartbeat() {
        if (heartbeatTask != null) {
            heartbeatHandler.removeCallbacks(heartbeatTask);
            heartbeatTask = null;
        }
    }
}
