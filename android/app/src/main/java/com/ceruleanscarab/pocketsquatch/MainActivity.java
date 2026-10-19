package com.ceruleanscarab.pocketsquatch;

import android.content.Context;
import android.net.Uri;
import android.util.Log;

import com.google.android.gms.tasks.Tasks;
import com.google.android.gms.wearable.DataItem;
import com.google.android.gms.wearable.DataItemBuffer;
import com.google.android.gms.wearable.DataMap;
import com.google.android.gms.wearable.DataMapItem;
import com.google.android.gms.wearable.PutDataMapRequest;
import com.google.android.gms.wearable.Wearable;

import org.libsdl.app.SDL;
import org.libsdl.app.SDLActivity;

import java.util.concurrent.TimeUnit;

// SDL runs the game (app/src/main/cpp/main.cpp, built as libmain.so). This names it, and
// talks to the Galaxy Watch companion for it over the Wear OS Data Layer: the save goes out
// as the item "/pet" with when it was written ("at", ms) and by whom ("from").
public class MainActivity extends SDLActivity {
    private static final String TAG = "PocketSquatch";
    private static long sWatchAt;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    // From the game when you leave the app: the watch takes it if it's newer than its own.
    public static void publishSave(byte[] save, long at) {
        Context ctx = SDL.getContext();
        if (ctx == null || save == null) return;
        try {
            PutDataMapRequest req = PutDataMapRequest.create("/pet");
            req.getDataMap().putByteArray("save", save);
            req.getDataMap().putLong("at", at);
            req.getDataMap().putString("from", "phone");
            Wearable.getDataClient(ctx).putDataItem(req.asPutDataRequest().setUrgent());
        } catch (Exception e) {
            Log.w(TAG, "couldn't publish to the watch", e);
        }
    }

    // From the game when you come back (on its own thread; waits up to 3 s): the watch's save
    // if it was written after `at`, else null. watchAt() then says when it was written.
    public static byte[] watchSaveNewerThan(long at) {
        Context ctx = SDL.getContext();
        if (ctx == null) return null;
        try {
            DataItemBuffer items = Tasks.await(
                    Wearable.getDataClient(ctx).getDataItems(Uri.parse("wear://*/pet")), 3, TimeUnit.SECONDS);
            byte[] best = null;
            long bestAt = at;
            for (DataItem item : items) {
                DataMap m = DataMapItem.fromDataItem(item).getDataMap();
                if (!"watch".equals(m.getString("from"))) continue;
                if (m.getLong("at") > bestAt) {
                    bestAt = m.getLong("at");
                    best = m.getByteArray("save");
                }
            }
            items.release();
            sWatchAt = bestAt;
            return best;
        } catch (Exception e) {
            return null;  // no watch, or no Google Play services: just the phone
        }
    }

    public static long watchAt() {
        return sWatchAt;
    }
}
