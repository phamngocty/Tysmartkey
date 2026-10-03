# Kế hoạch Thực hiện: Bộ lọc An toàn Cử chỉ Búng tay Wear OS & Đồng bộ Cài đặt 2 Chiều

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Triệt tiêu hoàn toàn lỗi vô tình bật/đề xe khi mở app trên Wear OS thông qua bộ lọc an toàn 4 lớp (Warm-up 2.0s, Phân cấp độ nhạy, Chế độ chỉ đề khi xe đã bật) và xây dựng hệ thống cài đặt đồng bộ 2 chiều thời gian thực giữa Điện thoại và Đồng hồ.

**Architecture:**
- **Wear OS (`wear` module):** Bổ sung Grace Period 2.0s khi `onResume`, tính toán ngưỡng động theo mức độ nhạy (26/21/16 m/s²), phân quyền hành vi theo `GESTURE_ACTION_MODE` (mặc định chỉ đề khi xe đã bật), lắng nghe path `/settings_sync` và gửi path `/settings_watch`.
- **Mobile Handheld (`app` module):** `WatchSyncHelper` quản lý đồng bộ cấu hình cử chỉ 2 chiều, `WearMessageListenerService` tiếp nhận cấu hình từ đồng hồ, `dialog_wear_gesture_settings.xml` và `MainActivity.kt` cung cấp UI tùy biến sang trọng.

**Tech Stack:** Kotlin, Jetpack Compose for Wear OS, Google Play Services Wearable Data Layer API (MessageClient & DataClient), Android Jetpack Material Components.

---

### Task 1: Cải tiến logic nhận diện cảm biến & Bộ lọc an toàn trên Wear OS

**Files:**
- Modify: `APP Controlesp/wear/src/main/java/com/example/control_esp/wear/MainActivity.kt:50-345`

- [ ] **Step 1: Khai báo các hằng số, biến cấu hình an toàn và thời gian Grace Period**
  - Thêm `appResumeTime = 0L`.
  - Khai báo hằng số Chế độ: `GESTURE_MODE_START_ONLY = 0`, `GESTURE_MODE_TOGGLE_AND_START = 1`.
  - Khai báo hằng số Độ nhạy: `SENSITIVITY_LOW = 0` ($26.0\text{ m/s}^2$), `SENSITIVITY_MED = 1` ($21.0\text{ m/s}^2$), `SENSITIVITY_HIGH = 2` ($16.0\text{ m/s}^2$).
  - Thêm state Compose:
    + `gestureActionMode by mutableStateOf(0)` (mặc định chỉ đề khi bật).
    + `gestureSensitivity by mutableStateOf(1)` (mặc định Vừa).

- [ ] **Step 2: Nâng cấp `onResume()` và `registerSensorIfNeeded()`**
  - Trong `onResume()`:
    + `appResumeTime = System.currentTimeMillis()`.
    + `lastGestureTime = System.currentTimeMillis()`.
  - Trong `onSensorChanged()`:
    + Thêm kiểm tra Grace Period:
      ```kotlin
      if (System.currentTimeMillis() - appResumeTime < 2000L) return
      ```
    + Tính toán ngưỡng `threshold` dựa trên `gestureSensitivity`:
      ```kotlin
      val threshold = when (gestureSensitivity) {
          0 -> 26.0 // Low
          2 -> 16.0 // High
          else -> 21.0 // Med (default)
      }
      ```
    + Nâng cooldown lên 2.5s (`currentTime - lastGestureTime > 2500`).

- [ ] **Step 3: Triển khai kiểm tra Chế độ hành vi cử chỉ `gestureActionMode`**
  - Khi búng tay thành công:
    ```kotlin
    if (gestureActionMode == 0) { // MODE_START_ONLY
        if (isVehicleOn) {
            triggerEngineStart()
        } else {
            Log.d(TAG, "Gesture ignored: vehicle is OFF in START_ONLY mode")
        }
    } else { // MODE_TOGGLE_AND_START
        if (isVehicleOn) {
            triggerEngineStart()
        } else {
            sendCommandToPhone("1")
        }
    }
    ```

- [ ] **Step 4: Tiếp nhận cấu hình đồng bộ `/settings_sync` trong `onMessageReceived`**
  - Parse chuỗi `isGestureEnabled|mode|sensitivity`.
  - Lưu vào `WEAR_PREF` và cập nhật state Compose `isGestureEnabled`, `gestureActionMode`, `gestureSensitivity`.
  - Gọi `registerSensorIfNeeded()`.

---

### Task 2: Nâng cấp Giao diện Cài đặt Tab 3 trên Wear OS

**Files:**
- Modify: `APP Controlesp/wear/src/main/java/com/example/control_esp/wear/MainActivity.kt:695-770`

- [ ] **Step 1: Mở rộng `SettingsScreen` nhận thêm state và callback**
  - Thêm tham số: `gestureMode: Int`, `gestureSensitivity: Int`, `onGestureModeToggle: (Int) -> Unit`, `onGestureSensitivityToggle: (Int) -> Unit`.
  - Cập nhật hiển thị Item Cài đặt:
    + Mục "Chế độ búng tay": Chạm để luân chuyển giữa "Chỉ Đề khi bật" và "Bật & Đề".
    + Mục "Độ nhạy": Chạm để luân chuyển giữa "Vừa", "Cao", "Thấp".
  - Mỗi khi thay đổi trên đồng hồ:
    + Lưu vào `WEAR_PREF`.
    + Gửi bản tin `/settings_watch` về điện thoại qua `sendCommandToPhone("$isGestureEnabled|$gestureActionMode|$gestureSensitivity", "/settings_watch")`.

