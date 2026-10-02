package com.example.control_esp

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.ClipData
import android.content.ClipboardManager
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.AlphaAnimation
import android.view.animation.Animation
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.graphics.Typeface
import android.content.ContentValues
import android.provider.MediaStore
import android.os.Environment
import android.util.Base64
import android.widget.*
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.AppCompatButton
import androidx.appcompat.widget.SwitchCompat
import androidx.cardview.widget.CardView
import android.content.res.ColorStateList
import androidx.core.content.ContextCompat
import androidx.core.widget.ImageViewCompat
import com.google.android.gms.wearable.*
import com.google.android.material.bottomnavigation.BottomNavigationView
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
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
    private var currentEstimatedDistance: Float = -1.0f
    private var currentBleRssi: Int = 0
    private var smoothedRssi: Float = 0f
    private var cbAutoStartTab2: androidx.appcompat.widget.SwitchCompat? = null
    private var cbAutoConnectTab2: androidx.appcompat.widget.SwitchCompat? = null
    private var cbBackgroundRunTab2: androidx.appcompat.widget.SwitchCompat? = null
    private var isBackgroundServiceEnabled = true

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
            handler.postDelayed(this, 1200)
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
    private val indexedImageChunks = java.util.concurrent.ConcurrentHashMap<Int, ByteArray>()
    private var expectedTotalChunks: Int = 134
    private var incomingImageWidth: Int = 160
    private var incomingImageHeight: Int = 160
    private var currentImageWidth: Int = 160
    private var currentImageHeight: Int = 160

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

    // Quản lý Báo Động Chống Dắt (Anti-Theft SW-420)
    private var isAntiTheftActive = false
    private var tvAntiTheftStatusRef: TextView? = null
    private var tvAntiTheftBadgeRef: TextView? = null

    private fun updateAntiTheftUI(active: Boolean) {
        runOnUiThread {
            isAntiTheftActive = active
            if (active) {
                tvAntiTheftStatusRef?.text = "Đang kích hoạt • Giám sát rung"
                tvAntiTheftStatusRef?.setTextColor(ContextCompat.getColor(this, R.color.secondary_teal))
                tvAntiTheftBadgeRef?.text = "BẢO VỆ"
                tvAntiTheftBadgeRef?.setTextColor(ContextCompat.getColor(this, R.color.secondary_teal))
            } else {
                tvAntiTheftStatusRef?.text = "Đang tắt (Chạm để bật)"
                tvAntiTheftStatusRef?.setTextColor(ContextCompat.getColor(this, R.color.text_muted))
                tvAntiTheftBadgeRef?.text = "TẮT"
                tvAntiTheftBadgeRef?.setTextColor(ContextCompat.getColor(this, R.color.text_secondary))
            }
        }
    }

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

    // Hiển thị ảnh quang học R503 trực tiếp trên Tab Vân tay
    private var ivTabFingerprintImageRef: ImageView? = null
    private var pbTabImageProgressRef: ProgressBar? = null
    private var tvTabImageStatusRef: TextView? = null
    private var tvTabImageDescRef: TextView? = null
    private var btnCaptureLiveImageRef: AppCompatButton? = null
    private var isCapturingLiveImage = false

    // Hiển thị ảnh vân tay và trạng thái test trong dialog tinh chỉnh
    private var ivFpTestImageRef: ImageView? = null
    private var pbFpTestImageProgressRef: ProgressBar? = null
    private var tvFpTestIdBadgeRef: TextView? = null
    private var tvFpTestImageInfoRef: TextView? = null

    // Quản lý Cấu hình Đèn Vòng Màu R503 (Aura RGB)
    data class LedEventState(var mode: Int, var color: Int, var speed: Int)
    private var ledUnlocked = LedEventState(1, 2, 120) // Breathing Blue
    private var ledLocked   = LedEventState(4, 2, 0)   // Always OFF
    private var ledSuccess  = LedEventState(2, 2, 40)  // Flashing Blue
    private var ledError    = LedEventState(2, 1, 30)  // Flashing Red
    private var ledConfigDialog: AlertDialog? = null
    private var currentLedTab = 0 // 0: Unlocked, 1: Locked, 2: Success, 3: Error
    private var rgbColorWheelRef: RgbColorWheelView? = null
    private var tvSelectedColorDisplayRef: TextView? = null
    private var rgEffectModeRef: RadioGroup? = null
    private var sbEffectSpeedRef: SeekBar? = null
    private var tvSpeedValueRef: TextView? = null
    private var tvCurrentEditingBadgeRef: TextView? = null
    private var btnTabUnlockedRef: Button? = null
    private var btnTabLockedRef: Button? = null
    private var btnTabSuccessRef: Button? = null
    private var btnTabErrorRef: Button? = null

    // Nâng cấp Firmware OTA qua BLE
    private var selectedOtaFileUri: android.net.Uri? = null
    private var btnStartOtaRef: AppCompatButton? = null
    private var tvFileInfoRef: TextView? = null
    private var tvFileMd5Ref: TextView? = null
    private var layoutOtaProgressRef: View? = null
    private var progressBarOtaRef: ProgressBar? = null
    private var tvOtaPercentRef: TextView? = null
    private var tvOtaStatusRef: TextView? = null
    private var tvOtaSpeedRef: TextView? = null

    private val selectFirmwareLauncher = registerForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.GetContent()
    ) { uri: android.net.Uri? ->
        if (uri != null) {
            selectedOtaFileUri = uri
            onOtaFileSelected(uri)
        }
    }

    // 🛡️ Quản lý Nhật Ký Bắt Quả Tang Vân Tay Lạ (Intruder Audit)
    data class IntruderLogItem(
        val index: Int,
        val filename: String,
        val timeStr: String,
        val fileSize: Long
    )
    private val intruderLogList = mutableListOf<IntruderLogItem>()
    private var intruderCount = 0
    private var tvIntruderBadgeRef: TextView? = null
    private var tvIntruderCountTagRef: TextView? = null
    private var intruderAdapter: IntruderLogAdapter? = null
    private var ivIntruderFingerprintRef: ImageView? = null
    private var llIntruderImagePreviewRef: View? = null
    private var tvIntruderImageTimeHeaderRef: TextView? = null
    private var pbIntruderLoadingRef: ProgressBar? = null
    private var tvIntruderEmptyRef: TextView? = null
    private var rvIntruderListRef: androidx.recyclerview.widget.RecyclerView? = null
    private var currentIntruderBitmap: Bitmap? = null
    private var isIntruderImageInverted = false
    private var isFetchingIntruderImage = false
    private var intruderCaptureEnabled = true
    private var swIntruderEnableRef: androidx.appcompat.widget.SwitchCompat? = null
    private var crankTimeMs = 1500

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

    // Bộ lắng nghe sự kiện Ghép Đôi Phần Cứng (BLE SMP Bonding & Pairing)
    private val bondStateReceiver = object : android.content.BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: android.content.Intent?) {
            if (intent?.action == BluetoothDevice.ACTION_BOND_STATE_CHANGED) {
                val bondState = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)
                val prevBondState = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, BluetoothDevice.BOND_NONE)
                val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                }
                val currentMac = if (isBleMode) BleManager.activeMac else BluetoothController.deviceMac
                if (device != null && currentMac != null && device.address.equals(currentMac, ignoreCase = true)) {
                    handleBondStateChanged(bondState, prevBondState)
                }
            }
        }
    }

    private fun handleBondStateChanged(bondState: Int, prevBondState: Int) {
        val tvStatus = findViewById<TextView?>(R.id.tvStatus)
        when (bondState) {
            BluetoothDevice.BOND_BONDING -> {
                Log.i("BLE_SEC", "Bonding in progress...")
                tvStatus?.text = "🔐 Đang ghép đôi... Nhập mã PIN 6 số"
                tvStatus?.setTextColor(getColor(R.color.accent_amber))
            }
            BluetoothDevice.BOND_BONDED -> {
                Log.i("BLE_SEC", "Bonding successful! Hardware AES-128 active.")
                tvStatus?.text = "BLE CONNECTED • Mã hóa AES-128"
                tvStatus?.setTextColor(getColor(R.color.secondary_teal))
                Toast.makeText(this, "🟢 Đã ghép đôi thành công! Kết nối được mã hóa AES-128.", Toast.LENGTH_LONG).show()
                triggerHapticFeedback()
                requestFingerprintList()
            }
            BluetoothDevice.BOND_NONE -> {
                if (prevBondState == BluetoothDevice.BOND_BONDING) {
                    Log.w("BLE_SEC", "Bonding failed or cancelled by user.")
                    tvStatus?.text = "❌ Ghép đôi thất bại / Sai mã PIN!"
                    tvStatus?.setTextColor(getColor(R.color.accent_danger))
                    Toast.makeText(this, "❌ Ghép đôi thất bại! Vui lòng nhập đúng mã PIN xe.", Toast.LENGTH_LONG).show()
                    triggerHapticFeedback()
                }
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

        // Khởi tạo BleManager & Đăng ký Receiver theo dõi Bonding
        BleManager.init(this)
        val bondFilter = android.content.IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        registerReceiver(bondStateReceiver, bondFilter)

        loadDevice()

        val btnConnect = findViewById<Button>(R.id.btnUnlock)
        val btnToggle = findViewById<Button>(R.id.btnToggle)
        val tvStatus = findViewById<TextView>(R.id.tvStatus)
        val header = findViewById<View>(R.id.header)
        val bottomNav = findViewById<BottomNavigationView>(R.id.bottom_navigation)

        // Tính toán và áp dụng Status Bar Insets chống bị đồng hồ/tai thỏ/notch che tên xe
        val density = resources.displayMetrics.density
        var initialStatusBarHeight = 0
        val statusBarResId = resources.getIdentifier("status_bar_height", "dimen", "android")
        if (statusBarResId > 0) {
            initialStatusBarHeight = resources.getDimensionPixelSize(statusBarResId)
        }
        val headerBaseTop = (12 * density).toInt()
        val headerBaseBottom = (10 * density).toInt()
        header.setPadding(header.paddingLeft, initialStatusBarHeight + headerBaseTop, header.paddingRight, headerBaseBottom)

        androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(header) { view, insets ->
            val statusBars = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.statusBars())
            val topPad = if (statusBars.top > 0) statusBars.top else initialStatusBarHeight
            view.setPadding(view.paddingLeft, topPad + headerBaseTop, view.paddingRight, headerBaseBottom)
            insets
        }

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

        // Header Actions (Khớp 100% Web Mockup & Hỗ trợ Đổi tên xe)
        viewConnectionDot = findViewById(R.id.viewConnectionDot)
        tvTitleRef = findViewById(R.id.tvTitle)
        tvTitleRef?.setOnClickListener { showRenameVehicleDialog() }
        findViewById<View?>(R.id.llHeaderTitleContainer)?.setOnClickListener { showRenameVehicleDialog() }
        findViewById<View?>(R.id.ivHeaderEditName)?.setOnClickListener { showRenameVehicleDialog() }
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
        cbBackgroundRunTab2 = findViewById(R.id.cbBackgroundRunTab2)
        findViewById<Button>(R.id.btnFindBikeTab2)?.setOnClickListener {
            handleFindVehicle()
        }

        val tabHome = findViewById<View>(R.id.tab_home)
        val tabAdvanced = findViewById<View>(R.id.tab_advanced)
        val tabSettings = findViewById<View>(R.id.tab_settings)
        val tabFingerprint = findViewById<View>(R.id.tab_fingerprint)

        val cbAutoConnect = findViewById<android.widget.CompoundButton>(R.id.cbAutoConnect)
        val cbAutoStart = findViewById<android.widget.CompoundButton>(R.id.cbAutoStart)
        val cbBackgroundRun = findViewById<android.widget.CompoundButton?>(R.id.cbBackgroundRun)
        val btnBatteryOptimization = findViewById<Button?>(R.id.btnBatteryOptimization)
        val cbShowInfo = findViewById<android.widget.CompoundButton>(R.id.cbShowInfo)
        val cbHideNav = findViewById<android.widget.CompoundButton>(R.id.cbHideNav)
        val btnMinus = findViewById<Button>(R.id.btnMinus)
        val btnPlus = findViewById<Button>(R.id.btnPlus)
        val tvDelayValue = findViewById<TextView>(R.id.tvDelayValue)

        btnBatteryOptimization?.setOnClickListener {
            requestIgnoreBatteryOptimization()
        }

        // Card Chống dắt (Anti-theft)
        val btnAntiTheftCard = findViewById<View>(R.id.btnAntiTheftCard)
        tvAntiTheftStatusRef = findViewById(R.id.tvAntiTheftStatus)
        tvAntiTheftBadgeRef = findViewById(R.id.tvAntiTheftBadge)
        updateAntiTheftUI(false)

        btnAntiTheftCard?.setOnClickListener {
            if (!isConnectedToVehicle()) {
                Toast.makeText(this, "Vui lòng kết nối xe trước khi bật/tắt chống dắt!", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            val targetState = !isAntiTheftActive
            sendVehicleCommand("SET_ALARM|" + (if (targetState) "1" else "0"))
            triggerHapticFeedback()
        }

        // Các nút trong tab vân tay & cài đặt mới
        val btnAddFp = findViewById<View>(R.id.btnAddFp)
        val btnRefreshFp = findViewById<View>(R.id.btnRefreshFp)
        val btnClearAllFp = findViewById<View>(R.id.btnClearAllFp)
        val btnViewHistory = findViewById<View>(R.id.btnViewHistory)
        val btnScanBle = findViewById<Button>(R.id.btnScanBle)
        val btnChangeKey = findViewById<Button>(R.id.btnChangeKey)

        // Card Trạng thái Đồng hồ Thông minh Wear OS trong Cài đặt
        val btnSyncWatchNow = findViewById<AppCompatButton?>(R.id.btnSyncWatchNow)
        btnSyncWatchNow?.setOnClickListener {
            WatchSyncHelper.syncCurrentStateToWatch(this)
            WatchSyncHelper.checkWatchConnection(this) { isConn, name ->
                updateWatchStatusUI(isConn, name)
                runOnUiThread {
                    Toast.makeText(
                        this,
                        if (isConn) "🟢 Đã kết nối với $name & đồng bộ thành công!" else "⚪ Chưa phát hiện đồng hồ kết nối. Hãy mở app trên Wear OS!",
                        Toast.LENGTH_SHORT
                    ).show()
                }
            }
        }

        WatchSyncHelper.onWatchConnectionStatusChanged = { isConn, name ->
            updateWatchStatusUI(isConn, name)
        }

        // Kiểm tra ban đầu
        WatchSyncHelper.checkWatchConnection(this) { isConn, name ->
            updateWatchStatusUI(isConn, name)
        }

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

        // Card Ảnh Vân Tay Quang Học R503 trên Tab Vân tay
        val ivTabFpImg = findViewById<ImageView?>(R.id.ivTabFingerprintImage)
        val pbTabFpProg = findViewById<ProgressBar?>(R.id.pbTabImageProgress)
        val tvTabFpStatus = findViewById<TextView?>(R.id.tvTabImageStatus)
        val tvTabFpDesc = findViewById<TextView?>(R.id.tvTabImageDesc)
        val btnCaptureLive = findViewById<AppCompatButton?>(R.id.btnCaptureLiveImage)

        ivTabFingerprintImageRef = ivTabFpImg
        pbTabImageProgressRef = pbTabFpProg
        tvTabImageStatusRef = tvTabFpStatus
        tvTabImageDescRef = tvTabFpDesc
        btnCaptureLiveImageRef = btnCaptureLive

        loadCachedFingerprintImage()

        val flRing = findViewById<View?>(R.id.flScannerRing)
        val onImageClick = View.OnClickListener {
            val file = File(cacheDir, "last_r503_scan.png")
            if (file.exists()) {
                val cachedBmp = BitmapFactory.decodeFile(file.absolutePath)
                if (cachedBmp != null) {
                    showFingerprintImageZoomDialog(cachedBmp)
                    return@OnClickListener
                }
            }
            if (lastRawFingerprintBytes != null) {
                val bmp = decodeR503ImageToBitmap(lastRawFingerprintBytes!!, currentImageWidth, currentImageHeight, isOpticalInvertMode)
                if (bmp != null) {
                    showFingerprintImageZoomDialog(bmp)
                    return@OnClickListener
                }
            }
            Toast.makeText(this, "Chưa có ảnh quét thực tế. Bấm 'Chụp Lăng Kính' để chụp!", Toast.LENGTH_SHORT).show()
        }
        ivTabFpImg?.setOnClickListener(onImageClick)
        flRing?.setOnClickListener(onImageClick)

        btnCaptureLive?.setOnClickListener {
            if (!isConnectedToVehicle()) {
                Toast.makeText(this, "Vui lòng kết nối xe trước khi chụp ảnh!", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            if (isCapturingLiveImage) {
                Toast.makeText(this, "Đang trong tiến trình chụp ảnh...", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            startLiveFingerprintCapture()
        }

        val btnLiveTestQuick = findViewById<View?>(R.id.btnLiveTestQuick)
        btnLiveTestQuick?.setOnClickListener {
            if (!isConnectedToVehicle()) {
                Toast.makeText(this, "Vui lòng kết nối xe trước khi quét thử!", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            sendVehicleCommand("TEST_FP|15")
            Toast.makeText(this, "Đã kích hoạt quét thử đọc mã vân tay (15s). Hãy chạm R503!", Toast.LENGTH_SHORT).show()
            tvFpSensorStatus?.text = "🔍 Đang quét thử đọc mã (15s)... Hãy chạm ngón tay vào R503"
            tvFpSensorStatus?.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
        }

        val btnCopyBase64Quick = findViewById<View?>(R.id.btnCopyBase64Quick)
        btnCopyBase64Quick?.setOnClickListener {
            copyFingerprintBmpBase64ToClipboard()
        }

        // 3. Công tắc bật/tắt nhanh chế độ truyền ảnh quang học BLE (tiết kiệm băng thông & vi xử lý)
        val swTabImageMode = findViewById<SwitchCompat?>(R.id.swTabImageMode)
        val tvTabImageModeSub = findViewById<TextView?>(R.id.tvTabImageModeSubtitle)
        val btnToggleImageMode = findViewById<View?>(R.id.btnToggleImageMode)

        val isImgModeOn = getSharedPreferences("BT_PREF", MODE_PRIVATE).getBoolean("FP_SEND_IMG", false)
        swTabImageMode?.isChecked = isImgModeOn
        tvTabImageModeSub?.text = if (isImgModeOn) "🟢 Đang BẬT: Truyền ảnh quang học (Đầy đủ trực quan)" else "⚪ Đang TẮT: Quẹt xe siêu tốc, tiết kiệm băng thông & CPU"

        swTabImageMode?.setOnCheckedChangeListener { _, isChecked ->
            fpSendImageVal = isChecked
            getSharedPreferences("BT_PREF", MODE_PRIVATE).edit().putBoolean("FP_SEND_IMG", isChecked).apply()
            swFpSendImageRef?.isChecked = isChecked
            tvTabImageModeSub?.text = if (isChecked) "🟢 Đang BẬT: Truyền ảnh quang học (Đầy đủ trực quan)" else "⚪ Đang TẮT: Quẹt xe siêu tốc, tiết kiệm băng thông & CPU"
            if (isConnectedToVehicle()) {
                sendVehicleCommand("SET_FP_IMG_MODE|" + if (isChecked) "1" else "0")
            }
            Toast.makeText(this, if (isChecked) "Đã BẬT nhận ảnh vân tay!" else "Đã TẮT nhận ảnh (Tiết kiệm băng thông & CPU)!", Toast.LENGTH_SHORT).show()
        }

        btnToggleImageMode?.setOnClickListener {
            swTabImageMode?.toggle()
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

        findViewById<Button?>(R.id.btnRenameVehicle)?.setOnClickListener {
            showRenameVehicleDialog()
        }
        findViewById<View?>(R.id.llVehicleNameClickArea)?.setOnClickListener {
            showRenameVehicleDialog()
        }

        btnChangeKey.setOnClickListener {
            if (isConnectedToVehicle()) {
                showChangeKeyDialog()
            } else {
                Toast.makeText(this, "Hãy kết nối xe trước khi đổi mã", Toast.LENGTH_SHORT).show()
            }
        }

        val btnUnpairAllBle = findViewById<View?>(R.id.btnUnpairAllBle)
        btnUnpairAllBle?.setOnClickListener {
            if (!isConnectedToVehicle()) {
                Toast.makeText(this, "Hãy kết nối xe trước khi thu hồi quyền!", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            AlertDialog.Builder(this)
                .setTitle("🛡️ Thu Hồi Toàn Bộ Quyền Ghép Đôi")
                .setMessage("Bạn có chắc chắn muốn xóa toàn bộ danh sách điện thoại đã ghép đôi (Bonding) trên xe?\n\nSau khi xóa, bất kỳ điện thoại nào (kể cả máy phụ hoặc người khác) muốn điều khiển xe đều phải nhập lại mã PIN bảo mật.")
                .setPositiveButton("Xóa Tất Cả") { _, _ ->
                    sendVehicleCommand("UNPAIR_ALL")
                    Toast.makeText(this, "Đang gửi lệnh thu hồi danh sách ghép đôi...", Toast.LENGTH_SHORT).show()
                }
                .setNegativeButton("Hủy", null)
                .show()
        }

        btnViewHistory.setOnClickListener {
            showUnlockHistoryDialog()
        }

        val btnIntruderAudit = findViewById<View?>(R.id.btnIntruderAudit)
        tvIntruderBadgeRef = findViewById<TextView?>(R.id.tvIntruderBadge)
        tvIntruderCountTagRef = findViewById<TextView?>(R.id.tvIntruderCountTag)
        btnIntruderAudit?.setOnClickListener {
            showIntruderAuditDialog()
        }

        val btnOtaUpdate = findViewById<Button?>(R.id.btnOtaUpdate)
        btnOtaUpdate?.setOnClickListener {
            showOtaUpdateDialog()
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

        // Tự động khởi động dịch vụ chạy ngầm nếu được bật
        if (isBackgroundServiceEnabled) {
            VehicleBackgroundService.startService(this)
        }

        // Đăng ký nhận phản hồi từ cả BLE và Classic BT
        BleManager.onMessageReceived = { handleVehicleFeedback(it) }
        BluetoothController.onMessageReceived = { handleVehicleFeedback(it) }
        BleManager.onRssiRead = { rssi ->
            runOnUiThread {
                currentBleRssi = rssi
                smoothedRssi = if (smoothedRssi == 0f) rssi.toFloat() else (smoothedRssi * 0.6f + rssi * 0.4f)
                val effectiveRssi = Math.round(smoothedRssi)
                tvBleRssiVal?.text = "$rssi dBm"
                tvFinderRssiDesc?.text = "Tín hiệu Bluetooth: $rssi dBm"
                val dist = BleManager.calculateDistance(effectiveRssi)
                currentEstimatedDistance = dist
                tvFinderDistance?.text = String.format(Locale.US, "Khoảng cách: ~ %.1f Mét", dist)
                syncStateToWatch()
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
                    // 🛡️ Đồng bộ thời gian thực (RTC) 1 lần duy nhất khi kết nối BLE để lưu vết vân tay lạ
                    val epochSec = System.currentTimeMillis() / 1000
                    sendVehicleCommand("SYNC_TIME|$epochSec")
                }
            }
        }

        // Chuyển Tab BottomNav
        bottomNav.setOnItemSelectedListener { item ->
            tabHome.visibility = if (item.itemId == R.id.nav_home) View.VISIBLE else View.GONE
            tabAdvanced.visibility = if (item.itemId == R.id.nav_advanced) View.VISIBLE else View.GONE
            tabSettings.visibility = if (item.itemId == R.id.nav_settings) View.VISIBLE else View.GONE
            tabFingerprint.visibility = if (item.itemId == R.id.nav_fingerprint) View.VISIBLE else View.GONE

            if (item.itemId == R.id.nav_settings) {
                WatchSyncHelper.checkWatchConnection(this) { isConn, name ->
                    updateWatchStatusUI(isConn, name)
                }
            }

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
        cbBackgroundRun?.isChecked = isBackgroundServiceEnabled
        cbBackgroundRunTab2?.isChecked = isBackgroundServiceEnabled

        cbBackgroundRun?.setOnCheckedChangeListener { _, isChecked ->
            isBackgroundServiceEnabled = isChecked
            if (cbBackgroundRunTab2?.isChecked != isChecked) {
                cbBackgroundRunTab2?.isChecked = isChecked
            }
            saveSettings()
            toggleBackgroundService(isChecked)
        }

        cbBackgroundRunTab2?.setOnCheckedChangeListener { _, isChecked ->
            isBackgroundServiceEnabled = isChecked
            if (cbBackgroundRun?.isChecked != isChecked) {
                cbBackgroundRun?.isChecked = isChecked
            }
            saveSettings()
            toggleBackgroundService(isChecked)
        }

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

        val btnConfigCrankTime = findViewById<View?>(R.id.btnConfigCrankTime)
        btnConfigCrankTime?.setOnClickListener {
            showCrankTimeSettingsDialog()
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

        btnStartHero?.setOnLongClickListener {
            showCrankTimeSettingsDialog()
            true
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

        val btnFpSettings = findViewById<View>(R.id.btnFpSettings)
        btnFpSettings?.setOnClickListener {
            showFingerprintSettingsDialog()
        }

        val btnR503LedSettings = findViewById<View>(R.id.btnR503LedSettings)
        btnR503LedSettings?.setOnClickListener {
            showR503LedSettingsDialog()
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

        val secStr = String.format(java.util.Locale.US, "%.1fs", crankTimeMs / 1000f)
        tvEngineBadge?.text = "$secStr Đang đề..."
        tvEngineBadge?.setTextColor(getColor(R.color.accent_amber))
        tvEngineDesc?.text = "Đang quay củ đề ($crankTimeMs ms)..."
        ivEngineIcon?.setColorFilter(getColor(R.color.accent_amber))

        ivBikeSilhouette?.let { bike ->
            val shakeAnim = android.view.animation.TranslateAnimation(-4f, 4f, 0f, 0f).apply {
                duration = 50
                repeatMode = Animation.REVERSE
                repeatCount = (crankTimeMs / 50).coerceIn(4, 100)
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
        }, crankTimeMs.toLong())
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
        if (status.startsWith("OTA_")) {
            OtaManager.handleBleFeedback(status)
            return
        }

        // 1. Nhận chunk ảnh: hỗ trợ cả định dạng mới có index (FP_IMG_CHUNK|<seq>|<total>|<b64>) và định dạng cũ
        if (status.startsWith("FP_IMG_CHUNK|")) {
            val payload = status.substringAfter("FP_IMG_CHUNK|").trim()
            val parts = payload.split("|", limit = 3)
            if (parts.size >= 3) {
                // Giao thức mới: FP_IMG_CHUNK|<seq>|<total>|<base64>
                val seq = parts[0].toIntOrNull()
                val tot = parts[1].toIntOrNull() ?: expectedTotalChunks
                val b64 = parts[2].trim()
                if (seq != null && b64.isNotEmpty()) {
                    expectedTotalChunks = tot
                    try {
                        val decodedBytes = Base64.decode(b64, Base64.DEFAULT)
                        if (decodedBytes != null && decodedBytes.isNotEmpty()) {
                            indexedImageChunks[seq] = decodedBytes
                        }
                    } catch (e: Exception) {
                        Log.w("FP_IMG", "Decode chunk $seq failed: ${e.message}")
                    }
                }
            } else if (payload.isNotEmpty()) {
                // Tương thích ngược: FP_IMG_CHUNK|<base64>
                synchronized(enrollImageBuffer) {
                    enrollImageBuffer.append(payload)
                }
            }
            return
        }

        // 2. Kết thúc nhận ảnh: Giải mã và xử lý Bitmap trong Thread riêng, giải phóng Main UI Thread
        if (status.startsWith("FP_IMG_END")) {
            val endReportedBytes = if (status.contains("|")) status.substringAfter("FP_IMG_END|").trim().toIntOrNull() else null
            Thread {
                var decodedBmp: Bitmap? = null
                if (indexedImageChunks.isNotEmpty()) {
                    // ƯU TIÊN 1: Lắp ráp chính xác tuyệt đối theo index chunk (Chống trôi, chống đen/trắng lệch dòng 100%)
                    val maxSeq = indexedImageChunks.keys.maxOrNull() ?: -1
                    val totalChunks = Math.max(expectedTotalChunks, maxSeq + 1).coerceAtLeast(1)
                    val chunkSize = 96

                    // Tính toán tổng số byte thực tế nhận được từ các chunk
                    var calcTotalBytes = 0
                    for (seq in 0 until totalChunks) {
                        val cBytes = indexedImageChunks[seq]
                        if (cBytes != null) {
                            calcTotalBytes += cBytes.size
                        }
                    }

                    // Tự động nhận diện độ phân giải thực tế dựa trên số byte thu thập được / thông báo firmware:
                    // 12.800 bytes = 25.600 pixel = 160 x 160 (R503 Tròn chuẩn)
                    // 18.432 bytes = 36.864 pixel = 192 x 192 (R503 Vuông / TZM1026)
                    // 29.952 bytes = 59.904 pixel = 208 x 288 (R307)
                    val actualW: Int
                    val actualH: Int
                    val expectedBytes: Int

                    val targetBytes = endReportedBytes ?: if (calcTotalBytes in 12700..12850) 12800
                        else if (calcTotalBytes in 18350..18500) 18432
                        else if (calcTotalBytes in 29800..30100) 29952
                        else if (expectedTotalChunks == 134 || maxSeq in 100..133) 12800
                        else if (expectedTotalChunks == 192 || maxSeq in 134..191) 18432
                        else if (expectedTotalChunks == 312 || maxSeq > 191) 29952
                        else calcTotalBytes

                    when (targetBytes) {
                        12800 -> {
                            actualW = 160
                            actualH = 160
                            expectedBytes = 12800
                        }
                        18432 -> {
                            actualW = 192
                            actualH = 192
                            expectedBytes = 18432
                        }
                        29952 -> {
                            actualW = 208
                            actualH = 288
                            expectedBytes = 29952
                        }
                        else -> {
                            if (incomingImageWidth * incomingImageHeight / 2 == targetBytes && targetBytes > 0) {
                                actualW = incomingImageWidth
                                actualH = incomingImageHeight
                                expectedBytes = targetBytes
                            } else {
                                val totalPix = targetBytes * 2
                                val side = Math.round(Math.sqrt(totalPix.toDouble())).toInt()
                                if (side * side == totalPix && side > 0) {
                                    actualW = side
                                    actualH = side
                                    expectedBytes = targetBytes
                                } else if (totalPix % 160 == 0 && totalPix > 0) {
                                    actualW = 160
                                    actualH = totalPix / 160
                                    expectedBytes = targetBytes
                                } else if (totalPix % 192 == 0 && totalPix > 0) {
                                    actualW = 192
                                    actualH = totalPix / 192
                                    expectedBytes = targetBytes
                                } else {
                                    actualW = incomingImageWidth
                                    actualH = incomingImageHeight
                                    expectedBytes = targetBytes.coerceAtLeast(1)
                                }
                            }
                        }
                    }

                    currentImageWidth = actualW
                    currentImageHeight = actualH
                    val rawBytes = ByteArray(expectedBytes)
                    // Mặc định nền trắng quang học 0xFF
                    java.util.Arrays.fill(rawBytes, 0xFF.toByte())

                    var receivedChunksCount = 0
                    for (seq in 0 until totalChunks) {
                        val cBytes = indexedImageChunks[seq]
                        if (cBytes != null) {
                            receivedChunksCount++
                            val targetOffset = seq * chunkSize
                            val copyLen = Math.min(cBytes.size, expectedBytes - targetOffset)
                            if (targetOffset < expectedBytes && copyLen > 0) {
                                System.arraycopy(cBytes, 0, rawBytes, targetOffset, copyLen)
                            }
                        }
                    }
                    Log.i("FP_IMG", "✅ Đã lắp ráp trọn vẹn: $receivedChunksCount / $totalChunks chunks ($expectedBytes bytes) -> $actualW x $actualH px")
                    indexedImageChunks.clear()
                    synchronized(enrollImageBuffer) { enrollImageBuffer.setLength(0) }
                    lastRawFingerprintBytes = rawBytes
                    decodedBmp = decodeR503ImageToBitmap(rawBytes, actualW, actualH, isOpticalInvertMode)
                    try {
                        val bmpBytes = generateBmpByteArray(rawBytes, actualW, actualH)
                        val b64Str = "data:image/bmp;base64," + android.util.Base64.encodeToString(bmpBytes, android.util.Base64.NO_WRAP)
                        Log.i("FP_IMG_BASE64", "==================== [BẮT ĐẦU CHUỖI ẢNH BASE64 BMP R503] ====================")
                        Log.i("FP_IMG_BASE64", b64Str)
                        Log.i("FP_IMG_BASE64", "==================== [KẾT THÚC CHUỖI ẢNH BASE64 BMP R503] ====================")
                    } catch (e: Exception) {
                        Log.e("FP_IMG_BASE64", "Error logging base64 bmp", e)
                    }
                } else {
                    // ƯU TIÊN 2: Fallback giải mã chuỗi Base64 truyền thống
                    val fullB64: String
                    synchronized(enrollImageBuffer) {
                        fullB64 = enrollImageBuffer.toString()
                        enrollImageBuffer.setLength(0)
                    }
                    if (fullB64.isNotEmpty()) {
                        try {
                            decodedBmp = safeDecodeR503Base64ToBitmap(fullB64, incomingImageWidth, incomingImageHeight)
                        } catch (e: Exception) {
                            Log.e("FP_IMG", "Decode image error in background", e)
                        }
                    }
                }

                if (decodedBmp != null) {
                    saveCachedFingerprintImage(decodedBmp)
                }

                val bmp = decodedBmp
                runOnUiThread {
                    pbTabImageProgressRef?.visibility = View.GONE
                    pbFpTestImageProgressRef?.visibility = View.GONE
                    isCapturingLiveImage = false
                    if (bmp != null) {
                        val timeStr = SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date())

                        // 1. Hiển thị trong dialog thêm vân tay (nếu đang mở)
                        ivEnrollFingerprintImageRef?.let {
                            ImageViewCompat.setImageTintList(it, null)
                            it.imageTintList = null
                            it.colorFilter = null
                            it.setImageBitmap(bmp)
                        }
                        cvEnrollImageCardRef?.visibility = View.VISIBLE
                        val anim = AlphaAnimation(0.2f, 1f).apply { duration = 350 }
                        cvEnrollImageCardRef?.startAnimation(anim)
                        tvEnrollStepDescRef?.text = "📸 Đã nhận ảnh vân tay thành công!"

                        // 2. Cập nhật trực tiếp lên Card Ảnh ở Tab Vân tay
                        ivTabFingerprintImageRef?.let {
                            ImageViewCompat.setImageTintList(it, null)
                            it.imageTintList = null
                            it.colorFilter = null
                            it.setImageBitmap(bmp)
                        }
                        tvTabImageStatusRef?.text = "✅ Chụp ảnh quang học thành công"
                        tvTabImageDescRef?.text = "Độ phân giải ${bmp.width}x${bmp.height} px (508 DPI) • Đã nhận lúc $timeStr"

                        // 3. Cập nhật trực tiếp lên Card Ảnh trong Dialog Tinh chỉnh & Test (nếu đang mở)
                        ivFpTestImageRef?.let {
                            ImageViewCompat.setImageTintList(it, null)
                            it.imageTintList = null
                            it.colorFilter = null
                            it.setImageBitmap(bmp)
                        }
                        tvFpTestImageInfoRef?.text = "Ảnh quang học ${bmp.width}x${bmp.height} (508 DPI) • $timeStr"
                        if (tvFpTestStatusRef?.text?.contains("Đang nhận ảnh") == true) {
                            val currentText = tvFpTestStatusRef?.text.toString().replace("• Đang nhận ảnh quang học từ cảm biến...", "")
                            tvFpTestStatusRef?.text = "$currentText\n📸 Đã hiển thị ảnh lăng kính ($timeStr)"
                        } else {
                            tvFpTestStatusRef?.text = "📸 Đã chụp ảnh lăng kính thành công lúc $timeStr"
                        }

                        // 4. Nếu đang tải ảnh vân tay kẻ gian vi phạm (Intruder Audit)
                        if (isFetchingIntruderImage) {
                            isFetchingIntruderImage = false
                            currentIntruderBitmap = bmp
                            isIntruderImageInverted = false
                            pbIntruderLoadingRef?.visibility = View.GONE
                            ivIntruderFingerprintRef?.setImageBitmap(bmp)
                            llIntruderImagePreviewRef?.visibility = View.VISIBLE
                            val anim = AlphaAnimation(0.2f, 1f).apply { duration = 350 }
                            llIntruderImagePreviewRef?.startAnimation(anim)
                            Toast.makeText(this@MainActivity, "✅ Đã tải ảnh vân tay kẻ gian thành công!", Toast.LENGTH_SHORT).show()
                        }

                        triggerHapticFeedback()
                    } else {
                        if (isFetchingIntruderImage) {
                            isFetchingIntruderImage = false
                            pbIntruderLoadingRef?.visibility = View.GONE
                            Toast.makeText(this@MainActivity, "⚠️ Không thể giải mã ảnh kẻ gian!", Toast.LENGTH_SHORT).show()
                        }
                        tvTabImageStatusRef?.text = "⚠️ Không thể giải mã ảnh"
                        tvTabImageDescRef?.text = "Dữ liệu ảnh BLE rỗng hoặc không đúng định dạng."
                        tvFpTestStatusRef?.text = "⚠️ Lỗi giải mã ảnh vân tay!"
                    }
                }
            }.start()
            return
        }

        runOnUiThread {
            val msg = if (status.startsWith("FB|")) status.substringAfter("FB|") else status
            when {
                // 🛡️ XỬ LÝ NHẬT KÝ BẮT QUẢ TANG VÂN TAY LẠ (INTRUDER AUDIT)
                msg.startsWith("INTRUDER_COUNT|") -> {
                    val count = msg.substringAfter("INTRUDER_COUNT|").trim().toIntOrNull() ?: 0
                    updateIntruderBadgeUI(count)
                }
                msg.startsWith("INTRUDER_START") -> {
                    intruderLogList.clear()
                    pbIntruderLoadingRef?.visibility = View.VISIBLE
                    tvIntruderEmptyRef?.visibility = View.GONE
                    rvIntruderListRef?.visibility = View.VISIBLE
                }
                msg.startsWith("INTRUDER_ITEM|") -> {
                    // INTRUDER_ITEM|<index>|<filename>|<timeStr>|<fileSize>
                    val parts = msg.split("|")
                    if (parts.size >= 5) {
                        val idx = parts[1].toIntOrNull() ?: 0
                        val fname = parts[2]
                        val timeStr = parts[3]
                        val size = parts[4].toLongOrNull() ?: 12800L
                        if (intruderLogList.none { it.filename == fname }) {
                            intruderLogList.add(IntruderLogItem(idx, fname, timeStr, size))
                        }
                    }
                }
                msg.startsWith("INTRUDER_LIST_END") -> {
                    pbIntruderLoadingRef?.visibility = View.GONE
                    intruderAdapter?.notifyDataSetChanged()
                    updateIntruderBadgeUI(intruderLogList.size)
                    if (intruderLogList.isEmpty()) {
                        tvIntruderEmptyRef?.visibility = View.VISIBLE
                        rvIntruderListRef?.visibility = View.GONE
                    } else {
                        tvIntruderEmptyRef?.visibility = View.GONE
                        rvIntruderListRef?.visibility = View.VISIBLE
                    }
                }
                msg.startsWith("INTRUDER_CAPTURED|") -> {
                    val timeStr = msg.substringAfter("INTRUDER_CAPTURED|").trim()
                    updateIntruderBadgeUI(intruderCount + 1)
                    UnlockHistoryManager.addEvent(
                        this,
                        "🚨 Bắt quả tang vân tay lạ!",
                        UnlockHistoryManager.TYPE_ALARM,
                        "Thời gian: $timeStr (Đã lưu ảnh vào Flash)"
                    )
                    Toast.makeText(this, "🚨 Cảnh báo: Bắt quả tang vân tay lạ lúc $timeStr! Đã lưu ảnh vào bộ nhớ.", Toast.LENGTH_LONG).show()
                    triggerHapticFeedback()
                }
                msg.startsWith("INTRUDER_CLEARED") -> {
                    intruderLogList.clear()
                    intruderAdapter?.notifyDataSetChanged()
                    updateIntruderBadgeUI(0)
                    tvIntruderEmptyRef?.visibility = View.VISIBLE
                    rvIntruderListRef?.visibility = View.GONE
                    llIntruderImagePreviewRef?.visibility = View.GONE
                    currentIntruderBitmap = null
                    Toast.makeText(this, "Đã xóa sạch toàn bộ ảnh vân tay lạ trên xe!", Toast.LENGTH_SHORT).show()
                }
                msg.startsWith("INTRUDER_CFG|") -> {
                    val en = msg.substringAfter("INTRUDER_CFG|").trim() == "1"
                    intruderCaptureEnabled = en
                    swIntruderEnableRef?.isChecked = en
                    getSharedPreferences("BT_PREF", MODE_PRIVATE).edit().putBoolean("INTRUDER_CAPTURE_EN", en).apply()
                }
                msg.startsWith("CRANK_TIME|") -> {
                    val t = msg.substringAfter("CRANK_TIME|").trim().toIntOrNull() ?: 1500
                    crankTimeMs = t.coerceIn(200, 5000)
                    getSharedPreferences("BT_PREF", MODE_PRIVATE).edit().putInt("CRANK_TIME_MS", crankTimeMs).apply()
                }
                msg == "NO_INTRUDER_LOGS" -> {
                    pbIntruderLoadingRef?.visibility = View.GONE
                    intruderLogList.clear()
                    intruderAdapter?.notifyDataSetChanged()
                    updateIntruderBadgeUI(0)
                    tvIntruderEmptyRef?.visibility = View.VISIBLE
                    rvIntruderListRef?.visibility = View.GONE
                }

                status == "DA_MO_KHOA" || msg == "DA_MO_KHOA" -> {
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
                status.startsWith("VEHICLE_NAME|") -> {
                    val name = status.substringAfter("VEHICLE_NAME|").trim()
                    if (name.isNotEmpty()) {
                        val mac = if (isBleMode) BleManager.activeMac else BluetoothController.deviceMac
                        val prefs = getSharedPreferences("BT_PREF", MODE_PRIVATE)
                        val savedForMac = if (mac != null) prefs.getString("VEHICLE_NAME_$mac", null) else null
                        if (name != "XE_tsmart_BLE" || savedForMac == null) {
                            vehicleCustomName = name
                            val editor = prefs.edit().putString("VEHICLE_NAME", name)
                            if (mac != null) {
                                editor.putString("VEHICLE_NAME_$mac", name)
                            }
                            editor.apply()
                            updateDeviceNameDisplay()
                        } else if (savedForMac != null && name == "XE_tsmart_BLE") {
                            val key = if (isBleMode) BleManager.SECRET_KEY else BluetoothController.SECRET_KEY
                            sendVehicleCommand("$key|SET_NAME|$savedForMac")
                        }
                    }
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
                    val parts = status.split("|")
                    incomingImageWidth = if (parts.size > 1) parts[1].toIntOrNull() ?: 160 else 160
                    incomingImageHeight = if (parts.size > 2) parts[2].toIntOrNull() ?: 160 else 160
                    currentImageWidth = incomingImageWidth
                    currentImageHeight = incomingImageHeight
                    expectedTotalChunks = if (parts.size > 3) parts[3].toIntOrNull() ?: 134 else 134
                    indexedImageChunks.clear()
                    synchronized(enrollImageBuffer) {
                        enrollImageBuffer.setLength(0)
                    }
                    tvEnrollStepDescRef?.text = "📸 Đang truyền ảnh vân tay từ cảm biến ($expectedTotalChunks gói)..."
                    pbTabImageProgressRef?.visibility = View.VISIBLE
                    tvTabImageStatusRef?.text = "📸 Đang nhận dữ liệu ảnh ($expectedTotalChunks gói)..."
                    tvTabImageDescRef?.text = "Đang truyền các gói tin quang học qua Bluetooth..."
                    pbFpTestImageProgressRef?.visibility = View.VISIBLE
                    tvFpTestIdBadgeRef?.text = "NHẬN ẢNH"
                    tvFpTestStatusRef?.text = "📸 Đang truyền dữ liệu ảnh từ lăng kính quang học R503 ($expectedTotalChunks gói)..."
                }
                status == "FP_CAPTURE_WAIT" -> {
                    pbTabImageProgressRef?.visibility = View.VISIBLE
                    tvTabImageStatusRef?.text = "📸 Chờ chạm ngón tay..."
                    tvTabImageDescRef?.text = "Đèn cảm biến đang sáng tím. Hãy áp ngón tay và giữ êm."
                    pbFpTestImageProgressRef?.visibility = View.VISIBLE
                    tvFpTestIdBadgeRef?.text = "CHẠM R503"
                    tvFpTestIdBadgeRef?.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
                    tvFpTestStatusRef?.text = "📸 Đèn cảm biến đang sáng tím. Hãy áp ngón tay và giữ êm trên R503..."
                }
                status == "FP_CAPTURE_TIMEOUT" -> {
                    pbTabImageProgressRef?.visibility = View.GONE
                    pbFpTestImageProgressRef?.visibility = View.GONE
                    isCapturingLiveImage = false
                    tvTabImageStatusRef?.text = "⏱️ Hết thời gian chờ"
                    tvTabImageDescRef?.text = "Không phát hiện ngón tay chạm cảm biến R503."
                    tvFpTestIdBadgeRef?.text = "HẾT GIỜ"
                    tvFpTestIdBadgeRef?.setTextColor(ContextCompat.getColor(this, R.color.accent_amber))
                    tvFpTestStatusRef?.text = "⏱️ Hết thời gian chờ chạm ngón tay!"
                    Toast.makeText(this, "Hết thời gian chờ chạm ngón tay!", Toast.LENGTH_SHORT).show()
                }
                status == "FP_IMG_ERR" -> {
                    pbTabImageProgressRef?.visibility = View.GONE
                    pbFpTestImageProgressRef?.visibility = View.GONE
                    isCapturingLiveImage = false
                    tvTabImageStatusRef?.text = "❌ Lỗi đọc ảnh R503"
                    tvTabImageDescRef?.text = "Cảm biến từ chối lệnh trích xuất ảnh hoặc mất kết nối."
                    tvFpTestIdBadgeRef?.text = "LỖI ĐỌC"
                    tvFpTestIdBadgeRef?.setTextColor(ContextCompat.getColor(this, R.color.accent_danger))
                    tvFpTestStatusRef?.text = "❌ Cảm biến R503 báo lỗi trích xuất ảnh!"
                    Toast.makeText(this, "Cảm biến R503 báo lỗi trích xuất ảnh!", Toast.LENGTH_SHORT).show()
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
                status == "UNPAIR_ALL_OK" -> {
                    Toast.makeText(this, "🛡️ Đã xóa sạch toàn bộ thiết bị ghép đôi trên xe thành công!", Toast.LENGTH_LONG).show()
                    triggerHapticFeedback()
                }
                status.startsWith("ALARM_STATUS|") -> {
                    val st = status.substringAfter("ALARM_STATUS|").trim()
                    updateAntiTheftUI(st == "1")
                }
                status == "CANH_BAO_RUNG" -> {
                    tvFpSensorStatus?.text = "🚨 CẢNH BÁO: Phát hiện rung lắc xe!"
                    tvFpSensorStatus?.setTextColor(getColor(R.color.accent_danger))
                    UnlockHistoryManager.addEvent(
                        this,
                        "Cảnh báo rung lắc (Chống trộm)",
                        UnlockHistoryManager.TYPE_ALARM,
                        "Cảm biến rung SW-420 kích hoạt"
                    )
                    triggerHapticFeedback()
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

                // --- CẤU HÌNH ĐÈN VÒNG MÀU R503 (AURA RGB) ---
                status.startsWith("LED_CFG|") -> {
                    val parts = status.split("|")
                    if (parts.size >= 13) {
                        ledUnlocked = LedEventState(parts[1].toIntOrNull() ?: 1, parts[2].toIntOrNull() ?: 2, parts[3].toIntOrNull() ?: 120)
                        ledLocked   = LedEventState(parts[4].toIntOrNull() ?: 4, parts[5].toIntOrNull() ?: 2, parts[6].toIntOrNull() ?: 0)
                        ledSuccess  = LedEventState(parts[7].toIntOrNull() ?: 2, parts[8].toIntOrNull() ?: 2, parts[9].toIntOrNull() ?: 40)
                        ledError    = LedEventState(parts[10].toIntOrNull() ?: 2, parts[11].toIntOrNull() ?: 1, parts[12].toIntOrNull() ?: 30)
                        updateLedConfigDialogUI()
                    }
                }
                status == "LED_CFG_OK" -> {
                    Toast.makeText(this, "💾 Đã lưu cấu hình đèn LED R503 thành công!", Toast.LENGTH_SHORT).show()
                    ledConfigDialog?.dismiss()
                }
                status == "LED_TEST_OK" -> {
                    Toast.makeText(this, "⚡ Cảm biến R503 đang sáng thử màu đã chọn!", Toast.LENGTH_SHORT).show()
                }
                status.startsWith("FP_IMG_MODE|") -> {
                    val modeOn = status.substringAfter("FP_IMG_MODE|").trim() == "1"
                    fpSendImageVal = modeOn
                    getSharedPreferences("BT_PREF", MODE_PRIVATE).edit().putBoolean("FP_SEND_IMG", modeOn).apply()
                    swFpSendImageRef?.isChecked = modeOn
                    findViewById<SwitchCompat?>(R.id.swTabImageMode)?.isChecked = modeOn
                    findViewById<TextView?>(R.id.tvTabImageModeSubtitle)?.text = if (modeOn) "🟢 Đang BẬT: Truyền ảnh quang học (Đầy đủ trực quan)" else "⚪ Đang TẮT: Quẹt xe siêu tốc, tiết kiệm băng thông & CPU"
                }
                status.startsWith("FP_TEST_STARTED|") -> {
                    val dur = status.substringAfter("FP_TEST_STARTED|").trim()
                    tvFpTestStatusRef?.text = "⏳ Đang quét thử nghiệm (còn ${dur}s)...\nHãy đặt ngón tay lên cảm biến R503 ngay!"
                    tvFpTestStatusRef?.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
                    tvFpTestIdBadgeRef?.text = "QUẸT THỬ (${dur}s)"
                    tvFpTestIdBadgeRef?.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
                    pbFpTestImageProgressRef?.visibility = View.VISIBLE
                    tvFpSensorStatus?.text = "🔬 Đang ở chế độ quẹt thử (còn ${dur}s)... Chạm R503"
                    tvFpSensorStatus?.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
                }
                status.startsWith("FP_TEST_RESULT|") -> {
                    // FP_TEST_RESULT|<id>|<name>|<confidence>[|<fpCode>]
                    val parts = status.split("|")
                    if (parts.size >= 4) {
                        val id = parts[1].toIntOrNull() ?: -1
                        val name = parts[2]
                        val conf = parts[3]
                        val fpCode = if (parts.size >= 5 && parts[4].isNotEmpty()) parts[4] else (if (id > 0) "FP-ID${String.format("%02d", id)}" else "FP-ACTIVE")
                        val timeStr = SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date())

                        // Huy hiệu hiển thị trực tiếp MÃ VÂN TAY
                        tvFpTestIdBadgeRef?.text = "MÃ: $fpCode"
                        tvFpTestIdBadgeRef?.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))

                        if (id > 0) {
                            tvFpTestStatusRef?.text = "🔑 MÃ VÂN TAY: $fpCode\n🟢 Cảm biến R503 đọc thành công!\n• Đối chiếu xe: Khớp $name (ID #$id) - Điểm: $conf/255\n• Thời gian nhận diện: $timeStr"
                            tvFpTestStatusRef?.setTextColor(ContextCompat.getColor(this, R.color.white))
                            tvFpSensorStatus?.text = "🔑 Mã: $fpCode • Khớp $name (#$id)"
                            tvFpSensorStatus?.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
                        } else {
                            tvFpTestStatusRef?.text = "🔑 MÃ VÂN TAY: $fpCode\n🟢 Cảm biến R503 đã đọc & trích xuất thành công mã vân tay!\n• Trạng thái: Ngón tay mới (Chưa lưu trong chìa khóa xe)\n• Thời gian nhận diện: $timeStr"
                            tvFpTestStatusRef?.setTextColor(ContextCompat.getColor(this, R.color.white))
                            tvFpSensorStatus?.text = "🔑 Đã đọc mã vân tay: $fpCode"
                            tvFpSensorStatus?.setTextColor(ContextCompat.getColor(this, R.color.primary_blue))
                        }
                        triggerHapticFeedback()
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
        indexedImageChunks.clear()
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
        val btnFpTestCaptureImg = dialogView.findViewById<Button>(R.id.btnFpTestCaptureImg)
        val tvTestStatus = dialogView.findViewById<TextView>(R.id.tvFpTestStatus)
        val ivFpTestImage = dialogView.findViewById<ImageView>(R.id.ivFpTestImage)
        val pbFpTestImageProgress = dialogView.findViewById<ProgressBar>(R.id.pbFpTestImageProgress)
        val tvFpTestIdBadge = dialogView.findViewById<TextView>(R.id.tvFpTestIdBadge)
        val tvFpTestImageInfo = dialogView.findViewById<TextView>(R.id.tvFpTestImageInfo)
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
        ivFpTestImageRef = ivFpTestImage
        pbFpTestImageProgressRef = pbFpTestImageProgress
        tvFpTestIdBadgeRef = tvFpTestIdBadge
        tvFpTestImageInfoRef = tvFpTestImageInfo

        // Hiển thị ảnh cache gần nhất nếu có sẵn trong bộ nhớ tạm
        val cachedFpFile = File(cacheDir, "last_r503_scan.png")
        if (cachedFpFile.exists()) {
            val bmp = BitmapFactory.decodeFile(cachedFpFile.absolutePath)
            if (bmp != null) {
                ivFpTestImage.setImageBitmap(bmp)
                ivFpTestImage.colorFilter = null
                val timeStr = getSharedPreferences("BT_PREF", MODE_PRIVATE).getString("LAST_FP_IMG_TIME", "") ?: ""
                tvFpTestImageInfo.text = if (timeStr.isNotEmpty()) "Ảnh gần nhất: $timeStr" else "Ảnh lưu trong bộ nhớ tạm"
            }
        }

        // Chạm vào ảnh trong dialog để mở trình soi kính lúp & xuất file .BMP/.PNG
        ivFpTestImage.setOnClickListener {
            if (cachedFpFile.exists()) {
                val bmp = BitmapFactory.decodeFile(cachedFpFile.absolutePath)
                if (bmp != null) {
                    showFingerprintImageZoomDialog(bmp)
                    return@setOnClickListener
                }
            }
            if (lastRawFingerprintBytes != null) {
                val bmp = decodeR503ImageToBitmap(lastRawFingerprintBytes!!, currentImageWidth, currentImageHeight, isOpticalInvertMode)
                if (bmp != null) {
                    showFingerprintImageZoomDialog(bmp)
                    return@setOnClickListener
                }
            }
            Toast.makeText(this, "Chưa có ảnh. Bấm 'Chụp Lăng Kính' để chụp trước!", Toast.LENGTH_SHORT).show()
        }

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
            tvTestStatus.text = "🔍 Chế độ quét thử đọc mã vân tay (15s) đang chạy...\nHãy đặt ngón tay bất kỳ lên cảm biến R503 để đọc mã!"
            tvTestStatus.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
            tvFpTestIdBadge.text = "CHỜ CHẠM..."
            tvFpTestIdBadge.setTextColor(ContextCompat.getColor(this, R.color.gold_primary))
            pbFpTestImageProgress.visibility = View.VISIBLE
        }

        btnFpTestCaptureImg.setOnClickListener {
            if (isCapturingLiveImage) {
                Toast.makeText(this, "Đang trong tiến trình chụp ảnh...", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            startLiveFingerprintCapture()
            tvTestStatus.text = "📸 Đang chờ chạm ngón tay để chụp ảnh lăng kính R503..."
            tvTestStatus.setTextColor(ContextCompat.getColor(this, R.color.secondary_teal))
            tvFpTestIdBadge.text = "CHỤP ẢNH"
            tvFpTestIdBadge.setTextColor(ContextCompat.getColor(this, R.color.secondary_teal))
            pbFpTestImageProgress.visibility = View.VISIBLE
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
        fpSettingsDialog?.setOnDismissListener {
            ivFpTestImageRef = null
            pbFpTestImageProgressRef = null
            tvFpTestIdBadgeRef = null
            tvFpTestImageInfoRef = null
        }
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
        findViewById<SwitchCompat?>(R.id.swTabImageMode)?.isChecked = fpSendImageVal
        findViewById<TextView?>(R.id.tvTabImageModeSubtitle)?.text = if (fpSendImageVal) "🟢 Đang BẬT: Truyền ảnh quang học (Đầy đủ trực quan)" else "⚪ Đang TẮT: Quẹt xe siêu tốc, tiết kiệm băng thông & CPU"
        getSharedPreferences("BT_PREF", MODE_PRIVATE).edit().putBoolean("FP_SEND_IMG", fpSendImageVal).apply()
    }

    private fun getCurrentTabState(): LedEventState {
        return when (currentLedTab) {
            0 -> ledUnlocked
            1 -> ledLocked
            2 -> ledSuccess
            else -> ledError
        }
    }

    private fun getSpeedDesc(speed: Int): String {
        return when {
            speed < 50 -> "Rất nhanh"
            speed < 90 -> "Nhanh"
            speed < 150 -> "Mượt mà"
            else -> "Chậm"
        }
    }

    private fun updateLedConfigDialogUI() {
        runOnUiThread {
            refreshCurrentLedTabUI()
        }
    }

    private fun refreshCurrentLedTabUI() {
        val state = getCurrentTabState()
        rgbColorWheelRef?.setColorIndex(state.color)

        val info = RgbColorWheelView.getColorInfo(state.color)
        tvSelectedColorDisplayRef?.text = "Màu Đang Chọn: ${info.name}"
        tvSelectedColorDisplayRef?.setTextColor(info.hex)

        when (state.mode) {
            1 -> rgEffectModeRef?.check(R.id.rbModeBreathing)
            2 -> rgEffectModeRef?.check(R.id.rbModeFlashing)
            3 -> rgEffectModeRef?.check(R.id.rbModeOn)
            4 -> rgEffectModeRef?.check(R.id.rbModeOff)
            else -> rgEffectModeRef?.check(R.id.rbModeBreathing)
        }

        sbEffectSpeedRef?.progress = state.speed.coerceIn(0, 220)
        tvSpeedValueRef?.text = "${state.speed} (${getSpeedDesc(state.speed)})"

        // Highlight active tab button
        val activeBg = R.drawable.button_background_secondary_on
        val inactiveBg = R.drawable.button_background_secondary
        btnTabUnlockedRef?.setBackgroundResource(if (currentLedTab == 0) activeBg else inactiveBg)
        btnTabUnlockedRef?.setTextColor(if (currentLedTab == 0) Color.WHITE else 0xFF94A3B8.toInt())

        btnTabLockedRef?.setBackgroundResource(if (currentLedTab == 1) activeBg else inactiveBg)
        btnTabLockedRef?.setTextColor(if (currentLedTab == 1) Color.WHITE else 0xFF94A3B8.toInt())

        btnTabSuccessRef?.setBackgroundResource(if (currentLedTab == 2) activeBg else inactiveBg)
        btnTabSuccessRef?.setTextColor(if (currentLedTab == 2) Color.WHITE else 0xFF94A3B8.toInt())

        btnTabErrorRef?.setBackgroundResource(if (currentLedTab == 3) activeBg else inactiveBg)
        btnTabErrorRef?.setTextColor(if (currentLedTab == 3) Color.WHITE else 0xFF94A3B8.toInt())

        val badgeText = when (currentLedTab) {
            0 -> "Đang chỉnh: [Xe Mở Khóa / Đang Chạy]"
            1 -> "Đang chỉnh: [Xe Đang Khóa / Đỗ Xe]"
            2 -> "Đang chỉnh: [Quét Đúng / Mở Xe Thành Công]"
            else -> "Đang chỉnh: [Quét Sai / Cảnh Báo Trộm]"
        }
        val badgeColor = when (currentLedTab) {
            0 -> 0xFF38BDF8.toInt()
            1 -> 0xFF94A3B8.toInt()
            2 -> 0xFF4ADE80.toInt()
            else -> 0xFFF87171.toInt()
        }
        tvCurrentEditingBadgeRef?.text = badgeText
        tvCurrentEditingBadgeRef?.setTextColor(badgeColor)
    }

    private fun showR503LedSettingsDialog() {
        if (!isConnectedToVehicle()) {
            Toast.makeText(this, "Vui lòng kết nối Bluetooth tới xe trước!", Toast.LENGTH_SHORT).show()
            return
        }

        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_r503_led_settings, null)
        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .create()
        dialog.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        ledConfigDialog = dialog

        val btnTabUnlocked = dialogView.findViewById<Button>(R.id.btnTabUnlocked)
        val btnTabLocked = dialogView.findViewById<Button>(R.id.btnTabLocked)
        val btnTabSuccess = dialogView.findViewById<Button>(R.id.btnTabSuccess)
        val btnTabError = dialogView.findViewById<Button>(R.id.btnTabError)
        val tvCurrentEditingBadge = dialogView.findViewById<TextView>(R.id.tvCurrentEditingBadge)
        val rgbColorWheel = dialogView.findViewById<RgbColorWheelView>(R.id.rgbColorWheel)
        val tvSelectedColorDisplay = dialogView.findViewById<TextView>(R.id.tvSelectedColorDisplay)
        val rgEffectMode = dialogView.findViewById<RadioGroup>(R.id.rgEffectMode)
        val tvSpeedValue = dialogView.findViewById<TextView>(R.id.tvSpeedValue)
        val sbEffectSpeed = dialogView.findViewById<SeekBar>(R.id.sbEffectSpeed)

        val btnPresetOcean = dialogView.findViewById<Button>(R.id.btnPresetOcean)
        val btnPresetCyberpunk = dialogView.findViewById<Button>(R.id.btnPresetCyberpunk)
        val btnPresetRacing = dialogView.findViewById<Button>(R.id.btnPresetRacing)
        val btnPresetEmerald = dialogView.findViewById<Button>(R.id.btnPresetEmerald)
        val btnPresetStealth = dialogView.findViewById<Button>(R.id.btnPresetStealth)

        val btnTestLedLive = dialogView.findViewById<Button>(R.id.btnTestLedLive)
        val btnSaveLedConfig = dialogView.findViewById<Button>(R.id.btnSaveLedConfig)
        val btnDismissLedDialog = dialogView.findViewById<Button>(R.id.btnDismissLedDialog)

        rgbColorWheelRef = rgbColorWheel
        tvSelectedColorDisplayRef = tvSelectedColorDisplay
        rgEffectModeRef = rgEffectMode
        sbEffectSpeedRef = sbEffectSpeed
        tvSpeedValueRef = tvSpeedValue
        tvCurrentEditingBadgeRef = tvCurrentEditingBadge
        btnTabUnlockedRef = btnTabUnlocked
        btnTabLockedRef = btnTabLocked
        btnTabSuccessRef = btnTabSuccess
        btnTabErrorRef = btnTabError

        // Gửi lệnh đọc cấu hình đèn LED hiện tại từ ESP32
        sendVehicleCommand("GET_LED_CFG")

        // Tab selection click listeners
        btnTabUnlocked.setOnClickListener { currentLedTab = 0; refreshCurrentLedTabUI() }
        btnTabLocked.setOnClickListener { currentLedTab = 1; refreshCurrentLedTabUI() }
        btnTabSuccess.setOnClickListener { currentLedTab = 2; refreshCurrentLedTabUI() }
        btnTabError.setOnClickListener { currentLedTab = 3; refreshCurrentLedTabUI() }

        // Color wheel listener
        rgbColorWheel.onColorSelected = { colorIndex, colorHex, colorName ->
            val state = getCurrentTabState()
            state.color = colorIndex
            tvSelectedColorDisplay.text = "Màu Đang Chọn: $colorName"
            tvSelectedColorDisplay.setTextColor(colorHex)
        }

        // Effect mode radio group listener
        rgEffectMode.setOnCheckedChangeListener { _, checkedId ->
            val state = getCurrentTabState()
            state.mode = when (checkedId) {
                R.id.rbModeBreathing -> 1
                R.id.rbModeOn -> 3
                R.id.rbModeFlashing -> 2
                else -> 4 // rbModeOff
            }
        }

        // Speed seekbar listener
        sbEffectSpeed.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val state = getCurrentTabState()
                state.speed = progress
                tvSpeedValue.text = "$progress (${getSpeedDesc(progress)})"
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        // Presets listeners
        btnPresetOcean.setOnClickListener {
            ledUnlocked = LedEventState(1, RgbColorWheelView.COLOR_CYAN, 120)
            ledLocked   = LedEventState(4, RgbColorWheelView.COLOR_CYAN, 0)
            ledSuccess  = LedEventState(2, RgbColorWheelView.COLOR_BLUE, 40)
            ledError    = LedEventState(2, RgbColorWheelView.COLOR_RED, 30)
            refreshCurrentLedTabUI()
            Toast.makeText(this, "Áp dụng phong cách: 🐬 Ocean Cyan", Toast.LENGTH_SHORT).show()
        }

        btnPresetCyberpunk.setOnClickListener {
            ledUnlocked = LedEventState(1, RgbColorWheelView.COLOR_PURPLE, 100)
            ledLocked   = LedEventState(4, RgbColorWheelView.COLOR_PURPLE, 0)
            ledSuccess  = LedEventState(2, RgbColorWheelView.COLOR_GREEN, 40)
            ledError    = LedEventState(2, RgbColorWheelView.COLOR_RED, 30)
            refreshCurrentLedTabUI()
            Toast.makeText(this, "Áp dụng phong cách: 🟣 Cyberpunk Purple", Toast.LENGTH_SHORT).show()
        }

        btnPresetRacing.setOnClickListener {
            ledUnlocked = LedEventState(1, RgbColorWheelView.COLOR_RED, 90)
            ledLocked   = LedEventState(4, RgbColorWheelView.COLOR_RED, 0)
            ledSuccess  = LedEventState(2, RgbColorWheelView.COLOR_YELLOW, 40)
            ledError    = LedEventState(2, RgbColorWheelView.COLOR_RED, 30)
            refreshCurrentLedTabUI()
            Toast.makeText(this, "Áp dụng phong cách: 🏎️ Sport Racing Red", Toast.LENGTH_SHORT).show()
        }

        btnPresetEmerald.setOnClickListener {
            ledUnlocked = LedEventState(1, RgbColorWheelView.COLOR_GREEN, 130)
            ledLocked   = LedEventState(4, RgbColorWheelView.COLOR_GREEN, 0)
            ledSuccess  = LedEventState(2, RgbColorWheelView.COLOR_CYAN, 40)
            ledError    = LedEventState(2, RgbColorWheelView.COLOR_RED, 30)
            refreshCurrentLedTabUI()
            Toast.makeText(this, "Áp dụng phong cách: 🌿 Emerald Nature", Toast.LENGTH_SHORT).show()
        }

        btnPresetStealth.setOnClickListener {
            ledUnlocked = LedEventState(3, RgbColorWheelView.COLOR_BLUE, 50)
            ledLocked   = LedEventState(4, RgbColorWheelView.COLOR_BLUE, 0)
            ledSuccess  = LedEventState(2, RgbColorWheelView.COLOR_WHITE, 30)
            ledError    = LedEventState(2, RgbColorWheelView.COLOR_RED, 30)
            refreshCurrentLedTabUI()
            Toast.makeText(this, "Áp dụng phong cách: 🛡️ Stealth Eco (Tiết Kiệm Điện)", Toast.LENGTH_SHORT).show()
        }

        // Live Test on R503
        btnTestLedLive.setOnClickListener {
            val state = getCurrentTabState()
            val testCmd = "TEST_LED|${state.mode}|${state.color}|${state.speed}|3"
            sendVehicleCommand(testCmd)
            triggerHapticFeedback()
            Toast.makeText(this, "⚡ Đang gửi lệnh thử nghiệm lên R503...", Toast.LENGTH_SHORT).show()
        }

        // Save LED Config to ESP32 Flash
        btnSaveLedConfig.setOnClickListener {
            val saveCmd = "SET_LED_CFG|" +
                    "${ledUnlocked.mode}|${ledUnlocked.color}|${ledUnlocked.speed}|" +
                    "${ledLocked.mode}|${ledLocked.color}|${ledLocked.speed}|" +
                    "${ledSuccess.mode}|${ledSuccess.color}|${ledSuccess.speed}|" +
                    "${ledError.mode}|${ledError.color}|${ledError.speed}"
            sendVehicleCommand(saveCmd)
            triggerHapticFeedback()
            Toast.makeText(this, "💾 Đang lưu cấu hình xuống xe...", Toast.LENGTH_SHORT).show()
        }

        btnDismissLedDialog.setOnClickListener {
            dialog.dismiss()
        }

        refreshCurrentLedTabUI()
        dialog.show()
    }

    private fun safeDecodeR503Base64ToBitmap(fullB64: String, width: Int = 192, height: Int = 192): Bitmap? {
        if (fullB64.isBlank()) return null
        val cleanB64 = fullB64.replace("\n", "").replace("\r", "").replace(" ", "").trim()
        var rawBytes: ByteArray? = null

        // Phương pháp 1: Chuẩn RFC 4648 (khi firmware gửi chunk bội số của 3 không padding)
        try {
            rawBytes = Base64.decode(cleanB64, Base64.DEFAULT)
        } catch (e: Exception) {
            Log.w("FP_IMG", "Standard Base64 decode failed (${e.message}), trying chunked fallback...")
        }

        // Phương pháp 2: Fallback ghép và giải mã từng chunk nếu có ký tự '=' ở giữa các chunk
        if (rawBytes == null || rawBytes.isEmpty()) {
            try {
                val bos = ByteArrayOutputStream()
                val parts = cleanB64.split("=").map { it.trim() }.filter { it.isNotEmpty() }
                for (part in parts) {
                    val padNeeded = (4 - (part.length % 4)) % 4
                    val chunkWithPad = part + "=".repeat(padNeeded)
                    try {
                        val b = Base64.decode(chunkWithPad, Base64.DEFAULT)
                        if (b != null && b.isNotEmpty()) {
                            bos.write(b)
                        }
                    } catch (_: Exception) {}
                }
                if (bos.size() > 0) {
                    rawBytes = bos.toByteArray()
                    Log.i("FP_IMG", "Chunked fallback decode succeeded: ${rawBytes.size} bytes")
                }
            } catch (e2: Exception) {
                Log.e("FP_IMG", "Chunked fallback error", e2)
            }
        }

        // Phương pháp 3: Fallback Base64.NO_PADDING
        if (rawBytes == null || rawBytes.isEmpty()) {
            try {
                rawBytes = Base64.decode(cleanB64, Base64.NO_PADDING)
            } catch (_: Exception) {}
        }

        if (rawBytes == null || rawBytes.isEmpty()) {
            Log.e("FP_IMG", "All Base64 decode strategies failed for length: ${cleanB64.length}")
            return null
        }

        Log.i("FP_IMG", "Decoded ${rawBytes.size} bytes. Generating Bitmap $width x $height...")
        lastRawFingerprintBytes = rawBytes
        return decodeR503ImageToBitmap(rawBytes, width, height, isOpticalInvertMode)
    }

    private var lastRawFingerprintBytes: ByteArray? = null
    private var isOpticalInvertMode: Boolean = false

    private fun decodeR503ImageToBitmap(
        rawBytes: ByteArray,
        width: Int = 160,
        height: Int = 160,
        invert: Boolean = false
    ): Bitmap? {
        if (rawBytes.isEmpty()) return null

        // Tự động nhận diện độ phân giải thực tế dựa trên kích thước mảng byte (12.800B -> 160x160, 18.432B -> 192x192)
        val actualW: Int
        val actualH: Int
        when (rawBytes.size) {
            12800 -> {
                actualW = 160
                actualH = 160
            }
            18432 -> {
                actualW = 192
                actualH = 192
            }
            29952 -> {
                actualW = 208
                actualH = 288
            }
            else -> {
                if (width * height / 2 == rawBytes.size && width > 0 && height > 0) {
                    actualW = width
                    actualH = height
                } else {
                    val totalPix = rawBytes.size * 2
                    val side = Math.round(Math.sqrt(totalPix.toDouble())).toInt()
                    if (side * side == totalPix && side > 0) {
                        actualW = side
                        actualH = side
                    } else if (totalPix % 160 == 0 && totalPix > 0) {
                        actualW = 160
                        actualH = totalPix / 160
                    } else if (totalPix % 192 == 0 && totalPix > 0) {
                        actualW = 192
                        actualH = totalPix / 192
                    } else {
                        actualW = width
                        actualH = height
                    }
                }
            }
        }

        currentImageWidth = actualW
        currentImageHeight = actualH

        val totalPixels = actualW * actualH
        val bitmap = Bitmap.createBitmap(actualW, actualH, Bitmap.Config.ARGB_8888)
        val pixels = IntArray(totalPixels)
        val maxCompBytes = rawBytes.size

        // Giải mã trực tiếp từng điểm ảnh 4-bit qua phép nhân 17 (0 -> 0 đen, 15 -> 255 trắng)
        // Tuyệt đối không dùng histogram percentile để tránh bóp nghẹt dynamic range của R503
        for (pixelIdx in 0 until totalPixels) {
            val compIdx = pixelIdx / 2
            val rawNibble = if (compIdx < maxCompBytes) {
                val b = rawBytes[compIdx].toInt() and 0xFF
                if (pixelIdx % 2 == 0) ((b ushr 4) and 0x0F) else (b and 0x0F)
            } else {
                15 // Nền trắng quang học nếu thiếu byte
            }

            val gray = (rawNibble * 17).coerceIn(0, 255)

            if (!invert) {
                // CHUẨN QUANG HỌC R503 (Optical Clear): Đỉnh vân đen sẫm, rãnh vân trắng sáng
                pixels[pixelIdx] = (0xFF shl 24) or (gray shl 16) or (gray shl 8) or gray
            } else {
                // CHẾ ĐỘ BIOMETRIC NEON: Đỉnh vân vàng kim biometric, nền đen sâu
                val inv = 255 - gray
                val r = inv
                val g = (inv * 0.82f).toInt().coerceIn(0, 255)
                val b = (inv * 0.35f).toInt().coerceIn(0, 255)
                pixels[pixelIdx] = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
            }
        }

        bitmap.setPixels(pixels, 0, actualW, 0, 0, actualW, actualH)
        return bitmap
    }

    private fun reRenderFingerprintImage() {
        val bytes = lastRawFingerprintBytes ?: return
        val bmp = decodeR503ImageToBitmap(bytes, currentImageWidth, currentImageHeight, isOpticalInvertMode) ?: return
        saveCachedFingerprintImage(bmp)
        runOnUiThread {
            ivTabFingerprintImageRef?.let {
                ImageViewCompat.setImageTintList(it, null)
                it.imageTintList = null
                it.colorFilter = null
                it.setImageBitmap(bmp)
            }
            ivFpTestImageRef?.let {
                ImageViewCompat.setImageTintList(it, null)
                it.imageTintList = null
                it.colorFilter = null
                it.setImageBitmap(bmp)
            }
        }
    }

    private fun showFingerprintImageZoomDialog(bmp: Bitmap) {
        val dialog = AlertDialog.Builder(this).create()
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(40, 40, 40, 40)
            setBackgroundColor(0xFF0F172A.toInt())
        }

        val tvTitle = TextView(this).apply {
            text = "🔍 ẢNH VÂN TAY QUANG HỌC R503"
            setTextColor(0xFFFFC107.toInt())
            textSize = 15f
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            setPadding(0, 0, 0, 16)
        }
        layout.addView(tvTitle)

        val card = CardView(this).apply {
            radius = 24f
            cardElevation = 8f
            setCardBackgroundColor(if (!isOpticalInvertMode) 0xFFFFFFFF.toInt() else 0xFF000000.toInt())
            layoutParams = LinearLayout.LayoutParams(540, 540).apply {
                gravity = Gravity.CENTER
                setMargins(0, 10, 0, 20)
            }
        }

        val imgView = ImageView(this).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
            scaleType = ImageView.ScaleType.FIT_CENTER
            ImageViewCompat.setImageTintList(this, null)
            imageTintList = null
            colorFilter = null
            setImageBitmap(bmp)
        }
        card.addView(imgView)
        layout.addView(card)

        var currentZoomLevel = 2f

        val tvMeta = TextView(this).apply {
            text = "Độ phân giải: ${bmp.width} x ${bmp.height} px (508 DPI)\nChế độ: " + if (!isOpticalInvertMode) "Quang học chuẩn nét (Optical Clear)" else "Biometric Vàng Kim (Neon)"
            setTextColor(0xFFCBD5E1.toInt())
            textSize = 12f
            gravity = Gravity.CENTER
            setPadding(0, 0, 0, 16)
        }
        layout.addView(tvMeta)

        val btnToggleMode = AppCompatButton(this).apply {
            text = if (!isOpticalInvertMode) "🟡 Đổi sang Chế độ Biometric Neon" else "⚪ Đổi sang Chế độ Quang học chuẩn nét"
            setBackgroundColor(0xFF1E293B.toInt())
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 12f
            setPadding(24, 16, 24, 16)
            setOnClickListener {
                isOpticalInvertMode = !isOpticalInvertMode
                reRenderFingerprintImage()
                val newBmp = decodeR503ImageToBitmap(lastRawFingerprintBytes ?: return@setOnClickListener, currentImageWidth, currentImageHeight, isOpticalInvertMode)
                if (newBmp != null) {
                    imgView.setImageBitmap(newBmp)
                    card.setCardBackgroundColor(if (!isOpticalInvertMode) 0xFFFFFFFF.toInt() else 0xFF000000.toInt())
                    tvMeta.text = "Độ phân giải: ${newBmp.width} x ${newBmp.height} px (508 DPI)\nChế độ: " + if (!isOpticalInvertMode) "Quang học chuẩn nét (Optical Clear)" else "Biometric Vàng Kim (Neon)"
                    text = if (!isOpticalInvertMode) "🟡 Đổi sang Chế độ Biometric Neon" else "⚪ Đổi sang Chế độ Quang học chuẩn nét"
                }
            }
        }
        layout.addView(btnToggleMode)

        // Hàng nút: Phóng to (Zoom 1x / 2x / 3x) khớp với tools/view_fingerprint.html
        val llZoomRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
            setPadding(0, 10, 0, 10)
        }
        val btnZoom1 = AppCompatButton(this).apply {
            text = "1x"
            setBackgroundColor(0xFF0F172A.toInt())
            setTextColor(0xFF94A3B8.toInt())
            textSize = 11f
            layoutParams = LinearLayout.LayoutParams(0, 90, 1f).apply { setMargins(4, 0, 4, 0) }
            setOnClickListener {
                card.layoutParams = LinearLayout.LayoutParams(360, 360).apply { gravity = Gravity.CENTER; setMargins(0, 10, 0, 16) }
                card.requestLayout()
            }
        }
        val btnZoom2 = AppCompatButton(this).apply {
            text = "2x (Chuẩn)"
            setBackgroundColor(0xFF1E293B.toInt())
            setTextColor(0xFF00F0FF.toInt())
            textSize = 11f
            layoutParams = LinearLayout.LayoutParams(0, 90, 1f).apply { setMargins(4, 0, 4, 0) }
            setOnClickListener {
                card.layoutParams = LinearLayout.LayoutParams(540, 540).apply { gravity = Gravity.CENTER; setMargins(0, 10, 0, 16) }
                card.requestLayout()
            }
        }
        val btnZoom3 = AppCompatButton(this).apply {
            text = "3x (Cực đại)"
            setBackgroundColor(0xFF0F172A.toInt())
            setTextColor(0xFFFFD700.toInt())
            textSize = 11f
            layoutParams = LinearLayout.LayoutParams(0, 90, 1f).apply { setMargins(4, 0, 4, 0) }
            setOnClickListener {
                card.layoutParams = LinearLayout.LayoutParams(680, 680).apply { gravity = Gravity.CENTER; setMargins(0, 10, 0, 16) }
                card.requestLayout()
            }
        }
        llZoomRow.addView(btnZoom1)
        llZoomRow.addView(btnZoom2)
        llZoomRow.addView(btnZoom3)
        layout.addView(llZoomRow)

        // Hàng nút: Lưu/Chia sẻ ảnh file .BMP gốc & .PNG (Tương tự view_fingerprint.html)
        val llExportRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
            setPadding(0, 4, 0, 10)
        }
        val btnSaveBmp = AppCompatButton(this).apply {
            text = "💾 Lưu BMP Gốc"
            setBackgroundColor(0xFF1E293B.toInt())
            setTextColor(0xFFFFD700.toInt())
            textSize = 11f
            layoutParams = LinearLayout.LayoutParams(0, 100, 1f).apply { setMargins(4, 0, 4, 0) }
            setOnClickListener {
                exportFingerprintImage("bmp", bmp)
            }
        }
        val btnSavePng = AppCompatButton(this).apply {
            text = "📤 Chia sẻ PNG"
            setBackgroundColor(0xFF10B981.toInt())
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 11f
            layoutParams = LinearLayout.LayoutParams(0, 100, 1f).apply { setMargins(4, 0, 4, 0) }
            setOnClickListener {
                exportFingerprintImage("png", bmp)
            }
        }
        val btnCopyBase64 = AppCompatButton(this).apply {
            text = "📋 Chép Base64"
            setBackgroundColor(0xFF0284C7.toInt())
            setTextColor(0xFFFFFFFF.toInt())
            textSize = 11f
            layoutParams = LinearLayout.LayoutParams(0, 100, 1f).apply { setMargins(4, 0, 4, 0) }
            setOnClickListener {
                copyFingerprintBmpBase64ToClipboard()
            }
        }
        llExportRow.addView(btnSaveBmp)
        llExportRow.addView(btnSavePng)
        llExportRow.addView(btnCopyBase64)
        layout.addView(llExportRow)

        val btnClose = AppCompatButton(this).apply {
            text = "Đóng"
            setBackgroundColor(0x00000000)
            setTextColor(0xFF94A3B8.toInt())
            textSize = 12f
            setOnClickListener { dialog.dismiss() }
        }
        layout.addView(btnClose)

        dialog.setView(layout)
        dialog.show()
    }

    private fun exportFingerprintImage(format: String, currentBmp: Bitmap) {
        try {
            val timeStamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
            val fileName = "fingerprint_${timeStamp}.${format.lowercase()}"
            val exportDir = File(cacheDir, "exports").apply { mkdirs() }
            val destFile = File(exportDir, fileName)

            if (format.equals("bmp", ignoreCase = true)) {
                val rawBytes = lastRawFingerprintBytes
                if (rawBytes != null && rawBytes.isNotEmpty()) {
                    val bmpBytes = generateBmpByteArray(rawBytes, currentImageWidth, currentImageHeight)
                    FileOutputStream(destFile).use { it.write(bmpBytes) }
                } else {
                    FileOutputStream(destFile).use { out ->
                        currentBmp.compress(Bitmap.CompressFormat.PNG, 100, out)
                    }
                }
            } else {
                FileOutputStream(destFile).use { out ->
                    currentBmp.compress(Bitmap.CompressFormat.PNG, 100, out)
                }
            }

            val uri = androidx.core.content.FileProvider.getUriForFile(
                this,
                "${applicationContext.packageName}.fileprovider",
                destFile
            )

            val shareIntent = android.content.Intent(android.content.Intent.ACTION_SEND).apply {
                type = if (format == "bmp") "image/bmp" else "image/png"
                putExtra(android.content.Intent.EXTRA_STREAM, uri)
                addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            startActivity(android.content.Intent.createChooser(shareIntent, "Xuất ảnh vân tay quang học ($fileName)"))
            Toast.makeText(this, "Đã tạo file $fileName thành công!", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Log.e("FP_EXPORT", "Export error", e)
            Toast.makeText(this, "Lỗi xuất file ảnh: ${e.localizedMessage}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun copyFingerprintBmpBase64ToClipboard() {
        val rawBytes = lastRawFingerprintBytes
        if (rawBytes == null || rawBytes.isEmpty()) {
            Toast.makeText(this, "Chưa có ảnh quét thực tế. Bấm 'Chụp Lăng Kính' để chụp!", Toast.LENGTH_SHORT).show()
            return
        }
        try {
            val bmpBytes = generateBmpByteArray(rawBytes, currentImageWidth, currentImageHeight)
            val b64Str = "data:image/bmp;base64," + android.util.Base64.encodeToString(bmpBytes, android.util.Base64.NO_WRAP)

            val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            val clip = ClipData.newPlainText("Fingerprint BMP Base64", b64Str)
            clipboard.setPrimaryClip(clip)

            Log.i("FP_IMG_BASE64", "==================== [BẮT ĐẦU CHUỖI ẢNH BASE64 BMP R503] ====================")
            Log.i("FP_IMG_BASE64", b64Str)
            Log.i("FP_IMG_BASE64", "==================== [KẾT THÚC CHUỖI ẢNH BASE64 BMP R503] ====================")

            Toast.makeText(this, "📋 Đã sao chép chuỗi Base64 BMP! Mở tools/view_fingerprint.html để dán và xem ảnh.", Toast.LENGTH_LONG).show()
        } catch (e: Exception) {
            Log.e("FP_IMG", "Error copying base64", e)
            Toast.makeText(this, "Lỗi sao chép Base64: ${e.localizedMessage}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun generateBmpByteArray(rawNibbleBytes: ByteArray, width: Int, height: Int): ByteArray {
        val actualW: Int
        val actualH: Int
        when (rawNibbleBytes.size) {
            12800 -> {
                actualW = 160
                actualH = 160
            }
            18432 -> {
                actualW = 192
                actualH = 192
            }
            29952 -> {
                actualW = 208
                actualH = 288
            }
            else -> {
                if (width * height / 2 == rawNibbleBytes.size && width > 0 && height > 0) {
                    actualW = width
                    actualH = height
                } else {
                    val totalPix = rawNibbleBytes.size * 2
                    val side = Math.round(Math.sqrt(totalPix.toDouble())).toInt()
                    if (side * side == totalPix && side > 0) {
                        actualW = side
                        actualH = side
                    } else if (totalPix % 160 == 0 && totalPix > 0) {
                        actualW = 160
                        actualH = totalPix / 160
                    } else if (totalPix % 192 == 0 && totalPix > 0) {
                        actualW = 192
                        actualH = totalPix / 192
                    } else {
                        actualW = width
                        actualH = height
                    }
                }
            }
        }
        val totalPixels = actualW * actualH
        val bmpHeaderSize = 14 + 40 + 1024 // 1078 bytes
        val bmpTotalSize = bmpHeaderSize + totalPixels
        val bmpData = ByteArray(bmpTotalSize)

        // Bitmap File Header (14 bytes)
        bmpData[0] = 'B'.code.toByte()
        bmpData[1] = 'M'.code.toByte()
        bmpData[2] = (bmpTotalSize and 0xFF).toByte()
        bmpData[3] = ((bmpTotalSize ushr 8) and 0xFF).toByte()
        bmpData[4] = ((bmpTotalSize ushr 16) and 0xFF).toByte()
        bmpData[5] = ((bmpTotalSize ushr 24) and 0xFF).toByte()
        bmpData[10] = (bmpHeaderSize and 0xFF).toByte()
        bmpData[11] = ((bmpHeaderSize ushr 8) and 0xFF).toByte()
        bmpData[12] = ((bmpHeaderSize ushr 16) and 0xFF).toByte()
        bmpData[13] = ((bmpHeaderSize ushr 24) and 0xFF).toByte()

        // Bitmap Info Header (40 bytes)
        bmpData[14] = 40
        bmpData[18] = (actualW and 0xFF).toByte()
        bmpData[19] = ((actualW ushr 8) and 0xFF).toByte()
        bmpData[22] = (actualH and 0xFF).toByte()
        bmpData[23] = ((actualH ushr 8) and 0xFF).toByte()
        bmpData[26] = 1 // Planes
        bmpData[28] = 8 // 8 bits per pixel (grayscale)
        bmpData[34] = (totalPixels and 0xFF).toByte()
        bmpData[35] = ((totalPixels ushr 8) and 0xFF).toByte()
        bmpData[36] = ((totalPixels ushr 16) and 0xFF).toByte()
        bmpData[37] = ((totalPixels ushr 24) and 0xFF).toByte()
        bmpData[38] = 0x13.toByte(); bmpData[39] = 0x0B.toByte() // 2835 ppm (~508 DPI)
        bmpData[42] = 0x13.toByte(); bmpData[43] = 0x0B.toByte()
        bmpData[46] = 0; bmpData[47] = 1 // 256 colors

        // Palette Grayscale (256 * 4 = 1024 bytes)
        for (i in 0 until 256) {
            bmpData[54 + i * 4 + 0] = i.toByte() // Blue
            bmpData[54 + i * 4 + 1] = i.toByte() // Green
            bmpData[54 + i * 4 + 2] = i.toByte() // Red
            bmpData[54 + i * 4 + 3] = 0
        }

        // Đảo ngược dòng quét Bottom-Up theo chuẩn Windows BMP (khớp test_r503.cpp & view_fingerprint.html)
        for (y in 0 until actualH) {
            val srcY = actualH - 1 - y
            val dstRowOffset = bmpHeaderSize + y * actualW
            for (x in 0 until actualW) {
                val pixelIdx = srcY * actualW + x
                val compIdx = pixelIdx / 2
                val valGray = if (compIdx < rawNibbleBytes.size) {
                    val b = rawNibbleBytes[compIdx].toInt() and 0xFF
                    if (pixelIdx % 2 == 0) ((b ushr 4) * 17) else ((b and 0x0F) * 17)
                } else {
                    255 // Mặc định nền trắng quang học
                }
                bmpData[dstRowOffset + x] = valGray.toByte()
            }
        }
        return bmpData
    }

    private fun startLiveFingerprintCapture() {
        isCapturingLiveImage = true
        enrollImageBuffer.setLength(0)
        indexedImageChunks.clear()
        pbTabImageProgressRef?.visibility = View.VISIBLE
        tvTabImageStatusRef?.text = "📸 Đang chụp ảnh lăng kính..."
        tvTabImageDescRef?.text = "Áp ngón tay lên R503 (nếu chụp vân tay) hoặc giữ mặt kính trống để soi lăng kính."
        triggerHapticFeedback()
        sendVehicleCommand("CAPTURE_FP_IMG")
    }

    private fun saveCachedFingerprintImage(bmp: Bitmap) {
        try {
            val file = File(cacheDir, "last_r503_scan.png")
            FileOutputStream(file).use { out ->
                bmp.compress(Bitmap.CompressFormat.PNG, 100, out)
            }
            val timeStr = SimpleDateFormat("HH:mm - dd/MM", Locale.getDefault()).format(Date())
            getSharedPreferences("BT_PREF", MODE_PRIVATE).edit()
                .putString("LAST_FP_IMG_TIME", timeStr)
                .apply()
        } catch (e: Exception) {
            Log.e("FP_IMG", "Error saving cached image", e)
        }
    }

    private fun loadCachedFingerprintImage() {
        try {
            val file = File(cacheDir, "last_r503_scan.png")
            if (file.exists()) {
                val bmp = BitmapFactory.decodeFile(file.absolutePath)
                if (bmp != null) {
                    ivTabFingerprintImageRef?.let {
                        ImageViewCompat.setImageTintList(it, null)
                        it.imageTintList = null
                        it.colorFilter = null
                        it.setImageBitmap(bmp)
                    }
                    val timeStr = getSharedPreferences("BT_PREF", MODE_PRIVATE)
                        .getString("LAST_FP_IMG_TIME", "") ?: ""
                    tvTabImageStatusRef?.text = "Ảnh quét gần nhất"
                    tvTabImageDescRef?.text = if (timeStr.isNotEmpty()) "Thời gian: $timeStr" else "Đã lưu trong bộ nhớ tạm"
                }
            }
        } catch (e: Exception) {
            Log.e("FP_IMG", "Error loading cached image", e)
        }
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

                // Tải tên riêng đã lưu cho xe có địa chỉ MAC này
                val prefs = getSharedPreferences("BT_PREF", MODE_PRIVATE)
                val savedForThisMac = prefs.getString("VEHICLE_NAME_${item.device.address}", null)
                vehicleCustomName = savedForThisMac
                    ?: if (item.name.isNotBlank() && !item.name.equals("XE_tsmart_BLE", ignoreCase = true)) item.name else "Honda SH 150i"

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
                        UnlockHistoryManager.TYPE_ALARM -> {
                            ivIcon.setImageResource(R.drawable.ic_auto)
                            ivIcon.imageTintList = android.content.res.ColorStateList.valueOf(getColor(R.color.primary_red))
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
                val prefs = getSharedPreferences("BT_PREF", MODE_PRIVATE)
                val savedForThisMac = prefs.getString("VEHICLE_NAME_${selectedDevice.address}", null)
                vehicleCustomName = savedForThisMac
                    ?: if (selectedDevice.name.isNotBlank()) selectedDevice.name else "Honda SH 150i"
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
            hint = "Nhập tên xe (VD: Honda SH 150i, Vespa, NVX...)"
            setTextColor(getColor(R.color.white))
            setHintTextColor(getColor(R.color.text_muted))
            textSize = 15f
        }
        val container = FrameLayout(this).apply {
            addView(input)
            input.setPadding(60, 40, 60, 40)
        }
        AlertDialog.Builder(this)
            .setTitle("Đổi tên xe (Garage Profile)")
            .setMessage("Đặt tên riêng cho xe. Tên sẽ được lưu vào bộ nhớ ứng dụng và đồng bộ vào bộ nhớ Flash NVS của bo mạch xe.")
            .setView(container)
            .setPositiveButton("Lưu") { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isNotEmpty()) {
                    vehicleCustomName = newName
                    val mac = if (isBleMode) BleManager.activeMac else BluetoothController.deviceMac
                    val prefs = getSharedPreferences("BT_PREF", MODE_PRIVATE)
                    val editor = prefs.edit().putString("VEHICLE_NAME", vehicleCustomName)
                    if (mac != null) {
                        editor.putString("VEHICLE_NAME_$mac", vehicleCustomName)
                    }
                    editor.apply()
                    updateDeviceNameDisplay()

                    // Gửi lệnh đồng bộ vào bộ nhớ Flash NVS của xe nếu đang kết nối
                    if (isConnectedToVehicle()) {
                        val key = if (isBleMode) BleManager.SECRET_KEY else BluetoothController.SECRET_KEY
                        sendVehicleCommand("$key|SET_NAME|$vehicleCustomName")
                        Toast.makeText(this, "Đã lưu và đồng bộ tên xe: $vehicleCustomName", Toast.LENGTH_SHORT).show()
                    } else {
                        Toast.makeText(this, "Đã lưu tên xe: $vehicleCustomName\n(Sẽ tự động cập nhật vào xe khi kết nối)", Toast.LENGTH_SHORT).show()
                    }
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
            .putString("VEHICLE_NAME_$mac", vehicleCustomName)
            .putString("VEHICLE_NAME", vehicleCustomName)
            .apply()
        updateDeviceNameDisplay()
    }

    @SuppressLint("MissingPermission")
    private fun updateDeviceNameDisplay() {
        val tvDeviceName = findViewById<TextView?>(R.id.tvDeviceName)
        val tvVehicleCardTitle = findViewById<TextView?>(R.id.tvVehicleCardTitle)
        val mac = if (isBleMode) BleManager.activeMac else BluetoothController.deviceMac

        tvTitleRef?.text = vehicleCustomName
        tvVehicleCardTitle?.text = vehicleCustomName

        if (mac == null) {
            tvDeviceName?.text = "Thiết bị: Chưa chọn"
            return
        }
        val type = if (isBleMode) "BLE" else "Classic"
        val hwName = if (isBleMode) (BleManager.deviceName ?: "XE_tsmart_BLE") else (BluetoothController.deviceName ?: "Xe BT")
        tvDeviceName?.text = "$hwName ($type) • $mac"
    }

    private fun saveSettings() {
        val mac = if (isBleMode) BleManager.activeMac else BluetoothController.deviceMac
        val prefs = getSharedPreferences("BT_PREF", MODE_PRIVATE)
        val editor = prefs.edit()
            .putBoolean("AUTO_CONNECT", isAutoConnectEnabled)
            .putBoolean("AUTO_START", isAutoStartEnabled)
            .putBoolean("BG_RUN_ENABLED", isBackgroundServiceEnabled)
            .putBoolean("SHOW_INFO", isShowInfoEnabled)
            .putBoolean("HIDE_NAV", isHideNavEnabled)
            .putString("VEHICLE_NAME", vehicleCustomName)
            .putInt("DELAY_VALUE", autoStartDelay)
        if (mac != null) {
            editor.putString("VEHICLE_NAME_$mac", vehicleCustomName)
        }
        editor.apply()
    }

    private fun loadDevice() {
        val prefs = getSharedPreferences("BT_PREF", MODE_PRIVATE)
        val mac = prefs.getString("DEVICE_MAC", null)
        val key = prefs.getString("SECRET_KEY", "271000") ?: "271000"
        
        // Tải tên riêng theo từng xe (dựa trên MAC đã lưu)
        vehicleCustomName = (if (mac != null) prefs.getString("VEHICLE_NAME_$mac", null) else null)
            ?: prefs.getString("VEHICLE_NAME", "Honda SH 150i")
            ?: "Honda SH 150i"
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
        isBackgroundServiceEnabled = prefs.getBoolean("BG_RUN_ENABLED", true)
        isShowInfoEnabled = prefs.getBoolean("SHOW_INFO", true)
        isHideNavEnabled = prefs.getBoolean("HIDE_NAV", false)
        autoStartDelay = prefs.getInt("DELAY_VALUE", 3)
        updateDeviceNameDisplay()
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
            if (isBleMode && BleManager.isBonded) {
                tvStatus.text = "BLE CONNECTED • Mã hóa AES-128"
            } else if (isBleMode) {
                tvStatus.text = "BLE CONNECTED • Đang chờ ghép đôi"
            } else {
                tvStatus.text = "CONNECTED • Đã kết nối"
            }
            tvStatus.setTextColor(getColor(R.color.secondary_teal))
            btnConnect.text = getString(R.string.btn_disconnect_bt)
            tvFpSensorStatus?.text = "Cảm biến R503: Sẵn sàng hoạt động"
            tvFpSensorStatus?.setTextColor(getColor(R.color.secondary_teal))
            requestFingerprintList()
            sendVehicleCommand("GET_ALARM")
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
            smoothedRssi = 0f
            currentEstimatedDistance = -1.0f
            if (isAutoConnectEnabled && !isManualDisconnect) {
                handler.removeCallbacks(reconnectRunnable)
                handler.postDelayed(reconnectRunnable, 4000)
            }
        }
    }

    private fun syncStateToWatch() {
        WatchSyncHelper.isVehiclePowerOn = isOn
        WatchSyncHelper.currentDistanceMeters = if (isConnectedToVehicle()) currentEstimatedDistance else -1.0f
        WatchSyncHelper.currentSignalRssi = if (isConnectedToVehicle()) currentBleRssi else 0
        WatchSyncHelper.syncCurrentStateToWatch(this)
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

    private fun onOtaFileSelected(uri: android.net.Uri) {
        try {
            var fileName = "firmware.bin"
            var fileSize = 0L
            val cursor = contentResolver.query(uri, null, null, null, null)
            cursor?.use {
                if (it.moveToFirst()) {
                    val nameIdx = it.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    val sizeIdx = it.getColumnIndex(android.provider.OpenableColumns.SIZE)
                    if (nameIdx != -1) fileName = it.getString(nameIdx)
                    if (sizeIdx != -1) fileSize = it.getLong(sizeIdx)
                }
            }
            if (fileSize == 0L) {
                val pfd = contentResolver.openFileDescriptor(uri, "r")
                fileSize = pfd?.statSize ?: 0L
                pfd?.close()
            }

            val sizeKb = fileSize / 1024
            tvFileInfoRef?.text = "File: $fileName ($sizeKb KB)"
            tvFileInfoRef?.setTextColor(ContextCompat.getColor(this, R.color.gold_bright))
            btnStartOtaRef?.isEnabled = true
        } catch (e: Exception) {
            Toast.makeText(this, "Lỗi đọc thông tin file: ${e.localizedMessage}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun showOtaUpdateDialog() {
        if (!isConnectedToVehicle()) {
            Toast.makeText(this, "Vui lòng kết nối Bluetooth với xe trước!", Toast.LENGTH_SHORT).show()
            return
        }

        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_ota_update, null)
        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .setCancelable(false)
            .create()

        dialog.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))

        val btnSelectFile = dialogView.findViewById<AppCompatButton>(R.id.btnSelectFile)
        val tvFileInfo = dialogView.findViewById<TextView>(R.id.tvFileInfo)
        val tvFileMd5 = dialogView.findViewById<TextView>(R.id.tvFileMd5)
        val btnStartOta = dialogView.findViewById<AppCompatButton>(R.id.btnStartOta)
        val btnCancelOta = dialogView.findViewById<AppCompatButton>(R.id.btnCancelOta)
        val layoutProgress = dialogView.findViewById<View>(R.id.layoutOtaProgress)
        val progressBar = dialogView.findViewById<ProgressBar>(R.id.progressBarOta)
        val tvPercent = dialogView.findViewById<TextView>(R.id.tvOtaPercent)
        val tvStatus = dialogView.findViewById<TextView>(R.id.tvOtaStatus)
        val tvSpeed = dialogView.findViewById<TextView>(R.id.tvOtaSpeed)

        tvFileInfoRef = tvFileInfo
        tvFileMd5Ref = tvFileMd5
        btnStartOtaRef = btnStartOta
        layoutOtaProgressRef = layoutProgress
        progressBarOtaRef = progressBar
        tvOtaPercentRef = tvPercent
        tvOtaStatusRef = tvStatus
        tvOtaSpeedRef = tvSpeed

        // Khôi phục file đã chọn trước đó (nếu có)
        if (selectedOtaFileUri != null) {
            onOtaFileSelected(selectedOtaFileUri!!)
        }

        btnSelectFile.setOnClickListener {
            if (OtaManager.isUpdating) return@setOnClickListener
            selectFirmwareLauncher.launch("*/*")
        }

        btnStartOta.setOnClickListener {
            val uri = selectedOtaFileUri
            if (uri == null) {
                Toast.makeText(this, "Vui lòng chọn file firmware trước!", Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }

            if (isOn) {
                Toast.makeText(this, "⚠️ Xe đang mở khóa ACC! Vui lòng tắt khóa điện trước khi cập nhật.", Toast.LENGTH_LONG).show()
                return@setOnClickListener
            }

            btnSelectFile.isEnabled = false
            btnStartOta.isEnabled = false
            btnCancelOta.text = "Hủy cập nhật"
            layoutProgress.visibility = View.VISIBLE
            progressBar.progress = 0
            tvPercent.text = "0%"
            tvStatus.text = "Đang chuẩn bị gói dữ liệu..."

            OtaManager.onProgress = { percent, bytesSent, totalBytes, speedKbps ->
                runOnUiThread {
                    progressBar.progress = percent
                    tvPercent.text = "$percent%"
                    val sentKb = bytesSent / 1024
                    val totalKb = totalBytes / 1024
                    tvSpeed.text = String.format(Locale.US, "%d KB / %d KB (%.1f KB/s)", sentKb, totalKb, speedKbps)
                }
            }

            OtaManager.onStatusChange = { msg ->
                runOnUiThread {
                    tvStatus.text = msg
                }
            }

            OtaManager.onCompleted = { success, msg ->
                runOnUiThread {
                    btnSelectFile.isEnabled = true
                    btnCancelOta.text = "Đóng"
                    btnStartOta.isEnabled = true
                    tvStatus.text = msg
                    if (success) {
                        tvPercent.text = "100%"
                        progressBar.progress = 100
                        Toast.makeText(this, msg, Toast.LENGTH_LONG).show()
                        triggerHapticFeedback()
                    } else {
                        Toast.makeText(this, msg, Toast.LENGTH_LONG).show()
                    }
                }
            }

            OtaManager.startOta(this, uri)
        }

        btnCancelOta.setOnClickListener {
            if (OtaManager.isUpdating) {
                AlertDialog.Builder(this)
                    .setTitle("Hủy Cập Nhật OTA?")
                    .setMessage("Tiến trình nạp firmware đang diễn ra. Bạn có chắc muốn dừng lại không?")
                    .setPositiveButton("Dừng Lại") { _, _ ->
                        OtaManager.abortOta()
                        dialog.dismiss()
                    }
                    .setNegativeButton("Tiếp Tục", null)
                    .show()
            } else {
                dialog.dismiss()
            }
        }

        dialog.show()
    }


    private fun toggleBackgroundService(enable: Boolean) {
        if (enable) {
            // Kiểm tra quyền thông báo trên Android 13+
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(arrayOf(android.Manifest.permission.POST_NOTIFICATIONS), 102)
                }
            }
            VehicleBackgroundService.startService(this)
            Toast.makeText(this, "🟢 Đã BẬT chạy ngầm: Luôn sẵn sàng kết nối xe!", Toast.LENGTH_SHORT).show()
        } else {
            VehicleBackgroundService.stopService(this)
            Toast.makeText(this, "⚪ Đã TẮT chạy ngầm", Toast.LENGTH_SHORT).show()
        }
    }

    private fun requestIgnoreBatteryOptimization() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val powerManager = getSystemService(Context.POWER_SERVICE) as android.os.PowerManager
            val isIgnoring = powerManager.isIgnoringBatteryOptimizations(packageName)
            if (isIgnoring) {
                Toast.makeText(this, "✅ Ứng dụng đã được cấp quyền chạy ngầm không giới hạn!", Toast.LENGTH_SHORT).show()
            } else {
                try {
                    val intent = android.content.Intent(android.provider.Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                        data = android.net.Uri.parse("package:$packageName")
                    }
                    startActivity(intent)
                } catch (e: Exception) {
                    // Fallback sang màn hình cài đặt quản lý pin tổng quát
                    try {
                        val intent = android.content.Intent(android.provider.Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)
                        startActivity(intent)
                    } catch (e2: Exception) {
                        Toast.makeText(this, "Vui lòng tắt tối ưu hóa pin cho ứng dụng trong Cài đặt", Toast.LENGTH_LONG).show()
                    }
                }
            }
        } else {
            Toast.makeText(this, "Hệ điều hành Android này không bị giới hạn pin Doze.", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
            pendingPermissionAction?.invoke()
        }
        pendingPermissionAction = null
    }

    override fun onResume() {
        super.onResume()
        WatchSyncHelper.checkWatchConnection(this) { isConn, name ->
            updateWatchStatusUI(isConn, name)
        }
    }

    private fun updateWatchStatusUI(isConnected: Boolean, deviceName: String?) {
        runOnUiThread {
            val tvWatchStatusBadge = findViewById<TextView?>(R.id.tvWatchStatusBadge)
            val ivWatchStatusIcon = findViewById<ImageView?>(R.id.ivWatchStatusIcon)
            val tvWatchDeviceName = findViewById<TextView?>(R.id.tvWatchDeviceName)
            val tvWatchStatusDesc = findViewById<TextView?>(R.id.tvWatchStatusDesc)

            if (isConnected) {
                val name = if (!deviceName.isNullOrBlank()) deviceName else "Đồng hồ Wear OS"
                tvWatchStatusBadge?.text = "🟢 Đã kết nối"
                tvWatchStatusBadge?.setTextColor(ContextCompat.getColor(this, R.color.accent_emerald))
                ivWatchStatusIcon?.setColorFilter(ContextCompat.getColor(this, R.color.accent_emerald))
                tvWatchDeviceName?.text = name
                tvWatchDeviceName?.setTextColor(ContextCompat.getColor(this, R.color.white))
                tvWatchStatusDesc?.text = "Đang kết nối & sẵn sàng nhận lệnh điều khiển xe"
                tvWatchStatusDesc?.setTextColor(ContextCompat.getColor(this, R.color.secondary_teal))
            } else {
                tvWatchStatusBadge?.text = "⚪ Chưa kết nối"
                tvWatchStatusBadge?.setTextColor(ContextCompat.getColor(this, R.color.text_muted))
                ivWatchStatusIcon?.setColorFilter(ContextCompat.getColor(this, R.color.text_muted))
                tvWatchDeviceName?.text = "Đồng hồ: Chưa kết nối"
                tvWatchDeviceName?.setTextColor(ContextCompat.getColor(this, R.color.text_secondary))
                tvWatchStatusDesc?.text = "Bật Bluetooth & mở app trên đồng hồ Wear OS"
                tvWatchStatusDesc?.setTextColor(ContextCompat.getColor(this, R.color.text_muted))
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        try {
            unregisterReceiver(bondStateReceiver)
        } catch (e: Exception) {
            Log.e("BLE_SEC", "Error unregistering bondStateReceiver", e)
        }
        handler.removeCallbacks(rssiPollRunnable)

        // Nếu người dùng bật Chạy Ngầm (isBackgroundServiceEnabled == true), KHÔNG ngắt kết nối BLE
        // Service VehicleBackgroundService sẽ tiếp quản và duy trì kết nối liên tục
        if (!isBackgroundServiceEnabled) {
            disconnectVehicle()
        } else {
            Log.i("APP_LIFECYCLE", "MainActivity onDestroy: Background Service is active, keeping BLE connected.")
        }
    }

    private fun updateIntruderBadgeUI(count: Int) {
        runOnUiThread {
            intruderCount = count
            tvIntruderCountTagRef?.text = "$count ẢNH"
            if (count > 0) {
                tvIntruderCountTagRef?.setTextColor(ContextCompat.getColor(this, R.color.accent_danger))
                tvIntruderBadgeRef?.text = "⚠️ Phát hiện $count lần chạm lạ!"
                tvIntruderBadgeRef?.setTextColor(ContextCompat.getColor(this, R.color.accent_danger))
            } else {
                tvIntruderCountTagRef?.setTextColor(ContextCompat.getColor(this, R.color.accent_cyan))
                tvIntruderBadgeRef?.text = "Xe an toàn • Chưa có vi phạm"
                tvIntruderBadgeRef?.setTextColor(ContextCompat.getColor(this, R.color.text_secondary))
            }
        }
    }

    // ==========================================
    // 🛡️ HỘP THOẠI BẮT QUẢ TANG VÂN TAY LẠ (INTRUDER AUDIT DIALOG)
    // ==========================================
    private fun showIntruderAuditDialog() {
        if (!isConnectedToVehicle()) {
            Toast.makeText(this, "Vui lòng kết nối xe trước khi xem nhật ký!", Toast.LENGTH_SHORT).show()
            return
        }

        val dialogView = layoutInflater.inflate(R.layout.dialog_intruder_audit, null)
        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .create()
        dialog.window?.setBackgroundDrawableResource(android.R.color.transparent)

        val btnClose = dialogView.findViewById<ImageButton>(R.id.btnCloseIntruderDialog)
        val btnRefresh = dialogView.findViewById<AppCompatButton>(R.id.btnRefreshIntruderList)
        val btnClear = dialogView.findViewById<AppCompatButton>(R.id.btnClearIntruderLogs)
        val pbLoading = dialogView.findViewById<ProgressBar>(R.id.pbIntruderLoading)
        val llPreview = dialogView.findViewById<View>(R.id.llIntruderImagePreview)
        val ivFp = dialogView.findViewById<ImageView>(R.id.ivIntruderFingerprint)
        val tvTimeHeader = dialogView.findViewById<TextView>(R.id.tvIntruderImageTimeHeader)
        val btnInvert = dialogView.findViewById<AppCompatButton>(R.id.btnInvertIntruderImg)
        val btnSave = dialogView.findViewById<AppCompatButton>(R.id.btnSaveIntruderImg)
        val tvEmpty = dialogView.findViewById<TextView>(R.id.tvIntruderEmpty)
        val rvList = dialogView.findViewById<androidx.recyclerview.widget.RecyclerView>(R.id.rvIntruderList)

        pbIntruderLoadingRef = pbLoading
        llIntruderImagePreviewRef = llPreview
        ivIntruderFingerprintRef = ivFp
        tvIntruderImageTimeHeaderRef = tvTimeHeader
        tvIntruderEmptyRef = tvEmpty
        rvIntruderListRef = rvList

        rvList.layoutManager = androidx.recyclerview.widget.LinearLayoutManager(this)
        intruderAdapter = IntruderLogAdapter(intruderLogList) { item ->
            isFetchingIntruderImage = true
            pbLoading.visibility = View.VISIBLE
            tvTimeHeader.text = "Ảnh trích xuất lúc: ${item.timeStr}"
            sendVehicleCommand("FETCH_INTRUDER_IMG|${item.filename}")
            Toast.makeText(this, "Đang tải ảnh ${item.filename} từ xe...", Toast.LENGTH_SHORT).show()
        }
        rvList.adapter = intruderAdapter

        if (intruderLogList.isEmpty()) {
            tvEmpty.visibility = View.VISIBLE
            rvList.visibility = View.GONE
        } else {
            tvEmpty.visibility = View.GONE
            rvList.visibility = View.VISIBLE
        }

        btnClose.setOnClickListener { dialog.dismiss() }

        btnRefresh.setOnClickListener {
            intruderLogList.clear()
            pbLoading.visibility = View.VISIBLE
            sendVehicleCommand("GET_INTRUDER_LIST")
        }

        btnClear.setOnClickListener {
            AlertDialog.Builder(this)
                .setTitle("🗑️ Xóa Toàn Bộ Nhật Ký")
                .setMessage("Bạn có chắc chắn muốn xóa tất cả ảnh vân tay kẻ gian đang lưu trong Flash ESP32?")
                .setPositiveButton("Xóa") { _, _ ->
                    sendVehicleCommand("CLEAR_INTRUDER_LOGS")
                }
                .setNegativeButton("Hủy", null)
                .show()
        }

        btnInvert.setOnClickListener {
            if (currentIntruderBitmap != null) {
                isIntruderImageInverted = !isIntruderImageInverted
                val inverted = invertBitmap(currentIntruderBitmap!!)
                ivFp.setImageBitmap(inverted)
            }
        }

        btnSave.setOnClickListener {
            if (currentIntruderBitmap != null) {
                saveBitmapToGallery(currentIntruderBitmap!!, "intruder_fp_${System.currentTimeMillis()}.png")
                Toast.makeText(this, "Đã lưu ảnh vân tay vào Thư viện máy!", Toast.LENGTH_SHORT).show()
            }
        }

        val swEnable = dialogView.findViewById<androidx.appcompat.widget.SwitchCompat>(R.id.swIntruderEnable)
        val tvSwitchSubtitle = dialogView.findViewById<TextView>(R.id.tvIntruderSwitchSubtitle)
        swIntruderEnableRef = swEnable

        swEnable?.isChecked = intruderCaptureEnabled
        tvSwitchSubtitle?.text = if (intruderCaptureEnabled) "🟢 Đang BẬT: Tự động lưu ảnh khi có vân tay lạ" else "⚪ Đang TẮT: Không lưu ảnh vân tay lạ"

        swEnable?.setOnCheckedChangeListener { _, isChecked ->
            intruderCaptureEnabled = isChecked
            tvSwitchSubtitle?.text = if (isChecked) "🟢 Đang BẬT: Tự động lưu ảnh khi có vân tay lạ" else "⚪ Đang TẮT: Không lưu ảnh vân tay lạ"
            getSharedPreferences("BT_PREF", MODE_PRIVATE).edit().putBoolean("INTRUDER_CAPTURE_EN", isChecked).apply()
            sendVehicleCommand("SET_INTRUDER_CFG|${if (isChecked) 1 else 0}")
            Toast.makeText(this, if (isChecked) "Đã BẬT tự động bắt quả tang vân tay lạ" else "Đã TẮT bắt quả tang vân tay lạ", Toast.LENGTH_SHORT).show()
        }

        dialog.show()
        // Tự động tải danh sách và đồng bộ cấu hình khi vừa mở dialog
        sendVehicleCommand("GET_INTRUDER_CFG")
        intruderLogList.clear()
        pbLoading.visibility = View.VISIBLE
        sendVehicleCommand("GET_INTRUDER_LIST")
    }

    // ==========================================
    // ⚡ HỘP THOẠI CÀI ĐẶT THỜI GIAN ĐỀ XE (CRANK TIME SETTINGS)
    // ==========================================
    private fun showCrankTimeSettingsDialog() {
        if (!isConnectedToVehicle()) {
            Toast.makeText(this, "Vui lòng kết nối xe trước khi cài đặt!", Toast.LENGTH_SHORT).show()
            return
        }

        val dialogView = layoutInflater.inflate(R.layout.dialog_crank_time_settings, null)
        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .create()
        dialog.window?.setBackgroundDrawableResource(android.R.color.transparent)

        val btnClose = dialogView.findViewById<ImageButton>(R.id.btnCloseCrankDialog)
        val tvCurrent = dialogView.findViewById<TextView>(R.id.tvCurrentCrankMs)
        val tvHint = dialogView.findViewById<TextView>(R.id.tvCrankVehicleHint)
        val etMs = dialogView.findViewById<EditText>(R.id.etCrankTimeMs)
        val sb = dialogView.findViewById<SeekBar>(R.id.sbCrankTime)
        val btnScooter = dialogView.findViewById<androidx.appcompat.widget.AppCompatButton>(R.id.btnPresetScooter)
        val btnStandard = dialogView.findViewById<androidx.appcompat.widget.AppCompatButton>(R.id.btnPresetStandard)
        val btnHeavy = dialogView.findViewById<androidx.appcompat.widget.AppCompatButton>(R.id.btnPresetHeavy)
        val btnSave = dialogView.findViewById<androidx.appcompat.widget.AppCompatButton>(R.id.btnSaveCrankTime)

        var selectedMs = crankTimeMs.coerceIn(300, 5000)

        fun updateUI(ms: Int) {
            selectedMs = ms.coerceIn(300, 5000)
            val sec = selectedMs / 1000f
            tvCurrent?.text = "$selectedMs ms (${String.format(java.util.Locale.US, "%.1fs", sec)})"
            if (etMs?.text?.toString() != selectedMs.toString()) {
                etMs?.setText(selectedMs.toString())
                etMs?.setSelection(etMs.text.length)
            }
            val progress = ((selectedMs - 300) / 100).coerceIn(0, 47)
            if (sb?.progress != progress) {
                sb?.progress = progress
            }
            tvHint?.text = when {
                selectedMs < 1000 -> "⚡ Đề cực nhạy: Phù hợp xe tay ga FI đời mới, dễ nổ"
                selectedMs <= 1800 -> "✨ Tiêu chuẩn: Chuẩn cho hầu hết các dòng xe số & tay ga phổ thông"
                else -> "🚀 Kéo dài: Dành cho xe bình yếu, xe phân khối lớn hoặc xe khó nổ"
            }
        }

        updateUI(selectedMs)

        sb?.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(p0: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    val ms = 300 + progress * 100
                    updateUI(ms)
                }
            }
            override fun onStartTrackingTouch(p0: SeekBar?) {}
            override fun onStopTrackingTouch(p0: SeekBar?) {}
        })

        etMs?.addTextChangedListener(object : android.text.TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: android.text.Editable?) {
                val inputMs = s?.toString()?.toIntOrNull()
                if (inputMs != null && inputMs in 200..5000 && inputMs != selectedMs) {
                    updateUI(inputMs)
                }
            }
        })

        btnScooter?.setOnClickListener { updateUI(800) }
        btnStandard?.setOnClickListener { updateUI(1500) }
        btnHeavy?.setOnClickListener { updateUI(2500) }

        btnSave?.setOnClickListener {
            val finalMs = etMs?.text?.toString()?.toIntOrNull() ?: selectedMs
            val clamped = finalMs.coerceIn(200, 5000)
            crankTimeMs = clamped
            getSharedPreferences("BT_PREF", MODE_PRIVATE).edit().putInt("CRANK_TIME_MS", clamped).apply()
            sendVehicleCommand("SET_CRANK_TIME|$clamped")
            Toast.makeText(this, "✅ Đã lưu thời gian đề xe: $clamped ms (${String.format(java.util.Locale.US, "%.1fs", clamped / 1000f)})", Toast.LENGTH_SHORT).show()
            dialog.dismiss()
        }

        btnClose?.setOnClickListener { dialog.dismiss() }

        dialog.show()
        sendVehicleCommand("GET_CRANK_TIME")
    }

    private fun invertBitmap(src: Bitmap): Bitmap {
        val out = Bitmap.createBitmap(src.width, src.height, src.config ?: Bitmap.Config.ARGB_8888)
        val canvas = Canvas(out)
        val paint = Paint()
        val matrix = ColorMatrix(floatArrayOf(
            -1f, 0f, 0f, 0f, 255f,
            0f, -1f, 0f, 0f, 255f,
            0f, 0f, -1f, 0f, 255f,
            0f, 0f, 0f, 1f, 0f
        ))
        paint.colorFilter = ColorMatrixColorFilter(matrix)
        canvas.drawBitmap(src, 0f, 0f, paint)
        return out
    }

    private fun saveBitmapToGallery(bmp: Bitmap, filename: String) {
        try {
            val resolver = contentResolver
            val contentValues = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, filename)
                put(MediaStore.MediaColumns.MIME_TYPE, "image/png")
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/TySmartKey")
                }
            }
            val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, contentValues)
            if (uri != null) {
                resolver.openOutputStream(uri)?.use { stream ->
                    bmp.compress(Bitmap.CompressFormat.PNG, 100, stream)
                }
            }
        } catch (e: Exception) {
            Log.e("SAVE_IMG", "Lỗi lưu ảnh", e)
        }
    }

    class IntruderLogAdapter(
        private val items: List<IntruderLogItem>,
        private val onItemClick: (IntruderLogItem) -> Unit
    ) : androidx.recyclerview.widget.RecyclerView.Adapter<IntruderLogAdapter.ViewHolder>() {

        class ViewHolder(view: View) : androidx.recyclerview.widget.RecyclerView.ViewHolder(view) {
            val tvTime: TextView = view.findViewById(R.id.tvIntruderTime)
            val tvDetails: TextView = view.findViewById(R.id.tvIntruderDetails)
            val btnFetch: AppCompatButton = view.findViewById(R.id.btnFetchIntruderItem)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_intruder_log, parent, false)
            return ViewHolder(view)
        }

        override fun onBindViewHolder(holder: ViewHolder, position: Int) {
            val item = items[position]
            holder.tvTime.text = item.timeStr
            val kb = String.format("%.1f KB", item.fileSize / 1024.0)
            holder.tvDetails.text = "${item.filename} • $kb"
            holder.btnFetch.setOnClickListener { onItemClick(item) }
        }

        override fun getItemCount() = items.size
    }
}
