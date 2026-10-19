package com.ceruleanscarab.pocketsquatch.wear;

import android.Manifest;
import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

// The watch screen: the clearing, its name and needs, and big buttons, all drawn by the game
// (scene/src/wrist.cpp) into a bitmap ten times a second.
public class WatchActivity extends Activity {
    private static final int SIZE = 480;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final int[] pixels = new int[SIZE * SIZE];
    private Bitmap bitmap;
    private ImageView view;
    private TextView empty;
    private long lastTick;
    private boolean running;

    private final Runnable frame = new Runnable() {
        @Override
        public void run() {
            if (!running) return;
            draw();
            handler.postDelayed(this, 100);
        }
    };

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        view = new ImageView(this);
        view.setScaleType(ImageView.ScaleType.FIT_CENTER);
        root.addView(view, new FrameLayout.LayoutParams(-1, -1));
        empty = new TextView(this);
        empty.setText("Open Pocket Squatch on your phone to bring your pet here.");
        empty.setGravity(Gravity.CENTER);
        empty.setTextColor(0xFFF3EBD9);
        empty.setPadding(40, 40, 40, 40);
        root.addView(empty, new FrameLayout.LayoutParams(-1, -1));
        setContentView(root);
        bitmap = Bitmap.createBitmap(SIZE, SIZE, Bitmap.Config.ARGB_8888);
        view.setImageBitmap(bitmap);
        view.setOnTouchListener(this::onTouch);
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != 0)
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 1);
    }

    @Override
    protected void onResume() {
        super.onResume();
        Pet.ensureLoaded(this);
        Pet.pullFromPhone(this, null);
        running = true;
        handler.post(frame);
    }

    @Override
    protected void onPause() {
        super.onPause();
        running = false;
        handler.removeCallbacks(frame);
        Pet.scheduleNudge(this, false);
    }

    private void draw() {
        boolean has;
        synchronized (Pet.LOCK) {
            has = Native.hasPet();
            if (has) {
                long now = SystemClock.uptimeMillis();
                if (now - lastTick >= 1000) {
                    Native.tickTo(Pet.nowSecs());
                    lastTick = now;
                }
                Native.frame(pixels, now);
            }
        }
        empty.setVisibility(has ? View.GONE : View.VISIBLE);
        view.setVisibility(has ? View.VISIBLE : View.INVISIBLE);
        if (has) {
            bitmap.setPixels(pixels, 0, SIZE, 0, 0, SIZE, SIZE);
            view.invalidate();
        }
    }

    private boolean onTouch(View v, MotionEvent e) {
        if (e.getActionMasked() != MotionEvent.ACTION_UP) return true;
        // The bitmap is square and centred in the view.
        float side = Math.min(v.getWidth(), v.getHeight());
        float x = (e.getX() - (v.getWidth() - side) / 2) * SIZE / side;
        float y = (e.getY() - (v.getHeight() - side) / 2) * SIZE / side;
        int done;
        synchronized (Pet.LOCK) {
            done = Native.hasPet() ? Native.tap((int) x, (int) y) : 0;
        }
        if (done != 0) {
            v.performHapticFeedback(android.view.HapticFeedbackConstants.CONFIRM);
            Pet.acted(this);
        }
        return true;
    }
}
