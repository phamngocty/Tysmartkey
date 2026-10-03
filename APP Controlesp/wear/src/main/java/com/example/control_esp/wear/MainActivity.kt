package com.example.control_esp.wear

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Build
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.wear.compose.foundation.pager.HorizontalPager
import androidx.wear.compose.foundation.pager.rememberPagerState
import androidx.wear.compose.material.*
import com.google.android.gms.wearable.*
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Locale

class MainActivity : ComponentActivity(), DataClient.OnDataChangedListener, MessageClient.OnMessageReceivedListener, SensorEventListener {

    companion object {
        private const val TAG = "WEAR_APP"
    }

    private var sensorManager: SensorManager? = null
    private var accelSensor: Sensor? = null
    private var lastGestureTime = 0L
    private var appResumeTime = 0L

    // State quan sát thời gian thực trên Jetpack Compose
    private var isEspConnected by mutableStateOf(false)
    private var isVehicleOn by mutableStateOf(false)
    private var isEngineStarting by mutableStateOf(false)
    private var isAutoStartEnabled by mutableStateOf(false)
    private var isGestureEnabled by mutableStateOf(true) // Cho phép búng tay đề máy
    private var gestureActionMode by mutableStateOf(0) // 0: MODE_START_ONLY (An toàn), 1: MODE_TOGGLE_AND_START
    private var gestureSensitivity by mutableStateOf(1) // 0: Thấp (26m/s²), 1: Vừa (21m/s²), 2: Cao (16m/s²)
    private var statusText by mutableStateOf("ĐANG KHỞI TẠO")
    private var vehicleDistance by mutableStateOf(-1f)
    private var vehicleRssi by mutableStateOf(0)
    private var vehicleNameOnWatch by mutableStateOf("Xe Tsmartkey")

    // Lọc mẫu cho cảm biến gia tốc
    private var lastX = 0f
    private var lastY = 0f
    private var lastZ = 0f
    private var isFirstSample = true
    private var isSensorRegistered = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 1. Tải cấu hình từ SharedPreferences
        val prefs = getSharedPreferences("WEAR_PREF", Context.MODE_PRIVATE)
        isAutoStartEnabled = prefs.getBoolean("AUTO_START", false)
        isGestureEnabled = prefs.getBoolean("GESTURE_START", true)
        gestureActionMode = prefs.getInt("GESTURE_ACTION_MODE", 0) // Mặc định 0: Chỉ đề khi xe bật
        gestureSensitivity = prefs.getInt("GESTURE_SENSITIVITY", 1) // Mặc định 1: Vừa

