package com.ceruleanscarab.pocketsquatch;

import android.content.Context;

// Pocket Pony has no watch companion (and so no Google Play services library at all).
final class WatchSync {
    static void publish(Context ctx, byte[] save, long at) {}

    static byte[] newerThan(Context ctx, long at, long[] when) {
        return null;
    }
}
