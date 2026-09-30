package com.example.control_esp

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Context
import android.util.Log
import com.google.android.gms.wearable.MessageEvent
import com.google.android.gms.wearable.WearableListenerService

class WearMessageListenerService : WearableListenerService() {
    override fun onMessageReceived(messageEvent: MessageEvent) {
        Log.d("WEAR_BRIDGE", "Global Message Received Path: ${messageEvent.path}")
        
        when (messageEvent.path) {
            "/ping" -> {
                Log.d("WEAR_BRIDGE", "Ping received from watch! Syncing current state...")
                WatchSyncHelper.syncCurrentStateToWatch(this)
            }
            "/command" -> {
                val command = String(messageEvent.data)
                Log.d("WEAR_BRIDGE", "Received command content: $command")
                if (command == "1") {
                    WatchSyncHelper.isVehiclePowerOn = true
                } else if (command == "0") {
                    WatchSyncHelper.isVehiclePowerOn = false
                }
                handleCommand(command)
                WatchSyncHelper.syncCurrentStateToWatch(this)
            }
            else -> {
                Log.d("WEAR_BRIDGE", "Unknown path: ${messageEvent.path}")
                super.onMessageReceived(messageEvent)
            }
        }
    }

    override fun onPeerConnected(peer: com.google.android.gms.wearable.Node) {
        super.onPeerConnected(peer)
        Log.d("WEAR_BRIDGE", "Watch peer connected: ${peer.displayName} (${peer.id})")
        WatchSyncHelper.isWatchConnected = true
        WatchSyncHelper.connectedWatchName = peer.displayName
        WatchSyncHelper.onWatchConnectionStatusChanged?.invoke(true, peer.displayName)
        WatchSyncHelper.syncCurrentStateToWatch(this)
    }

    override fun onPeerDisconnected(peer: com.google.android.gms.wearable.Node) {
        super.onPeerDisconnected(peer)
        Log.d("WEAR_BRIDGE", "Watch peer disconnected: ${peer.displayName}")
        WatchSyncHelper.checkWatchConnection(this)
    }

    private fun handleCommand(command: String) {
        val prefs = getSharedPreferences("BT_PREF", Context.MODE_PRIVATE)
        val secretKey = prefs.getString("SECRET_KEY", "271000") ?: "271000"
        val mac = prefs.getString("DEVICE_MAC", null)
        val isBle = prefs.getBoolean("IS_BLE", true)

        BleManager.SECRET_KEY = secretKey
        BluetoothController.SECRET_KEY = secretKey

        // 1. Kiểm tra nếu BLE đang kết nối
        if (BleManager.isConnected) {
            Log.d("WEAR_BRIDGE", "ESP32 BLE is connected. Sending command: $command")
            BleManager.sendCommand(command)
            return
        }

        // 2. Kiểm tra nếu Classic Bluetooth đang kết nối
        if (BluetoothController.isConnected) {
            Log.d("WEAR_BRIDGE", "ESP32 Classic BT is connected. Sending command: $command")
            BluetoothController.sendCommand(command)
            return
        }

        // 3. Nếu chưa kết nối, tự động kết nối ngầm theo loại thiết bị đã lưu
        if (mac != null) {
            val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
            val adapter = bluetoothManager.adapter

            if (adapter == null || !adapter.isEnabled) {
                Log.e("WEAR_BRIDGE", "Bluetooth adapter is null or disabled")
                return
            }

            if (isBle) {
                Log.d("WEAR_BRIDGE", "Attempting background BLE connection to $mac...")
                BleManager.init(this)
                BleManager.connect(this, mac) { success ->
                    if (success) {
                        Log.d("WEAR_BRIDGE", "Background BLE connected! Relaying command: $command")
                        BleManager.sendCommand(command)
                        WatchSyncHelper.syncCurrentStateToWatch(this)
                    } else {
                        Log.e("WEAR_BRIDGE", "Background BLE connection failed to $mac")
                        WatchSyncHelper.syncCurrentStateToWatch(this)
                    }
                }
            } else {
                try {
                    val device = adapter.getRemoteDevice(mac)
                    Log.d("WEAR_BRIDGE", "Attempting background Classic BT connection to $mac...")
                    BluetoothController.connect(device) { success ->
                        if (success) {
                            Log.d("WEAR_BRIDGE", "Background Classic connected! Relaying command: $command")
                            BluetoothController.sendCommand(command)
                            WatchSyncHelper.syncCurrentStateToWatch(this)
                        } else {
                            Log.e("WEAR_BRIDGE", "Background Classic connection failed to $mac")
                            WatchSyncHelper.syncCurrentStateToWatch(this)
                        }
                    }
                } catch (e: Exception) {
                    Log.e("WEAR_BRIDGE", "Error getting remote device", e)
                }
            }
        } else {
            Log.e("WEAR_BRIDGE", "No saved MAC address found in Prefs")
        }
    }
}
