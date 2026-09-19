package com.example.control_esp.wear

import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.*
import androidx.wear.compose.foundation.pager.HorizontalPager
import androidx.wear.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.semantics.*
import androidx.wear.compose.material.*
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import com.google.android.gms.wearable.*

class MainActivity : ComponentActivity(), DataClient.OnDataChangedListener, SensorEventListener {

    private var sensorManager: SensorManager? = null
    private var accelSensor: Sensor? = null
    private var lastGestureTime = 0L

    private var isEspConnected by mutableStateOf(false)
    private var isVehicleOn by mutableStateOf(false)
    private var isEngineStarting by mutableStateOf(false)
    private var isAutoStartEnabled by mutableStateOf(false)
    private var isGestureEnabled by mutableStateOf(true) // Cho phép búng tay đề máy
    private var engineStartCount by mutableStateOf(0) // Đếm số lần đề máy
    private var statusText by mutableStateOf("ĐANG KHỞI TẠO")

    @OptIn(ExperimentalFoundationApi::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            var isPhoneConnected by remember { mutableStateOf(false) }
            val pagerState = rememberPagerState(pageCount = { 3 })
            val pageIndicatorState = remember {
                object : PageIndicatorState {
                    override val pageCount: Int get() = pagerState.pageCount
                    override val pageOffset: Float get() = pagerState.currentPageOffsetFraction
                    override val selectedPage: Int get() = pagerState.currentPage
                }
            }

            LaunchedEffect(Unit) {
                // Load settings
                val prefs = getSharedPreferences("WEAR_PREF", MODE_PRIVATE)
                isAutoStartEnabled = prefs.getBoolean("AUTO_START", false)
                isGestureEnabled = prefs.getBoolean("GESTURE_START", true)

                // Init sensors
                sensorManager = getSystemService(SENSOR_SERVICE) as SensorManager
                accelSensor = sensorManager?.getDefaultSensor(Sensor.TYPE_LINEAR_ACCELERATION)

                // Gửi Ping khi mở app để kiểm tra kết nối
                sendCommandToPhone(data = "ping", path = "/ping")

                while(true) {
                    Wearable.getNodeClient(this@MainActivity).connectedNodes.addOnSuccessListener { nodes ->
                        isPhoneConnected = nodes.isNotEmpty()
                        if (!isPhoneConnected) {
                            statusText = "MẤT KẾT NỐI ĐT"
                            isEspConnected = false
                        } else if (!isEspConnected) {
                            statusText = "CHƯA KẾT NỐI XE"
                        }
                    }
                    kotlinx.coroutines.delay(5000)
                }
            }

            val scope = rememberCoroutineScope()
            MaterialTheme {
                // Sử dụng duy nhất Box và Button chuẩn để tránh xung đột hệ thống
                Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {
                    HorizontalPager(state = pagerState) { page ->
                        when (page) {
                            0 -> MainControlScreen(
                                statusText = statusText,
                                isPhoneConnected = isPhoneConnected,
                                isEspConnected = isEspConnected,
                                isVehicleOn = isVehicleOn,
                                isEngineStarting = isEngineStarting,
                                onSendCommand = { cmd ->
                                    onSendCommandWithAutoStart(cmd, scope)
                                },
                                onLongClick = {
                                    triggerEngineStart()
                                }
                            )
                            1 -> AdvancedScreen(isPhoneConnected, isEspConnected) { onSendCommandWithAutoStart(it, scope) }
                            2 -> SettingsScreen(
                                isPhoneConnected = isPhoneConnected,
                                isEspConnected = isEspConnected,
                                isAutoStartEnabled = isAutoStartEnabled,
                                isGestureEnabled = isGestureEnabled,
                                onAutoStartToggle = {
                                    isAutoStartEnabled = it
                                    getSharedPreferences("WEAR_PREF", MODE_PRIVATE).edit()
                                        .putBoolean("AUTO_START", it).apply()
                                },
                                onGestureToggle = {
                                    isGestureEnabled = it
                                    getSharedPreferences("WEAR_PREF", MODE_PRIVATE).edit()
                                        .putBoolean("GESTURE_START", it).apply()
                                }
                            )
                        }
                    }
                    
                    HorizontalPageIndicator(
                        pageIndicatorState = pageIndicatorState,
                        modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 6.dp)
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        Wearable.getDataClient(this).addListener(this)
        registerSensorIfNeeded()
    }

    override fun onPause() {
        super.onPause()
        Wearable.getDataClient(this).removeListener(this)
        unregisterSensor()
    }

    private fun registerSensorIfNeeded() {
        if (isGestureEnabled && isVehicleOn && engineStartCount < 2) {
            accelSensor?.let {
                sensorManager?.registerListener(this, it, SensorManager.SENSOR_DELAY_GAME)
                Log.d("WEAR_GESTURE", "Sensor registered")
            }
        } else {
            unregisterSensor()
        }
    }

    private fun unregisterSensor() {
        sensorManager?.unregisterListener(this)
        Log.d("WEAR_GESTURE", "Sensor unregistered")
    }

    override fun onSensorChanged(event: SensorEvent?) {
        if (!isGestureEnabled || !isVehicleOn || isEngineStarting || event?.sensor?.type != Sensor.TYPE_LINEAR_ACCELERATION) return

        val x = event.values[0]
        val y = event.values[1]
        val z = event.values[2]
        val magnitude = Math.sqrt((x * x + y * y + z * z).toDouble())

        // Ngưỡng phát hiện búng tay (Pinch) - Độ nhạy 18-20 m/s^2
        if (magnitude > 18.0) {
            val currentTime = System.currentTimeMillis()
            if (currentTime - lastGestureTime > 2000) { // Cooldown 2s
                lastGestureTime = currentTime
                Log.d("WEAR_GESTURE", "Pinch detected! Magnitude: $magnitude")
                // Kích hoạt ĐỀ MÁY (Lệnh 2)
                triggerEngineStart()
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    private fun triggerEngineStart() {
        // Hàm này sẽ được gọi từ Sensor hoặc Long Click
        if (!isEngineStarting && isVehicleOn) {
            if (engineStartCount >= 2) {
                Log.w("WEAR_GESTURE", "Limit reached. Killing sensor.")
                unregisterSensor() // Tắt ngay lập tức để tiết kiệm pin
                return
            }

            isEngineStarting = true
            engineStartCount++
            Log.d("WEAR_GESTURE", "Triggering engine start. Count: $engineStartCount")
            
            // Nếu đây là lần thứ 2, tắt sensor ngay sau lệnh này
            if (engineStartCount >= 2) {
                Log.d("WEAR_GESTURE", "2nd start successful. Sensor unregistered.")
                unregisterSensor()
            }

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

                // Reset số lần đề khi xe thay đổi trạng thái (Tắt đi bật lại)
                if (newIsVehicleOn != isVehicleOn) {
                    engineStartCount = 0
                }

                isEspConnected = newIsEspConnected
                isVehicleOn = newIsVehicleOn
                statusText = if (isEspConnected) "XE ĐÃ SẴN SÀNG" else "CHƯA KẾT NỐI XE"
                
                // Cập nhật lại sensor khi trạng thái xe thay đổi
                registerSensorIfNeeded()
            }
        }
    }

    private fun onSendCommandWithAutoStart(cmd: String, scope: kotlinx.coroutines.CoroutineScope) {
        if (cmd == "2") {
            isEngineStarting = true
            scope.launch {
                delay(2000)
                isEngineStarting = false
            }
        }
        
        sendCommandToPhone(cmd)
        // Nếu bật "Tự động đề" và lệnh là "Bật xe" (1)
        if (isAutoStartEnabled && cmd == "1") {
            scope.launch {
                delay(3000) // Đợi 3s rồi đề máy
                isEngineStarting = true
                sendCommandToPhone("2")
                delay(2000)
                isEngineStarting = false
            }
        }
    }

    private fun sendCommandToPhone(data: String, path: String = "/command") {
        Log.d("WEAR_BRIDGE", "Attempting to send to $path: $data")
        
        // Sử dụng CapabilityClient để tìm đúng thiết bị có khả năng điều khiển ESP32
        Wearable.getCapabilityClient(this)
            .getCapability("esp32_control", CapabilityClient.FILTER_REACHABLE)
            .addOnSuccessListener { capabilityInfo ->
                val nodes = capabilityInfo.nodes
                Log.d("WEAR_BRIDGE", "Capability nodes found: ${nodes.size}")
                
                if (nodes.isEmpty()) {
                    Log.w("WEAR_BRIDGE", "No nodes with 'esp32_control' capability. Falling back to all nodes.")
                    fallbackSendAllNodes(path, data)
                } else {
                    for (node in nodes) {
                        sendMessageToNode(node.id, node.displayName, path, data)
                    }
                }
            }
            .addOnFailureListener { e ->
                Log.e("WEAR_BRIDGE", "Failed to get capabilities", e)
                fallbackSendAllNodes(path, data)
            }
    }

    private fun fallbackSendAllNodes(path: String, data: String) {
        Wearable.getNodeClient(this).connectedNodes.addOnSuccessListener { nodes ->
            Log.d("WEAR_BRIDGE", "Fallback connected nodes: ${nodes.size}")
            for (node in nodes) {
                sendMessageToNode(node.id, node.displayName, path, data)
            }
        }
    }

    private fun sendMessageToNode(nodeId: String, displayName: String, path: String, data: String) {
        Log.d("WEAR_BRIDGE", "Sending to node: $displayName ($nodeId) | Path: $path")
        Wearable.getMessageClient(this).sendMessage(nodeId, path, data.toByteArray())
            .addOnSuccessListener { 
                Log.d("WEAR_BRIDGE", "Success: Sent $data to $displayName via $path")
            }
            .addOnFailureListener { e -> 
                Log.e("WEAR_BRIDGE", "Failed to send $data to $displayName", e)
            }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun MainControlScreen(
    statusText: String,
    isPhoneConnected: Boolean,
    isEspConnected: Boolean,
    isVehicleOn: Boolean,
    isEngineStarting: Boolean,
    onSendCommand: (String) -> Unit,
    onLongClick: () -> Unit
) {
    val haptic = LocalHapticFeedback.current
    val focusRequester = remember { FocusRequester() }
    
    // Tự động yêu cầu focus khi màn hình hiển thị để hỗ trợ cử chỉ hệ thống
    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
    }

    val bgColor by animateColorAsState(
        targetValue = when {
            isEngineStarting -> Color(0xFF331F00) // Màu nâu cam khi đề
            !isPhoneConnected || !isEspConnected -> Color.Black
            isVehicleOn -> Color(0xFF330804)
            else -> Color(0xFF021633)
        }, label = "bg"
    )
    val glowColor by animateColorAsState(
        targetValue = when {
            isEngineStarting -> Color(0xFFFFD600) // Vàng khi đề
            !isPhoneConnected || !isEspConnected -> Color.Transparent
            isVehicleOn -> Color(0xFFFF3B30)
            else -> Color(0xFF007AFF)
        }, label = "glow"
    )

    Box(modifier = Modifier.fillMaxSize().background(bgColor), contentAlignment = Alignment.Center) {
        Box(modifier = Modifier.fillMaxSize().background(
            Brush.radialGradient(listOf(glowColor.copy(alpha = 0.5f), Color.Transparent), radius = 400f)
        ))

        // SỬ DỤNG BUTTON CHUẨN ĐỂ SAMSUNG NHẬN DIỆN DOUBLE PINCH
        Button(
            onClick = {
                Log.d("WEAR_GESTURE", "System Double Pinch Detected!")
                if (isPhoneConnected) {
                    onSendCommand(if (isVehicleOn) "0" else "1")
                }
            },
            modifier = Modifier
                .size(160.dp)
                .focusRequester(focusRequester)
                .focusable()
                .semantics {
                    role = Role.Button
                    contentDescription = if (isVehicleOn) "Tắt xe" else "Bật xe"
                    // Gán hành động click rõ ràng vào Semantics cho Wear OS 5
                    onClick(label = "Kích hoạt hành động chính") {
                        if (isPhoneConnected) {
                            onSendCommand(if (isVehicleOn) "0" else "1")
                            true
                        } else false
                    }
                },
            colors = ButtonDefaults.buttonColors(
                backgroundColor = Color.Transparent,
                contentColor = Color.White
            ),
            shape = CircleShape
        ) {
            // Lớp xử lý Chạm (Touch) và Nhấn giữ (Long Click) vật lý
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .combinedClickable(
                        onClick = {
                            Log.d("WEAR_GESTURE", "Manual Touch Detected")
                            if (isPhoneConnected) {
                                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                onSendCommand(if (isVehicleOn) "0" else "1")
                            }
                        },
                        onLongClick = {
                            Log.d("WEAR_GESTURE", "Manual Long Click Detected")
                            if (isPhoneConnected && isVehicleOn) {
                                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                onLongClick()
                            }
                        }
                    ),
                contentAlignment = Alignment.Center
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
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
                    Spacer(modifier = Modifier.height(12.dp))
                    Icon(
                        painter = painterResource(id = if (isEngineStarting) R.drawable.ic_engine else R.drawable.ic_power),
                        contentDescription = null,
                        modifier = Modifier.size(80.dp),
                        tint = Color.White
                    )
                    Spacer(modifier = Modifier.height(12.dp))
                    Text(
                        text = when {
                            isEngineStarting -> "VUI LÒNG ĐỢI"
                            isVehicleOn -> "GIỮ ĐỀ MÁY"
                            else -> "BẬT XE"
                        },
                        fontSize = 10.sp,
                        color = Color.White.copy(alpha = 0.7f)
                    )
                }
            }
        }
    }
}

@Composable
fun AdvancedScreen(isPhoneConnected: Boolean, isEspConnected: Boolean, onSendCommand: (String) -> Unit) {
    Column(
        modifier = Modifier.fillMaxSize().background(Color.Black).padding(20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text("TÍNH NĂNG KHÁC", fontSize = 12.sp, fontWeight = FontWeight.Bold, color = Color.White)
        Spacer(modifier = Modifier.height(20.dp))
        Button(
            onClick = { 
                if (isPhoneConnected) {
                    onSendCommand("3")
                } else {
                    // Toast will be shown by sendCommandToPhone if nodes is empty, 
                    // but here we can add extra check if needed.
                }
            },
            modifier = Modifier.fillMaxWidth(),
            colors = ButtonDefaults.buttonColors(backgroundColor = Color(0xFF1C1C1E))
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(painter = painterResource(id = R.drawable.ic_auto), contentDescription = null, modifier = Modifier.size(24.dp))
                Spacer(modifier = Modifier.width(8.dp))
                Text("TÌM XE / AUTO")
            }
        }
    }
}

@Composable
fun SettingsScreen(
    isPhoneConnected: Boolean,
    isEspConnected: Boolean,
    isAutoStartEnabled: Boolean,
    isGestureEnabled: Boolean,
    onAutoStartToggle: (Boolean) -> Unit,
    onGestureToggle: (Boolean) -> Unit
) {
    val scrollState = rememberScalingLazyListState()

    ScalingLazyColumn(
        modifier = Modifier.fillMaxSize().background(Color.Black),
        state = scrollState,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        item {
            Text(
                "CÀI ĐẶT & STATUS",
                modifier = Modifier.padding(bottom = 8.dp),
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

        item { Spacer(modifier = Modifier.height(8.dp)) }
        
        item { StatusItem("Điện thoại", isPhoneConnected) }
        item { StatusItem("Xe (tsmart)", isEspConnected) }
        
        item {
            Text(
                "Version 1.5.0",
                modifier = Modifier.padding(top = 8.dp),
                fontSize = 9.sp,
                color = Color.Gray
            )
        }
    }
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
                enabled = true,
                modifier = Modifier.semantics {
                    this.contentDescription = if (checked) "Đã chọn" else "Chưa chọn"
                }
            )
        },
        modifier = Modifier.fillMaxWidth().padding(horizontal = 4.dp),
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
    Row(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, fontSize = 10.sp, color = Color.White)
        Text(if (connected) "OK" else "LỖI", fontSize = 10.sp, color = if (connected) Color.Green else Color.Red)
    }
}
