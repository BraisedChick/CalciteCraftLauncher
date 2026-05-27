package com.calcite.ui;

import android.content.Context;
import android.content.res.TypedArray;
import android.util.AttributeSet;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.widget.AppCompatTextView;

import com.calcite.R;
import com.calcite.ui.MioThemeEngine;

/**
 * 主题感知文本标签
 * useThemeColor=true: 文本色跟随主题主色（适用于标签标题）
 * useThemeColor=false: 文本色使用 XML 设置的固定色
 */
public class MioTextView extends AppCompatTextView {

    private boolean useThemeColor = true;

    public MioTextView(@NonNull Context context) {
        super(context);
    }

    public MioTextView(@NonNull Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        init(context, attrs);
    }

    public MioTextView(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(context, attrs);
    }

    private void init(Context context, @Nullable AttributeSet attrs) {
        if (attrs != null) {
            TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.MioTextView);
            useThemeColor = a.getBoolean(R.styleable.MioTextView_mioAutoTint, true);
            a.recycle();
        }

        if (useThemeColor) {
            applyTheme();
            MioThemeEngine.getInstance().registerListener(this, this::applyTheme);
        }
    }

    private void applyTheme() {
        setTextColor(MioThemeEngine.getInstance().getTheme().getColor());
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        MioThemeEngine.getInstance().unregisterListener(this);
    }
}
