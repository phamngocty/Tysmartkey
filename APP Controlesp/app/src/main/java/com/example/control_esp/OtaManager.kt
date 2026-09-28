package com.example.control_esp

import android.content.Context
import android.net.Uri
import android.util.Log
import kotlinx.coroutines.*
import java.io.InputStream
import java.security.MessageDigest

object OtaManager {
    private const val TAG = "OtaManager"
    private const val CHUNK_SIZE = 240 // Kích thước gói nạp tối ưu cho BLE MTU 512
    private const val CHUNK_DELAY_MS = 8L // Độ trễ giữa các gói chống tràn buffer flash

    var isUpdating = false
        private set

    private var otaJob: Job? = null
    private var firmwareBytes: ByteArray? = null
    private var expectedMd5: String = ""

    // Callbacks phản hồi tiến trình
    var onProgress: ((percent: Int, bytesSent: Int, totalBytes: Int, speedKbps: Float) -> Unit)? = null
    var onStatusChange: ((String) -> Unit)? = null
    var onCompleted: ((success: Boolean, message: String) -> Unit)? = null

    /**
     * Bắt đầu quy trình cập nhật OTA từ URI file .bin do người dùng chọn
     */
    fun startOta(context: Context, fileUri: Uri) {
        if (isUpdating) {
            onCompleted?.invoke(false, "Tiến trình OTA đang chạy, vui lòng chờ.")
            return
        }

        if (!BleManager.isConnected) {
            onCompleted?.invoke(false, "Chưa kết nối Bluetooth với xe!")
            return
        }

        if (!BleManager.isBonded) {
            onCompleted?.invoke(false, "Thiết bị chưa được ghép đôi bảo mật (Bonded). Vui lòng ghép đôi với mã Passkey trước khi cập nhật OTA!")
            return
        }

        try {
            val inputStream: InputStream? = context.contentResolver.openInputStream(fileUri)
            val bytes = inputStream?.readBytes()
            inputStream?.close()

            if (bytes == null || bytes.isEmpty()) {
                onCompleted?.invoke(false, "Không thể đọc nội dung file firmware.")
                return
            }

            if (bytes.size > 1966080) { // 1.875 MB
                onCompleted?.invoke(false, "File firmware quá lớn (${bytes.size / 1024} KB). Giới hạn là 1.87 MB.")
                return
            }

            firmwareBytes = bytes
            expectedMd5 = calculateMd5(bytes)
            Log.i(TAG, "Loaded firmware: ${bytes.size} bytes, MD5: $expectedMd5")

            isUpdating = true
            onStatusChange?.invoke("Đang khởi tạo OTA với xe...")

            // Gửi lệnh bắt đầu OTA tới ESP32
            val beginCmd = "OTA_BEGIN|${bytes.size}|$expectedMd5"
            BleManager.sendCommand(beginCmd)

        } catch (e: Exception) {
            Log.e(TAG, "Error starting OTA", e)
            isUpdating = false
            onCompleted?.invoke(false, "Lỗi đọc file: ${e.localizedMessage}")
        }
    }

    /**
     * Bắt đầu quy trình OTA từ ByteArray trực tiếp (nếu có firmware nhúng sẵn trong app)
     */
    fun startOtaWithBytes(bytes: ByteArray) {
        if (isUpdating) return
        if (!BleManager.isConnected) {
            onCompleted?.invoke(false, "Chưa kết nối Bluetooth với xe!")
            return
        }

        firmwareBytes = bytes
        expectedMd5 = calculateMd5(bytes)
        isUpdating = true
        onStatusChange?.invoke("Đang khởi tạo OTA với xe...")
        BleManager.sendCommand("OTA_BEGIN|${bytes.size}|$expectedMd5")
    }

