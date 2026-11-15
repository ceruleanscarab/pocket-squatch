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

import java.util.concurrent.TimeUnit;

// The Galaxy Watch companion, over the Wear OS Data Layer: the save goes out as the item "/pet"
// with when it was written ("at", ms) and by whom ("from").
final class WatchSync {
    private static final String TAG = "PocketSquatch";

    static void publish(Context ctx, byte[] save, long at) {
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

    // The watch's save if it was written after `at`, else null; when[0] is set to when it was.
    static byte[] newerThan(Context ctx, long at, long[] when) {
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
            when[0] = bestAt;
            return best;
        } catch (Exception e) {
            return null;  // no watch, or no Google Play services: just the phone
        }
    }
}
