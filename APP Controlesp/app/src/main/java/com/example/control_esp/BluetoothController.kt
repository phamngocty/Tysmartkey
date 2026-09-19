package com.example.control_esp

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.util.Log
import java.io.InputStream
import java.io.OutputStream
import java.util.*

@SuppressLint("MissingPermission")
object BluetoothController {
    private var socket: BluetoothSocket? = null
    private var outputStream: OutputStream? = null
    private var inputStream: InputStream? = null
    private var listenerThread: Thread? = null
    private val uuid: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
    
    var SECRET_KEY: String = "271000"
    var deviceName: String? = null
    var deviceMac: String? = null

    // Callback để báo về UI hoặc Service khi có tin nhắn từ ESP32
    var onMessageReceived: ((String) -> Unit)? = null

    val isConnected: Boolean
        get() = socket?.isConnected == true

    fun connect(device: BluetoothDevice, onResult: (Boolean) -> Unit) {
        Thread {
            try {
                disconnect()

                socket = device.createRfcommSocketToServiceRecord(uuid)
                socket?.connect()
                outputStream = socket?.outputStream
                inputStream = socket?.inputStream
                
                startListening() // Bắt đầu lắng nghe phản hồi

                deviceMac = device.address
                deviceName = device.name
                
                onResult(true)
            } catch (e: Exception) {
                Log.e("BT_CONTROLLER", "Connection failed", e)
                socket = null
                outputStream = null
                inputStream = null
                onResult(false)
            }
        }.start()
    }

    private fun startListening() {
        listenerThread = Thread {
            val buffer = ByteArray(1024)
            while (isConnected) {
                try {
                    val bytes = inputStream?.read(buffer) ?: -1
                    if (bytes > 0) {
                        val message = String(buffer, 0, bytes).trim()
                        Log.d("BT_CONTROLLER", "Raw Feedback: $message")
                        
                        // Xử lý nếu tin nhắn bắt đầu bằng FB|
                        if (message.contains("FB|")) {
                            val parts = message.split("FB|")
                            if (parts.size > 1) {
                                val status = parts[1].trim()
                                onMessageReceived?.invoke(status)
                            }
                        }
                    }
                } catch (e: Exception) {
                    Log.e("BT_CONTROLLER", "Listen error", e)
                    break
                }
            }
        }
        listenerThread?.start()
    }

    fun disconnect() {
        try {
            listenerThread?.interrupt()
            inputStream?.close()
            outputStream?.close()
            socket?.close()
        } catch (e: Exception) {
            Log.e("BT_CONTROLLER", "Disconnect error", e)
        } finally {
            socket = null
            outputStream = null
            inputStream = null
            listenerThread = null
        }
    }

    fun sendCommand(cmd: String): Boolean {
        return try {
            if (socket?.isConnected != true || outputStream == null) {
                Log.e("BT_CONTROLLER", "Not connected")
                return false
            }
            
            val key = if (SECRET_KEY.isNotEmpty()) SECRET_KEY else "NO_KEY"
            val fullCmd = "$key|$cmd\n"
            
            outputStream?.write(fullCmd.toByteArray())
            Log.d("BT_CONTROLLER", "Sent: $fullCmd")
            true
        } catch (e: Exception) {
            Log.e("BT_CONTROLLER", "Send error", e)
            disconnect()
            false
        }
    }
}
