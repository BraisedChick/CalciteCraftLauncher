package com.calcite.ui;

import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.TypedArray;
import android.util.AttributeSet;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.widget.AppCompatEditText;

import com.calcite.R;
import com.calcite.ui.MioThemeEngine;

/**
 * 主题感知输入框，仿照 FCL FCLEditText 设计
 */
public class MioEditText extends AppCompatEditText {

    private boolean autoTint = true;

    public MioEditText(@NonNull Context context) {
        super(context);
        init(context, null);
    }

    public MioEditText(@NonNull Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        init(context, attrs);
    }

    public MioEditText(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(context, attrs);
    }

    private void init(Context context, @Nullable AttributeSet attrs) {
        if (attrs != null) {
            TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.MioEditText);
            autoTint = a.getBoolean(R.styleable.MioEditText_mioAutoTint, true);
            a.recycle();
        }

        if (autoTint) {
            applyTheme();
            MioThemeEngine.getInstance().registerListener(this, this::applyTheme);
        }
    }

    private void applyTheme() {
        int color = MioThemeEngine.getInstance().getTheme().getColor();
        setBackgroundTintList(ColorStateList.valueOf(color));
        setTextColor(MioThemeEngine.getInstance().getTheme().getAutoTint());
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        MioThemeEngine.getInstance().unregisterListener(this);
    }
}
