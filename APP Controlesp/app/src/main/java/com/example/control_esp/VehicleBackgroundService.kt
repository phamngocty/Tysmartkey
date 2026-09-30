package com.example.control_esp

import android.annotation.SuppressLint
import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log
import androidx.core.app.NotificationCompat

/**
 * VehicleBackgroundService:
 * Chạy dưới dạng Foreground Service để giữ app Android luôn sống trong nền (Background).
 * Đảm bảo:
 * 1. Không bị Android Kill/Doze khi tắt màn hình hoặc chuyển sang ứng dụng khác.
 * 2. Luôn duy trì BLE GATT Connection và tự động quét/kết nối lại khi xe tiến lại gần.
 * 3. Đồng bộ thông báo trạng thái xe thời gian thực trên Notification Bar.
 */
@SuppressLint("MissingPermission")
class VehicleBackgroundService : Service() {

    companion object {
        private const val TAG = "VehicleBgService"
        const val CHANNEL_ID = "tsmart_vehicle_bg_channel"
        const val NOTIFICATION_ID = 1001

        const val ACTION_START = "com.example.control_esp.ACTION_START_BG_SERVICE"
        const val ACTION_STOP = "com.example.control_esp.ACTION_STOP_BG_SERVICE"

        var isRunning = false
            private set

        fun startService(context: Context) {
            val intent = Intent(context, VehicleBackgroundService::class.java).apply {
                action = ACTION_START
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stopService(context: Context) {
            val intent = Intent(context, VehicleBackgroundService::class.java).apply {
                action = ACTION_STOP
            }
            context.startService(intent)
        }
    }

    private val handler = Handler(Looper.getMainLooper())
    private var isReconnecting = false

    private val reconnectRunnable = object : Runnable {
        override fun run() {
            checkAndReconnect()
            handler.postDelayed(this, 8000) // Chu kỳ quét kiểm tra kết nối nền mỗi 8s
        }
    }

    override fun onCreate() {
        super.onCreate()
        Log.i(TAG, "VehicleBackgroundService onCreate")
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                Log.i(TAG, "Stopping VehicleBackgroundService")
                isRunning = false
                handler.removeCallbacks(reconnectRunnable)
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
                return START_NOT_STICKY
            }
            else -> {
                Log.i(TAG, "Starting VehicleBackgroundService as Foreground Service")
                isRunning = true
                startForegroundWithNotification()
                setupBleListeners()
                handler.removeCallbacks(reconnectRunnable)
                handler.post(reconnectRunnable)
                return START_STICKY
            }
        }
    }

    private fun startForegroundWithNotification() {
        val notification = buildNotification(
            title = getVehicleName(),
            content = if (BleManager.isConnected) "🟢 Đã kết nối • Sẵn sàng điều khiển" else "📡 Đang chạy ngầm • Sẵn sàng kết nối khi lại gần"
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Dịch Vụ Kết Nối Xe (Chạy Ngầm)",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Giữ kết nối Bluetooth liên tục và tự động kết nối xe khi lại gần"
                setShowBadge(false)
            }
            val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(title: String, content: String): Notification {
        val launchIntent = packageManager.getLaunchIntentForPackage(packageName)?.apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            launchIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_ble_signal)
            .setContentTitle(title)
            .setContentText(content)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .setContentIntent(pendingIntent)
            .build()
    }

    private fun updateNotification(content: String) {
        val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.notify(NOTIFICATION_ID, buildNotification(getVehicleName(), content))
    }

    private fun getVehicleName(): String {
        val prefs = getSharedPreferences("BT_PREF", Context.MODE_PRIVATE)
        val mac = prefs.getString("DEVICE_MAC", null)
        return (if (mac != null) prefs.getString("VEHICLE_NAME_$mac", null) else null)
            ?: prefs.getString("VEHICLE_NAME", "Honda SH 150i")
            ?: "Honda SH 150i"
    }

    private fun setupBleListeners() {
        // Gắn listener lắng nghe trạng thái kết nối
        val originalStateChange = BleManager.onConnectionStateChanged
        BleManager.onConnectionStateChanged = { connected ->
            originalStateChange?.invoke(connected)
            WatchSyncHelper.syncCurrentStateToWatch(this@VehicleBackgroundService)
            if (connected) {
                updateNotification("🟢 Đã kết nối • Sẵn sàng điều khiển")
            } else {
                updateNotification("📡 Xe ngoài vùng sóng • Tự kết nối khi lại gần")
            }
        }
    }

    private fun checkAndReconnect() {
        val prefs = getSharedPreferences("BT_PREF", Context.MODE_PRIVATE)
        val isAutoConnect = prefs.getBoolean("AUTO_CONNECT", true)
        val isBgEnabled = prefs.getBoolean("BG_RUN_ENABLED", true)
        val mac = prefs.getString("DEVICE_MAC", null)
        val isBle = prefs.getBoolean("IS_BLE", true)

        if (!isBgEnabled || !isAutoConnect || mac.isNullOrEmpty() || isReconnecting) {
            return
        }

        if (isBle) {
            if (BleManager.isConnected) {
                BleManager.readRssi()
                WatchSyncHelper.syncCurrentStateToWatch(this)
                return
            }
            if (!BleManager.isConnected) {
                isReconnecting = true
                Log.d(TAG, "Background re-connecting BLE to $mac...")
                BleManager.connect(this, mac) { success ->
                    isReconnecting = false
                    if (success) {
                        Log.i(TAG, "Background BLE connected successfully!")
                        updateNotification("🟢 Đã kết nối • Sẵn sàng điều khiển")
                    } else {
                        Log.d(TAG, "Background BLE reconnect attempt failed, will retry next cycle")
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.i(TAG, "VehicleBackgroundService onDestroy")
        isRunning = false
        handler.removeCallbacks(reconnectRunnable)
    }

    override fun onBind(intent: Intent?): IBinder? = null
}
