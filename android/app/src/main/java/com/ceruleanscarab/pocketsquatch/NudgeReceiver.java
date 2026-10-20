package com.ceruleanscarab.pocketsquatch;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

// The alarm PhonePet.plan set: a notification if it needs you now, then the next plan.
public class NudgeReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context ctx, Intent intent) {
        String[] f = PhonePet.ask(ctx);
        if (f == null) return;
        boolean needs = f.length > 2 && !f[1].isEmpty();
        if (needs) PhonePet.notify(ctx, f[2], f[1]);
        PhonePet.plan(ctx, needs);
    }
}