---

### Task 3: Quản lý Đồng bộ 2 Chiều trên Mobile App (`WatchSyncHelper` & `WearMessageListenerService`)

**Files:**
- Modify: `APP Controlesp/app/src/main/java/com/example/control_esp/WatchSyncHelper.kt`
- Modify: `APP Controlesp/app/src/main/java/com/example/control_esp/WearMessageListenerService.kt`

- [ ] **Step 1: Bổ sung hàm đồng bộ cấu hình trong `WatchSyncHelper.kt`**
  - Hàm `syncWearSettingsToWatch(context: Context)`:
    + Đọc `isGestureEnabled`, `gestureMode`, `gestureSensitivity` từ SharedPreferences `BT_PREF` (hoặc `WEAR_PREF`).
    + Gửi payload `"$isGestureEnabled|$gestureMode|$gestureSensitivity"` qua path `/settings_sync` đến tất cả node đồng hồ kết nối.
  - Thêm callback `onWearSettingsChangedFromWatch: ((Boolean, Int, Int) -> Unit)?`.

- [ ] **Step 2: Cập nhật `WearMessageListenerService.kt`**
  - Trong `when (messageEvent.path)`:
    + `"/ping"`: Gọi thêm `WatchSyncHelper.syncWearSettingsToWatch(this)` bên cạnh `syncCurrentStateToWatch(this)`.
    + `"/settings_watch"`: Parse payload, lưu vào SharedPreferences trên điện thoại, gọi `WatchSyncHelper.onWearSettingsChangedFromWatch?.invoke(enabled, mode, sens)`.

---

### Task 4: Thiết kế Giao diện Dialog Cài đặt Cử chỉ Wear OS trên Điện thoại

**Files:**
- Create: `APP Controlesp/app/src/main/res/layout/dialog_wear_gesture_settings.xml`
- Modify: `APP Controlesp/app/src/main/res/layout/activity_main.xml:1745-1760`

- [ ] **Step 1: Tạo layout `dialog_wear_gesture_settings.xml`**
  - Header sang trọng vàng gold: "CÀI ĐẶT CỬ CHỈ ĐỒNG HỒ".
  - SwitchCompat: "Kích hoạt cử chỉ búng tay (Wear OS)".
  - RadioGroup chọn Chế độ:
    + Radio 1: "Chỉ Đề nổ khi xe đã BẬT điện (Khuyên dùng - An toàn)".
    + Radio 2: "Bật điện xe (khi tắt) & Đề nổ (khi bật)".
  - RadioGroup chọn Độ nhạy:
    + Thấp (Chống nhầm cao - $26\text{ m/s}^2$).
    + Vừa (Mặc định khuyên dùng - $21\text{ m/s}^2$).
    + Cao (Nhạy - $16\text{ m/s}^2$).
  - Nút "Lưu & Đồng bộ ngay" (`btnSaveWearSettings`).

- [ ] **Step 2: Thêm nút "Cài đặt cử chỉ" vào thẻ Wear OS trong `activity_main.xml`**
  - Thêm `btnWatchGestureSettings` bên cạnh `btnSyncWatchNow` trong Card Trạng thái Đồng hồ.

---

### Task 5: Tích hợp Logic Xử lý Cài đặt trong `MainActivity.kt` của Mobile App

**Files:**
- Modify: `APP Controlesp/app/src/main/java/com/example/control_esp/MainActivity.kt`

- [ ] **Step 1: Bắt sự kiện bấm `btnWatchGestureSettings`**
  - Khởi tạo và hiển thị Dialog `dialog_wear_gesture_settings.xml`.
  - Load cấu hình hiện tại từ SharedPreferences.
  - Bắt sự kiện khi bấm nút "Lưu & Đồng bộ":
    + Lưu SharedPreferences.
    + Gọi `WatchSyncHelper.syncWearSettingsToWatch(this)`.
    + Toast thông báo thành công.

- [ ] **Step 2: Đăng ký callback nhận thay đổi từ đồng hồ**
  - Gán `WatchSyncHelper.onWearSettingsChangedFromWatch`: nếu dialog đang mở hoặc giao diện cần refresh, cập nhật ngay lập tức.

---

### Task 6: Kiểm tra Biên dịch và Nghiệm thu Hệ thống

- [ ] **Step 1: Build module `wear` và module `app` bằng Gradle**
  - Lệnh: Propose Gradle assembleDebug để kiểm tra biên dịch không có lỗi cú pháp hoặc thiếu tài nguyên.
- [ ] **Step 2: Rà soát lại mã nguồn và tài liệu hệ thống**
  - Đảm bảo tuân thủ nguyên tắc Karpathy: can thiệp phẫu thuật, không sinh mã rác, dọn dẹp sạch sẽ các import thừa.
