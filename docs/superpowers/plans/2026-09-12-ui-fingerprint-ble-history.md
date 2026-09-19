# Kế hoạch Triển khai: Nâng cấp UI App, Tab Vân tay, Quét BLE & Lịch sử Mở khóa

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Khắc phục triệt để lỗi kết nối BLE, bổ sung cơ chế tự động kết nối vĩnh viễn, thiết kế lại Tab Vân tay và Tab Cài đặt hiện đại, thêm tính năng Lịch sử mở khóa xe.

**Architecture:** Nâng cấp tầng giao tiếp BLE (`BleManager.kt`) bắt đúng tên quảng bá `scanRecord` và giải phóng tài nguyên GATT; xây dựng `UnlockHistoryManager` ghi nhật ký sự kiện mở khóa vào SharedPreferences; tái thiết kế layout XML (`activity_main.xml`, dialogs, item cards) theo chuẩn Material Dark Theme cao cấp.

**Tech Stack:** Kotlin, Android SDK 34, Android BLE GATT API, Jetpack / Material Components, SharedPreferences.

---

### Task 1: Nâng cấp BleManager & AndroidManifest (Sửa lỗi quét & ngắt kết nối BLE)

**Files:**
- Modify: `APP Controlesp/app/src/main/AndroidManifest.xml`
- Modify: `APP Controlesp/app/src/main/java/com/example/control_esp/BleManager.kt`

- [ ] **Step 1: Cập nhật AndroidManifest.xml với thuộc tính neverForLocation**
Thêm `android:usesPermissionFlags="neverForLocation"` cho `android.permission.BLUETOOTH_SCAN` để trên Android 12+ không bị chặn quét nếu chưa bật GPS.

- [ ] **Step 2: Cập nhật BleManager.kt**
  - Trích xuất tên thiết bị từ `scanResult.scanRecord?.deviceName ?: device.name ?: "Thiết bị BLE"`.
  - Bổ sung `BleDeviceInfo(val device: BluetoothDevice, val name: String, val rssi: Int)`.
  - Gọi `gatt.close()` khi gặp `STATE_DISCONNECTED` để tránh lỗi 133 rò rỉ kết nối GATT.

- [ ] **Step 3: Kiểm tra biên dịch Module :app**
Chạy `./gradlew :app:assembleDebug` để đảm bảo code biên dịch sạch sẽ.

---

### Task 2: Tạo Mô-đun Quản lý Lịch sử Mở khóa (UnlockHistoryManager)

**Files:**
- Create: `APP Controlesp/app/src/main/java/com/example/control_esp/UnlockHistoryManager.kt`
- Create: `APP Controlesp/app/src/main/res/layout/item_unlock_history.xml`
- Create: `APP Controlesp/app/src/main/res/layout/dialog_unlock_history.xml`

- [ ] **Step 1: Viết lớp UnlockHistoryManager.kt**
  - `data class UnlockHistoryItem(val id: String, val timestamp: Long, val title: String, val type: String, val detail: String)`
  - Lưu và tải danh sách sự kiện từ `SharedPreferences` bằng định dạng JSON (tối đa 100 mục gần nhất).
  - Hàm `addEvent()`, `getHistory()`, `clearHistory()`.

- [ ] **Step 2: Tạo giao diện item_unlock_history.xml**
  - Card bo góc 10dp, icon trạng thái (Mở khóa thành công xanh lá, Khóa xe đỏ, App xanh dương), tiêu đề, thời gian định dạng `HH:mm - dd/MM/yyyy`, chi tiết.

- [ ] **Step 3: Tạo dialog_unlock_history.xml**
  - Tiêu đề "Nhật ký mở khóa xe", nút "Xóa lịch sử", danh sách hiển thị các mục lịch sử hoặc thông báo khi rỗng.

---

### Task 3: Tạo Giao diện Hộp thoại Quét thiết bị BLE (dialog_ble_scan)

