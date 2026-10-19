package com.ceruleanscarab.pocketsquatch.wear;

// The game itself, in C++ (src/main/cpp/wear_jni.cpp). Only called through Pet, which holds the lock.
final class Native {
    static {
        System.loadLibrary("squatchwear");
    }

    static native boolean load(byte[] save, long nowSecs);
    static native boolean hasPet();
    static native byte[] save();
    static native void tickTo(long nowSecs);
    static native void frame(int[] argb, long ms);
    static native int tap(int x, int y);
    static native String name();
    static native String status();
    static native float[] meters();
    static native String needNow();
    static native long nextNeedSecs();

    private Native() {}
}
