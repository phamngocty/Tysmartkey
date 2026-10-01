package com.example.control_esp

import android.content.Context
import android.util.Log
import com.google.android.gms.wearable.PutDataMapRequest
import com.google.android.gms.wearable.Wearable

/**
 * WatchSyncHelper:
 * Cung cấp cơ chế đồng bộ trạng thái xe (Kết nối, Nguồn ACC, Khoảng cách, Tên xe, RSSI)
 * từ điện thoại Android sang đồng hồ thông minh Wear OS qua Google Wearable Data Layer API.
 */
object WatchSyncHelper {
    private const val TAG = "WatchSyncHelper"

    var isVehiclePowerOn: Boolean = false
    var currentDistanceMeters: Float = -1.0f
    var currentSignalRssi: Int = 0

    // Trạng thái kết nối với Đồng hồ Wear OS
    var isWatchConnected: Boolean = false
    var connectedWatchName: String? = null
    var onWatchConnectionStatusChanged: ((Boolean, String?) -> Unit)? = null

    /**
     * Kiểm tra trạng thái kết nối với đồng hồ Wear OS qua Google Play Services NodeClient
     */
    fun checkWatchConnection(context: Context, callback: ((Boolean, String?) -> Unit)? = null) {
        try {
            Wearable.getNodeClient(context).connectedNodes
                .addOnSuccessListener { nodes ->
                    val watchNode = nodes.firstOrNull()
                    isWatchConnected = (watchNode != null)
                    connectedWatchName = watchNode?.displayName
                    Log.d(TAG, "checkWatchConnection: connected=$isWatchConnected, name=$connectedWatchName (total nodes=${nodes.size})")
                    callback?.invoke(isWatchConnected, connectedWatchName)
                    onWatchConnectionStatusChanged?.invoke(isWatchConnected, connectedWatchName)
                }
                .addOnFailureListener { e ->
                    Log.w(TAG, "checkWatchConnection failed", e)
                    isWatchConnected = false
                    connectedWatchName = null
                    callback?.invoke(false, null)
                    onWatchConnectionStatusChanged?.invoke(false, null)
                }
        } catch (e: Exception) {
            Log.e(TAG, "Error in checkWatchConnection", e)
            isWatchConnected = false
            connectedWatchName = null
            callback?.invoke(false, null)
            onWatchConnectionStatusChanged?.invoke(false, null)
        }
    }

    fun syncCurrentStateToWatch(context: Context) {
        try {
            val prefs = context.getSharedPreferences("BT_PREF", Context.MODE_PRIVATE)
            val mac = prefs.getString("DEVICE_MAC", null)
            val vehicleName = (if (mac != null) prefs.getString("VEHICLE_NAME_$mac", null) else null)
                ?: prefs.getString("VEHICLE_NAME", "Xe Tsmartkey")
                ?: "Xe Tsmartkey"

            val isConnected = BleManager.isConnected || BluetoothController.isConnected

            val rssi = if (isConnected) {
                if (currentSignalRssi != 0) currentSignalRssi else BleManager.lastRssi
            } else 0

            val distance = if (isConnected && rssi != 0) {
                if (currentDistanceMeters > 0f) {
                    currentDistanceMeters
                } else {
                    BleManager.calculateDistance(rssi)
                }
            } else {
                -1.0f
            }

            val putDataMapReq = PutDataMapRequest.create("/status")
            putDataMapReq.dataMap.putBoolean("esp_connected", isConnected)
            putDataMapReq.dataMap.putBoolean("power_on", isVehiclePowerOn)
            putDataMapReq.dataMap.putFloat("distance", distance)
            putDataMapReq.dataMap.putInt("rssi", rssi)
            putDataMapReq.dataMap.putString("vehicle_name", vehicleName)
            putDataMapReq.dataMap.putLong("timestamp", System.currentTimeMillis())

            val putDataReq = putDataMapReq.asPutDataRequest()
            putDataReq.setUrgent()

            Wearable.getDataClient(context).putDataItem(putDataReq)
                .addOnSuccessListener {
                    Log.d(TAG, "Synced state to watch via DataClient: ESP=$isConnected, ON=$isVehiclePowerOn, Dist=$distance, Rssi=$rssi, Name=$vehicleName")
                }
                .addOnFailureListener { e ->
                    Log.w(TAG, "Failed to sync to watch via DataClient", e)
                }

            // Gửi tức thời (<10ms) qua MessageClient để đồng hồ cập nhật ngay lập tức
            val statusPayload = "$isConnected|$isVehiclePowerOn|$distance|$rssi|$vehicleName"
            Wearable.getNodeClient(context).connectedNodes.addOnSuccessListener { nodes ->
                val watchNode = nodes.firstOrNull()
                isWatchConnected = (watchNode != null)
                connectedWatchName = watchNode?.displayName
                onWatchConnectionStatusChanged?.invoke(isWatchConnected, connectedWatchName)

                for (node in nodes) {
                    Wearable.getMessageClient(context).sendMessage(
                        node.id,
                        "/status_direct",
                        statusPayload.toByteArray(Charsets.UTF_8)
                    )
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error in syncCurrentStateToWatch", e)
        }
    }
}
