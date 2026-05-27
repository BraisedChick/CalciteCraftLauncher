package com.calcite.ui;

import android.graphics.Color;

public class MioTheme {

    private int color;
    private int ltColor;
    private int dkColor;
    private int autoTint;

    public MioTheme(int color) {
        setColor(color);
    }

    public int getColor() { return color; }
    public int getLtColor() { return ltColor; }
    public int getDkColor() { return dkColor; }
    public int getAutoTint() { return autoTint; }

    public void setColor(int color) {
        this.color = color;
        updateDerivedColors(color);
    }

    private void updateDerivedColors(int primaryColor) {
        float[] hsv = new float[3];
        Color.colorToHSV(primaryColor, hsv);

        float[] ltHsv = { hsv[0], hsv[1] * 0.3f, Math.min(hsv[2] * 1.3f, 1.0f) };
        this.ltColor = Color.HSVToColor(ltHsv);

        float[] dkHsv = { hsv[0], hsv[1], hsv[2] * 0.6f };
        this.dkColor = Color.HSVToColor(dkHsv);

        float luminance = (0.299f * Color.red(primaryColor)
                + 0.587f * Color.green(primaryColor)
                + 0.114f * Color.blue(primaryColor)) / 255f;
        this.autoTint = luminance > 0.5f ? Color.BLACK : Color.WHITE;
    }

    public static MioTheme createDefault() {
        return new MioTheme(Color.parseColor("#4CAF50"));
    }
}
