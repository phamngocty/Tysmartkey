package com.example.control_esp

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.core.content.FileProvider
import kotlinx.coroutines.*
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

/**
 * 🚀 UPDATE MANAGER (Tích hợp quản lý Cập nhật In-App APK & Firmware BLE OTA)
 * Hỗ trợ tải linh hoạt bất kỳ file .bin nào được chỉ định từ version.json
 */
object UpdateManager {
    private const val TAG = "UpdateManager"

    // URL metadata mặc định của hệ thống
    const val DEFAULT_VERSION_URL = "http://192.168.1.114:3002/api/v1/repos/nas152/Tysmartkey/raw/version.json"

    data class AppUpdateInfo(
        val hasUpdate: Boolean,
        val versionCode: Int,
        val versionName: String,
        val apkUrl: String,
        val apkName: String,
        val changelog: String
    )

    data class FirmwareUpdateInfo(
        val versionCode: Int,
        val versionName: String,
        val binUrl: String,
        val binName: String,
        val secondaryBinUrl: String = "",
        val oledBinUrl: String = "",
        val changelog: String
    )

    data class UpdateCheckResult(
        val isSuccess: Boolean,
        val errorMessage: String? = null,
        val appInfo: AppUpdateInfo? = null,
        val fwInfo: FirmwareUpdateInfo? = null
    )

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    /**
     * 1. Kiểm tra phiên bản mới từ version.json
     */
    fun checkUpdates(
        context: Context,
        versionUrl: String = DEFAULT_VERSION_URL,
        onResult: (UpdateCheckResult) -> Unit
    ) {
        scope.launch {
            try {
                Log.d(TAG, "Checking updates from: $versionUrl")
                val url = URL(versionUrl)
                val conn = url.openConnection() as HttpURLConnection
                conn.requestMethod = "GET"
                conn.connectTimeout = 8000
                conn.readTimeout = 8000
                conn.connect()

                if (conn.responseCode != HttpURLConnection.HTTP_OK) {
                    throw Exception("HTTP Error: ${conn.responseCode} ${conn.responseMessage}")
                }

                val jsonStr = conn.inputStream.bufferedReader().use { it.readText() }
                conn.disconnect()

                val rootJson = JSONObject(jsonStr)

                // 1. Phân tích App Android
                var appInfo: AppUpdateInfo? = null
                if (rootJson.has("app")) {
                    val appObj = rootJson.getJSONObject("app")
                    val remoteVersionCode = appObj.optInt("versionCode", 0)
                    val remoteVersionName = appObj.optString("versionName", "1.0.0")
                    val apkUrl = appObj.optString("apkUrl", "")
                    val apkName = appObj.optString("apkName", "app-debug.apk")
                    val changelog = appObj.optString("changelog", "Cập nhật mới và sửa lỗi")

                    val currentAppVersionCode = try {
                        val pInfo = context.packageManager.getPackageInfo(context.packageName, 0)
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                            pInfo.longVersionCode.toInt()
                        } else {
                            @Suppress("DEPRECATION")
                            pInfo.versionCode
                        }
                    } catch (e: Exception) {
                        1
                    }

                    val hasUpdate = remoteVersionCode > currentAppVersionCode
                    appInfo = AppUpdateInfo(
                        hasUpdate = hasUpdate,
                        versionCode = remoteVersionCode,
                        versionName = remoteVersionName,
                        apkUrl = apkUrl,
                        apkName = apkName,
                        changelog = changelog
                    )
                }

                // 2. Phân tích Firmware ESP32
                var fwInfo: FirmwareUpdateInfo? = null
                if (rootJson.has("firmware")) {
                    val fwObj = rootJson.getJSONObject("firmware")
                    fwInfo = FirmwareUpdateInfo(
                        versionCode = fwObj.optInt("versionCode", 0),
                        versionName = fwObj.optString("versionName", "1.0.0"),
                        binUrl = fwObj.optString("binUrl", ""),
                        binName = fwObj.optString("binName", "firmware.bin"),
                        secondaryBinUrl = fwObj.optString("secondaryBinUrl", ""),
                        oledBinUrl = fwObj.optString("oledBinUrl", ""),
                        changelog = fwObj.optString("changelog", "")
                    )
                }

                withContext(Dispatchers.Main) {
                    onResult(
                        UpdateCheckResult(
                            isSuccess = true,
                            appInfo = appInfo,
                            fwInfo = fwInfo
                        )
                    )
                }

            } catch (e: Exception) {
                Log.e(TAG, "Error checking update", e)
                withContext(Dispatchers.Main) {
                    onResult(
                        UpdateCheckResult(
                            isSuccess = false,
                            errorMessage = e.localizedMessage ?: "Không thể kết nối đến máy chủ cập nhật"
                        )
                    )
                }
            }
        }
    }

    /**
     * 2. Tải file APK ngầm và mở bộ cài đặt Android an toàn qua FileProvider
     */
    fun downloadAndInstallApk(
        context: Context,
        apkUrl: String,
        apkName: String = "update.apk",
        onProgress: (percent: Int) -> Unit,
        onCompleted: (success: Boolean, errorMsg: String?) -> Unit
    ) {
        scope.launch {
            try {
                Log.d(TAG, "Downloading APK from: $apkUrl")
                val url = URL(apkUrl)
                val conn = url.openConnection() as HttpURLConnection
                conn.connectTimeout = 15000
                conn.readTimeout = 15000
                conn.connect()

                if (conn.responseCode != HttpURLConnection.HTTP_OK) {
                    throw Exception("Lỗi máy chủ: ${conn.responseCode}")
                }

                val fileLength = conn.contentLength
                val downloadDir = context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS) ?: context.cacheDir
                val apkFile = File(downloadDir, apkName)
                if (apkFile.exists()) apkFile.delete()

                val input: InputStream = conn.inputStream
                val output = FileOutputStream(apkFile)

                val data = ByteArray(4096)
                var total: Long = 0
                var count: Int
                var lastPercent = 0

                while (input.read(data).also { count = it } != -1) {
                    total += count.toLong()
                    output.write(data, 0, count)

                    if (fileLength > 0) {
                        val percent = (total * 100 / fileLength).toInt()
                        if (percent != lastPercent) {
                            lastPercent = percent
                            withContext(Dispatchers.Main) {
                                onProgress(percent)
                            }
                        }
                    }
                }

                output.flush()
                output.close()
                input.close()
                conn.disconnect()

                Log.d(TAG, "APK Downloaded: ${apkFile.absolutePath} (${apkFile.length()} bytes)")

                withContext(Dispatchers.Main) {
                    onCompleted(true, null)
                    installApk(context, apkFile)
                }

            } catch (e: Exception) {
                Log.e(TAG, "Error downloading APK", e)
                withContext(Dispatchers.Main) {
                    onCompleted(false, e.localizedMessage)
                }
            }
        }
    }

    /**
     * Mở Intent cài đặt APK an toàn với FileProvider
     */
    fun installApk(context: Context, apkFile: File) {
        try {
            val authority = "${context.packageName}.fileprovider"
            val apkUri = FileProvider.getUriForFile(context, authority, apkFile)

            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(apkUri, "application/vnd.android.package-archive")
                flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_GRANT_READ_URI_PERMISSION
            }
            context.startActivity(intent)
        } catch (e: Exception) {
            Log.e(TAG, "Error launching package installer", e)
        }
    }

    /**
     * 3. Tải file firmware .bin từ URL rồi chuyển trực tiếp qua OtaManager để nạp BLE vào ESP32
     */
    fun downloadFirmwareAndFlashBle(
        binUrl: String,
        onDownloadProgress: (percent: Int) -> Unit,
        onStatus: (String) -> Unit,
        onCompleted: (success: Boolean, message: String) -> Unit
    ) {
        scope.launch {
            try {
                onStatus("Đang tải firmware từ máy chủ...")
                val url = URL(binUrl)
                val conn = url.openConnection() as HttpURLConnection
                conn.connectTimeout = 15000
                conn.readTimeout = 15000
                conn.connect()

                if (conn.responseCode != HttpURLConnection.HTTP_OK) {
                    throw Exception("Lỗi kết nối tải firmware: ${conn.responseCode}")
                }

                val totalLength = conn.contentLength
                val input: InputStream = conn.inputStream
                val buffer = java.io.ByteArrayOutputStream()
                val data = ByteArray(4096)
                var count: Int
                var totalRead: Long = 0
                var lastPercent = 0

                while (input.read(data).also { count = it } != -1) {
                    totalRead += count
                    buffer.write(data, 0, count)
                    if (totalLength > 0) {
                        val percent = (totalRead * 100 / totalLength).toInt()
                        if (percent != lastPercent) {
                            lastPercent = percent
                            withContext(Dispatchers.Main) {
                                onDownloadProgress(percent)
                            }
                        }
                    }
                }

                input.close()
                conn.disconnect()

                val firmwareBytes = buffer.toByteArray()
                Log.i(TAG, "Firmware downloaded successfully: ${firmwareBytes.size} bytes")

                withContext(Dispatchers.Main) {
                    onStatus("Tải xong! Đang chuyển dữ liệu sang bộ nạp BLE...")
                    
                    // Cấu hình callbacks cho OtaManager
                    OtaManager.onStatusChange = { st -> onStatus(st) }
                    OtaManager.onProgress = { p, sent, total, spd ->
                        onDownloadProgress(p)
                    }
                    OtaManager.onCompleted = { ok, msg ->
                        onCompleted(ok, msg)
                    }

                    // Kích hoạt nạp firmware qua BLE vào ESP32
                    OtaManager.startOtaWithBytes(firmwareBytes)
                }

            } catch (e: Exception) {
                Log.e(TAG, "Error downloading firmware", e)
                withContext(Dispatchers.Main) {
                    onCompleted(false, "Lỗi tải firmware: ${e.localizedMessage}")
                }
            }
        }
    }

    /**
     * Hiển thị Hộp thoại Cập nhật App chuẩn Material Design có Changelog
     */
    fun showAppUpdateDialog(
        context: Context,
        appInfo: AppUpdateInfo,
        onStartDownload: () -> Unit
    ) {
        val builder = AlertDialog.Builder(context)
        builder.setTitle("🎉 Có phiên bản mới: v${appInfo.versionName}")
        val msg = "Phiên bản hiện tại có bản cập nhật mới (Build ${appInfo.versionCode}).\n\n" +
                "📋 Nhật ký thay đổi:\n${appInfo.changelog}\n\n" +
                "Bạn có muốn cập nhật ứng dụng ngay bây giờ?"
        builder.setMessage(msg)
        builder.setPositiveButton("Cập nhật ngay") { _, _ ->
            onStartDownload()
        }
        builder.setNegativeButton("Để sau", null)
        builder.setCancelable(true)
        builder.show()
    }
}
