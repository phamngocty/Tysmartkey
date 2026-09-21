package com.example.control_esp

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.os.Build
import android.util.Log
import java.util.*
import java.util.concurrent.ConcurrentHashMap

data class BleDeviceInfo(
    val device: BluetoothDevice,
    val name: String,
    val rssi: Int
)

@SuppressLint("MissingPermission")
object BleManager {
    private const val TAG = "BleManager"

    // UUIDs tương ứng với Firmware ESP32-C3 NimBLE
    val SERVICE_UUID: UUID = UUID.fromString("0000ff01-0000-1000-8000-00805f9b34fb")
    val CHARACTERISTIC_UUID: UUID = UUID.fromString("0000ff02-0000-1000-8000-00805f9b34fb")
    private val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private var bluetoothAdapter: BluetoothAdapter? = null
    private val gattMap = ConcurrentHashMap<String, BluetoothGatt>()
    
    var activeMac: String? = null
    var deviceName: String? = null
    var SECRET_KEY: String = "271000"

    // Callbacks phản hồi về UI
    var onMessageReceived: ((String) -> Unit)? = null
    var onConnectionStateChanged: ((Boolean) -> Unit)? = null
    var onRssiRead: ((Int) -> Unit)? = null
    var onBondStateChanged: ((Int) -> Unit)? = null
    var lastRssi: Int = -62

    val isConnected: Boolean
        get() = activeMac?.let { gattMap.containsKey(it) } == true

    val currentBondState: Int
        get() = activeMac?.let { bluetoothAdapter?.getRemoteDevice(it)?.bondState } ?: BluetoothDevice.BOND_NONE

    val isBonded: Boolean
        get() = currentBondState == BluetoothDevice.BOND_BONDED

    fun readRssi() {
        val mac = activeMac ?: return
        gattMap[mac]?.readRemoteRssi()
    }

    fun init(context: Context) {
        val manager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothAdapter = manager.adapter
    }

    fun startScan(
        onDeviceFound: (BleDeviceInfo) -> Unit,
        onScanFailed: (() -> Unit)? = null
    ): ScanCallback? {
        val scanner = bluetoothAdapter?.bluetoothLeScanner
        if (scanner == null) {
            onScanFailed?.invoke()
            return null
        }

        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult?) {
                val dev = result?.device ?: return
                val advertisedName = result.scanRecord?.deviceName
                val name = if (!advertisedName.isNullOrBlank()) {
                    advertisedName.trim()
                } else if (!dev.name.isNullOrBlank()) {
                    dev.name.trim()
                } else {
                    "Thiết bị BLE"
                }
                val rssi = result.rssi
                onDeviceFound(BleDeviceInfo(dev, name, rssi))
            }

            override fun onBatchScanResults(results: MutableList<ScanResult>?) {
                results?.forEach { result ->
                    val dev = result.device ?: return@forEach
                    val advertisedName = result.scanRecord?.deviceName
                    val name = if (!advertisedName.isNullOrBlank()) {
                        advertisedName.trim()
                    } else if (!dev.name.isNullOrBlank()) {
                        dev.name.trim()
                    } else {
                        "Thiết bị BLE"
                    }
                    onDeviceFound(BleDeviceInfo(dev, name, result.rssi))
                }
            }

