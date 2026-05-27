package com.calcite.ui;

import android.graphics.Color;
import android.os.Handler;
import android.view.View;

import java.util.HashMap;
import java.util.Map;

public class MioThemeEngine {

    private static MioThemeEngine instance;

    private MioTheme theme;
    private final Handler handler = new Handler();
    private final Map<View, Runnable> listeners = new HashMap<>();

    private MioThemeEngine() {
        theme = MioTheme.createDefault();
    }

    public static MioThemeEngine getInstance() {
        if (instance == null) {
            instance = new MioThemeEngine();
        }
        return instance;
    }

    public MioTheme getTheme() {
        return theme;
    }

    public void registerListener(View view, Runnable runnable) {
        listeners.put(view, runnable);
        handler.post(runnable);
    }

    public void unregisterListener(View view) {
        listeners.remove(view);
    }

    public void setColor(int color) {
        theme.setColor(color);
        notifyListeners();
    }

    public void notifyListeners() {
        for (Map.Entry<View, Runnable> entry : listeners.entrySet()) {
            if (entry.getKey() != null && entry.getValue() != null) {
                handler.post(entry.getValue());
            }
        }
    }

    public static int getColorFromHSV(float hue, float saturation, float value) {
        return Color.HSVToColor(new float[]{hue, saturation, value});
    }
}