    /**
     * Lắng nghe phản hồi từ ESP32 để điều khiển trạng thái OTA
     */
    fun handleBleFeedback(fb: String) {
        if (!isUpdating && !fb.startsWith("OTA_")) return

        when {
            fb == "OTA_READY" -> {
                Log.i(TAG, "ESP32 ready! Bắt đầu truyền dữ liệu firmware...")
                onStatusChange?.invoke("Xe đã sẵn sàng! Đang truyền dữ liệu...")
                startStreamingChunks()
            }
            fb == "OTA_SUCCESS" -> {
                Log.i(TAG, "OTA hoàn tất thành công 100%!")
                isUpdating = false
                otaJob?.cancel()
                onCompleted?.invoke(true, "🎉 Cập nhật Firmware thành công! Xe đang khởi động lại...")
            }
            fb == "OTA_ERR_VEHICLE_ON" -> {
                isUpdating = false
                otaJob?.cancel()
                onCompleted?.invoke(false, "⚠️ Xe đang mở khóa ACC! Vui lòng tắt khóa điện trước khi cập nhật.")
            }
            fb == "OTA_ERR_BUSY" -> {
                isUpdating = false
                otaJob?.cancel()
                onCompleted?.invoke(false, "⚠️ ESP32 đang bận (đang thêm vân tay hoặc chụp ảnh).")
            }
            fb == "OTA_ERR_INVALID_SIZE" -> {
                isUpdating = false
                otaJob?.cancel()
                onCompleted?.invoke(false, "❌ Kích thước file không hợp lệ hoặc vượt quá phân vùng OTA.")
            }
            fb == "OTA_ERR_BEGIN" -> {
                isUpdating = false
                otaJob?.cancel()
                onCompleted?.invoke(false, "❌ ESP32 không thể khởi tạo phân vùng ghi OTA.")
            }
            fb == "OTA_ERR_WRITE" -> {
                isUpdating = false
                otaJob?.cancel()
                onCompleted?.invoke(false, "❌ Lỗi ghi khối dữ liệu vào Flash ESP32.")
            }
            fb == "OTA_ERR_VERIFY" -> {
                isUpdating = false
                otaJob?.cancel()
                onCompleted?.invoke(false, "❌ Lỗi xác thực tính toàn vẹn (Checksum MD5 không khớp)!")
            }
            fb == "OTA_TIMEOUT" -> {
                isUpdating = false
                otaJob?.cancel()
                onCompleted?.invoke(false, "⏳ Quá thời gian chờ gói tin (Timeout)! Tiến trình bị hủy.")
            }
            fb == "OTA_ABORTED" -> {
                isUpdating = false
                otaJob?.cancel()
                onCompleted?.invoke(false, "⚠️ Đã hủy cập nhật theo yêu cầu.")
            }
        }
    }

    private fun startStreamingChunks() {
        val bytes = firmwareBytes ?: run {
            isUpdating = false
            onCompleted?.invoke(false, "Không tìm thấy dữ liệu firmware.")
            return
        }

        otaJob?.cancel()
        otaJob = CoroutineScope(Dispatchers.IO).launch {
            val totalBytes = bytes.size
            var sentBytes = 0
            val startTime = System.currentTimeMillis()

            try {
                while (sentBytes < totalBytes && isActive && isUpdating) {
                    val remaining = totalBytes - sentBytes
                    val currentChunkSize = if (remaining > CHUNK_SIZE) CHUNK_SIZE else remaining
                    val chunk = bytes.copyOfRange(sentBytes, sentBytes + currentChunkSize)

                    var writeOk = BleManager.writeOtaChunk(chunk)
                    var retryCount = 0
                    while (!writeOk && retryCount < 5 && isActive && isUpdating) {
                        retryCount++
                        Log.w(TAG, "Write chunk failed at byte $sentBytes (attempt $retryCount/5), retrying in 25ms...")
                        delay(25)
                        writeOk = BleManager.writeOtaChunk(chunk)
                    }

                    if (!writeOk) {
                        throw Exception("Không thể gửi khối dữ liệu tại byte $sentBytes sau 5 lần thử. Kết nối BLE bị gián đoạn.")
                    }

                    sentBytes += currentChunkSize

                    val percent = ((sentBytes.toDouble() / totalBytes.toDouble()) * 100).toInt()
                    val elapsedSec = (System.currentTimeMillis() - startTime) / 1000f
                    val speedKbps = if (elapsedSec > 0) (sentBytes / 1024f) / elapsedSec else 0f

                    withContext(Dispatchers.Main) {
                        onProgress?.invoke(percent, sentBytes, totalBytes, speedKbps)
                    }

                    delay(CHUNK_DELAY_MS)
                }

                if (sentBytes >= totalBytes && isActive && isUpdating) {
                    withContext(Dispatchers.Main) {
                        onStatusChange?.invoke("Đã truyền 100%. Đang xác thực MD5 trên xe...")
                    }
                    Log.i(TAG, "Streaming completed! Sending OTA_END command...")
                    BleManager.sendCommand("OTA_END")
                }
            } catch (e: Exception) {
                Log.e(TAG, "Exception during OTA chunk streaming", e)
                BleManager.sendCommand("OTA_ABORT")
                withContext(Dispatchers.Main) {
                    isUpdating = false
                    onCompleted?.invoke(false, "Lỗi truyền dữ liệu: ${e.localizedMessage}")
                }
            }
        }
    }

    fun abortOta() {
        if (!isUpdating) return
        isUpdating = false
        otaJob?.cancel()
        BleManager.sendCommand("OTA_ABORT")
        onCompleted?.invoke(false, "Đã hủy cập nhật.")
    }

    private fun calculateMd5(bytes: ByteArray): String {
        val md = MessageDigest.getInstance("MD5")
        val digest = md.digest(bytes)
        return digest.joinToString("") { "%02x".format(it) }
    }
}
