package com.ceruleanscarab.pocketsquatch;

import android.Manifest;
import android.app.NotificationManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;


import org.libsdl.app.SDL;
import org.libsdl.app.SDLActivity;

import java.util.List;

// SDL runs the game (app/src/main/cpp/main.cpp, built as libmain.so). This names it, and does
// the Android jobs the game asks for:
//   - the Galaxy Watch companion (WatchSync: the Bigfoot build only; the Pocket Pony build has none)
//   - Wi-Fi scans for the trail map (the names are hashed in the game; none are kept; the
//     Pocket Pony build has no trail map and no Wi-Fi permissions, so it never asks)
//   - planning the "needs you" notification when you leave (PhonePet, NudgeReceiver)
public class MainActivity extends SDLActivity {
    private static final String TAG = "PocketSquatch";
    private static final int ASK_NOTIFY = 7, ASK_WIFI = 8;
    private static long sWatchAt;
    private static volatile String sScan;  // finished scan results, until the game takes them
    private static boolean sScanWanted;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    @Override
    protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, ASK_NOTIFY);
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Back in the app: a "needs you" notification has done its job.
        ((NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE)).cancel(PhonePet.NOTIFICATION);
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        if (code == ASK_WIFI) {
            boolean ok = results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED;
            if (ok && sScanWanted) scanNow(this);
            else if (!ok) sScan = "";  // no permission: an empty scan, so the map stops waiting
        }
    }

    // ---- Wi-Fi scans for the trail map

    private static String wifiPermission() {
        return Build.VERSION.SDK_INT >= 33 ? Manifest.permission.NEARBY_WIFI_DEVICES
                                           : Manifest.permission.ACCESS_FINE_LOCATION;
    }

    // From the game when the trail map wants a scan. Asks for permission the first time.
    public static void startWifiScan() {
        final SDLActivity act = mSingleton;
        if (act == null) return;
        act.runOnUiThread(() -> {
            sScanWanted = true;
            if (act.checkSelfPermission(wifiPermission()) != PackageManager.PERMISSION_GRANTED) {
                act.requestPermissions(new String[] {wifiPermission()}, ASK_WIFI);
                return;
            }
            scanNow(act);
        });
    }

    private static void scanNow(Context ctx) {
        sScanWanted = false;
        final WifiManager wm = (WifiManager) ctx.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        try {
            wm.startScan();  // Android allows a few a minute; otherwise the last results stand
        } catch (Exception ignored) {
        }
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            StringBuilder b = new StringBuilder();
            try {
                List<ScanResult> results = wm.getScanResults();
                for (ScanResult r : results) {
                    @SuppressWarnings("deprecation") String ssid = r.SSID;
                    if (ssid == null || ssid.isEmpty() || ssid.contains("\n")) continue;
                    b.append(r.level).append('\t').append(ssid).append('\n');
                }
            } catch (SecurityException e) {
                Log.w(TAG, "no permission for Wi-Fi scans");
            }
            sScan = b.toString();
        }, 3000);
    }

    // The finished scan, once: lines of "rssi<TAB>name"; null while there's none.
    public static String takeWifiScan() {
        String s = sScan;
        sScan = null;
        return s;
    }

    // ---- the "needs you" notification, planned when you leave the app

    public static void planNudge() {
        Context ctx = SDL.getContext();
        if (ctx != null) PhonePet.plan(ctx, false);
    }

    // From the game when you leave the app: the watch takes it if it's newer than its own.
    public static void publishSave(byte[] save, long at) {
        Context ctx = SDL.getContext();
        if (ctx != null && save != null) WatchSync.publish(ctx, save, at);
    }

    // From the game when you come back (on its own thread; waits up to 3 s): the watch's save
    // if it was written after `at`, else null. watchAt() then says when it was written.
    public static byte[] watchSaveNewerThan(long at) {
        Context ctx = SDL.getContext();
        if (ctx == null) return null;
        long[] when = {at};
        byte[] best = WatchSync.newerThan(ctx, at, when);
        sWatchAt = when[0];
        return best;
    }

    public static long watchAt() {
        return sWatchAt;
    }
}