        // 2. Khởi tạo SensorManager an toàn ngay trong onCreate (trước khi Compose hiển thị)
        sensorManager = getSystemService(Context.SENSOR_SERVICE) as? SensorManager
        accelSensor = sensorManager?.getDefaultSensor(Sensor.TYPE_LINEAR_ACCELERATION)
            ?: sensorManager?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)

        // 3. Thiết lập giao diện Compose
        setContent {
            WearMainScreen()
        }

        // 4. Gửi ping tới điện thoại để đồng bộ tức thì khi mở app
        sendCommandToPhone(data = "ping", path = "/ping")
    }

    @OptIn(ExperimentalFoundationApi::class)
    @Composable
    private fun WearMainScreen() {
        var isPhoneConnected by remember { mutableStateOf(false) }
        val pagerState = rememberPagerState(pageCount = { 3 })
        val pageIndicatorState = remember {
            object : PageIndicatorState {
                override val pageCount: Int get() = 3
                override val pageOffset: Float get() = pagerState.currentPageOffsetFraction.coerceIn(0f, 1f)
                override val selectedPage: Int get() = pagerState.currentPage
            }
        }
        val scope = rememberCoroutineScope()

        // Định kỳ kiểm tra kết nối với điện thoại và ping lấy trạng thái xe tức thời
        LaunchedEffect(Unit) {
            while (true) {
                Wearable.getNodeClient(this@MainActivity).connectedNodes.addOnSuccessListener { nodes ->
                    isPhoneConnected = nodes.isNotEmpty()
                    if (!isPhoneConnected) {
                        statusText = "MẤT KẾT NỐI ĐT"
                        isEspConnected = false
                    } else {
                        // Điện thoại đã kết nối, ping lấy trạng thái ESP32 mới nhất
                        sendCommandToPhone(data = "ping", path = "/ping")
                    }
                }
                delay(3000)
            }
        }

        MaterialTheme {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black)
            ) {
                HorizontalPager(
                    state = pagerState,
                    modifier = Modifier.fillMaxSize()
                ) { page ->
                    when (page) {
                        0 -> MainControlScreen(
                            statusText = statusText,
                            isPhoneConnected = isPhoneConnected,
                            isEspConnected = isEspConnected,
                            isVehicleOn = isVehicleOn,
                            isEngineStarting = isEngineStarting,
                            onTogglePower = {
                                val nextCmd = if (isVehicleOn) "0" else "1"
                                onSendCommandWithAutoStart(nextCmd, scope)
                            },
                            onEngineStart = {
                                triggerEngineStart()
                            }
                        )
                        1 -> AdvancedScreen(
                            isPhoneConnected = isPhoneConnected,
                            isEspConnected = isEspConnected,
                            vehicleName = vehicleNameOnWatch,
                            distance = vehicleDistance,
                            rssi = vehicleRssi,
                            onSendCommand = { onSendCommandWithAutoStart(it, scope) }
                        )
                        2 -> SettingsScreen(
                            isPhoneConnected = isPhoneConnected,
                            isEspConnected = isEspConnected,
                            isAutoStartEnabled = isAutoStartEnabled,
                            isGestureEnabled = isGestureEnabled,
                            gestureMode = gestureActionMode,
                            gestureSensitivity = gestureSensitivity,
                            onAutoStartToggle = {
                                isAutoStartEnabled = it
                                getSharedPreferences("WEAR_PREF", Context.MODE_PRIVATE).edit()
                                    .putBoolean("AUTO_START", it).apply()
                            },
                            onGestureToggle = {
                                isGestureEnabled = it
                                getSharedPreferences("WEAR_PREF", Context.MODE_PRIVATE).edit()
                                    .putBoolean("GESTURE_START", it).apply()
                                registerSensorIfNeeded(force = true)
                                sendSettingsToPhone()
                            },
                            onGestureModeCycle = {
                                gestureActionMode = if (gestureActionMode == 0) 1 else 0
                                getSharedPreferences("WEAR_PREF", Context.MODE_PRIVATE).edit()
                                    .putInt("GESTURE_ACTION_MODE", gestureActionMode).apply()
                                sendSettingsToPhone()
                            },
                            onGestureSensitivityCycle = {
                                gestureSensitivity = (gestureSensitivity + 1) % 3
                                getSharedPreferences("WEAR_PREF", Context.MODE_PRIVATE).edit()
                                    .putInt("GESTURE_SENSITIVITY", gestureSensitivity).apply()
                                sendSettingsToPhone()
                            }
                        )
                    }
                }

                HorizontalPageIndicator(
                    pageIndicatorState = pageIndicatorState,
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                        .padding(bottom = 6.dp)
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        appResumeTime = System.currentTimeMillis()
        lastGestureTime = System.currentTimeMillis()
        Wearable.getDataClient(this).addListener(this)
        Wearable.getMessageClient(this).addListener(this)
        registerSensorIfNeeded()
    }

    override fun onPause() {
        super.onPause()
        Wearable.getDataClient(this).removeListener(this)
        Wearable.getMessageClient(this).removeListener(this)
        unregisterSensor()
    }

    override fun onMessageReceived(messageEvent: MessageEvent) {
        if (messageEvent.path == "/status_direct" || messageEvent.path == "/status") {
            try {
                val payload = String(messageEvent.data, Charsets.UTF_8)
                val parts = payload.split("|")
                if (parts.size >= 5) {
                    val newIsEspConnected = parts[0].toBoolean()
                    val newIsVehicleOn = parts[1].toBoolean()
                    val newDistance = parts[2].toFloatOrNull() ?: -1f
                    val newRssi = parts[3].toIntOrNull() ?: 0
                    val newVehicleName = parts[4]

                    isEspConnected = newIsEspConnected
                    isVehicleOn = newIsVehicleOn
                    vehicleDistance = newDistance
                    vehicleRssi = newRssi
                    if (newVehicleName.isNotBlank()) {
                        vehicleNameOnWatch = newVehicleName
                    }
                    statusText = if (isEspConnected) {
                        if (isVehicleOn) "XE ĐANG BẬT" else "XE ĐÃ SẴN SÀNG"
                    } else {
                        "CHƯA KẾT NỐI XE"
                    }
                    registerSensorIfNeeded()
                    Log.d(TAG, "Direct status received: ESP=$isEspConnected, ON=$isVehicleOn, Dist=$vehicleDistance, Rssi=$vehicleRssi")
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error parsing status message", e)
            }
        } else if (messageEvent.path == "/settings_sync") {
            try {
                val payload = String(messageEvent.data, Charsets.UTF_8)
                val parts = payload.split("|")
                if (parts.size >= 3) {
                    val enabled = parts[0].toBoolean()
                    val mode = parts[1].toIntOrNull() ?: 0
                    val sens = parts[2].toIntOrNull() ?: 1

                    isGestureEnabled = enabled
                    gestureActionMode = mode
                    gestureSensitivity = sens

                    getSharedPreferences("WEAR_PREF", Context.MODE_PRIVATE).edit()
                        .putBoolean("GESTURE_START", enabled)
                        .putInt("GESTURE_ACTION_MODE", mode)
                        .putInt("GESTURE_SENSITIVITY", sens)
                        .apply()

                    registerSensorIfNeeded(force = true)
                    Log.d(TAG, "Settings synced from phone: Enabled=$enabled, Mode=$mode, Sens=$sens")
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error parsing settings sync", e)
            }
        }
    }

    private fun sendSettingsToPhone() {
        val payload = "$isGestureEnabled|$gestureActionMode|$gestureSensitivity"
        sendCommandToPhone(payload, "/settings_watch")
    }

    private fun registerSensorIfNeeded(force: Boolean = false) {
        if (isGestureEnabled) {
            if (isSensorRegistered && !force) return
            val sensor = accelSensor ?: (sensorManager?.getDefaultSensor(Sensor.TYPE_LINEAR_ACCELERATION)
                ?: sensorManager?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER))
            accelSensor = sensor
            if (sensor != null) {
                sensorManager?.unregisterListener(this)
                val registered = sensorManager?.registerListener(this, sensor, SensorManager.SENSOR_DELAY_GAME) ?: false
                isSensorRegistered = registered
                Log.d(TAG, "Registered sensor for pinch: ${sensor.name}, ok=$registered")
            } else {
                Log.w(TAG, "No motion sensor available on watch!")
            }
        } else {
            unregisterSensor()
        }
    }

    private fun unregisterSensor() {
        sensorManager?.unregisterListener(this)
        isSensorRegistered = false
        isFirstSample = true
        Log.d(TAG, "Sensor unregistered")
    }

    override fun onSensorChanged(event: SensorEvent?) {
        if (!isGestureEnabled || isEngineStarting || event == null) return

        // 1. Lớp 1 - Khóa an toàn Warm-up: Bỏ qua mọi dao động trong 2.0s đầu sau khi mở app / bật sáng
        if (System.currentTimeMillis() - appResumeTime < 2000L) return

        val x = event.values[0]
        val y = event.values[1]
        val z = event.values[2]

        val magnitude: Double
        val threshold: Double

        if (event.sensor.type == Sensor.TYPE_LINEAR_ACCELERATION) {
            // Gia tốc tuyến tính đã khử trọng lực
            magnitude = Math.sqrt((x * x + y * y + z * z).toDouble())
            threshold = when (gestureSensitivity) {
                0 -> 26.0 // Thấp (Chống nhầm cao - yêu cầu búng dứt khoát)
                2 -> 16.0 // Cao (Dễ nhận)
                else -> 21.0 // Vừa (Mặc định khuyên dùng)
            }
        } else {
            // Cảm biến gia tốc chuẩn: Tính delta biến thiên (jerk)
            if (isFirstSample) {
                lastX = x; lastY = y; lastZ = z
                isFirstSample = false
                return
            }
            val dx = x - lastX
            val dy = y - lastY
            val dz = z - lastZ
            lastX = x; lastY = y; lastZ = z
            magnitude = Math.sqrt((dx * dx + dy * dy + dz * dz).toDouble())
            threshold = when (gestureSensitivity) {
                0 -> 22.0 // Thấp
                2 -> 14.0 // Cao
                else -> 18.0 // Vừa
            }
        }

        if (magnitude > threshold) {
            val currentTime = System.currentTimeMillis()
            if (currentTime - lastGestureTime > 2500L) { // Cooldown 2.5s chống kích hoạt lặp
                lastGestureTime = currentTime
                Log.d(TAG, "⚡ BÚNG TAY THÀNH CÔNG! Mag: $magnitude (ngưỡng: $threshold) | VehicleOn: $isVehicleOn | Mode: $gestureActionMode")

                triggerWatchVibration()

                if (gestureActionMode == 0) {
                    // Chế độ 0 (MODE_START_ONLY - An toàn tối đa): Chỉ cho phép Đề nổ khi xe ĐÃ BẬT
                    if (isVehicleOn) {
                        Log.d(TAG, "Búng tay đề máy (Chế độ Start-only)")
                        triggerEngineStart()
                    } else {
                        Log.d(TAG, "Bỏ qua búng tay vì xe đang TẮT (Chế độ an toàn Start-only)")
                    }
                } else {
                    // Chế độ 1 (MODE_TOGGLE_AND_START): Bật xe khi tắt, Đề nổ xe khi bật
                    if (isVehicleOn) {
                        Log.d(TAG, "Búng tay đề nổ máy (Lệnh 2)")
                        triggerEngineStart()
                    } else {
                        Log.d(TAG, "Búng tay bật xe (Lệnh 1)")
                        sendCommandToPhone("1")
                    }
                }
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    private fun triggerWatchVibration() {
        try {
            val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vm = getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager
                vm?.defaultVibrator
            } else {
                @Suppress("DEPRECATION")
                getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator?.vibrate(VibrationEffect.createOneShot(120, VibrationEffect.DEFAULT_AMPLITUDE))
            } else {
                @Suppress("DEPRECATION")
                vibrator?.vibrate(120)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Vibration error", e)
        }
    }

    private fun triggerEngineStart() {
        if (!isEngineStarting && isVehicleOn) {
            isEngineStarting = true
            Log.d(TAG, "Triggering engine start via gesture / long-click")
            sendCommandToPhone("2")

            // Hiệu ứng visual 2s
            MainScope().launch {
                delay(2000)
                isEngineStarting = false
            }
        }
    }

    override fun onDataChanged(dataEvents: DataEventBuffer) {
        dataEvents.forEach { event ->
            if (event.type == DataEvent.TYPE_CHANGED && event.dataItem.uri.path == "/status") {
                val dataMap = DataMapItem.fromDataItem(event.dataItem).dataMap
                val newIsEspConnected = dataMap.getBoolean("esp_connected", false)
                val newIsVehicleOn = dataMap.getBoolean("power_on", false)
                val newDistance = dataMap.getFloat("distance", -1f)
                val newRssi = dataMap.getInt("rssi", 0)
                val newVehicleName = dataMap.getString("vehicle_name", "")

                isEspConnected = newIsEspConnected
                isVehicleOn = newIsVehicleOn
                vehicleDistance = newDistance
                vehicleRssi = newRssi
                if (newVehicleName.isNotBlank()) {
                    vehicleNameOnWatch = newVehicleName
                }
                statusText = if (isEspConnected) {
                    if (isVehicleOn) "XE ĐANG BẬT" else "XE ĐÃ SẴN SÀNG"
                } else {
                    "CHƯA KẾT NỐI XE"
                }

                registerSensorIfNeeded()
            }
        }
    }

    private fun onSendCommandWithAutoStart(cmd: String, scope: CoroutineScope) {
        if (cmd == "2") {
            isEngineStarting = true
            scope.launch {
                delay(2000)
                isEngineStarting = false
            }
        }

        sendCommandToPhone(cmd)

        // Nếu bật "Tự động đề" và lệnh gửi là "Bật xe" (1)
        if (isAutoStartEnabled && cmd == "1") {
            scope.launch {
                delay(3000) // Đợi 3s sau khi bật ACC rồi tự đề nổ
                isEngineStarting = true
                sendCommandToPhone("2")
                delay(2000)
                isEngineStarting = false
            }
        }
    }

    private fun sendCommandToPhone(data: String, path: String = "/command") {
        Log.d(TAG, "Attempting to send to $path: $data")

        Wearable.getNodeClient(this).connectedNodes
            .addOnSuccessListener { nodes ->
                Log.d(TAG, "Connected nodes: ${nodes.size}")
                if (nodes.isNotEmpty()) {
                    for (node in nodes) {
                        sendMessageToNode(node.id, node.displayName, path, data)
                    }
                } else {
                    fallbackSendCapability(path, data)
                }
            }
            .addOnFailureListener { e ->
                Log.w(TAG, "Failed getting connected nodes, fallback to capabilities", e)
                fallbackSendCapability(path, data)
            }
    }

    private fun fallbackSendCapability(path: String, data: String) {
        Wearable.getCapabilityClient(this)
            .getCapability("esp32_control", CapabilityClient.FILTER_REACHABLE)
            .addOnSuccessListener { capabilityInfo ->
                for (node in capabilityInfo.nodes) {
                    sendMessageToNode(node.id, node.displayName, path, data)
                }
            }
    }

    private fun sendMessageToNode(nodeId: String, displayName: String, path: String, data: String) {
        Log.d(TAG, "Sending to node: $displayName ($nodeId) | Path: $path")
        Wearable.getMessageClient(this).sendMessage(nodeId, path, data.toByteArray())
            .addOnSuccessListener {
                Log.d(TAG, "Success: Sent $data to $displayName via $path")
            }
            .addOnFailureListener { e ->
                Log.e(TAG, "Failed to send $data to $displayName", e)
            }
    }
}

// =========================================================================
// TAB 1: MÀN HÌNH ĐIỀU KHIỂN CHÍNH (NÚT TRÒN TRUNG TÂM BẬT/TẮT/ĐỀ MÁY)
// =========================================================================
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun MainControlScreen(
    statusText: String,
    isPhoneConnected: Boolean,
    isEspConnected: Boolean,
    isVehicleOn: Boolean,
    isEngineStarting: Boolean,
    onTogglePower: () -> Unit,
    onEngineStart: () -> Unit
) {
    val haptic = LocalHapticFeedback.current

    val bgColor by animateColorAsState(
        targetValue = when {
            isEngineStarting -> Color(0xFF331F00) // Màu nâu cam khi đề
            !isPhoneConnected || !isEspConnected -> Color.Black
            isVehicleOn -> Color(0xFF330804) // Màu đỏ rượu khi bật điện
            else -> Color(0xFF021633) // Xanh dương thẫm khi khóa an toàn
        },
        label = "bg"
    )

    val glowColor by animateColorAsState(
        targetValue = when {
            isEngineStarting -> Color(0xFFFFD600) // Vàng Neon khi đề
            !isPhoneConnected || !isEspConnected -> Color.Transparent
            isVehicleOn -> Color(0xFFFF3B30) // Đỏ Neon khi bật
            else -> Color(0xFF007AFF) // Xanh Neon khi sẵn sàng
        },
        label = "glow"
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(bgColor),
        contentAlignment = Alignment.Center
    ) {
        // Vòng phát sáng Gradient hướng tâm
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    Brush.radialGradient(
                        listOf(glowColor.copy(alpha = 0.5f), Color.Transparent),
                        radius = 420f
                    )
                )
        )

        // NÚT ĐIỀU KHIỂN CHÍNH DUY NHẤT: BẬT / TẮT (Chạm) & ĐỀ NỔ (Giữ)
        Box(
            modifier = Modifier
                .size(152.dp)
                .clip(CircleShape)
                .background(Color(0xFF141416).copy(alpha = 0.85f))
                .combinedClickable(
                    onClick = {
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        onTogglePower()
                    },
                    onLongClick = {
                        if (isVehicleOn) {
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            onEngineStart()
                        }
                    }
                ),
            contentAlignment = Alignment.Center
        ) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center
            ) {
                Text(
                    text = if (isEngineStarting) "ĐANG ĐỀ MÁY..." else statusText,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold,
                    color = when {
                        isEngineStarting -> Color(0xFFFFD600)
                        isEspConnected -> Color(0xFF34C759)
                        else -> Color(0xFF9E9E9E)
                    }
                )

                Spacer(modifier = Modifier.height(10.dp))

                Icon(
                    painter = painterResource(id = if (isEngineStarting) R.drawable.ic_engine else R.drawable.ic_power),
                    contentDescription = if (isVehicleOn) "Tắt xe" else "Bật xe",
                    modifier = Modifier.size(76.dp),
                    tint = when {
                        isEngineStarting -> Color(0xFFFFD600)
                        isVehicleOn -> Color(0xFFFF3B30)
                        isEspConnected -> Color(0xFF007AFF)
                        else -> Color.White.copy(alpha = 0.6f)
                    }
                )

                Spacer(modifier = Modifier.height(10.dp))

                Text(
                    text = when {
                        isEngineStarting -> "VUI LÒNG ĐỢI"
                        isVehicleOn -> "CHẠM: TẮT • GIỮ: ĐỀ"
                        else -> "CHẠM ĐỂ BẬT XE"
                    },
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Medium,
                    color = Color.White.copy(alpha = 0.75f)
                )
            }
        }
    }
}

