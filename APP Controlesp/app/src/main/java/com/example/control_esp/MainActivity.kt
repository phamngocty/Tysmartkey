package com.example.control_esp

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import android.view.animation.AlphaAnimation
import android.view.animation.Animation
import android.graphics.Bitmap
import android.util.Base64
import android.widget.*
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SwitchCompat
import androidx.cardview.widget.CardView
import androidx.core.content.ContextCompat
import com.google.android.gms.wearable.*
import com.google.android.material.bottomnavigation.BottomNavigationView
import java.util.*

data class FingerprintItem(val id: Int, var name: String)

class MainActivity : AppCompatActivity() {

    private var bluetoothAdapter: BluetoothAdapter? = null
    private var isOn = false
    private var isConnecting = false
    private var isBleMode = true
    private var autoStartDelay = 3
    private var isAutoStartEnabled = false
    private var isAutoConnectEnabled = false
    private var isShowInfoEnabled = true
    private var isHideNavEnabled = false
    private var vehicleCustomName = "Honda SH 150i"
    private var tvTitleRef: TextView? = null

    private val handler = Handler(Looper.getMainLooper())
    private var pendingPermissionAction: (() -> Unit)? = null

    // Vehicle Telemetry & Twin UI references
    private var tvBikeStatusPill: TextView? = null
    private var ivBikeSilhouette: ImageView? = null
    private var viewHeadlightGlow: View? = null
    private var viewTurnSignalFront: View? = null
    private var viewTurnSignalRear: View? = null
    private var tvVoltageVal: TextView? = null
    private var tvBleRssiVal: TextView? = null
    private var tvTempVal: TextView? = null
    private var viewConnectionDot: View? = null

    // Radar Finder Tab 2 references
    private var tvFinderDistance: TextView? = null
    private var tvFinderRssiDesc: TextView? = null
    private var cbAutoStartTab2: androidx.appcompat.widget.SwitchCompat? = null
    private var cbAutoConnectTab2: androidx.appcompat.widget.SwitchCompat? = null

    // Hero Action references
    private var btnStartHero: View? = null
    private var tvEngineTitle: TextView? = null
    private var tvEngineDesc: TextView? = null
    private var tvEngineBadge: TextView? = null
    private var ivEngineIcon: ImageView? = null

    private var btnFindHero: View? = null
    private var tvFindTitle: TextView? = null
    private var tvFindDesc: TextView? = null
    private var tvFindBadge: TextView? = null
    private var ivFindIcon: ImageView? = null

    private var isEngineRunning = false
    private val rssiPollRunnable = object : Runnable {
        override fun run() {
            if (isConnectedToVehicle() && isBleMode) {
                BleManager.readRssi()
            }
            handler.postDelayed(this, 3000)
        }
    }

    // Quản lý danh sách vân tay
    private val fingerprintList = mutableListOf<FingerprintItem>()
    private var enrollCustomDialog: AlertDialog? = null
    private var enrollCountDownTimer: android.os.CountDownTimer? = null
    private var tvEnrollStepDescRef: TextView? = null
    private var pbEnrollTimerRef: ProgressBar? = null
    private var tvEnrollTimerTextRef: TextView? = null
    private var tvEnrollFingerNameRef: TextView? = null
    private var pbEnrollProgressRef: ProgressBar? = null
    private var tvEnrollTouchCountRef: TextView? = null
    private var tvEnrollPercentRef: TextView? = null
    private var currentEnrollName: String = "Vân tay mới"
    private var currentEnrollSendImage: Boolean = false
    private var cvEnrollImageCardRef: CardView? = null
    private var ivEnrollFingerprintImageRef: ImageView? = null
    private var enrollImageBuffer = StringBuilder()

    // Quản lý Chế độ Chống Nước Mưa (Anti-Rain Mode)
    private var isRainEnabled = false
    private var rainTouchHoldMs = 500
    private var rainMaxWrong = 5
    private var rainCooldownSec = 30
    private var rainAutoOffSec = 3600
    private var rainRemainingSec = 0
    private var swRainQuickRef: androidx.appcompat.widget.SwitchCompat? = null
    private var tvRainStatusSubtitleRef: TextView? = null
    private var tvFpSensorStatus: TextView? = null
    private var rainCountDownTimer: android.os.CountDownTimer? = null

    // Quản lý Tinh chỉnh Cảm biến Vân tay (Fingerprint Fine-Tuning)
    private var fpSettingsDialog: AlertDialog? = null
    private var fpSecLevelVal = 2
    private var fpScanTimeVal = 1200
    private var fpEnrollModeVal = 4
    private var fpSendImageVal = false
    private var swFpSendImageRef: SwitchCompat? = null
    private var tvFpTestStatusRef: TextView? = null
    private var tvFpSecLevelValRef: TextView? = null
    private var tvFpSecLevelDescRef: TextView? = null
    private var sbFpSecLevelRef: SeekBar? = null
    private var tvFpScanTimeValRef: TextView? = null
    private var sbFpScanTimeRef: SeekBar? = null
    private var rgFpEnrollModeRef: RadioGroup? = null
    private var rbEnroll4TouchRef: RadioButton? = null
    private var rbEnroll2TouchRef: RadioButton? = null


