# Thiết kế Chi tiết: Nâng cấp UI App, Tab Vân tay, Quét BLE & Lịch sử Mở khóa

> **Dự án**: Tsmartkey - Khóa xe thông minh ESP32-C3 & Ứng dụng Android  
> **Tài liệu**: Đặc tả thiết kế tính năng UI & BLE (Design Spec)  
> **Ngày tạo**: 2026-09-12  

---

## 1. Mục tiêu & Yêu cầu tổng quan

1. **Khắc phục lỗi kết nối BLE giữa App Android và ESP32-C3**:
   - Sửa lỗi quét BLE không nhận diện được thiết bị do thuộc tính `device.name` bị `null` ở lần quét đầu.
   - Bổ sung cấu hình quyền `BLUETOOTH_SCAN` với cờ `neverForLocation` trên Android 12+ và dọn dẹp hàng đợi GATT (`gatt.close()`).
2. **Cơ chế Tự động kết nối vĩnh viễn (Auto-Reconnect)**:
   - Quét và chọn xe lần đầu trong Tab Cài đặt.
   - Lưu cấu hình vào `SharedPreferences`, các lần mở App sau hoặc khi xe vào vùng phủ sóng sẽ tự động kết nối ngầm mà không cần thao tác lại.
3. **Hoàn thiện Tab Vân tay (Fingerprint Management)**:
   - Giao diện Dark Theme hiện đại, thẻ vân tay hiển thị trực quan (Tên ngón, ID, nút Đổi tên, nút Xóa).
   - Nút "Thêm vân tay mới" với quy trình hướng dẫn 2 bước trực quan tương ứng tín hiệu từ R503.
   - Nút "Làm mới" đồng bộ danh sách.
   - Nút "Xem lịch sử mở khóa" mở giao diện danh sách nhật ký các lần mở/khóa xe.
4. **Hoàn thiện Tab Cài đặt (Settings)**:
   - Khu vực Quét & Kết nối Bluetooth BLE: hiển thị trạng thái xe, nút quét tìm thiết bị với Dialog/BottomSheet hiển thị thiết bị phát hiện thời gian thực.
   - Khu vực Quản lý bảo mật: Đổi mã Secret Key, Xóa toàn bộ vân tay trên xe (có hộp thoại xác nhận cảnh báo).
   - Cài đặt thời gian tự động đề xe sau khi bật nguồn.
5. **Tính năng Lịch sử mở khóa xe (Unlock History)**:
   - Lưu trữ cục bộ danh sách các sự kiện mở/khóa xe (thời gian, hình thức mở: ngón vân tay nào, App điện thoại, Đồng hồ Wear OS, cảnh báo quẹt sai).
   - Giao diện Timeline hiển thị chi tiết và nút Xóa lịch sử khi cần.

---

## 2. Kiến trúc & Thiết kế Kỹ thuật

### 2.1 Cải tiến Lớp BLE Client (`BleManager.kt`)
- **Quét BLE**:
  - Đọc tên thiết bị từ `ScanResult.scanRecord?.deviceName ?: device.name ?: "Thiết bị BLE"`.
  - Hỗ trợ lọc theo `SERVICE_UUID` hoặc tiền tố tên `XE_tsmart`.
- **Quản lý kết nối GATT**:
  - Khi trạng thái chuyển sang `STATE_DISCONNECTED`, lập tức gọi `gatt.close()` và giải phóng bộ nhớ để tránh lỗi Android GATT 133.
  - Tự động thử kết nối lại với độ trễ (exponential backoff) khi bị mất kết nối đột ngột.

### 2.2 Quản lý Lịch sử Mở khóa (`UnlockHistoryManager.kt`)
- **Cấu trúc dữ liệu**:
  ```kotlin
  data class UnlockHistoryItem(
      val id: String = UUID.randomUUID().toString(),
      val timestamp: Long,
      val title: String,       // "Mở khóa bằng vân tay: Ngón cái phải", "Mở khóa qua App", "Khóa xe"
      val type: String,        // FINGERPRINT_SUCCESS, FINGERPRINT_FAIL, APP_COMMAND, WATCH_COMMAND
      val detail: String = ""  // Ghi chú thêm (VD: "ID ngón: 1")
  )
  ```
