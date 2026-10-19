package com.ceruleanscarab.pocketsquatch.wear;

import com.google.android.gms.wearable.DataEvent;
import com.google.android.gms.wearable.DataEventBuffer;
import com.google.android.gms.wearable.DataMap;
import com.google.android.gms.wearable.DataMapItem;
import com.google.android.gms.wearable.WearableListenerService;

// The phone published a save (it does when you leave the app): take it if it's newer.
public class DataListener extends WearableListenerService {
    @Override
    public void onDataChanged(DataEventBuffer events) {
        for (DataEvent e : events) {
            if (e.getType() != DataEvent.TYPE_CHANGED || !"/pet".equals(e.getDataItem().getUri().getPath())) continue;
            DataMap m = DataMapItem.fromDataItem(e.getDataItem()).getDataMap();
            if ("phone".equals(m.getString("from"))) Pet.adopt(this, m.getByteArray("save"), m.getLong("at"));
        }
    }
}