    // Quản lý tự động kết nối lại khi xe lại gần
    private var isManualDisconnect = false
    private val reconnectRunnable = Runnable {
        if (!isConnectedToVehicle() && isAutoConnectEnabled && !isManualDisconnect) {
            val currentMac = if (isBleMode) BleManager.activeMac else BluetoothController.deviceMac
            if (currentMac != null) {
                val tvStatus = findViewById<TextView>(R.id.tvStatus)
                tvStatus?.text = "Đang tự động kết nối lại..."
                withBluetoothPermission { connectVehicle() }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        bluetoothAdapter = (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

        if (bluetoothAdapter == null) {
            Toast.makeText(this, "Thiết bị không hỗ trợ Bluetooth", Toast.LENGTH_LONG).show()
            return
        }

        // Khởi tạo BleManager
        BleManager.init(this)

        loadDevice()

        val btnConnect = findViewById<Button>(R.id.btnUnlock)
        val btnToggle = findViewById<Button>(R.id.btnToggle)
        val tvStatus = findViewById<TextView>(R.id.tvStatus)
        val bottomNav = findViewById<BottomNavigationView>(R.id.bottom_navigation)
        androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(bottomNav) { view, insets ->
            val navInsets = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.systemBars())
            view.setPadding(view.paddingLeft, view.paddingTop, view.paddingRight, navInsets.bottom)
            insets
        }

        // Bind vehicle digital twin & telemetry views
        tvBikeStatusPill = findViewById(R.id.tvBikeStatusPill)
        ivBikeSilhouette = findViewById(R.id.ivBikeSilhouette)
        viewHeadlightGlow = findViewById(R.id.viewHeadlightGlow)
        viewTurnSignalFront = findViewById(R.id.viewTurnSignalFront)
        viewTurnSignalRear = findViewById(R.id.viewTurnSignalRear)
        tvVoltageVal = findViewById(R.id.tvVoltageVal)
        tvBleRssiVal = findViewById(R.id.tvBleRssiVal)
        tvTempVal = findViewById(R.id.tvTempVal)

        // Bind Hero Actions
        btnStartHero = findViewById(R.id.btnStartHero)
        tvEngineTitle = findViewById(R.id.tvEngineTitle)
        tvEngineDesc = findViewById(R.id.tvEngineDesc)
        tvEngineBadge = findViewById(R.id.tvEngineBadge)
        ivEngineIcon = findViewById(R.id.ivEngineIcon)

        btnFindHero = findViewById(R.id.btnFindHero)
        tvFindTitle = findViewById(R.id.tvFindTitle)
        tvFindDesc = findViewById(R.id.tvFindDesc)
        tvFindBadge = findViewById(R.id.tvFindBadge)
        ivFindIcon = findViewById(R.id.ivFindIcon)

        // Header Actions (Khớp 100% Web Mockup)
        viewConnectionDot = findViewById(R.id.viewConnectionDot)
        tvTitleRef = findViewById(R.id.tvTitle)
        tvTitleRef?.setOnClickListener {
            showRenameVehicleDialog()
        }
        findViewById<ImageButton>(R.id.btnHeaderHistory)?.setOnClickListener {
            showUnlockHistoryDialog()
        }
        findViewById<ImageButton>(R.id.btnHeaderConnect)?.setOnClickListener {
            if (isConnecting) return@setOnClickListener
            if (isConnectedToVehicle()) {
                disconnectVehicle()
            } else {
                withBluetoothPermission { selectDevice() }
            }
        }

        // Tab 2 Finder & Radar Controls
        tvFinderDistance = findViewById(R.id.tvFinderDistance)
        tvFinderRssiDesc = findViewById(R.id.tvFinderRssiDesc)
        cbAutoStartTab2 = findViewById(R.id.cbAutoStartTab2)
        cbAutoConnectTab2 = findViewById(R.id.cbAutoConnectTab2)
        findViewById<Button>(R.id.btnFindBikeTab2)?.setOnClickListener {
            handleFindVehicle()
        }

        val tabHome = findViewById<View>(R.id.tab_home)
        val tabAdvanced = findViewById<View>(R.id.tab_advanced)
        val tabSettings = findViewById<View>(R.id.tab_settings)
        val tabFingerprint = findViewById<View>(R.id.tab_fingerprint)

        val cbAutoConnect = findViewById<android.widget.CompoundButton>(R.id.cbAutoConnect)
        val cbAutoStart = findViewById<android.widget.CompoundButton>(R.id.cbAutoStart)
        val cbShowInfo = findViewById<android.widget.CompoundButton>(R.id.cbShowInfo)
        val cbHideNav = findViewById<android.widget.CompoundButton>(R.id.cbHideNav)
        val btnMinus = findViewById<Button>(R.id.btnMinus)
        val btnPlus = findViewById<Button>(R.id.btnPlus)
        val tvDelayValue = findViewById<TextView>(R.id.tvDelayValue)

        // Card Chống dắt (Anti-theft)
        val btnAntiTheftCard = findViewById<View>(R.id.btnAntiTheftCard)
        val tvAntiTheftStatus = findViewById<TextView>(R.id.tvAntiTheftStatus)
        val tvAntiTheftBadge = findViewById<TextView>(R.id.tvAntiTheftBadge)
        var isAntiTheftActive = false

        btnAntiTheftCard?.setOnClickListener {
            isAntiTheftActive = !isAntiTheftActive
            if (isAntiTheftActive) {
                tvAntiTheftStatus?.text = "Đang kích hoạt • Giám sát rung"
                tvAntiTheftStatus?.setTextColor(ContextCompat.getColor(this, R.color.secondary_teal))
                tvAntiTheftBadge?.text = "BẢO VỆ"
                tvAntiTheftBadge?.setTextColor(ContextCompat.getColor(this, R.color.secondary_teal))
            } else {
                tvAntiTheftStatus?.text = "Đang tắt (Chạm để bật)"
                tvAntiTheftStatus?.setTextColor(ContextCompat.getColor(this, R.color.text_muted))
                tvAntiTheftBadge?.text = "TẮT"
                tvAntiTheftBadge?.setTextColor(ContextCompat.getColor(this, R.color.text_secondary))
            }
        }

        // Các nút trong tab vân tay & cài đặt mới
        val btnAddFp = findViewById<Button>(R.id.btnAddFp)
        val btnRefreshFp = findViewById<Button>(R.id.btnRefreshFp)
        val btnClearAllFp = findViewById<Button>(R.id.btnClearAllFp)
        val btnViewHistory = findViewById<Button>(R.id.btnViewHistory)
        val btnScanBle = findViewById<Button>(R.id.btnScanBle)
        val btnChangeKey = findViewById<Button>(R.id.btnChangeKey)

        // Các view Chế độ Chống Nước Mưa
        val cardRainMode = findViewById<LinearLayout?>(R.id.cardRainMode)
        val swRainQuick = findViewById<androidx.appcompat.widget.SwitchCompat?>(R.id.swRainQuick)
        val btnRainSettings = findViewById<ImageButton?>(R.id.btnRainSettings)
        val tvRainStatusSubtitle = findViewById<TextView?>(R.id.tvRainStatusSubtitle)
        swRainQuickRef = swRainQuick
        tvRainStatusSubtitleRef = tvRainStatusSubtitle
        tvFpSensorStatus = findViewById(R.id.tvFpSensorStatus)

        swRainQuick?.setOnCheckedChangeListener { _, isChecked ->
            if (!isConnectedToVehicle()) {
                swRainQuick.isChecked = !isChecked
                return@setOnCheckedChangeListener
            }
            toggleRainMode(isChecked)
        }

        btnRainSettings?.setOnClickListener {
            showRainSettingsDialog()
        }

        cardRainMode?.setOnClickListener {
            showRainSettingsDialog()
        }


        // Init UI
        cbAutoConnect.isChecked = isAutoConnectEnabled
        cbAutoStart.isChecked = isAutoStartEnabled
        cbShowInfo.isChecked = isShowInfoEnabled
        cbHideNav.isChecked = isHideNavEnabled
        tvDelayValue.text = "${autoStartDelay}s"
        updateDeviceNameDisplay()

        btnScanBle.setOnClickListener {
            withBluetoothPermission { showBleScanDialog() }
        }

        btnChangeKey.setOnClickListener {
            if (isConnectedToVehicle()) {
                showChangeKeyDialog()
            } else {
                Toast.makeText(this, "Hãy kết nối xe trước khi đổi mã", Toast.LENGTH_SHORT).show()
            }
        }

        btnViewHistory.setOnClickListener {
            showUnlockHistoryDialog()
        }

        findViewById<View>(R.id.header).visibility = if (isShowInfoEnabled) View.VISIBLE else View.GONE
        bottomNav.visibility = if (isHideNavEnabled) View.GONE else View.VISIBLE

        tabHome.setOnLongClickListener {
            isHideNavEnabled = !isHideNavEnabled
            bottomNav.visibility = if (isHideNavEnabled) View.GONE else View.VISIBLE
            cbHideNav.isChecked = isHideNavEnabled
            saveSettings()
            true
        }

        // Tự động kết nối nếu được bật
        if (isAutoConnectEnabled && (BleManager.activeMac != null || BluetoothController.deviceMac != null)) {
            tvStatus.text = "Đang tự động kết nối..."
            withBluetoothPermission { connectVehicle() }
        }

        // Đăng ký nhận phản hồi từ cả BLE và Classic BT
        BleManager.onMessageReceived = { handleVehicleFeedback(it) }
        BluetoothController.onMessageReceived = { handleVehicleFeedback(it) }
        BleManager.onRssiRead = { rssi ->
            runOnUiThread {
                tvBleRssiVal?.text = "$rssi dBm"
                tvFinderRssiDesc?.text = "Tín hiệu Bluetooth: $rssi dBm"
                val dist = Math.pow(10.0, (-59.0 - rssi) / 20.0)
                val distClamped = Math.min(Math.max(dist, 0.5), 15.0)
                tvFinderDistance?.text = String.format(Locale.US, "Khoảng cách: ~ %.1f Mét", distClamped)
            }
        }

        BleManager.onConnectionStateChanged = { connected ->
            runOnUiThread {
                updateConnectionUI(connected)
                if (!connected && isAutoConnectEnabled && !isManualDisconnect) {
                    handler.removeCallbacks(reconnectRunnable)
                    handler.postDelayed(reconnectRunnable, 3500)
                } else if (connected) {
                    handler.removeCallbacks(reconnectRunnable)
                }
            }
        }

        // Chuyển Tab BottomNav
        bottomNav.setOnItemSelectedListener { item ->
            tabHome.visibility = if (item.itemId == R.id.nav_home) View.VISIBLE else View.GONE
            tabAdvanced.visibility = if (item.itemId == R.id.nav_advanced) View.VISIBLE else View.GONE
            tabSettings.visibility = if (item.itemId == R.id.nav_settings) View.VISIBLE else View.GONE
            tabFingerprint.visibility = if (item.itemId == R.id.nav_fingerprint) View.VISIBLE else View.GONE

            if (item.itemId == R.id.nav_fingerprint && isConnectedToVehicle()) {
                requestFingerprintList()
            }
            true
        }

        cbAutoConnect.setOnCheckedChangeListener { _, isChecked -> 
            isAutoConnectEnabled = isChecked
            if (cbAutoConnectTab2?.isChecked != isChecked) {
                cbAutoConnectTab2?.isChecked = isChecked
            }
            saveSettings() 
        }
        cbAutoStart.setOnCheckedChangeListener { _, isChecked -> 
            isAutoStartEnabled = isChecked
            if (cbAutoStartTab2?.isChecked != isChecked) {
                cbAutoStartTab2?.isChecked = isChecked
            }
            saveSettings() 
        }

        cbAutoConnectTab2?.isChecked = isAutoConnectEnabled
        cbAutoStartTab2?.isChecked = isAutoStartEnabled
        cbAutoConnectTab2?.setOnCheckedChangeListener { _, isChecked ->
            isAutoConnectEnabled = isChecked
            if (cbAutoConnect.isChecked != isChecked) {
                cbAutoConnect.isChecked = isChecked
            }
            saveSettings()
        }
        cbAutoStartTab2?.setOnCheckedChangeListener { _, isChecked ->
            isAutoStartEnabled = isChecked
            if (cbAutoStart.isChecked != isChecked) {
                cbAutoStart.isChecked = isChecked
            }
            saveSettings()
        }
        cbShowInfo.setOnCheckedChangeListener { _, isChecked ->
            isShowInfoEnabled = isChecked
            findViewById<View>(R.id.header).visibility = if (isChecked) View.VISIBLE else View.GONE
            saveSettings()
        }
        cbHideNav.setOnCheckedChangeListener { _, isChecked ->
            isHideNavEnabled = isChecked
            bottomNav.visibility = if (isChecked) View.GONE else View.VISIBLE
            saveSettings()
        }

        btnMinus.setOnClickListener { if (autoStartDelay > 1) { autoStartDelay--; tvDelayValue.text = "${autoStartDelay}s"; saveSettings() } }
        btnPlus.setOnClickListener { if (autoStartDelay < 10) { autoStartDelay++; tvDelayValue.text = "${autoStartDelay}s"; saveSettings() } }

        btnConnect.setOnClickListener {
            if (isConnecting) return@setOnClickListener

            val currentMac = if (isBleMode) BleManager.activeMac else BluetoothController.deviceMac
            val currentKey = if (isBleMode) BleManager.SECRET_KEY else BluetoothController.SECRET_KEY

            if (currentMac == null) {
                withBluetoothPermission { selectDevice() }
            } else if (currentKey.isEmpty()) {
                showKeyDialog()
            } else {
                if (isConnectedToVehicle()) {
                    disconnectVehicle()
                } else {
                    tvStatus.text = "Đang kết nối..."
                    withBluetoothPermission { connectVehicle() }
                }
            }
        }

        btnConnect.setOnLongClickListener {
            if (isConnecting) return@setOnLongClickListener true
            withBluetoothPermission { selectDevice() }
            true
        }

        btnToggle.setOnClickListener {
            if (!isConnectedToVehicle()) {
                triggerHapticFeedback()
                return@setOnClickListener
            }
            if (isOn) {
                sendVehicleCommand("0")
            } else {
                sendVehicleCommand("1")
            }
        }

        btnStartHero?.setOnClickListener {
            if (!isConnectedToVehicle()) {
                triggerHapticFeedback()
                return@setOnClickListener
            }
            if (!isOn) {
                triggerHapticFeedback()
                return@setOnClickListener
            }
            handleEngineStart()
        }

        btnFindHero?.setOnClickListener {
            if (!isConnectedToVehicle()) {
                triggerHapticFeedback()
                return@setOnClickListener
            }
            handleFindVehicle()
        }

        // Long click vào Cài đặt để đổi Secret Key
        bottomNav.findViewById<View>(R.id.nav_settings).setOnLongClickListener {
            if (isConnectedToVehicle()) {
                showChangeKeyDialog()
            }
            true
        }

        // Xử lý sự kiện Tab Vân tay
        btnAddFp.setOnClickListener {
            showAddFingerprintDialog()
        }

        btnRefreshFp.setOnClickListener {
            if (isConnectedToVehicle()) {
                requestFingerprintList()
            }
        }

        btnClearAllFp.setOnClickListener {
            if (!isConnectedToVehicle()) {
                return@setOnClickListener
            }
            AlertDialog.Builder(this)
                .setTitle("Cảnh báo")
                .setMessage("Bạn có chắc chắn muốn xóa TOÀN BỘ vân tay trên xe?")
                .setPositiveButton("Xóa tất cả") { _, _ ->
                    sendVehicleCommand("FP_CLEAR|CONFIRM")
                }
                .setNegativeButton("Hủy", null)
                .show()
        }

        val btnFpSettings = findViewById<Button>(R.id.btnFpSettings)
        btnFpSettings?.setOnClickListener {
            showFingerprintSettingsDialog()
        }

        // Đồng bộ dữ liệu từ đồng hồ Wear OS
        Wearable.getDataClient(this).addListener { dataEvents ->
            dataEvents.forEach { event ->
                if (event.type == DataEvent.TYPE_CHANGED && event.dataItem.uri.path == "/device_config") {
                    val dataMap = DataMapItem.fromDataItem(event.dataItem).dataMap
                    val name = dataMap.getString("device_name")
                    val mac = dataMap.getString("device_mac")
                    if (mac != null) {
                        Log.d("WEAR_SYNC", "Received device config from watch: $name ($mac)")
                        saveDevice(mac, BleManager.SECRET_KEY, isBleMode)
                        runOnUiThread {
                            updateDeviceNameDisplay()
                        }
                    }
                }
            }
        }
    }

    // ==========================================
    // 📡 TRUYỀN THÔNG & PHẢN HỒI (FEEDBACK)
    // ==========================================

    private fun isConnectedToVehicle(): Boolean {
        return (isBleMode && BleManager.isConnected) || (!isBleMode && BluetoothController.isConnected)
    }

    private fun sendVehicleCommand(cmd: String): Boolean {
        return if (isBleMode) {
            BleManager.sendCommand(cmd)
        } else {
            BluetoothController.sendCommand(cmd)
        }
    }

    private fun triggerHapticFeedback() {
        try {
            val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vm = getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? android.os.VibratorManager
                vm?.defaultVibrator
            } else {
                @Suppress("DEPRECATION")
                getSystemService(Context.VIBRATOR_SERVICE) as? android.os.Vibrator
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator?.vibrate(android.os.VibrationEffect.createOneShot(80, android.os.VibrationEffect.DEFAULT_AMPLITUDE))
            } else {
                @Suppress("DEPRECATION")
                vibrator?.vibrate(80)
            }
        } catch (e: Exception) {
            Log.e("HAPTIC", "Vibration error", e)
        }
    }

    private fun handleEngineStart() {
        sendVehicleCommand("2")
        triggerHapticFeedback()

        // Hoạt ảnh đề xe 1.5s (cranking vibration)
        tvEngineBadge?.text = "1.5s Đang đề..."
        tvEngineBadge?.setTextColor(getColor(R.color.accent_amber))
        tvEngineDesc?.text = "Đang quay củ đề..."
        ivEngineIcon?.setColorFilter(getColor(R.color.accent_amber))

        ivBikeSilhouette?.let { bike ->
            val shakeAnim = android.view.animation.TranslateAnimation(-4f, 4f, 0f, 0f).apply {
                duration = 50
                repeatMode = Animation.REVERSE
                repeatCount = 30 // 1.5s
            }
            bike.startAnimation(shakeAnim)
        }

        handler.postDelayed({
            isEngineRunning = true
            tvEngineBadge?.text = "1400 RPM"
            tvEngineBadge?.setTextColor(getColor(R.color.secondary_teal))
            tvEngineDesc?.text = "Động cơ đang nổ"
            ivEngineIcon?.setColorFilter(getColor(R.color.secondary_teal))
            ivBikeSilhouette?.clearAnimation()
        }, 1500)
    }

    private fun animateHazardBlink(durationMs: Long = 1200L) {
        viewTurnSignalFront?.visibility = View.VISIBLE
        viewTurnSignalRear?.visibility = View.VISIBLE

        val blinkAnim = AlphaAnimation(0.1f, 1.0f).apply {
            duration = 200
            repeatMode = Animation.REVERSE
            repeatCount = 5 // 3 nhịp chớp xi-nhan
        }
        viewTurnSignalFront?.startAnimation(blinkAnim)
        viewTurnSignalRear?.startAnimation(blinkAnim)

        handler.postDelayed({
            viewTurnSignalFront?.clearAnimation()
            viewTurnSignalRear?.clearAnimation()
            viewTurnSignalFront?.visibility = View.GONE
            viewTurnSignalRear?.visibility = View.GONE
            tvFindBadge?.text = "Relay 3"
            tvFindBadge?.setTextColor(getColor(R.color.text_muted))
            tvFindDesc?.text = "Còi & Xi-nhan"
        }, durationMs)
    }

    private fun handleFindVehicle() {
        sendVehicleCommand("3")
        triggerHapticFeedback()

        tvFindBadge?.text = "3 Nhịp Còi & Đèn"
        tvFindBadge?.setTextColor(getColor(R.color.accent_amber))
        tvFindDesc?.text = "Đang nháy đèn..."

        animateHazardBlink(1200L)
    }

    private fun handleVehicleFeedback(status: String) {
        runOnUiThread {
            when {
                status == "DA_MO_KHOA" -> {
                    val wasOff = !isOn
                    isOn = true
                    updatePowerUI(true)
                    if (wasOff) {
                        UnlockHistoryManager.addEvent(this, "Mở khóa xe qua App", UnlockHistoryManager.TYPE_APP_UNLOCK, "Điều khiển Bluetooth")
                        if (isAutoStartEnabled) {
                            handler.postDelayed({ if (isConnectedToVehicle() && isOn) handleEngineStart() }, autoStartDelay * 1000L)
                        }
                    }
                }
                status == "DA_KHOA_XE" -> {
                    val wasOn = isOn
                    isOn = false
                    updatePowerUI(false)
                    if (wasOn) {
                        UnlockHistoryManager.addEvent(this, "Khóa xe qua App", UnlockHistoryManager.TYPE_APP_LOCK, "Điều khiển Bluetooth")
                    }
                }
                status.startsWith("TELE|") -> {
                    val parts = status.split("|")
                    if (parts.size >= 3) {
                        val voltage = parts[1]
                        val tempC = parts[2]
                        tvVoltageVal?.text = "${voltage}V"
                        tvTempVal?.text = "${tempC}°C"
                        if (parts.size >= 4) {
                            val isUnl = parts[3] == "1"
                            if (isUnl != isOn) {
                                isOn = isUnl
                                updatePowerUI(isUnl)
                            }
                        }
                    }
                }
                status.startsWith("STATUS|") -> {
                    val parts = status.split("|")
                    if (parts.size >= 2) {
                        val isUnl = parts[1] == "1"
                        if (isUnl != isOn) {
                            isOn = isUnl
                            updatePowerUI(isUnl)
                        }
                    }
                }
                status == "DA_DE_MAY" -> {
                    if (!isEngineRunning) {
                        isEngineRunning = true
                        tvEngineBadge?.text = "1400 RPM"
                        tvEngineBadge?.setTextColor(getColor(R.color.secondary_teal))
                        tvEngineDesc?.text = "Động cơ đang nổ"
                        ivEngineIcon?.setColorFilter(getColor(R.color.secondary_teal))
                    }
                    UnlockHistoryManager.addEvent(this, "Đề nổ máy xe", UnlockHistoryManager.TYPE_ENGINE_START, "Khởi động động cơ thành công")
                }
                status == "DA_TIM_XE" -> {
                    animateHazardBlink(1200L)
                    UnlockHistoryManager.addEvent(this, "Tìm xe trong bãi", UnlockHistoryManager.TYPE_LOCATE, "Nháy xi-nhan và còi 3 nhịp")
                }
                status == "DA_DOI_KEY" -> {
                    // Thành công
                }
                status == "LOI_SAI_KEY" -> {
                    // Sai mã bảo mật
                }
                status == "LOI_CHUA_MO_KHOA" -> {
                    // Chưa mở khóa xe
                }

                // --- XỬ LÝ VÂN TAY ---
                status == "FP_LIST_START" -> {
                    fingerprintList.clear()
                }
                status.startsWith("FP_ITEM|") -> {
                    val parts = status.split("|")
                    if (parts.size >= 3) {
                        val id = parts[1].toIntOrNull() ?: 0
                        val name = parts[2]
                        fingerprintList.add(FingerprintItem(id, name))
                    }
                }
                status == "FP_LIST_END" -> {
                    renderFingerprintList()
                }
                status.startsWith("FP_ENROLL_PROGRESS|") -> {
                    val parts = status.split("|")
                    val step = parts.getOrNull(1)?.toIntOrNull() ?: 1
                    val total = parts.getOrNull(2)?.toIntOrNull() ?: 5
                    val action = parts.getOrNull(3) ?: ""
                    val id = parts.getOrNull(4) ?: ""

                    if (id.isNotEmpty()) {
                        tvEnrollFingerNameRef?.text = "ID #$id • $currentEnrollName"
                    }

                    pbEnrollProgressRef?.max = total
                    pbEnrollProgressRef?.progress = step
                    val percent = (step * 100) / total
                    tvEnrollTouchCountRef?.text = "Lần chạm: $step / $total"
                    tvEnrollPercentRef?.text = "$percent%"

                    when (action) {
                        "TOUCH_1", "TOUCH_CENTER_1" -> {
                            tvEnrollStepDescRef?.text = "🎯 Lần 1/$total: Chạm thẳng tâm ngón tay\n(Áp phẳng và giữ nhẹ đầu ngón tay)"
                            startEnrollTimer(60)
                        }
                        "LIFT" -> {
                            enrollCountDownTimer?.cancel()
                            tvEnrollStepDescRef?.text = "🖐️ Rất tốt! Hãy nhấc ngón tay ra khỏi cảm biến..."
                            pbEnrollTimerRef?.progress = 0
                            tvEnrollTimerTextRef?.text = "Chờ nhấc ngón tay..."
                            triggerHapticFeedback()
                        }
                        "TOUCH_2", "TOUCH_CENTER_2" -> {
                            tvEnrollStepDescRef?.text = "🔄 Lần 2/$total: Đặt lại thẳng tâm ngón tay\n(Để hoàn thiện mẫu chính diện)"
                            startEnrollTimer(60)
                        }
                        "TOUCH_3", "TOUCH_EDGE_1", "TOUCH_LEFT" -> {
                            tvEnrollStepDescRef?.text = "↖️ Lần 3/$total: Chạm nghiêng mép ngón tay\n(Nghiêng nhẹ ngón tay sang một bên)"
                            startEnrollTimer(60)
                        }
                        "TOUCH_4", "TOUCH_EDGE_2", "TOUCH_RIGHT" -> {
                            tvEnrollStepDescRef?.text = "↗️ Lần 4/$total: Đặt lại đúng mép nghiêng đó\n(Để tạo mẫu góc nghiêng đa điểm)"
                            startEnrollTimer(60)
                        }
                        "TOUCH_TIP" -> {
                            tvEnrollStepDescRef?.text = "🏁 Lần 5/$total: Chạm thử nghiệm mở khóa\n(Đặt tự nhiên để kiểm tra nhận diện)"
                            startEnrollTimer(60)
                        }
                    }
                }
                status.startsWith("FP_ENROLL_STEP_1") -> {
                    val parts = status.split("|")
                    val id = if (parts.size > 1) parts[1] else ""
                    if (id.isNotEmpty()) {
                        tvEnrollFingerNameRef?.text = "ID #$id • $currentEnrollName"
                    }
                }
                status.startsWith("FP_ENROLL_STEP_2") -> {
                    enrollCountDownTimer?.cancel()
                    tvEnrollStepDescRef?.text = "🖐️ Hãy nhấc ngón tay ra khỏi cảm biến!"
                    pbEnrollTimerRef?.progress = 0
                    tvEnrollTimerTextRef?.text = "Chờ nhấc ngón tay..."
                }
                status.startsWith("FP_ENROLL_STEP_3") -> {
                    // Fallback
                }
                status.startsWith("FP_IMG_START") -> {
                    enrollImageBuffer.setLength(0)
                    tvEnrollStepDescRef?.text = "📸 Đang truyền ảnh vân tay từ cảm biến..."
                }
                status.startsWith("FP_IMG_CHUNK|") -> {
                    val chunk = status.substringAfter("FP_IMG_CHUNK|").trim()
                    if (chunk.isNotEmpty()) {
                        enrollImageBuffer.append(chunk)
                    }
                }
                status == "FP_IMG_END" -> {
                    val fullB64 = enrollImageBuffer.toString()
                    enrollImageBuffer.setLength(0)
                    if (fullB64.isNotEmpty()) {
                        try {
                            val rawBytes = Base64.decode(fullB64, Base64.DEFAULT)
                            val bmp = decodeR503ImageToBitmap(rawBytes, 192, 192)
                            if (bmp != null) {
                                ivEnrollFingerprintImageRef?.setImageBitmap(bmp)
                                cvEnrollImageCardRef?.visibility = View.VISIBLE
                                val anim = AlphaAnimation(0.2f, 1f).apply {
                                    duration = 350
                                }
                                cvEnrollImageCardRef?.startAnimation(anim)
                                tvEnrollStepDescRef?.text = "📸 Đã nhận ảnh vân tay thành công!"
                            }
                        } catch (e: Exception) {
                            Log.e("FP_IMG", "Decode image error", e)
                        }
                    }
                }
                status == "FP_ENROLL_LIFT_FIRST" -> {
                    tvEnrollStepDescRef?.text = "⚠️ Phát hiện ngón tay đặt sẵn!\nVui lòng nhấc ngón tay ra khỏi cảm biến để bắt đầu."
                    triggerHapticFeedback()
                }
                status.startsWith("FP_ENROLL_RETRY") -> {
                    val stepStr = status.substringAfter("FP_ENROLL_RETRY_").substringBefore("|").trim()
                    val stepNum = stepStr.toIntOrNull() ?: 1
                    tvEnrollStepDescRef?.text = "⚠️ Chưa nhận diện được hoặc trượt tay!\nVui lòng đặt lại và giữ êm ngón tay."
                    triggerHapticFeedback()
                }
                status.startsWith("FP_ENROLL_OK") -> {
                    val parts = status.split("|")
                    val name = if (parts.size > 2) parts[2] else currentEnrollName
                    val total = pbEnrollProgressRef?.max ?: 2
                    pbEnrollProgressRef?.progress = total
                    tvEnrollTouchCountRef?.text = "Lần chạm: $total / $total"
                    tvEnrollPercentRef?.text = "100%"
                    tvEnrollStepDescRef?.text = "🎉 Đã thêm thành công vân tay [$name]!\n(Đã lưu vào bộ nhớ xe và sẵn sàng mở khóa)"
                    tvEnrollTimerTextRef?.text = "Hoàn tất!"
                    pbEnrollTimerRef?.progress = 60
                    triggerHapticFeedback()
                    handler.postDelayed({
                        dismissEnrollDialog()
                        requestFingerprintList()
                    }, 1800)
                }
                status == "FP_ENROLL_CANCELLED" -> {
                    dismissEnrollDialog()
                }
                status.startsWith("FP_ENROLL_FAILED") -> {
                    enrollCountDownTimer?.cancel()
                    pbEnrollTimerRef?.progress = 0
                    val reason = if (status.contains("|")) status.substringAfter("FP_ENROLL_FAILED|").trim() else ""
                    val message = when {
                        reason.startsWith("DA_TON_TAI") -> {
                            val existId = reason.substringAfter("DA_TON_TAI|").trim()
                            "⚠️ Ngón tay này đã được đăng ký trước đó${if (existId.isNotEmpty()) " (ID #$existId)" else ""}!"
                        }
                        reason == "KHONG_KHOP" -> "❌ Hai lần chạm không khớp cùng một ngón tay! Vui lòng thử lại."
                        reason == "BO_NHO_DAY" -> "❌ Bộ nhớ vân tay trên xe đã đầy (tối đa 200)!"
                        reason == "LOI_CAM_BIEN" -> "❌ Không tìm thấy cảm biến R503 - Kiểm tra dây RX/TX nguồn 3.3V"
                        reason == "LOI_HE_THONG" -> "❌ Lỗi bộ nhớ hệ thống ESP32"
                        reason == "LOI_LUU_TRU" -> "❌ Lỗi lưu dữ liệu vào cảm biến R503"
                        reason.startsWith("TIMEOUT_STEP") -> {
                            val stepNum = reason.substringAfter("TIMEOUT_STEP_").trim()
                            "⏱️ Quá thời gian chờ chạm ngón tay${if (stepNum.isNotEmpty()) " bước $stepNum" else ""} (60s)"
                        }
                        reason == "TIMEOUT_RELEASE" -> "⏱️ Hết thời gian chờ nhấc ngón tay ra khỏi cảm biến"
                        reason == "TIMEOUT" -> "⏱️ Hết thời gian quét vân tay"
                        reason == "ANH_MO_1" -> "Ảnh vân tay 1 mờ, vui lòng thử lại"
                        reason == "ANH_MO_2" -> "Ảnh vân tay 2 mờ, vui lòng thử lại"
                        reason == "DANG_XU_LY" -> "Đang xử lý, vui lòng chạm ngón tay"
                        else -> if (reason.isNotEmpty()) "Lỗi thêm vân tay: $reason" else "Không thể thêm vân tay"
                    }
                    tvEnrollStepDescRef?.text = message
                    tvEnrollTimerTextRef?.text = "Thất bại"
                    triggerHapticFeedback()
                    handler.postDelayed({
                        dismissEnrollDialog()
                    }, 3500)
                }
                status.startsWith("FP_DELETE_OK") -> {
                    requestFingerprintList()
                }
                status.startsWith("FP_RENAME_OK") -> {
                    requestFingerprintList()
                }
                status == "FP_CLEAR_OK" -> {
                    fingerprintList.clear()
                    renderFingerprintList()
                }
                status.startsWith("FP_MATCHED_UNLOCK|") -> {
                    val parts = status.split("|")
                    val id = if (parts.size > 1) parts[1] else ""
                    val name = if (parts.size > 2) parts[2] else "Vân tay"
                    isOn = true
                    updatePowerUI(true)
                    tvFpSensorStatus?.text = "✅ Mở khóa: $name (ID #$id)"
                    tvFpSensorStatus?.setTextColor(getColor(R.color.secondary_teal))
                    UnlockHistoryManager.addEvent(
                        this,
                        "Mở khóa xe: $name",
                        UnlockHistoryManager.TYPE_FINGERPRINT_SUCCESS,
                        if (id.isNotEmpty()) "ID: $id" else "Vân tay hợp lệ"
                    )
                    if (isAutoStartEnabled) {
                        handler.postDelayed({ if (isConnectedToVehicle() && isOn) handleEngineStart() }, autoStartDelay * 1000L)
                    }
                }
                status.startsWith("FP_MATCHED_LOCK|") -> {
                    val parts = status.split("|")
                    val id = if (parts.size > 1) parts[1] else ""
                    val name = if (parts.size > 2) parts[2] else "Vân tay"
                    isOn = false
                    updatePowerUI(false)
                    tvFpSensorStatus?.text = "🔒 Khóa xe: $name (ID #$id)"
                    tvFpSensorStatus?.setTextColor(getColor(R.color.text_secondary))
                    UnlockHistoryManager.addEvent(
                        this,
                        "Khóa xe: $name",
                        UnlockHistoryManager.TYPE_FINGERPRINT_LOCK,
                        if (id.isNotEmpty()) "ID: $id" else "Vân tay hợp lệ"
                    )
                }
                status.startsWith("FP_MATCHED|") -> {
                    val parts = status.split("|")
                    val id = if (parts.size > 1) parts[1] else ""
                    val name = if (parts.size > 2) parts[2] else "Vân tay"
                    if (isOn) {
                        tvFpSensorStatus?.text = "✅ Mở khóa: $name (ID #$id)"
                        tvFpSensorStatus?.setTextColor(getColor(R.color.secondary_teal))
                        UnlockHistoryManager.addEvent(
                            this,
                            "Mở khóa xe: $name",
                            UnlockHistoryManager.TYPE_FINGERPRINT_SUCCESS,
                            if (id.isNotEmpty()) "ID: $id" else ""
                        )
                    } else {
                        tvFpSensorStatus?.text = "🔒 Khóa xe: $name (ID #$id)"
                        tvFpSensorStatus?.setTextColor(getColor(R.color.text_secondary))
                        UnlockHistoryManager.addEvent(
                            this,
                            "Khóa xe: $name",
                            UnlockHistoryManager.TYPE_FINGERPRINT_LOCK,
                            if (id.isNotEmpty()) "ID: $id" else ""
                        )
                    }
                }
                status.startsWith("FP_MATCHED_LOCK") -> {
                    isOn = false
                    updatePowerUI(false)
                    UnlockHistoryManager.addEvent(this, "Khóa xe bằng vân tay", UnlockHistoryManager.TYPE_FINGERPRINT_LOCK)
                }
                status == "FP_NOT_MATCH" -> {
                    tvFpSensorStatus?.text = "⚠️ Vân tay không khớp!"
                    tvFpSensorStatus?.setTextColor(getColor(R.color.accent_danger))
                    UnlockHistoryManager.addEvent(
                        this,
                        "Cảnh báo: Quẹt vân tay không đúng!",
                        UnlockHistoryManager.TYPE_FINGERPRINT_FAIL,
                        "Vân tay không có trong hệ thống"
                    )
                }

                // --- CHẾ ĐỘ CHỐNG NƯỚC MƯA (ANTI-RAIN MODE) ---
                status.startsWith("RAIN_CONFIG|") -> {
                    val parts = status.split("|")
                    if (parts.size >= 6) {
                        isRainEnabled = parts[1] == "1"
                        rainTouchHoldMs = parts[2].toIntOrNull() ?: 500
                        rainMaxWrong = parts[3].toIntOrNull() ?: 5
                        rainCooldownSec = parts[4].toIntOrNull() ?: 30
                        rainAutoOffSec = parts[5].toIntOrNull() ?: 3600
                        rainRemainingSec = if (parts.size >= 7) parts[6].toIntOrNull() ?: 0 else 0
                        updateRainCardUI()
                    }
                }
                status == "RAIN_AUTO_OFF" -> {
                    isRainEnabled = false
                    rainRemainingSec = 0
                    updateRainCardUI()
                }
                status.startsWith("RAIN_COOLDOWN|") -> {
                    val cd = status.substringAfter("RAIN_COOLDOWN|").trim()
                    tvRainStatusSubtitleRef?.text = "TẠM KHÓA ${cd}S DO MƯA NHIỄU"
                    tvRainStatusSubtitleRef?.setTextColor(ContextCompat.getColor(this, R.color.accent_amber))
                }

                // --- TINH CHỈNH CẢM BIẾN VÂN TAY (FP FINE-TUNING) ---
                status.startsWith("FP_CFG|") -> {
                    val parts = status.split("|")
                    if (parts.size >= 4) {
                        val secLevel = parts[1].toIntOrNull() ?: 2
                        val scanWin = parts[2].toIntOrNull() ?: 1200
                        val enrollMode = parts[3].toIntOrNull() ?: 4
                        val sendImg = if (parts.size >= 5) parts[4] == "1" else getSharedPreferences("BT_PREF", MODE_PRIVATE).getBoolean("FP_SEND_IMG", false)
                        updateFpSettingsDialogUI(secLevel, scanWin, enrollMode, sendImg)
                    }
                }
                status == "FP_CFG_OK" -> {
                    Toast.makeText(this, "Đã lưu cấu hình tinh chỉnh vân tay thành công!", Toast.LENGTH_SHORT).show()
                    fpSettingsDialog?.dismiss()
                }
                status.startsWith("FP_TEST_STARTED|") -> {
                    val dur = status.substringAfter("FP_TEST_STARTED|").trim()
                    tvFpTestStatusRef?.text = "⏳ Đang quét thử nghiệm (còn ${dur}s)...\nHãy đặt ngón tay lên cảm biến R503 ngay!"
                    tvFpTestStatusRef?.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
                }
                status.startsWith("FP_TEST_RESULT|") -> {
                    // FP_TEST_RESULT|<id>|<name>|<confidence>
                    val parts = status.split("|")
                    if (parts.size >= 4) {
                        val id = parts[1].toIntOrNull() ?: -1
                        val name = parts[2]
                        val conf = parts[3]
                        if (id > 0) {
                            tvFpTestStatusRef?.text = "✅ KHỚP VÂN TAY!\n• ID: #$id ($name)\n• Độ tin cậy (Confidence): $conf\n(Mở khóa xe hoàn hảo)"
                            tvFpTestStatusRef?.setTextColor(ContextCompat.getColor(this, R.color.accent_emerald))
                        } else {
                            tvFpTestStatusRef?.text = "❌ KHÔNG KHỚP MẪU NÀO!\n(Vân tay chưa đăng ký hoặc thử giảm Mức bảo mật xuống Mức 2)"
                            tvFpTestStatusRef?.setTextColor(ContextCompat.getColor(this, R.color.accent_danger))
                        }
                    }
                }

            }
            syncStateToWatch()
        }
    }

    // ==========================================
    // 🖐️ QUẢN LÝ VÂN TAY (UI & LOGIC)
    // ==========================================

    private fun updateRainCardUI() {
        runOnUiThread {
            swRainQuickRef?.setOnCheckedChangeListener(null)
            swRainQuickRef?.isChecked = isRainEnabled
            swRainQuickRef?.setOnCheckedChangeListener { _, isChecked ->
                if (!isConnectedToVehicle()) {
                    swRainQuickRef?.isChecked = !isChecked
                    tvFpSensorStatus?.text = "⚠️ Chưa kết nối với xe"
                    tvFpSensorStatus?.setTextColor(getColor(R.color.accent_amber))
                    return@setOnCheckedChangeListener
                }
                toggleRainMode(isChecked)
            }

            rainCountDownTimer?.cancel()

            if (isRainEnabled) {
                if (rainAutoOffSec > 0 && rainRemainingSec > 0) {
                    startRainCountdown(rainRemainingSec)
                } else if (rainAutoOffSec == 0) {
                    tvRainStatusSubtitleRef?.text = "ĐANG BẬT (Không tự tắt)"
                    tvRainStatusSubtitleRef?.setTextColor(ContextCompat.getColor(this, R.color.gold_bright))
                } else {
                    tvRainStatusSubtitleRef?.text = "ĐANG BẬT"
                    tvRainStatusSubtitleRef?.setTextColor(ContextCompat.getColor(this, R.color.gold_bright))
                }
            } else {
                tvRainStatusSubtitleRef?.text = "ĐANG TẮT"
                tvRainStatusSubtitleRef?.setTextColor(ContextCompat.getColor(this, R.color.text_secondary))
            }
        }
    }

    private fun startRainCountdown(seconds: Int) {
        rainCountDownTimer?.cancel()
        rainCountDownTimer = object : android.os.CountDownTimer(seconds * 1000L, 1000L) {
            override fun onTick(millisUntilFinished: Long) {
                val remSec = (millisUntilFinished / 1000).toInt()
                rainRemainingSec = remSec
                val min = remSec / 60
                val sec = remSec % 60
                val timeStr = String.format("%02d:%02d", min, sec)
                tvRainStatusSubtitleRef?.text = "ĐANG BẬT (Tự tắt sau: $timeStr)"
                tvRainStatusSubtitleRef?.setTextColor(ContextCompat.getColor(this@MainActivity, R.color.gold_bright))
            }

            override fun onFinish() {
                isRainEnabled = false
                updateRainCardUI()
            }
        }.start()
    }

    private fun toggleRainMode(enabled: Boolean) {
        val valInt = if (enabled) 1 else 0
        sendVehicleCommand("TOGGLE_RAIN|$valInt")
    }

    private fun sendRainConfig(enabled: Boolean, holdMs: Int, maxWrong: Int, cooldown: Int, autoOff: Int) {
        val enVal = if (enabled) 1 else 0
        sendVehicleCommand("SET_RAIN|$enVal|$holdMs|$maxWrong|$cooldown|$autoOff")
    }

    private fun showRainSettingsDialog() {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_rain_settings, null)
        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .create()
        dialog.window?.setBackgroundDrawableResource(android.R.color.transparent)

        val swMaster = dialogView.findViewById<androidx.appcompat.widget.SwitchCompat>(R.id.swDialogRainMaster)
        val tvSwitchDesc = dialogView.findViewById<TextView>(R.id.tvRainSwitchDesc)
        val tvHoldVal = dialogView.findViewById<TextView>(R.id.tvHoldTimeVal)
        val sbHold = dialogView.findViewById<SeekBar>(R.id.sbHoldTime)
        val tvWrongVal = dialogView.findViewById<TextView>(R.id.tvMaxWrongVal)
        val sbWrong = dialogView.findViewById<SeekBar>(R.id.sbMaxWrong)
        val tvCdVal = dialogView.findViewById<TextView>(R.id.tvCooldownVal)
        val sbCd = dialogView.findViewById<SeekBar>(R.id.sbCooldown)
        val tvAutoOffVal = dialogView.findViewById<TextView>(R.id.tvAutoOffVal)
        val sbAutoOff = dialogView.findViewById<SeekBar>(R.id.sbAutoOff)
        val btnCancel = dialogView.findViewById<Button>(R.id.btnCancelRainSettings)
        val btnSave = dialogView.findViewById<Button>(R.id.btnSaveRainSettings)

        // 1. Master Switch
        swMaster.isChecked = isRainEnabled
        tvSwitchDesc.text = if (isRainEnabled) "Đang kích hoạt" else "Đang tắt"
        swMaster.setOnCheckedChangeListener { _, isChecked ->
            tvSwitchDesc.text = if (isChecked) "Đang kích hoạt" else "Đang tắt"
        }

        // 2. Hold Time: min 200, step 50 -> 200 + progress * 50
        val curHoldProgress = ((rainTouchHoldMs - 200).coerceAtLeast(0) / 50).coerceIn(0, 26)
        sbHold.progress = curHoldProgress
        tvHoldVal.text = "${rainTouchHoldMs} ms"
        sbHold.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val ms = 200 + progress * 50
                tvHoldVal.text = "$ms ms"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // 3. Max Wrong: 0..10
        sbWrong.progress = rainMaxWrong.coerceIn(0, 10)
        tvWrongVal.text = if (rainMaxWrong == 0) "0 (Tắt còi)" else "$rainMaxWrong lần"
        sbWrong.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                tvWrongVal.text = if (progress == 0) "0 (Tắt còi)" else "$progress lần"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // 4. Cooldown
        val cdOptions = intArrayOf(5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 240, 300)
        var cdIdx = cdOptions.indexOfFirst { it >= rainCooldownSec }
        if (cdIdx == -1) cdIdx = 4
        sbCd.max = cdOptions.size - 1
        sbCd.progress = cdIdx
        tvCdVal.text = "${cdOptions[cdIdx]} giây"
        sbCd.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                tvCdVal.text = "${cdOptions[progress]} giây"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // 5. Auto Off Options
        val autoOffOptions = intArrayOf(0, 900, 1800, 3600, 7200, 14400)
        val autoOffLabels = arrayOf("Không tự tắt", "15 phút", "30 phút", "1 giờ", "2 giờ", "4 giờ")
        var autoOffIdx = autoOffOptions.indexOfFirst { it == rainAutoOffSec }
        if (autoOffIdx == -1) autoOffIdx = 3
        sbAutoOff.max = autoOffOptions.size - 1
        sbAutoOff.progress = autoOffIdx
        tvAutoOffVal.text = autoOffLabels[autoOffIdx]
        sbAutoOff.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                tvAutoOffVal.text = autoOffLabels[progress]
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        btnCancel.setOnClickListener { dialog.dismiss() }

        btnSave.setOnClickListener {
            if (!isConnectedToVehicle()) {
                tvFpSensorStatus?.text = "⚠️ Chưa kết nối với xe"
                tvFpSensorStatus?.setTextColor(getColor(R.color.accent_amber))
                dialog.dismiss()
                return@setOnClickListener
            }
            val enabled = swMaster.isChecked
            val holdMs = 200 + sbHold.progress * 50
            val maxWrong = sbWrong.progress
            val cooldown = cdOptions[sbCd.progress]
            val autoOff = autoOffOptions[sbAutoOff.progress]

            sendRainConfig(enabled, holdMs, maxWrong, cooldown, autoOff)
            dialog.dismiss()
        }

        dialog.show()
    }

    private fun requestFingerprintList() {
        sendVehicleCommand("FP_LIST")
        sendVehicleCommand("GET_RAIN")
    }


    private fun renderFingerprintList() {
        val container = findViewById<LinearLayout>(R.id.llFingerprintList) ?: return
        val tvEmpty = findViewById<TextView>(R.id.tvEmptyFp)
        container.removeAllViews()

        if (fingerprintList.isEmpty()) {
            tvEmpty?.visibility = View.VISIBLE
            return
        }

        tvEmpty?.visibility = View.GONE

        val inflater = LayoutInflater.from(this)
        val itemsSnapshot = ArrayList(fingerprintList)
        for (item in itemsSnapshot) {
            val itemView = inflater.inflate(R.layout.item_fingerprint, container, false)
            val tvFpName = itemView.findViewById<TextView>(R.id.tvFpName)
            val tvFpId = itemView.findViewById<TextView>(R.id.tvFpId)
            val btnEdit = itemView.findViewById<View>(R.id.btnEditFp)
            val btnDelete = itemView.findViewById<View>(R.id.btnDeleteFp)

            tvFpName?.text = item.name
            tvFpId?.text = if (item.id == 1) "ID #1 • Đề máy + Mở nguồn (Chủ xe)" else "ID #${item.id} • Mở nguồn"

            btnEdit?.setOnClickListener {
                showRenameDialog(item)
            }

            btnDelete?.setOnClickListener {
                AlertDialog.Builder(this)
                    .setTitle("Xác nhận xóa")
                    .setMessage("Bạn có chắc chắn muốn xóa vân tay [${item.name}] (ID: ${item.id})?")
                    .setPositiveButton("Xóa") { _, _ ->
                        sendVehicleCommand("FP_DELETE|${item.id}")
                    }
                    .setNegativeButton("Hủy", null)
                    .show()
            }

            container.addView(itemView)
        }
    }

    private fun startEnrollTimer(totalSeconds: Int = 60) {
        enrollCountDownTimer?.cancel()
        pbEnrollTimerRef?.max = totalSeconds
        pbEnrollTimerRef?.progress = totalSeconds
        tvEnrollTimerTextRef?.text = "Thời gian chờ: ${totalSeconds}s"

        enrollCountDownTimer = object : android.os.CountDownTimer((totalSeconds * 1000).toLong(), 1000) {
            override fun onTick(millisUntilFinished: Long) {
                val secLeft = (millisUntilFinished / 1000).toInt()
                pbEnrollTimerRef?.progress = secLeft
                tvEnrollTimerTextRef?.text = "Thời gian chờ: ${secLeft}s"
            }

            override fun onFinish() {
                pbEnrollTimerRef?.progress = 0
                tvEnrollTimerTextRef?.text = "Hết thời gian chờ"
            }
        }.start()
    }

    private fun dismissEnrollDialog() {
        enrollCountDownTimer?.cancel()
        enrollCountDownTimer = null
        enrollCustomDialog?.dismiss()
        enrollCustomDialog = null
        tvEnrollStepDescRef = null
        pbEnrollTimerRef = null
        tvEnrollTimerTextRef = null
        tvEnrollFingerNameRef = null
        pbEnrollProgressRef = null
        tvEnrollTouchCountRef = null
        tvEnrollPercentRef = null
        cvEnrollImageCardRef = null
        ivEnrollFingerprintImageRef = null
        enrollImageBuffer.setLength(0)
    }

    private fun showAddFingerprintDialog() {
        if (!isConnectedToVehicle()) {
            tvFpSensorStatus?.text = "⚠️ Vui lòng kết nối với xe trước"
            tvFpSensorStatus?.setTextColor(getColor(R.color.accent_amber))
            return
        }

        // Tự động tìm số thứ tự trống tiếp theo để đặt tên mặc định
        val nextId = (1..200).firstOrNull { id -> fingerprintList.none { it.id == id } } ?: (fingerprintList.size + 1)
        val defaultName = "Vân tay $nextId"

        // Giao diện nhập tên với các tag chọn nhanh
        val container = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(60, 30, 60, 20)
        }

        val tvHint = TextView(this).apply {
            text = "Đặt tên nhận diện cho ngón tay này:"
            setTextColor(getColor(R.color.text_secondary))
            textSize = 13f
            setPadding(0, 0, 0, 16)
        }
        container.addView(tvHint)

        val input = EditText(this).apply {
            setText(defaultName)
            setSelection(text.length)
            setTextColor(getColor(R.color.white))
            setHintTextColor(getColor(R.color.text_muted))
            textSize = 15f
        }
        container.addView(input)

        val quickTagsLayout = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, 20, 0, 0)
        }
        val quickTags = arrayOf("Ngón cái P", "Ngón trỏ P", "Ngón cái T", "Ngón trỏ T")
        for (tag in quickTags) {
            val btnTag = Button(this, null, android.R.attr.buttonStyleSmall).apply {
                text = tag
                textSize = 10f
                isAllCaps = false
                setTextColor(getColor(R.color.gold_primary))
                setBackgroundResource(R.drawable.header_icon_btn_bg)
                val params = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply {
                    marginEnd = 6
                }
                layoutParams = params
                setOnClickListener {
                    input.setText(tag)
                    input.setSelection(tag.length)
                }
            }
            quickTagsLayout.addView(btnTag)
        }
        container.addView(quickTagsLayout)

        // Toggle truyền hình ảnh vân tay khi quét
        val prefs = getSharedPreferences("BT_PREF", MODE_PRIVATE)
        val initialSendImg = prefs.getBoolean("FP_SEND_IMG", false)

        val switchLayout = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = android.view.Gravity.CENTER_VERTICAL
            setPadding(0, 30, 0, 0)
        }
        val textLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }
        val tvSwTitle = TextView(this).apply {
            text = "Gửi ảnh vân tay khi lấy mẫu"
            setTextColor(getColor(R.color.white))
            textSize = 13f
            typeface = android.graphics.Typeface.DEFAULT_BOLD
        }
        val tvSwSub = TextView(this).apply {
            text = "Hiển thị ảnh 192x192 lên màn hình (+2s)"
            setTextColor(getColor(R.color.text_secondary))
            textSize = 11f
        }
        textLayout.addView(tvSwTitle)
        textLayout.addView(tvSwSub)
        switchLayout.addView(textLayout)

        val swSendImage = SwitchCompat(this).apply {
            isChecked = initialSendImg
            setOnCheckedChangeListener { _, isChecked ->
                prefs.edit().putBoolean("FP_SEND_IMG", isChecked).apply()
            }
        }
        switchLayout.addView(swSendImage)
        container.addView(switchLayout)

        AlertDialog.Builder(this)
            .setTitle("Thêm Vân Tay Mới (R503)")
            .setView(container)
            .setPositiveButton("Bắt đầu lấy mẫu") { _, _ ->
                val typedName = input.text.toString().trim()
                val finalName = if (typedName.isNotEmpty()) typedName else defaultName
                val cleanName = finalName.replace("|", "").replace("\n", "").replace("\r", "").take(30)
                val sendImg = swSendImage.isChecked
                startEnrollProcess(cleanName, sendImg)
            }
            .setNegativeButton("Hủy", null)
            .show()
    }

    private fun startEnrollProcess(fingerName: String, sendImage: Boolean = false) {
        currentEnrollName = fingerName
        currentEnrollSendImage = sendImage
        enrollImageBuffer.setLength(0)

        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_enroll_fingerprint, null)
        val tvFingerName = dialogView.findViewById<TextView>(R.id.tvEnrollFingerName)
        val tvStepDesc = dialogView.findViewById<TextView>(R.id.tvEnrollStepDesc)
        val pbTimer = dialogView.findViewById<ProgressBar>(R.id.pbEnrollTimer)
        val tvTimerText = dialogView.findViewById<TextView>(R.id.tvEnrollTimerText)
        val btnCancel = dialogView.findViewById<Button>(R.id.btnCancelEnroll)
        val pbProgress = dialogView.findViewById<ProgressBar>(R.id.pbEnrollProgress)
        val tvTouchCount = dialogView.findViewById<TextView>(R.id.tvEnrollTouchCount)
        val tvPercent = dialogView.findViewById<TextView>(R.id.tvEnrollPercent)
        val cvImageCard = dialogView.findViewById<CardView>(R.id.cvEnrollImageCard)
        val ivFpImage = dialogView.findViewById<ImageView>(R.id.ivEnrollFingerprintImage)

        cvEnrollImageCardRef = cvImageCard
        ivEnrollFingerprintImageRef = ivFpImage
        cvEnrollImageCardRef?.visibility = View.GONE

        tvFingerName.text = "Tên: $fingerName"
        tvStepDesc.text = "Đang kết nối cảm biến R503..."
        pbProgress?.max = 5
        pbProgress?.progress = 1
        tvTouchCount?.text = "Lần chạm: 1 / 5"
        tvPercent?.text = "20%"

        tvEnrollFingerNameRef = tvFingerName
        tvEnrollStepDescRef = tvStepDesc
        pbEnrollTimerRef = pbTimer
        tvEnrollTimerTextRef = tvTimerText
        pbEnrollProgressRef = pbProgress
        tvEnrollTouchCountRef = tvTouchCount
        tvEnrollPercentRef = tvPercent

        enrollCustomDialog?.dismiss()
        enrollCustomDialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .setCancelable(false)
            .create()

        btnCancel.setOnClickListener {
            sendVehicleCommand("FP_CANCEL")
            dismissEnrollDialog()
        }

        enrollCustomDialog?.show()
        startEnrollTimer(60)

        val cmd = if (sendImage) "FP_ENROLL|$fingerName|IMG" else "FP_ENROLL|$fingerName|NO_IMG"
        sendVehicleCommand(cmd)
    }

    private fun showRenameDialog(item: FingerprintItem) {
        val input = EditText(this)
        input.setText(item.name)
        val container = FrameLayout(this); container.addView(input); input.setPadding(60, 40, 60, 40)

        AlertDialog.Builder(this)
            .setTitle("Đổi tên vân tay ID ${item.id}")
            .setView(container)
            .setPositiveButton("Cập nhật") { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isNotEmpty()) {
                    sendVehicleCommand("FP_RENAME|${item.id}|$newName")
                }
            }
            .setNegativeButton("Hủy", null)
            .show()
    }

    private fun showFingerprintSettingsDialog() {
        if (!isConnectedToVehicle()) {
            Toast.makeText(this, "Vui lòng kết nối Bluetooth tới xe trước!", Toast.LENGTH_SHORT).show()
            return
        }

        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_fingerprint_settings, null)
        val tvSecLevelVal = dialogView.findViewById<TextView>(R.id.tvFpSecLevelVal)
        val tvSecLevelDesc = dialogView.findViewById<TextView>(R.id.tvFpSecLevelDesc)
        val sbSecLevel = dialogView.findViewById<SeekBar>(R.id.sbFpSecLevel)
        val tvScanTimeVal = dialogView.findViewById<TextView>(R.id.tvFpScanTimeVal)
        val sbScanTime = dialogView.findViewById<SeekBar>(R.id.sbFpScanTime)
        val rgEnrollMode = dialogView.findViewById<RadioGroup>(R.id.rgFpEnrollMode)
        val rb4Touch = dialogView.findViewById<RadioButton>(R.id.rbEnroll4Touch)
        val rb2Touch = dialogView.findViewById<RadioButton>(R.id.rbEnroll2Touch)
        val swSendImage = dialogView.findViewById<SwitchCompat>(R.id.swFpSendImage)
        val btnLiveTest = dialogView.findViewById<Button>(R.id.btnFpLiveTest)
        val tvTestStatus = dialogView.findViewById<TextView>(R.id.tvFpTestStatus)
        val btnCancel = dialogView.findViewById<Button>(R.id.btnCancelFpSettings)
        val btnSave = dialogView.findViewById<Button>(R.id.btnSaveFpSettings)

        tvFpSecLevelValRef = tvSecLevelVal
        tvFpSecLevelDescRef = tvSecLevelDesc
        sbFpSecLevelRef = sbSecLevel
        tvFpScanTimeValRef = tvScanTimeVal
        sbFpScanTimeRef = sbScanTime
        rgFpEnrollModeRef = rgEnrollMode
        rbEnroll4TouchRef = rb4Touch
        rbEnroll2TouchRef = rb2Touch
        swFpSendImageRef = swSendImage
        tvFpTestStatusRef = tvTestStatus

        // Security level slider: max 4 (0 -> 4 corresponds to level 1 -> 5)
        sbSecLevel.progress = (fpSecLevelVal - 1).coerceIn(0, 4)
        updateSecLevelText(fpSecLevelVal)
        sbSecLevel.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val level = progress + 1
                fpSecLevelVal = level
                updateSecLevelText(level)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // Scan time slider: max 14 (0 -> 14 corresponds to 600ms -> 2000ms with step 100ms)
        val scanProgress = ((fpScanTimeVal - 600) / 100).coerceIn(0, 14)
        sbScanTime.progress = scanProgress
        tvScanTimeVal.text = "$fpScanTimeVal ms"
        sbScanTime.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val time = 600 + progress * 100
                fpScanTimeVal = time
                tvScanTimeVal.text = "$time ms"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        if (fpEnrollModeVal == 2) {
            rb2Touch.isChecked = true
        } else {
            rb4Touch.isChecked = true
        }

        val prefs = getSharedPreferences("BT_PREF", MODE_PRIVATE)
        fpSendImageVal = prefs.getBoolean("FP_SEND_IMG", false)
        swSendImage?.isChecked = fpSendImageVal
        swSendImage?.setOnCheckedChangeListener { _, isChecked ->
            fpSendImageVal = isChecked
            prefs.edit().putBoolean("FP_SEND_IMG", isChecked).apply()
        }

        btnLiveTest.setOnClickListener {
            sendVehicleCommand("TEST_FP|15")
            tvTestStatus.text = "⏳ Chế độ test đang chạy (15s). Hãy đặt ngón tay lên cảm biến R503 ngay bây giờ!"
            tvTestStatus.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
        }

        btnCancel.setOnClickListener {
            fpSettingsDialog?.dismiss()
        }

        btnSave.setOnClickListener {
            val enrollMode = if (rb2Touch.isChecked) 2 else 4
            fpEnrollModeVal = enrollMode
            val sendImgInt = if (swSendImage?.isChecked == true) 1 else 0
            sendVehicleCommand("SET_FP_CFG|$fpSecLevelVal|$fpScanTimeVal|$fpEnrollModeVal|$sendImgInt")
            fpSettingsDialog?.dismiss()
        }

        fpSettingsDialog?.dismiss()
        fpSettingsDialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .create()
        fpSettingsDialog?.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        fpSettingsDialog?.show()

        // Yêu cầu ESP32 trả về cấu hình hiện tại để đồng bộ chính xác
        sendVehicleCommand("GET_FP_CFG")
    }

    private fun updateSecLevelText(level: Int) {
        val (label, desc) = when (level) {
            1 -> Pair("Mức 1 (Siêu nhạy - Nhanh nhất)", "Nhận diện cực nhanh ngay cả khi ngón tay hơi khô/ướt. FAR < 0.01%.")
            2 -> Pair("Mức 2 (Nhạy cao - Khuyên dùng)", "Cân bằng hoàn hảo giữa tốc độ mở khóa và tính bảo mật.")
            3 -> Pair("Mức 3 (Tiêu chuẩn)", "Chuẩn bảo mật nhà sản xuất (FAR < 0.001%).")
            4 -> Pair("Mức 4 (Chặt chẽ)", "Yêu cầu góc đặt vân tay chuẩn xác hơn.")
            5 -> Pair("Mức 5 (Tối đa)", "Khắt khe tối đa, chỉ dùng khi yêu cầu bảo mật cấp ngân hàng.")
            else -> Pair("Mức $level", "")
        }
        tvFpSecLevelValRef?.text = label
        tvFpSecLevelDescRef?.text = desc
    }

    private fun updateFpSettingsDialogUI(secLevel: Int, scanWin: Int, enrollMode: Int, sendImg: Boolean = false) {
        fpSecLevelVal = secLevel.coerceIn(1, 5)
        fpScanTimeVal = scanWin.coerceIn(400, 3000)
        fpEnrollModeVal = if (enrollMode == 2) 2 else 4
        fpSendImageVal = sendImg

        sbFpSecLevelRef?.progress = (fpSecLevelVal - 1).coerceIn(0, 4)
        updateSecLevelText(fpSecLevelVal)

        val scanProgress = ((fpScanTimeVal - 600) / 100).coerceIn(0, 14)
        sbFpScanTimeRef?.progress = scanProgress
        tvFpScanTimeValRef?.text = "$fpScanTimeVal ms"

        if (fpEnrollModeVal == 2) {
            rbEnroll2TouchRef?.isChecked = true
        } else {
            rbEnroll4TouchRef?.isChecked = true
        }

        swFpSendImageRef?.isChecked = fpSendImageVal
        getSharedPreferences("BT_PREF", MODE_PRIVATE).edit().putBoolean("FP_SEND_IMG", fpSendImageVal).apply()
    }

    private fun decodeR503ImageToBitmap(rawBytes: ByteArray, width: Int = 192, height: Int = 192): Bitmap? {
        val totalPixels = width * height
        val neededBytes = totalPixels / 2
        if (rawBytes.size < neededBytes) {
            Log.w("FP_IMG", "Decoded bytes too short: ${rawBytes.size} < $neededBytes")
            return null
        }
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val pixels = IntArray(totalPixels)
        var byteIdx = 0
        for (i in 0 until totalPixels step 2) {
            if (byteIdx >= rawBytes.size) break
            val b = rawBytes[byteIdx].toInt() and 0xFF
            byteIdx++

            // Pixel 1: high nibble (4 bits)
            val p1 = (b ushr 4) and 0x0F
            // Pixel 2: low nibble (4 bits)
            val p2 = b and 0x0F

            // R503 optical sensor: 0 is dark (ridge), 15 (0xF) is bright (background)
            val g1 = p1 * 17
            val g2 = p2 * 17

            pixels[i] = (0xFF shl 24) or (g1 shl 16) or (g1 shl 8) or g1
            if (i + 1 < totalPixels) {
                pixels[i + 1] = (0xFF shl 24) or (g2 shl 16) or (g2 shl 8) or g2
            }
        }
        bitmap.setPixels(pixels, 0, width, 0, 0, width, height)
        return bitmap
    }

    // ==========================================
    // 📶 KẾT NỐI & THIẾT BỊ
    // ==========================================

    @SuppressLint("MissingPermission")
    private fun selectDevice() {
        val options = arrayOf("Quét thiết bị BLE (ESP32-C3)", "Thiết bị Classic BT đã ghép đôi")
        AlertDialog.Builder(this)
            .setTitle("Chọn phương thức kết nối")
            .setItems(options) { _, which ->
                if (which == 0) {
                    showBleScanDialog()
                } else {
                    selectClassicDevice()
                }
            }
            .show()
    }

    @SuppressLint("MissingPermission")
    private fun showBleScanDialog() {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_ble_scan, null)
        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .create()

        val pbScanning = dialogView.findViewById<ProgressBar>(R.id.pbScanning)
        val llContainer = dialogView.findViewById<LinearLayout>(R.id.llBleDeviceContainer)
        val tvEmpty = dialogView.findViewById<TextView>(R.id.tvEmptyScan)
        val btnRescan = dialogView.findViewById<Button>(R.id.btnRescan)
        val btnClose = dialogView.findViewById<Button>(R.id.btnCloseScan)

        val discoveredMap = mutableMapOf<String, BleDeviceInfo>()
        var scanCallback: android.bluetooth.le.ScanCallback? = null

        fun renderDevice(item: BleDeviceInfo) {
            val itemView = LayoutInflater.from(this).inflate(R.layout.item_ble_device, llContainer, false)
            val tvName = itemView.findViewById<TextView>(R.id.tvDeviceItemName)
            val tvMac = itemView.findViewById<TextView>(R.id.tvDeviceItemMac)
            val tvRssi = itemView.findViewById<TextView>(R.id.tvDeviceItemRssi)

            val isTargetVehicle = item.name.contains("XE_tsmart", ignoreCase = true) || item.name.contains("tsmart", ignoreCase = true)
            if (isTargetVehicle) {
                tvName.text = "★ ${item.name} (Xe Tsmartkey)"
                tvName.setTextColor(getColor(R.color.secondary_teal))
            } else {
                tvName.text = item.name
            }
            tvMac.text = item.device.address
            tvRssi.text = "${item.rssi} dBm"

            itemView.setOnClickListener {
                BleManager.stopScan(scanCallback)
                dialog.dismiss()

                isManualDisconnect = false
                isBleMode = true
                BleManager.activeMac = item.device.address
                BleManager.deviceName = item.name
                updateDeviceNameDisplay()

                if (BleManager.SECRET_KEY.isEmpty()) {
                    showKeyDialog()
                } else {
                    isAutoConnectEnabled = true
                    saveDevice(item.device.address, BleManager.SECRET_KEY, true)
                    saveSettings()
                    connectVehicle()
                }
            }

            if (isTargetVehicle) {
                llContainer.addView(itemView, 0)
            } else {
                llContainer.addView(itemView)
            }
        }

        fun startScanning() {
            llContainer.removeAllViews()
            discoveredMap.clear()
            tvEmpty.visibility = View.VISIBLE
            tvEmpty.text = "Đang quét tìm thiết bị BLE xung quanh..."
            llContainer.addView(tvEmpty)
            pbScanning.visibility = View.VISIBLE

            scanCallback = BleManager.startScan(
                onDeviceFound = { deviceInfo ->
                    runOnUiThread {
                        if (!discoveredMap.containsKey(deviceInfo.device.address)) {
                            discoveredMap[deviceInfo.device.address] = deviceInfo
                            tvEmpty.visibility = View.GONE
                            renderDevice(deviceInfo)
                        }
                    }
                },
                onScanFailed = {
                    runOnUiThread {
                        pbScanning.visibility = View.GONE
                        tvEmpty.text = "Quét BLE thất bại. Hãy kiểm tra Bluetooth!"
                    }
                }
            )

            // Dừng quét sau 10s
            handler.postDelayed({
                BleManager.stopScan(scanCallback)
                pbScanning.visibility = View.GONE
                if (discoveredMap.isEmpty()) {
                    tvEmpty.text = "Không tìm thấy xe. Hãy chắc chắn xe đang bật nguồn!"
                }
            }, 10000)
        }

        btnRescan.setOnClickListener {
            BleManager.stopScan(scanCallback)
            startScanning()
        }

        btnClose.setOnClickListener {
            BleManager.stopScan(scanCallback)
            dialog.dismiss()
        }

        dialog.setOnDismissListener {
            BleManager.stopScan(scanCallback)
        }

        dialog.show()
        startScanning()
    }

    private fun showUnlockHistoryDialog() {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_unlock_history, null)
        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .create()

        val llContainer = dialogView.findViewById<LinearLayout>(R.id.llHistoryContainer)
        val tvEmpty = dialogView.findViewById<TextView>(R.id.tvEmptyHistory)
        val btnClear = dialogView.findViewById<Button>(R.id.btnClearHistory)
        val btnClose = dialogView.findViewById<Button>(R.id.btnCloseHistory)

        fun refreshList() {
            llContainer.removeAllViews()
            val history = UnlockHistoryManager.getHistory(this)
            tvEmpty.visibility = if (history.isEmpty()) View.VISIBLE else View.GONE
            if (history.isNotEmpty()) {
                val inflater = LayoutInflater.from(this)
                for (item in history) {
                    val itemView = inflater.inflate(R.layout.item_unlock_history, llContainer, false)
                    val ivIcon = itemView.findViewById<ImageView>(R.id.ivHistoryIcon)
                    val tvTitle = itemView.findViewById<TextView>(R.id.tvHistoryTitle)
                    val tvTime = itemView.findViewById<TextView>(R.id.tvHistoryTime)
                    val tvDetail = itemView.findViewById<TextView>(R.id.tvHistoryDetail)

                    tvTitle.text = item.title
                    tvTime.text = item.getFormattedTime()

                    when (item.type) {
                        UnlockHistoryManager.TYPE_FINGERPRINT_SUCCESS -> {
                            ivIcon.setImageResource(R.drawable.ic_fingerprint)
                            ivIcon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.secondary_teal))
                        }
                        UnlockHistoryManager.TYPE_FINGERPRINT_LOCK -> {
                            ivIcon.setImageResource(R.drawable.ic_fingerprint)
                            ivIcon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.accent_amber))
                        }
                        UnlockHistoryManager.TYPE_FINGERPRINT_FAIL -> {
                            ivIcon.setImageResource(R.drawable.ic_fingerprint)
                            ivIcon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.primary_red))
                        }
                        UnlockHistoryManager.TYPE_APP_UNLOCK -> {
                            ivIcon.setImageResource(R.drawable.ic_power)
                            ivIcon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.secondary_teal))
                        }
                        UnlockHistoryManager.TYPE_APP_LOCK -> {
                            ivIcon.setImageResource(R.drawable.ic_power)
                            ivIcon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.primary_red))
                        }
                        UnlockHistoryManager.TYPE_ENGINE_START -> {
                            ivIcon.setImageResource(R.drawable.ic_engine)
                            ivIcon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.gold_primary))
                        }
                        UnlockHistoryManager.TYPE_LOCATE -> {
                            ivIcon.setImageResource(R.drawable.ic_auto)
                            ivIcon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.secondary_teal))
                        }
                        else -> {
                            ivIcon.setImageResource(R.drawable.ic_power)
                            ivIcon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.text_secondary))
                        }
                    }

                    if (item.detail.isNotEmpty()) {
                        tvDetail.visibility = View.VISIBLE
                        tvDetail.text = item.detail
                    } else {
                        tvDetail.visibility = View.GONE
                    }

                    llContainer.addView(itemView)
                }
            }
        }

        btnClear.setOnClickListener {
            AlertDialog.Builder(this)
                .setTitle("Xác nhận xóa")
                .setMessage("Bạn có chắc chắn muốn xóa toàn bộ lịch sử mở khóa?")
                .setPositiveButton("Xóa") { _, _ ->
                    UnlockHistoryManager.clearHistory(this)
                    refreshList()
                }
                .setNegativeButton("Hủy", null)
                .show()
        }

        btnClose.setOnClickListener { dialog.dismiss() }

        refreshList()
        dialog.show()
    }

    @SuppressLint("MissingPermission")
    private fun selectClassicDevice() {
        val devices = bluetoothAdapter?.bondedDevices?.toList() ?: return
        if (devices.isEmpty()) {
            Toast.makeText(this, "Chưa pair thiết bị Bluetooth nào", Toast.LENGTH_SHORT).show()
            return
        }

        val deviceNames = devices.map { "${it.name}\n${it.address}" }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Chọn thiết bị Classic Bluetooth")
            .setItems(deviceNames) { _, which ->
                val selectedDevice = devices[which]
                isBleMode = false
                BluetoothController.deviceMac = selectedDevice.address
                BluetoothController.deviceName = selectedDevice.name
                updateDeviceNameDisplay()
                showKeyDialog()
            }
            .show()
    }

    private fun showKeyDialog() {
        val input = EditText(this)
        input.hint = "Nhập mã bảo mật hiện tại"
        val container = FrameLayout(this); container.addView(input); input.setPadding(60, 40, 60, 40)
        AlertDialog.Builder(this).setTitle("Xác thực").setView(container).setPositiveButton("OK") { _, _ ->
            val key = input.text.toString()
            BleManager.SECRET_KEY = key
            BluetoothController.SECRET_KEY = key
            val mac = if (isBleMode) BleManager.activeMac!! else BluetoothController.deviceMac!!
            saveDevice(mac, key, isBleMode)
            withBluetoothPermission { connectVehicle() }
        }.show()
    }

    private fun showChangeKeyDialog() {
        val input = EditText(this)
        input.hint = "Nhập mã bảo mật MỚI"
        val container = FrameLayout(this); container.addView(input); input.setPadding(60, 40, 60, 40)
        AlertDialog.Builder(this)
            .setTitle("Đổi mã bảo mật")
            .setMessage("Mã mới sẽ được lưu vào xe. Hãy ghi nhớ mã này!")
            .setView(container)
            .setPositiveButton("Cập nhật") { _, _ ->
                val newKey = input.text.toString().trim()
                if (newKey.isNotEmpty()) {
                    sendVehicleCommand("9|$newKey")
                    BleManager.SECRET_KEY = newKey
                    BluetoothController.SECRET_KEY = newKey
                    val mac = if (isBleMode) BleManager.activeMac!! else BluetoothController.deviceMac!!
                    saveDevice(mac, newKey, isBleMode)
                }
            }
            .setNegativeButton("Hủy", null)
            .show()
    }

    private fun showRenameVehicleDialog() {
        val input = EditText(this).apply {
            setText(vehicleCustomName)
            setSelection(text.length)
            hint = "Nhập tên xe (VD: Honda SH 150i)"
        }
        val container = FrameLayout(this).apply {
            addView(input)
            input.setPadding(60, 40, 60, 40)
        }
        AlertDialog.Builder(this)
            .setTitle("Đổi tên xe (Garage Profile)")
            .setMessage("Đặt tên gợi nhớ cho xe của bạn trên ứng dụng")
            .setView(container)
            .setPositiveButton("Lưu") { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isNotEmpty()) {
                    vehicleCustomName = newName
                    getSharedPreferences("BT_PREF", MODE_PRIVATE).edit()
                        .putString("VEHICLE_NAME", vehicleCustomName)
                        .apply()
                    updateDeviceNameDisplay()
                    tvTitleRef?.text = vehicleCustomName
                    Toast.makeText(this, "Đã cập nhật tên xe: $vehicleCustomName", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("Hủy", null)
            .show()
    }

    private fun saveDevice(mac: String, key: String, isBle: Boolean) {
        getSharedPreferences("BT_PREF", MODE_PRIVATE).edit()
            .putString("DEVICE_MAC", mac)
            .putString("SECRET_KEY", key)
            .putBoolean("IS_BLE", isBle)
            .putString("VEHICLE_NAME", vehicleCustomName)
            .apply()
        updateDeviceNameDisplay()
    }

    @SuppressLint("MissingPermission")
    private fun updateDeviceNameDisplay() {
        val tvDeviceName = findViewById<TextView>(R.id.tvDeviceName)
        val mac = if (isBleMode) BleManager.activeMac else BluetoothController.deviceMac

        if (mac == null) {
            tvDeviceName.text = "Thiết bị: Chưa chọn"
            tvTitleRef?.text = vehicleCustomName
            return
        }
        val type = if (isBleMode) "BLE" else "Classic"
        tvDeviceName.text = "$vehicleCustomName ($type)"
        tvTitleRef?.text = vehicleCustomName
    }

    private fun saveSettings() {
        getSharedPreferences("BT_PREF", MODE_PRIVATE).edit()
            .putBoolean("AUTO_CONNECT", isAutoConnectEnabled)
            .putBoolean("AUTO_START", isAutoStartEnabled)
            .putBoolean("SHOW_INFO", isShowInfoEnabled)
            .putBoolean("HIDE_NAV", isHideNavEnabled)
            .putString("VEHICLE_NAME", vehicleCustomName)
            .putInt("DELAY_VALUE", autoStartDelay).apply()
    }

    private fun loadDevice() {
        val prefs = getSharedPreferences("BT_PREF", MODE_PRIVATE)
        val mac = prefs.getString("DEVICE_MAC", null)
        val key = prefs.getString("SECRET_KEY", "271000") ?: "271000"
        vehicleCustomName = prefs.getString("VEHICLE_NAME", "Honda SH 150i") ?: "Honda SH 150i"
        isBleMode = prefs.getBoolean("IS_BLE", true)

        if (isBleMode) {
            BleManager.activeMac = mac
            BleManager.SECRET_KEY = key
        } else {
            BluetoothController.deviceMac = mac
            BluetoothController.SECRET_KEY = key
        }

        isAutoConnectEnabled = prefs.getBoolean("AUTO_CONNECT", false)
        isAutoStartEnabled = prefs.getBoolean("AUTO_START", false)
        isShowInfoEnabled = prefs.getBoolean("SHOW_INFO", true)
        isHideNavEnabled = prefs.getBoolean("HIDE_NAV", false)
        autoStartDelay = prefs.getInt("DELAY_VALUE", 3)
        tvTitleRef?.text = vehicleCustomName
    }

    @SuppressLint("MissingPermission")
    private fun connectVehicle() {
        isManualDisconnect = false
        isConnecting = true
        if (isBleMode) {
            val mac = BleManager.activeMac ?: return
            BleManager.connect(this, mac) { success ->
                runOnUiThread {
                    isConnecting = false
                    updateConnectionUI(success)
                }
            }
        } else {
            val mac = BluetoothController.deviceMac ?: return
            val device = bluetoothAdapter?.getRemoteDevice(mac) ?: return
            BluetoothController.connect(device) { success ->
                runOnUiThread {
                    isConnecting = false
                    updateConnectionUI(success)
                }
            }
        }
    }

    private fun disconnectVehicle() {
        isManualDisconnect = true
        handler.removeCallbacks(reconnectRunnable)
        if (isBleMode) {
            BleManager.disconnect()
        } else {
            BluetoothController.disconnect()
        }
        updateConnectionUI(false)
        updatePowerUI(false)
    }

    private fun updateConnectionUI(connected: Boolean) {
        syncStateToWatch()
        val tvStatus = findViewById<TextView>(R.id.tvStatus)
        val btnConnect = findViewById<Button>(R.id.btnUnlock)
        val tvFpSensorStatus = findViewById<TextView>(R.id.tvFpSensorStatus)
        val name = if (isBleMode) BleManager.deviceName else BluetoothController.deviceName

        if (connected) {
            viewConnectionDot?.setBackgroundResource(R.drawable.badge_dot_green)
            val tvTitle = findViewById<TextView>(R.id.tvTitle)
            tvTitle?.text = vehicleCustomName
            tvStatus.text = "BLE CONNECTED • Đã kết nối"
            tvStatus.setTextColor(getColor(R.color.secondary_teal))
            btnConnect.text = getString(R.string.btn_disconnect_bt)
            tvFpSensorStatus?.text = "Cảm biến R503: Sẵn sàng hoạt động"
            tvFpSensorStatus?.setTextColor(getColor(R.color.secondary_teal))
            requestFingerprintList()
            handler.removeCallbacks(rssiPollRunnable)
            handler.post(rssiPollRunnable)
        } else {
            dismissEnrollDialog()
            viewConnectionDot?.setBackgroundResource(R.drawable.badge_dot_red)
            if (isOn) {
                // CƠ CHẾ AN TOÀN Ô TÔ/XE MÁY: Xe vẫn nổ máy chạy bình thường ngoài đường
                tvStatus.text = "⚠️ Mất kết nối khi xe đang nổ máy (Xe vẫn chạy an toàn)"
                tvStatus.setTextColor(getColor(R.color.accent_amber))
                tvBikeStatusPill?.text = "⚠️ XE VẪN ĐANG BẬT"
                tvBikeStatusPill?.setTextColor(getColor(R.color.accent_amber))
                tvFpSensorStatus?.text = "Chạm vân tay hoặc tắt khóa cơ để khóa xe"
                tvFpSensorStatus?.setTextColor(getColor(R.color.accent_amber))
            } else {
                tvStatus.text = "DISCONNECTED • Chưa kết nối"
                tvStatus.setTextColor(getColor(R.color.text_secondary))
                tvFpSensorStatus?.text = "Xe chưa kết nối (Chạm quét để kết nối)"
                tvFpSensorStatus?.setTextColor(getColor(R.color.text_secondary))
            }
            btnConnect.text = getString(R.string.btn_connect_bt)
            handler.removeCallbacks(rssiPollRunnable)
            tvBleRssiVal?.text = "-- dBm"
            if (isAutoConnectEnabled && !isManualDisconnect) {
                handler.removeCallbacks(reconnectRunnable)
                handler.postDelayed(reconnectRunnable, 4000)
            }
        }
    }

    private fun syncStateToWatch() {
        try {
            val putDataMapReq = PutDataMapRequest.create("/status")
            putDataMapReq.dataMap.putBoolean("esp_connected", isConnectedToVehicle())
            putDataMapReq.dataMap.putBoolean("power_on", isOn)
            putDataMapReq.dataMap.putLong("timestamp", System.currentTimeMillis())

            val putDataReq = putDataMapReq.asPutDataRequest()
            putDataReq.setUrgent()

            Wearable.getDataClient(this).putDataItem(putDataReq)
                .addOnSuccessListener { Log.d("WEAR_SYNC", "State synced: ESP=${isConnectedToVehicle()}, ON=$isOn") }
                .addOnFailureListener { e -> Log.e("WEAR_SYNC", "Sync failed", e) }
        } catch (e: Exception) {
            Log.e("WEAR_SYNC", "Sync error", e)
        }
    }

    private fun updatePowerUI(on: Boolean) {
        isOn = on
        syncStateToWatch()

        val btnToggle = findViewById<Button>(R.id.btnToggle)
        val pulseRing = findViewById<View>(R.id.pulseRing)

        btnToggle.text = getString(if (on) R.string.btn_turn_off else R.string.btn_turn_on)
        btnToggle.setBackgroundResource(if (on) R.drawable.button_power_on_bg else R.drawable.button_power_bg)
        btnToggle.setTextColor(getColor(R.color.white))
        btnToggle.compoundDrawableTintList = android.content.res.ColorStateList.valueOf(Color.WHITE)

        if (on) {
            tvBikeStatusPill?.text = "⚡ ĐÃ MỞ KHÓA"
            tvBikeStatusPill?.setTextColor(getColor(R.color.accent_cyan))
            viewHeadlightGlow?.visibility = View.VISIBLE
            ivBikeSilhouette?.alpha = 1.0f
            startPulseAnimation(pulseRing, getColor(R.color.primary_blue))
        } else {
            tvBikeStatusPill?.text = "🔒 ĐANG KHÓA"
            tvBikeStatusPill?.setTextColor(getColor(R.color.accent_danger))
            viewHeadlightGlow?.visibility = View.GONE
            ivBikeSilhouette?.alpha = 0.55f
            pulseRing.clearAnimation()
            pulseRing.visibility = View.GONE

            // Reset engine running state when vehicle is locked
            isEngineRunning = false
            tvEngineBadge?.text = "Relay 2"
            tvEngineBadge?.setTextColor(getColor(R.color.text_muted))
            tvEngineDesc?.text = "Khởi động máy"
            ivEngineIcon?.setColorFilter(getColor(R.color.secondary_teal))
        }
    }

    private fun startPulseAnimation(view: View, colorInt: Int) {
        view.visibility = View.VISIBLE
        view.backgroundTintList = android.content.res.ColorStateList.valueOf(colorInt)
        val anim = AlphaAnimation(0.1f, 0.5f)
        anim.duration = 1000
        anim.repeatMode = Animation.REVERSE
        anim.repeatCount = Animation.INFINITE
        view.startAnimation(anim)
    }

    private fun withBluetoothPermission(action: () -> Unit) {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_SCAN)
        } else {
            arrayOf(android.Manifest.permission.ACCESS_FINE_LOCATION)
        }

        if (permissions.any { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }) {
            pendingPermissionAction = action
            requestPermissions(permissions, 1)
        } else {
            action()
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
            pendingPermissionAction?.invoke()
        }
        pendingPermissionAction = null
    }

    override fun onDestroy() {
        super.onDestroy()
        handler.removeCallbacks(rssiPollRunnable)
        disconnectVehicle()
    }
}
