package com.calcite.ui;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.MotionEvent;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.widget.AppCompatButton;

import com.calcite.R;
import com.calcite.ui.MioThemeEngine;

/**
 * 主题感知按钮，仿照 FCL FCLButton 设计
 * 按下时显示主题色背景，抬起后恢复透明
 */
public class MioButton extends AppCompatButton {

    private final GradientDrawable normalBg = new GradientDrawable();
    private final GradientDrawable pressedBg = new GradientDrawable();
    private boolean isDown;

    public MioButton(@NonNull Context context) {
        super(context);
        init(context, null, GradientDrawable.RECTANGLE, true);
    }

    public MioButton(@NonNull Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        init(context, attrs, GradientDrawable.RECTANGLE, true);
    }

    public MioButton(@NonNull Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init(context, attrs, GradientDrawable.RECTANGLE, true);
    }

    private void init(Context context, @Nullable AttributeSet attrs, int defaultShape, boolean defaultAutoPadding) {
        int shape = defaultShape;
        boolean autoPadding = defaultAutoPadding;
        if (attrs != null) {
            TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.MioButton);
            shape = a.getInt(R.styleable.MioButton_mioShape, defaultShape);
            autoPadding = a.getBoolean(R.styleable.MioButton_mioAutoPadding, defaultAutoPadding);
            a.recycle();
        }

        setSingleLine(true);
        setAllCaps(false);
        setGravity(Gravity.CENTER);
        setMinWidth(0);
        setMinHeight(0);
        setMinimumWidth(0);
        setMinimumHeight(0);
        setStateListAnimator(null);

        if (autoPadding) {
            int h = shape == GradientDrawable.RECTANGLE ? 32 : 20;
            int v = 20;
            setPadding(
                    dp2px(context, h / 2f),
                    dp2px(context, v / 2f),
                    dp2px(context, h / 2f),
                    dp2px(context, v / 2f)
            );
        }

        float corner = dp2px(context, 8);
        normalBg.setShape(shape);
        normalBg.setCornerRadius(corner);
        normalBg.setColor(Color.TRANSPARENT);

        pressedBg.setShape(shape);
        pressedBg.setCornerRadius(corner);

        applyTheme();

        // 注册主题变化监听
        MioThemeEngine.getInstance().registerListener(this, this::applyTheme);
    }

    private void applyTheme() {
        int ltColor = MioThemeEngine.getInstance().getTheme().getLtColor();
        pressedBg.setColor(ltColor);
        if (isDown) {
            setBackground(pressedBg);
            setTextColor(MioThemeEngine.getInstance().getTheme().getAutoTint());
        } else {
            setBackground(normalBg);
            setTextColor(ltColor);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            isDown = true;
            setBackground(pressedBg);
            setTextColor(MioThemeEngine.getInstance().getTheme().getAutoTint());
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            isDown = false;
            setBackground(normalBg);
            setTextColor(MioThemeEngine.getInstance().getTheme().getLtColor());
        }
        return super.onTouchEvent(event);
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        MioThemeEngine.getInstance().unregisterListener(this);
    }

    private static int dp2px(Context context, float dp) {
        return (int) (dp * context.getResources().getDisplayMetrics().density + 0.5f);
    }
}
