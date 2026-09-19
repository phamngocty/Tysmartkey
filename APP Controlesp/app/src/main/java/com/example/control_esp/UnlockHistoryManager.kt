package com.example.control_esp

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.*

data class UnlockHistoryItem(
    val id: String = UUID.randomUUID().toString(),
    val timestamp: Long,
    val title: String,
    val type: String,
    val detail: String = ""
) {
    fun getFormattedTime(): String {
        val sdf = SimpleDateFormat("HH:mm:ss - dd/MM/yyyy", Locale.getDefault())
        return sdf.format(Date(timestamp))
    }
}

object UnlockHistoryManager {
    private const val PREF_NAME = "UNLOCK_HISTORY_PREF"
    private const val KEY_HISTORY = "history_json"
    private const val MAX_HISTORY_COUNT = 100

    const val TYPE_FINGERPRINT_SUCCESS = "FP_SUCCESS"
    const val TYPE_FINGERPRINT_LOCK = "FP_LOCK"
    const val TYPE_FINGERPRINT_FAIL = "FP_FAIL"
    const val TYPE_APP_UNLOCK = "APP_UNLOCK"
    const val TYPE_APP_LOCK = "APP_LOCK"
    const val TYPE_ENGINE_START = "ENGINE_START"
    const val TYPE_LOCATE = "LOCATE"
    const val TYPE_WATCH_ACTION = "WATCH_ACTION"

    fun addEvent(context: Context, title: String, type: String, detail: String = "") {
        val list = getHistory(context).toMutableList()

        // Chống lặp sự kiện cùng loại trong khoảng 1.5 giây
        if (list.isNotEmpty()) {
            val latest = list[0]
            if (latest.type == type && (System.currentTimeMillis() - latest.timestamp < 1500)) {
                return
            }
        }

        val newItem = UnlockHistoryItem(
            timestamp = System.currentTimeMillis(),
            title = title,
            type = type,
            detail = detail
        )
        // Đưa sự kiện mới nhất lên đầu danh sách
        list.add(0, newItem)
        if (list.size > MAX_HISTORY_COUNT) {
            list.removeAt(list.lastIndex)
        }

        saveList(context, list)
    }

    fun getHistory(context: Context): List<UnlockHistoryItem> {
        val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
        val jsonStr = prefs.getString(KEY_HISTORY, "[]") ?: "[]"
        val result = mutableListOf<UnlockHistoryItem>()
        try {
            val jsonArray = JSONArray(jsonStr)
            for (i in 0 until jsonArray.length()) {
                val obj = jsonArray.getJSONObject(i)
                result.add(
                    UnlockHistoryItem(
                        id = obj.optString("id", UUID.randomUUID().toString()),
                        timestamp = obj.optLong("timestamp", System.currentTimeMillis()),
                        title = obj.optString("title", "Mở khóa xe"),
                        type = obj.optString("type", TYPE_APP_UNLOCK),
                        detail = obj.optString("detail", "")
                    )
                )
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return result
    }

    fun clearHistory(context: Context) {
        val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
        prefs.edit().remove(KEY_HISTORY).apply()
    }

    private fun saveList(context: Context, list: List<UnlockHistoryItem>) {
        val jsonArray = JSONArray()
        for (item in list) {
            val obj = JSONObject().apply {
                put("id", item.id)
                put("timestamp", item.timestamp)
                put("title", item.title)
                put("type", item.type)
                put("detail", item.detail)
            }
            jsonArray.put(obj)
        }
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_HISTORY, jsonArray.toString())
            .apply()
    }
}