// =========================================================================
// TAB 2: MÀN HÌNH TÌM XE & RADAR HIỂN THỊ KHOẢNG CÁCH THỰC TẾ
// =========================================================================
@Composable
fun AdvancedScreen(
    isPhoneConnected: Boolean,
    isEspConnected: Boolean,
    vehicleName: String,
    distance: Float,
    rssi: Int,
    onSendCommand: (String) -> Unit
) {
    val haptic = LocalHapticFeedback.current
    val scrollState = rememberScalingLazyListState()

    ScalingLazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black),
        state = scrollState,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        item {
            Text(
                text = "RADAR TÌM XE",
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                color = Color(0xFFFFD600), // Gold
                letterSpacing = 1.sp
            )
        }

        item {
            Text(
                text = vehicleName,
                fontSize = 13.sp,
                fontWeight = FontWeight.Bold,
                color = Color.White,
                maxLines = 1
            )
            Spacer(modifier = Modifier.height(4.dp))
        }

        // Card hiển thị khoảng cách và tín hiệu RSSI
        item {
            Box(
                modifier = Modifier
                    .fillMaxWidth(0.92f)
                    .clip(RoundedCornerShape(14.dp))
                    .background(Color(0xFF161618))
                    .padding(vertical = 8.dp, horizontal = 12.dp),
                contentAlignment = Alignment.Center
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    if (isEspConnected && distance > 0f) {
                        Text(
                            text = String.format(Locale.US, "%.1f m", distance),
                            fontSize = 22.sp,
                            fontWeight = FontWeight.ExtraBold,
                            color = Color(0xFF34C759) // Emerald Green
                        )
                        Text(
                            text = "Tín hiệu: $rssi dBm",
                            fontSize = 10.sp,
                            color = Color(0xFF9E9E9E)
                        )
                    } else if (!isPhoneConnected) {
                        Text(
                            text = "-- m",
                            fontSize = 20.sp,
                            fontWeight = FontWeight.Bold,
                            color = Color(0xFF9E9E9E)
                        )
                        Text(
                            text = "Mất kết nối ĐT",
                            fontSize = 10.sp,
                            color = Color(0xFFFF3B30)
                        )
                    } else {
                        Text(
                            text = "-- m",
                            fontSize = 20.sp,
                            fontWeight = FontWeight.Bold,
                            color = Color(0xFF9E9E9E)
                        )
                        Text(
                            text = "Chưa kết nối xe",
                            fontSize = 10.sp,
                            color = Color(0xFFFFCC00)
                        )
                    }
                }
            }
            Spacer(modifier = Modifier.height(8.dp))
        }

        // Nút Tìm xe kích hoạt còi và xi-nhan (Lệnh "3")
        item {
            Button(
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    if (isPhoneConnected) {
                        onSendCommand("3")
                    }
                },
                modifier = Modifier
                    .fillMaxWidth(0.92f)
                    .height(44.dp),
                colors = ButtonDefaults.buttonColors(
                    backgroundColor = Color(0xFFE5A919), // Gold Amber
                    contentColor = Color.Black
                ),
                shape = RoundedCornerShape(22.dp)
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.Center
                ) {
                    Icon(
                        painter = painterResource(id = R.drawable.ic_auto),
                        contentDescription = "Tìm xe",
                        modifier = Modifier.size(20.dp),
                        tint = Color.Black
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = "TÌM XE (CÒI/ĐÈN)",
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Bold,
                        color = Color.Black
                    )
                }
            }
        }
    }
}