- **Lưu trữ**: Dùng `SharedPreferences` lưu JSON danh sách (giới hạn 100 sự kiện gần nhất) để đảm bảo tốc độ và không phụ thuộc thư viện SQLite cồng kềnh.
- **Kích hoạt ghi nhận**: Khi nhận các sự kiện phản hồi từ ESP32:
  - `FP_MATCHED|<ID>|<NAME>` -> Ghi nhận mở bằng vân tay.
  - `FP_MATCHED_LOCK` -> Ghi nhận khóa xe bằng vân tay.
  - `FP_NOT_MATCH` -> Ghi nhận cảnh báo quét sai.
  - `DA_MO_KHOA` / `DA_KHOA_XE` qua lệnh App/Watch -> Ghi nhận mở/khóa qua điều khiển từ xa.

### 2.3 Giao diện Người dùng (UI / UX Layouts)

#### A. Tab Vân tay (`tab_fingerprint` trong `activity_main.xml`)
- Header Card: Trạng thái cảm biến R503 (Sẵn sàng / Chưa kết nối).
- Nút Action kép:
  - Nút chính: **"Thêm vân tay mới"** (nổi bật, icon vân tay neon).
  - Nút phụ: **"Xem lịch sử mở khóa"** (icon đồng hồ/nhật ký).
- Nút icon góc: **"Làm mới danh sách"**.
- Danh sách thẻ vân tay: Bo tròn góc 12dp, nền `card_bg`, icon vân tay xanh Cyan, tên ngón rõ ràng, nút Sửa & Xóa gọn gàng.

#### B. BottomSheet / Dialog Lịch sử Mở khóa (`dialog_unlock_history.xml`)
- Danh sách dòng thời gian (RecyclerView hoặc ListView lồng CardView).
- Icon phân loại: Xanh lá (Mở thành công), Đỏ (Khóa xe / Quẹt sai), Xanh dương (Điều khiển qua App).
- Nút "Xóa lịch sử" ở góc trên.

#### C. Tab Cài đặt (`tab_settings` trong `activity_main.xml`)
- Card 1 - **Kết nối Xe & BLE**:
  - Thông tin xe kết nối hiện tại (Tên, MAC, trạng thái).
  - Nút **"Quét tìm thiết bị ESP32"** -> Mở Dialog hiển thị danh sách BLE với radar quét.
  - Switch: **"Tự động kết nối khi mở App"** (mặc định BẬT khi đã chọn xe).
  - Nút **"Quên thiết bị / Đổi xe khác"**.
- Card 2 - **Bảo mật & Vân tay**:
  - Mục **"Đổi mã bảo mật xe"** (Secret Key).
  - Mục **"Xóa tất cả vân tay trên xe"** (Nút màu đỏ với cảnh báo).
- Card 3 - **Tiện ích xe**:
  - Tự động đề xe sau khi mở khóa (Bật/tắt + thời gian delay 1s - 10s).
  - Tùy chọn ẩn/hiện Header thông tin và Thanh điều hướng.

---

## 3. Kế hoạch Kiểm thử & Xác minh (Verification Plan)
1. **Kiểm tra mã nguồn & Build**: Chạy Gradle assembleDebug trên Android module `:app`.
2. **Kiểm tra kết nối BLE**:
   - Quét thiết bị hiển thị đúng `XE_tsmart_BLE`.
   - Kết nối và lưu MAC, tắt app mở lại tự động kết nối thành công.
3. **Kiểm tra Tab Vân tay**:
   - Thêm vân tay quy trình 2 bước.
   - Sửa tên vân tay, xóa vân tay.
4. **Kiểm tra Lịch sử mở khóa**:
   - Quẹt vân tay hoặc mở qua app -> kiểm tra mục Lịch sử mở khóa có hiển thị đúng thời gian và loại sự kiện.
