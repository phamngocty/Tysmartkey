# KẾ HOẠCH THIẾT KẾ: BẢO VỆ & LƯU ẢNH VÂN TAY KẺ GIAN (INTRUDER FINGERPRINT AUDIT)

## 1. Mục Tiêu & Yêu Cầu
- **Bắt quả tang khi xe đang khóa:** Khi phát hiện chạm cảm biến R503 (`R503_WAKE_PIN == LOW`) mà kết quả tìm kiếm là vân tay lạ (`FINGERPRINT_NOTFOUND`), ESP32 kích hoạt báo động đồng thời **tự động trích xuất ngay lập tức ảnh vân tay từ lăng kính R503** (12.800 bytes).
- **Lưu trữ Flash an toàn (LittleFS):** Lưu ảnh vào bộ nhớ Flash ESP32-C3 dưới định dạng nhị phân thô (`/intruder/fp_<epoch>.raw`), tối đa lưu **5 - 10 ảnh** theo cơ chế xoay vòng **FIFO** (đầy thì xóa ảnh cũ nhất).
- **Thời gian thực siêu nhẹ (ESP32Time):**
  - Sử dụng thư viện `fbiego/ESP32Time` quản lý RTC nội của ESP32.
  - Mỗi khi App Android kết nối BLE thành công, App chỉ gửi duy nhất **1 gói tin** `SYNC_TIME|<epoch>` để cài giờ chuẩn cho ESP32 mà không tốn băng thông duy trì.
- **Trích xuất bằng App Android:**
  - App kết nối vào xe sẽ nhận được số lượng ảnh vi phạm: `FB|INTRUDER_COUNT|<n>`.
  - App có thể xem danh sách thời gian các lần vi phạm (`GET_INTRUDER_LIST`).
  - Cho phép người dùng bấm tải ảnh chi tiết (`FETCH_INTRUDER_IMG|<filename>`) để phóng to, xem trực quan và lưu vào thư viện máy tính/điện thoại.

---

## 2. Kiến Trúc Dữ Liệu & Bộ Nhớ (Flash LittleFS)

### 2.1 Cấu Trúc File & Giới Hạn Bộ Nhớ
- Cảm biến R503 tạo ra **12.800 bytes** (100 gói dữ liệu x 128B).
- 10 ảnh vân tay chiếm: $10 \times 12.8 \text{ KB} \approx 128 \text{ KB}$.
- ESP32-C3 có 4MB Flash, phân vùng LittleFS cấp phát ~300KB đến 500KB hoàn toàn dư dả và an toàn.
- Đường dẫn lưu: `/intruder/fp_<timestamp>.raw`.
  - Ví dụ: `/intruder/fp_1727823900.raw` (tương ứng 01:45:00 02/10/2026).
  - Nếu xe chưa được đồng bộ giờ (`rtc.getEpoch() < 100000`), dùng uptime: `/intruder/fp_up_<millis>.raw`.

### 2.2 Thuật Toán Quản Lý Hàng Đợi FIFO (Tối Đa 10 Ảnh)
- Khi chuẩn bị lưu ảnh mới:
  1. Quét toàn bộ danh sách file trong thư mục `/intruder/`.
  2. Nếu số file $\ge 10$: Tìm file có timestamp cũ nhất (nhỏ nhất) và gọi `LittleFS.remove(oldestFile)`.
  3. Ghi file mới vào Flash.

---

## 3. Kiến Trúc Đồng Bộ Thời Gian (ESP32Time)
- **Thư viện:** `fbiego/ESP32Time` (khai báo trong `platformio.ini`).
- **Giao thức đồng bộ một lần:**
  - Khi App Android kết nối BLE:
    - App gửi lệnh: `271000|SYNC_TIME|1727823900` (mật khẩu BLE + mã lệnh + epoch UTC/Local).
  - ESP32 xử lý:
    ```cpp
    ESP32Time rtc;
    // Khi nhận SYNC_TIME:
    unsigned long epoch = strtoul(param, NULL, 10);
    rtc.setTime(epoch);
    hasSyncedTime = true;
    ```
  - Sau đó, toàn bộ thời gian của các lần xâm nhập được lấy qua:
    `String timeStr = rtc.getTime("%H:%M:%S %d/%m/%Y");`

---

## 4. Giao Thức BLE Điều Khiển & Trích Xuất

| Hướng | Cú pháp gói tin | Chức năng |
| :--- | :--- | :--- |
| **App $\to$ ESP32** | `<PASS>\|SYNC_TIME\|<epoch>` | Đồng bộ giờ thực duy nhất 1 lần khi kết nối |
| **ESP32 $\to$ App** | `FB\|INTRUDER_COUNT\|<count>` | Báo ngay số lượng ảnh vi phạm đang lưu |
| **App $\to$ ESP32** | `<PASS>\|GET_INTRUDER_LIST` | Yêu cầu lấy danh sách các vụ xâm nhập |
| **ESP32 $\to$ App** | `FB\|INTRUDER_ITEM\|<idx>\|<filename>\|<time_str>` | Trả về từng mục sự kiện cho App |
| **App $\to$ ESP32** | `<PASS>\|FETCH_INTRUDER_IMG\|<filename>` | Yêu cầu tải nội dung ảnh vân tay của mục chọn |
| **ESP32 $\to$ App** | `FP_IMG_START\|160\|160\|<totalChunks>` | Báo bắt đầu truyền ảnh thô |
| **ESP32 $\to$ App** | `FP_IMG_CHUNK\|<seq>\|<total>\|<base64>` | Truyền từng chunk ảnh thô |
| **ESP32 $\to$ App** | `FP_IMG_END\|12800` | Kết thúc truyền ảnh |
| **App $\to$ ESP32** | `<PASS>\|CLEAR_INTRUDER_LOGS` | Xóa toàn bộ ảnh vi phạm trong Flash |

---

## 5. Quy Trình Xử Lý Tại Cảm Biến R503 (Trong Firmware)
1. Trong hàm `handleFingerprintTouch()` khi xe đang khóa (`!isUnlocked`):
   - Chụp ảnh `finger.getImage()`.
   - Chuyển đặc trưng `finger.image2Tz(1)`.
   - Tìm kiếm `finger.fingerSearch()`.
   - **Nếu trả về `FINGERPRINT_NOTFOUND` (Vân tay lạ):**
     1. Bật còi cảnh báo + nháy LED đỏ.
     2. Ghi log xâm nhập.
     3. Khởi chạy sub-routine trích xuất ảnh `r503CaptureIntruderToFlash()`.
     4. Lệnh `UpImage (0x0A)` đọc trực tiếp từ `ImageBuffer` (vẫn còn giữ ảnh ngón tay kẻ gian vừa quét) -> lưu thẳng vào `LittleFS`.
