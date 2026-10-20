package com.ceruleanscarab.pocketsquatch;

import android.app.AlarmManager;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.util.Log;

import java.io.File;
import java.nio.file.Files;

// "Needs you" notifications while the app is closed. The sim is predictable, so when you leave
// the app the game works out (forecast(), in main.cpp) when the pet will next be hungry,
// lonely, sick or calling, and an alarm wakes NudgeReceiver then. They stay on the phone:
// the watch app buzzes for itself.
final class PhonePet {
    static final int NOTIFICATION = 1;
    private static final String CHANNEL = "nudges";
    private static final long MIN_GAP_MS = 45 * 60 * 1000L;
    private static boolean sLoaded;

    // Answers "secondsUntilNextNeed\nwhatItNeedsNow\nNAME" for a save at the given time.
    private static native String forecast(byte[] save, long nowSecs);

    private static synchronized boolean load() {
        if (sLoaded) return true;
        try {
            System.loadLibrary("SDL2");
            System.loadLibrary("main");
            sLoaded = true;
        } catch (Throwable t) {
            Log.w("PocketSquatch", "couldn't load the game", t);
        }
        return sLoaded;
    }

    // [secs, need, name], or null if there's no pet yet.
    static String[] ask(Context ctx) {
        File f = new File(ctx.getFilesDir(), "squatch.sav");
        if (!f.exists() || !load()) return null;
        try {
            String r = forecast(Files.readAllBytes(f.toPath()), System.currentTimeMillis() / 1000);
            return r == null ? null : r.split("\n", -1);
        } catch (Exception e) {
            return null;
        }
    }

    // Sets the alarm for the next time it'll need you, at least MIN_GAP_MS after the last nudge.
    static void plan(Context ctx, boolean justNudged) {
        String[] f = ask(ctx);
        if (f == null) return;
        long secs;
        try {
            secs = Long.parseLong(f[0]);
        } catch (NumberFormatException e) {
            return;
        }
        long now = System.currentTimeMillis();
        SharedPreferences p = ctx.getSharedPreferences("nudge", Context.MODE_PRIVATE);
        if (justNudged) p.edit().putLong("last", now).apply();
        long when = secs < 0 ? now + 6 * 3600 * 1000L : now + secs * 1000;
        when = Math.max(when, Math.max(p.getLong("last", 0) + MIN_GAP_MS, now + 60 * 1000L));
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        PendingIntent pi = PendingIntent.getBroadcast(ctx, 2, new Intent(ctx, NudgeReceiver.class),
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, when, pi);
    }

    static void notify(Context ctx, String name, String need) {
        NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        nm.createNotificationChannel(new NotificationChannel(CHANNEL, "When your pet needs you",
                NotificationManager.IMPORTANCE_DEFAULT));
        PendingIntent open = PendingIntent.getActivity(ctx, 0, new Intent(ctx, MainActivity.class),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        String who = name.isEmpty() ? "Squatch" : name.charAt(0) + name.substring(1).toLowerCase();
        android.app.Notification n = new android.app.Notification.Builder(ctx, CHANNEL)
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentTitle(who + " " + need)
                .setContentText("Tap to look in on " + who + ".")
                .setContentIntent(open)
                .setAutoCancel(true)
                .setLocalOnly(true)  // the watch has its own
                .build();
        nm.notify(NOTIFICATION, n);
    }

    private PhonePet() {}
}