            override fun onScanFailed(errorCode: Int) {
                Log.e(TAG, "BLE Scan failed with code: $errorCode")
                onScanFailed?.invoke()
            }
        }

        scanner.startScan(callback)
        return callback
    }

    fun stopScan(callback: ScanCallback?) {
        if (callback != null) {
            try {
                bluetoothAdapter?.bluetoothLeScanner?.stopScan(callback)
            } catch (e: Exception) {
                Log.e(TAG, "Error stopping scan", e)
            }
        }
    }

    fun connect(context: Context, macAddress: String, preferredName: String? = null, onResult: (Boolean) -> Unit) {
        val adapter = bluetoothAdapter ?: run {
            onResult(false)
            return
        }
        if (!BluetoothAdapter.checkBluetoothAddress(macAddress)) {
            onResult(false)
            return
        }

        val device = adapter.getRemoteDevice(macAddress)

        // Ngắt kết nối thiết bị cũ nếu khác MAC
        if (activeMac != null && activeMac != macAddress) {
            disconnect(activeMac!!)
        }

        activeMac = macAddress
        deviceName = preferredName ?: device.name ?: "XE_tsmart_BLE"

        val callback = object : BluetoothGattCallback() {
            override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    Log.i(TAG, "Connected to GATT server: ${gatt.device.address}")
                    val mtuSuccess = gatt.requestMtu(512)
                    if (!mtuSuccess) {
                        Log.w(TAG, "requestMtu failed to initiate, discovering services directly")
                        gatt.discoverServices()
                    }
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    Log.i(TAG, "Disconnected from GATT server (status=$status).")
                    gattMap.remove(gatt.device.address)
                    incomingBuffer.setLength(0)
                    try {
                        gatt.disconnect()
                        gatt.close()
                    } catch (e: Exception) {
                        Log.e(TAG, "Error closing GATT", e)
                    }
                    onConnectionStateChanged?.invoke(false)
                    onResult(false)
                }
            }

            override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
                Log.i(TAG, "MTU changed to $mtu (status=$status)")
                gatt.discoverServices()
            }

            override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    val service = gatt.getService(SERVICE_UUID)
                    val characteristic = service?.getCharacteristic(CHARACTERISTIC_UUID)

                    if (characteristic != null) {
                        // 1. Đăng ký nhận Notify từ ESP32
                        gatt.setCharacteristicNotification(characteristic, true)
                        val descriptor = characteristic.getDescriptor(CCCD_UUID)
                        if (descriptor != null) {
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                                gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                            } else {
                                @Suppress("DEPRECATION")
                                descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                                @Suppress("DEPRECATION")
                                gatt.writeDescriptor(descriptor)
                            }
                        }
                        
                        gattMap[gatt.device.address] = gatt
                        Log.i(TAG, "Services discovered & Notifications enabled.")

                        // Nếu thiết bị chưa ghép đôi (chưa lưu LTK AES-128), kích hoạt quá trình xác thực SMP
                        if (gatt.device.bondState != BluetoothDevice.BOND_BONDED) {
                            Log.i(TAG, "Device not bonded (state=${gatt.device.bondState}). Triggering SMP pairing...")
                            try {
                                gatt.device.createBond()
                            } catch (e: Exception) {
                                Log.w(TAG, "createBond failed", e)
                            }
                            try {
                                gatt.readCharacteristic(characteristic)
                            } catch (e: Exception) {
                                Log.w(TAG, "readCharacteristic failed", e)
                            }
                        }

                        onConnectionStateChanged?.invoke(true)
                        onResult(true)
                    } else {
                        Log.w(TAG, "Characteristic not found on device!")
                        onResult(false)
                    }
                } else {
                    Log.w(TAG, "onServicesDiscovered failed: $status")
                    onResult(false)
                }
            }

            @Deprecated("Deprecated in Java")
            override fun onCharacteristicRead(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int
            ) {
                Log.d(TAG, "onCharacteristicRead (legacy) status: $status")
            }

            override fun onCharacteristicRead(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                value: ByteArray,
                status: Int
            ) {
                Log.d(TAG, "onCharacteristicRead status: $status")
            }

            // Xử lý dữ liệu phản hồi từ ESP32 gửi lên
            @Deprecated("Deprecated in Java")
            override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
                if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
                    @Suppress("DEPRECATION")
                    val data = characteristic.value ?: return
                    handleIncomingBytes(data)
                }
            }

            override fun onCharacteristicChanged(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                value: ByteArray
            ) {
                handleIncomingBytes(value)
            }

            override fun onReadRemoteRssi(gatt: BluetoothGatt, rssi: Int, status: Int) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    lastRssi = rssi
                    onRssiRead?.invoke(rssi)
                }
            }
        }

        device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
    }

    private val incomingBuffer = StringBuilder()

    @Synchronized
    private fun handleIncomingBytes(bytes: ByteArray) {
        val incomingStr = String(bytes, Charsets.UTF_8)
        incomingBuffer.append(incomingStr)

        // Phòng chống tràn bộ đệm (đặt ngưỡng 256KB an toàn cho luồng ảnh Base64 28KB)
        if (incomingBuffer.length > 262144) {
            val lastFbIdx = incomingBuffer.lastIndexOf("FB|")
            if (lastFbIdx >= 0) {
                incomingBuffer.delete(0, lastFbIdx)
            } else {
                incomingBuffer.setLength(0)
            }
        }

        var newlineIdx = incomingBuffer.indexOf("\n")
        while (newlineIdx != -1) {
            val completeLine = incomingBuffer.substring(0, newlineIdx).trim()
            incomingBuffer.delete(0, newlineIdx + 1)

            if (completeLine.isNotEmpty()) {
                val fbIdx = completeLine.indexOf("FB|")
                if (fbIdx != -1) {
                    val status = completeLine.substring(fbIdx + 3).trim()
                    if (status.isNotEmpty()) {
                        if (status.contains("FB|")) {
                            val parts = completeLine.split("FB|")
                            for (i in 1 until parts.size) {
                                val s = parts[i].trim()
                                if (s.isNotEmpty()) onMessageReceived?.invoke(s)
                            }
                        } else {
                            onMessageReceived?.invoke(status)
                        }
                    }
                }
            }
            newlineIdx = incomingBuffer.indexOf("\n")
        }
    }

    fun disconnect(macAddress: String = activeMac ?: "") {
        if (macAddress.isNotEmpty()) {
            gattMap[macAddress]?.let {
                try {
                    it.disconnect()
                    it.close()
                } catch (e: Exception) {
                    Log.e(TAG, "Error closing GATT in disconnect", e)
                }
                gattMap.remove(macAddress)
            }
        }
        if (macAddress == activeMac) {
            incomingBuffer.setLength(0)
            onConnectionStateChanged?.invoke(false)
        }
    }

    fun sendCommand(cmd: String): Boolean {
        val mac = activeMac ?: return false
        val gatt = gattMap[mac] ?: return false

        val service = gatt.getService(SERVICE_UUID) ?: return false
        val characteristic = service.getCharacteristic(CHARACTERISTIC_UUID) ?: return false

        val key = if (SECRET_KEY.isNotEmpty()) SECRET_KEY else "NO_KEY"
        val fullCmd = "$key|$cmd\n"
        val bytes = fullCmd.toByteArray()

        val isDeviceBonded = (gatt.device.bondState == BluetoothDevice.BOND_BONDED)
        val writeType = if (isDeviceBonded) {
            BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        } else {
            BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        }

        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val status = gatt.writeCharacteristic(characteristic, bytes, writeType)
            status == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.value = bytes
            @Suppress("DEPRECATION")
            characteristic.writeType = writeType
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(characteristic)
        }.also {
            if (it) Log.d(TAG, "Command sent via BLE: $fullCmd")
            else Log.e(TAG, "Failed to write characteristic")
        }
    }

    fun triggerBonding(): Boolean {
        val mac = activeMac ?: return false
        val device = bluetoothAdapter?.getRemoteDevice(mac) ?: return false
        return if (device.bondState == BluetoothDevice.BOND_NONE) {
            device.createBond()
        } else {
            true
        }
    }
}