// =========================================================================
// TAB 3: MÀN HÌNH CÀI ĐẶT & TRẠNG THÁI HỆ THỐNG
// =========================================================================
@Composable
fun SettingsScreen(
    isPhoneConnected: Boolean,
    isEspConnected: Boolean,
    isAutoStartEnabled: Boolean,
    isGestureEnabled: Boolean,
    gestureMode: Int,
    gestureSensitivity: Int,
    onAutoStartToggle: (Boolean) -> Unit,
    onGestureToggle: (Boolean) -> Unit,
    onGestureModeCycle: () -> Unit,
    onGestureSensitivityCycle: () -> Unit
) {
    val scrollState = rememberScalingLazyListState()

    ScalingLazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black),
        state = scrollState,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        item {
            Text(
                "CÀI ĐẶT & STATUS",
                modifier = Modifier.padding(bottom = 6.dp),
                fontSize = 12.sp,
                fontWeight = FontWeight.Bold,
                color = Color.White
            )
        }

        item {
            ToggleSettingItem(
                label = "Tự động đề máy",
                checked = isAutoStartEnabled,
                onCheckedChange = onAutoStartToggle
            )
        }

        item {
            ToggleSettingItem(
                label = "Búng tay đề máy",
                checked = isGestureEnabled,
                onCheckedChange = onGestureToggle
            )
        }

        if (isGestureEnabled) {
            item {
                ClickableSettingItem(
                    title = "Chế độ cử chỉ",
                    value = if (gestureMode == 0) "Chỉ đề khi bật" else "Bật & Đề xe",
                    onClick = onGestureModeCycle
                )
            }

            item {
                ClickableSettingItem(
                    title = "Độ nhạy búng tay",
                    value = when (gestureSensitivity) {
                        0 -> "Thấp (26 m/s²)"
                        2 -> "Cao (16 m/s²)"
                        else -> "Vừa (21 m/s²)"
                    },
                    onClick = onGestureSensitivityCycle
                )
            }
        }

        item { Spacer(modifier = Modifier.height(6.dp)) }

        item { StatusItem("Điện thoại", isPhoneConnected) }
        item { StatusItem("Xe (tsmart)", isEspConnected) }

        item {
            Text(
                "Version 1.6.0",
                modifier = Modifier.padding(top = 6.dp),
                fontSize = 9.sp,
                color = Color.Gray
            )
        }
    }
}