**Files:**
- Create: `APP Controlesp/app/src/main/res/layout/item_ble_device.xml`
- Create: `APP Controlesp/app/src/main/res/layout/dialog_ble_scan.xml`

- [ ] **Step 1: Tạo layout item_ble_device.xml**
  - Card hiển thị tên thiết bị (in đậm), địa chỉ MAC, mức sóng RSSI, icon Bluetooth.

- [ ] **Step 2: Tạo layout dialog_ble_scan.xml**
  - ProgressBar xoay tròn quét BLE, nút Làm mới quét, danh sách thiết bị tìm thấy, nút Hủy.

---

### Task 4: Nâng cấp Giao diện Tab Vân tay & Tab Cài đặt trong activity_main.xml

**Files:**
- Modify: `APP Controlesp/app/src/main/res/layout/activity_main.xml`
- Modify: `APP Controlesp/app/src/main/res/layout/item_fingerprint.xml`

- [ ] **Step 1: Cải tiến item_fingerprint.xml**
  - Thiết kế thẻ vân tay phong cách Dark sang trọng, icon vân tay xanh Cyan, hiển thị tên ngón, ID, nút Đổi tên và Xóa.

- [ ] **Step 2: Cập nhật Tab Vân tay trong activity_main.xml**
  - Header hiển thị trạng thái cảm biến R503.
  - Hàng nút hành động: Nút nổi bật "Thêm vân tay mới", nút "Xem lịch sử mở khóa", nút "Làm mới danh sách".
  - Danh sách cuộn chứa các thẻ vân tay với trạng thái rỗng đẹp mắt.

- [ ] **Step 3: Cập nhật Tab Cài đặt trong activity_main.xml**
  - Card 1: Quản lý kết nối BLE (Trạng thái, Tên xe, MAC, nút "Quét tìm thiết bị ESP32", switch "Tự động kết nối", nút "Quên thiết bị").
  - Card 2: Bảo mật xe & Vân tay (Nút "Đổi mã bảo mật xe", Nút "Xóa toàn bộ vân tay").
  - Card 3: Tiện ích & Tự động đề xe (Delay slider/counter, ẩn hiện header, nav).

---

### Task 5: Tích hợp Toàn bộ Logic vào MainActivity.kt & Kiểm thử Hoàn thiện

**Files:**
- Modify: `APP Controlesp/app/src/main/java/com/example/control_esp/MainActivity.kt`

- [ ] **Step 1: Tích hợp Quét BLE & Tự động kết nối vĩnh viễn**
  - Hiển thị Dialog quét BLE trực quan khi nhấn "Quét tìm thiết bị".
  - Lưu MAC, Name, Key và cờ `AUTO_CONNECT = true`.
  - Trong `onCreate()`, nếu `isAutoConnectEnabled && activeMac != null` lập tức gọi `connectVehicle()`.

- [ ] **Step 2: Tích hợp Ghi nhận Lịch sử Mở khóa**
  - Ghi nhận sự kiện khi nhận phản hồi:
    + `FP_MATCHED` -> Ghi nhận "Mở xe bằng vân tay: [Tên ngón]".
    + `FP_MATCHED_LOCK` -> Ghi nhận "Khóa xe bằng vân tay".
    + `FP_NOT_MATCH` -> Ghi nhận "Cảnh báo: Vân tay không hợp lệ!".
    + `DA_MO_KHOA` -> Ghi nhận "Mở xe qua App / Watch".
    + `DA_KHOA_XE` -> Ghi nhận "Khóa xe qua App / Watch".
  - Gắn sự kiện mở Dialog Lịch sử mở khóa xe khi người dùng bấm nút "Xem lịch sử mở khóa".

- [ ] **Step 3: Tích hợp Đổi mã bảo mật & Xóa toàn bộ vân tay trong Tab Cài đặt**
  - Di chuyển các chức năng quản trị vào đúng vị trí mới.

- [ ] **Step 4: Build và Kiểm tra toàn diện**
  - Chạy `./gradlew :app:assembleDebug` kiểm tra lỗi cú pháp và tương thích tài nguyên.
