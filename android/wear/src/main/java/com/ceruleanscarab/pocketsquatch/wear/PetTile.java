package com.ceruleanscarab.pocketsquatch.wear;

import androidx.annotation.NonNull;
import androidx.concurrent.futures.ResolvableFuture;
import androidx.wear.protolayout.ActionBuilders;
import androidx.wear.protolayout.ColorBuilders;
import androidx.wear.protolayout.DimensionBuilders;
import androidx.wear.protolayout.LayoutElementBuilders;
import androidx.wear.protolayout.ModifiersBuilders;
import androidx.wear.protolayout.ResourceBuilders;
import androidx.wear.protolayout.TimelineBuilders;
import androidx.wear.tiles.RequestBuilders;
import androidx.wear.tiles.TileBuilders;
import androidx.wear.tiles.TileService;

import com.google.common.util.concurrent.ListenableFuture;

// A tile in the watch's carousel: the pet's name, how it's doing, and its meters at a glance.
// Tap it to open the watch screen.
public class PetTile extends TileService {
    private static final String RESOURCES = "1";

    @NonNull
    @Override
    protected ListenableFuture<TileBuilders.Tile> onTileRequest(@NonNull RequestBuilders.TileRequest request) {
        String name, status;
        float[] m = null;
        synchronized (Pet.LOCK) {
            boolean has = Pet.ensureLoaded(this);
            if (has) Native.tickTo(Pet.nowSecs());
            name = has ? Native.name() : "POCKET SQUATCH";
            status = has ? Native.status() : "OPEN THE PHONE APP";
            if (has) m = Native.meters();
        }
        LayoutElementBuilders.Column.Builder col = new LayoutElementBuilders.Column.Builder()
                .addContent(text(name, 22, 0xFFF2C14E, true))
                .addContent(text(status, 15, 0xFFF3EBD9, false));
        if (m != null) {
            col.addContent(text("Food " + pips(m[0]), 14, 0xFFE07AA8, false))
               .addContent(text("Happy " + pips(m[1]), 14, 0xFFF08CA0, false))
               .addContent(text("Clean " + pips(m[2]), 14, 0xFF8AD2EE, false));
        }
        ActionBuilders.LaunchAction open = new ActionBuilders.LaunchAction.Builder()
                .setAndroidActivity(new ActionBuilders.AndroidActivity.Builder()
                        .setPackageName(getPackageName())
                        .setClassName(WatchActivity.class.getName())
                        .build())
                .build();
        LayoutElementBuilders.Box root = new LayoutElementBuilders.Box.Builder()
                .setWidth(DimensionBuilders.expand())
                .setHeight(DimensionBuilders.expand())
                .addContent(col.build())
                .setModifiers(new ModifiersBuilders.Modifiers.Builder()
                        .setClickable(new ModifiersBuilders.Clickable.Builder().setId("open").setOnClick(open).build())
                        .build())
                .build();
        TileBuilders.Tile tile = new TileBuilders.Tile.Builder()
                .setResourcesVersion(RESOURCES)
                .setFreshnessIntervalMillis(15 * 60 * 1000)
                .setTileTimeline(TimelineBuilders.Timeline.fromLayoutElement(root))
                .build();
        ResolvableFuture<TileBuilders.Tile> f = ResolvableFuture.create();
        f.set(tile);
        return f;
    }

    @NonNull
    @Override
    protected ListenableFuture<ResourceBuilders.Resources> onTileResourcesRequest(
            @NonNull RequestBuilders.ResourcesRequest request) {
        ResolvableFuture<ResourceBuilders.Resources> f = ResolvableFuture.create();
        f.set(new ResourceBuilders.Resources.Builder().setVersion(RESOURCES).build());
        return f;
    }

    private static String pips(float v) {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < 4; i++) b.append(v - i >= 0.5f ? '●' : '○');
        return b.toString();
    }

    private static LayoutElementBuilders.Text text(String s, float sp, int argb, boolean bold) {
        return new LayoutElementBuilders.Text.Builder()
                .setText(s)
                .setFontStyle(new LayoutElementBuilders.FontStyle.Builder()
                        .setSize(DimensionBuilders.sp(sp))
                        .setColor(ColorBuilders.argb(argb))
                        .setWeight(bold ? LayoutElementBuilders.FONT_WEIGHT_BOLD : LayoutElementBuilders.FONT_WEIGHT_NORMAL)
                        .build())
                .build();
    }
}
