package com.calcite;

import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.KeyEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

public class RendererSurfaceView extends SurfaceView implements SurfaceHolder.Callback {
    private static final String TAG = "RendererSurfaceView";
    private MainActivity activity;
    private boolean isRendererInitialized = false;

    public RendererSurfaceView(Context context) {
        super(context);
        init();
    }

    public RendererSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public RendererSurfaceView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        getHolder().addCallback(this);
        activity = (MainActivity) getContext();
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override
    public boolean onCheckIsTextEditor() {
        return true;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        outAttrs.inputType = EditorInfo.TYPE_CLASS_TEXT;
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI;
        return new BaseInputConnection(this, false) {
            @Override
            public boolean commitText(CharSequence text, int newCursorPosition) {
                if (activity != null) {
                    for (int i = 0; i < text.length(); i++) {
                        activity.addImGuiCharacter((int) text.charAt(i));
                    }
                }
                return true;
            }

            @Override
            public boolean sendKeyEvent(KeyEvent event) {
                if (event.getAction() == KeyEvent.ACTION_DOWN && activity != null) {
                    if (event.getKeyCode() == KeyEvent.KEYCODE_DEL) {
                        activity.addImGuiCharacter(127); // 退格键
                    } else {
                        int c = event.getUnicodeChar();
                        if (c != 0) {
                            activity.addImGuiCharacter(c);
                        }
                    }
                }
                return true;
            }

            @Override
            public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                if (activity != null) {
                    activity.addImGuiCharacter(127); // DEL
                }
                return true;
            }
        };
    }

    public void initialize() {
        Log.i(TAG, "initialize() called - no-op, waiting for callbacks");
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.i(TAG, "=== surfaceCreated ===");
        Log.i(TAG, "Surface valid: " + holder.getSurface().isValid());
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "=== surfaceChanged: " + width + "x" + height + " ===");
        Log.i(TAG, "Surface valid: " + holder.getSurface().isValid());
        Log.i(TAG, "isRendererInitialized: " + isRendererInitialized);
        
        if (!isRendererInitialized && holder.getSurface().isValid() && width > 0 && height > 0) {
            // 延迟 100ms 再初始化，确保布局稳定
            Log.i(TAG, "Scheduling renderer initialization in 100ms...");
            postDelayed(() -> {
                if (holder.getSurface().isValid() && !isRendererInitialized) {
                    Log.i(TAG, "Initializing renderer with surface size: " + width + "x" + height);
                    isRendererInitialized = true;
                    activity.onVulkanSurfaceCreated(holder.getSurface());
                } else {
                    Log.w(TAG, "Surface invalid or already initialized, skipping");
                }
            }, 100);
        } else if (isRendererInitialized) {
            Log.i(TAG, "Renderer already initialized, calling resize");
            activity.onSurfaceChanged(width, height);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "=== surfaceDestroyed ===");
        isRendererInitialized = false;
        activity.onVulkanSurfaceDestroyed();
    }
}
