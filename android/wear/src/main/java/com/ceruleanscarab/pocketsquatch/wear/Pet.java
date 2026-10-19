package com.ceruleanscarab.pocketsquatch.wear;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.util.Log;

import androidx.wear.tiles.TileService;

import com.google.android.gms.tasks.Tasks;
import com.google.android.gms.wearable.DataItem;
import com.google.android.gms.wearable.DataItemBuffer;
import com.google.android.gms.wearable.DataMap;
import com.google.android.gms.wearable.DataMapItem;
import com.google.android.gms.wearable.PutDataMapRequest;
import com.google.android.gms.wearable.Wearable;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.file.Files;
import java.util.concurrent.TimeUnit;

// The watch's copy of the pet, and keeping it in step with the phone.
//
// Both sides publish the save as a Data Layer item at "/pet", with "at" (when it was written,
// in ms) and "from" ("phone" or "watch"). Whichever was written last wins. The watch only
// publishes after you've done something, so just looking never overrides the phone.
final class Pet {
    static final String TAG = "PocketSquatch";
    static final Object LOCK = new Object();
    private static final String FILE = "squatch.sav";
    private static final String PREFS = "pet", AT = "at";
    private static final long MIN_NUDGE_GAP_MS = 45 * 60 * 1000L;

    static long nowSecs() { return System.currentTimeMillis() / 1000; }

    // Loads the local copy once per process; true if there's a pet.
    static boolean ensureLoaded(Context ctx) {
        synchronized (LOCK) {
            if (Native.hasPet()) return true;
            File f = new File(ctx.getFilesDir(), FILE);
            if (!f.exists()) return false;
            try {
                return Native.load(Files.readAllBytes(f.toPath()), nowSecs());
            } catch (Exception e) {
                Log.w(TAG, "couldn't read the watch's save", e);
                return false;
            }
        }
    }

    static long localAt(Context ctx) { return prefs(ctx).getLong(AT, 0); }

    // A save from the phone: taken if it's newer than ours.
    static boolean adopt(Context ctx, byte[] save, long at) {
        synchronized (LOCK) {
            if (save == null || at <= localAt(ctx)) return false;
            if (!Native.load(save, nowSecs())) return false;
            write(ctx, save);
            prefs(ctx).edit().putLong(AT, at).apply();
            Log.i(TAG, "took the phone's save from " + at);
        }
        afterChange(ctx);
        return true;
    }

    // After something was done on the watch: keep it, tell the phone, and re-plan.
    static void acted(Context ctx) {
        byte[] save;
        long at = System.currentTimeMillis();
        synchronized (LOCK) {
            save = Native.save();
            if (save == null) return;
            write(ctx, save);
            prefs(ctx).edit().putLong(AT, at).apply();
        }
        PutDataMapRequest req = PutDataMapRequest.create("/pet");
        req.getDataMap().putByteArray("save", save);
        req.getDataMap().putLong("at", at);
        req.getDataMap().putString("from", "watch");
        Wearable.getDataClient(ctx).putDataItem(req.asPutDataRequest().setUrgent());
        afterChange(ctx);
    }

    // Asks the Data Layer for the phone's latest; runs off the UI thread.
    static void pullFromPhone(Context ctx, Runnable done) {
        Context app = ctx.getApplicationContext();
        new Thread(() -> {
            try {
                DataItemBuffer items = Tasks.await(
                        Wearable.getDataClient(app).getDataItems(Uri.parse("wear://*/pet")), 5, TimeUnit.SECONDS);
                byte[] best = null;
                long bestAt = 0;
                for (DataItem item : items) {
                    DataMap m = DataMapItem.fromDataItem(item).getDataMap();
                    if (!"phone".equals(m.getString("from"))) continue;
                    if (m.getLong("at") > bestAt) {
                        bestAt = m.getLong("at");
                        best = m.getByteArray("save");
                    }
                }
                items.release();
                if (best != null) adopt(app, best, bestAt);
            } catch (Exception e) {
                Log.w(TAG, "couldn't reach the phone", e);
            }
            if (done != null) done.run();
        }).start();
    }

    // The tile and the next nudge follow any change.
    static void afterChange(Context ctx) {
        try {
            TileService.getUpdater(ctx).requestUpdate(PetTile.class);
        } catch (Exception ignored) {
        }
        scheduleNudge(ctx, false);
    }

    // Sets an alarm for when it will next need you (a nudge on the wrist), never sooner than
    // MIN_NUDGE_GAP_MS after the last one.
    static void scheduleNudge(Context ctx, boolean justNudged) {
        long secs;
        synchronized (LOCK) {
            if (!ensureLoaded(ctx)) return;
            Native.tickTo(nowSecs());
            secs = Native.nextNeedSecs();
        }
        long now = System.currentTimeMillis();
        long when = secs < 0 ? now + 6 * 3600 * 1000L : now + secs * 1000;
        long earliest = (justNudged ? now : prefs(ctx).getLong("nudged", 0)) + MIN_NUDGE_GAP_MS;
        when = Math.max(when, Math.max(earliest, now + 60 * 1000L));
        AlarmManager am = (AlarmManager) ctx.getSystemService(Context.ALARM_SERVICE);
        PendingIntent pi = PendingIntent.getBroadcast(ctx, 1, new Intent(ctx, NudgeReceiver.class),
                PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, when, pi);
        if (justNudged) prefs(ctx).edit().putLong("nudged", now).apply();
    }

    private static void write(Context ctx, byte[] save) {
        File f = new File(ctx.getFilesDir(), FILE), tmp = new File(ctx.getFilesDir(), FILE + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(save);
        } catch (Exception e) {
            Log.w(TAG, "couldn't write the watch's save", e);
            return;
        }
        if (!tmp.renameTo(f)) Log.w(TAG, "couldn't replace the watch's save");
    }

    private static SharedPreferences prefs(Context ctx) {
        return ctx.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private Pet() {}
}
