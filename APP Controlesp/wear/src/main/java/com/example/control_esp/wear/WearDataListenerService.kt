package com.example.control_esp.wear

import android.content.Context
import android.util.Log
import com.google.android.gms.wearable.DataEvent
import com.google.android.gms.wearable.DataEventBuffer
import com.google.android.gms.wearable.DataMapItem
import com.google.android.gms.wearable.MessageEvent
import com.google.android.gms.wearable.WearableListenerService

/**
 * WearDataListenerService:
 * Dịch vụ chạy ngầm trên Wear OS tiếp nhận cấu hình cử chỉ từ điện thoại
 * ngay cả khi ứng dụng trên đồng hồ đang đóng hoặc màn hình tắt.
 */
class WearDataListenerService : WearableListenerService() {
    companion object {
        private const val TAG = "WearDataListener"
    }

    override fun onDataChanged(dataEvents: DataEventBuffer) {
        dataEvents.forEach { event ->
            if (event.type == DataEvent.TYPE_CHANGED && event.dataItem.uri.path == "/wear_settings") {
                try {
                    val dataMap = DataMapItem.fromDataItem(event.dataItem).dataMap
                    val enabled = dataMap.getBoolean("gesture_enabled", true)
                    val mode = dataMap.getInt("gesture_mode", 0)
                    val sens = dataMap.getInt("gesture_sensitivity", 1)

                    getSharedPreferences("WEAR_PREF", Context.MODE_PRIVATE).edit()
                        .putBoolean("GESTURE_START", enabled)
                        .putInt("GESTURE_ACTION_MODE", mode)
                        .putInt("GESTURE_SENSITIVITY", sens)
                        .apply()
                    Log.d(TAG, "Background saved wear settings from DataClient: enabled=$enabled, mode=$mode, sens=$sens")
                } catch (e: Exception) {
                    Log.e(TAG, "Error processing background wear_settings data", e)
                }
            }
        }
    }

    override fun onMessageReceived(messageEvent: MessageEvent) {
        if (messageEvent.path == "/settings_sync") {
            try {
                val payload = String(messageEvent.data, Charsets.UTF_8)
                val parts = payload.split("|")
                if (parts.size >= 3) {
                    val enabled = parts[0].toBoolean()
                    val mode = parts[1].toIntOrNull() ?: 0
                    val sens = parts[2].toIntOrNull() ?: 1

                    getSharedPreferences("WEAR_PREF", Context.MODE_PRIVATE).edit()
                        .putBoolean("GESTURE_START", enabled)
                        .putInt("GESTURE_ACTION_MODE", mode)
                        .putInt("GESTURE_SENSITIVITY", sens)
                        .apply()
                    Log.d(TAG, "Background saved wear settings from MessageClient: enabled=$enabled, mode=$mode, sens=$sens")
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error processing background settings_sync message", e)
            }
        }
    }
}