@Composable
fun ClickableSettingItem(title: String, value: String, onClick: () -> Unit) {
    Chip(
        onClick = onClick,
        label = {
            Column {
                Text(title, fontSize = 9.sp, color = Color.Gray)
                Text(value, fontSize = 11.sp, fontWeight = FontWeight.Bold, color = Color(0xFFFFD600))
            }
        },
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 4.dp, vertical = 2.dp),
        colors = ChipDefaults.chipColors(
            backgroundColor = Color(0xFF1C1C1E)
        )
    )
}

@Composable
fun ToggleSettingItem(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    ToggleChip(
        checked = checked,
        onCheckedChange = onCheckedChange,
        label = { Text(label, fontSize = 10.sp) },
        toggleControl = {
            Checkbox(
                checked = checked,
                enabled = true
            )
        },
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 4.dp),
        colors = ToggleChipDefaults.toggleChipColors(
            checkedStartBackgroundColor = Color(0xFF1C1C1E),
            checkedEndBackgroundColor = Color(0xFF1C1C1E),
            uncheckedStartBackgroundColor = Color(0xFF1C1C1E),
            uncheckedEndBackgroundColor = Color(0xFF1C1C1E)
        )
    )
}

@Composable
fun StatusItem(label: String, connected: Boolean) {
    Row(
        modifier = Modifier
            .fillMaxWidth(0.9f)
            .padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, fontSize = 10.sp, color = Color.White)
        Text(
            if (connected) "OK" else "LỖI",
            fontSize = 10.sp,
            fontWeight = FontWeight.Bold,
            color = if (connected) Color(0xFF34C759) else Color(0xFFFF3B30)
        )
    }
}
