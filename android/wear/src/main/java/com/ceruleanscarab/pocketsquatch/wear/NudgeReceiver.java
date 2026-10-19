package com.ceruleanscarab.pocketsquatch.wear;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

import androidx.core.app.NotificationCompat;

// The alarm Pet.scheduleNudge set: if it needs you now, a buzz and a notification; then plan the next.
public class NudgeReceiver extends BroadcastReceiver {
    private static final String CHANNEL = "nudges";

    @Override
    public void onReceive(Context ctx, Intent intent) {
        String need, name;
        synchronized (Pet.LOCK) {
            if (!Pet.ensureLoaded(ctx)) return;
            Native.tickTo(Pet.nowSecs());
            need = Native.needNow();
            name = Native.name();
        }
        if (need == null) {
            Pet.scheduleNudge(ctx, false);
            return;
        }
        NotificationManager nm = (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        nm.createNotificationChannel(new NotificationChannel(CHANNEL, "When your pet needs you",
                NotificationManager.IMPORTANCE_DEFAULT));
        PendingIntent open = PendingIntent.getActivity(ctx, 0, new Intent(ctx, WatchActivity.class),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        String who = name.charAt(0) + name.substring(1).toLowerCase();
        nm.notify(1, new NotificationCompat.Builder(ctx, CHANNEL)
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentTitle(who + " " + need)
                .setContentText("Tap to look in on " + who + ".")
                .setContentIntent(open)
                .setAutoCancel(true)
                .build());
        Pet.scheduleNudge(ctx, true);
    }
}
