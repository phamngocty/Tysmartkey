# KẾ HOẠCH TRIỂN KHAI: BẢO VỆ & LƯU ẢNH VÂN TAY KẺ GIAN (INTRUDER FINGERPRINT CAPTURE)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Xây dựng tính năng an ninh tự động bắt quả tang và lưu ảnh vân tay kẻ gian (12.800 bytes) vào bộ nhớ Flash ESP32 (LittleFS) kèm thời gian thực (thư viện `fbiego/ESP32Time`, đồng bộ 1 lần duy nhất qua BLE), cho phép App Android trích xuất lại ảnh vi phạm để kiểm tra.

**Architecture:** Sử dụng `LittleFS` để lưu trữ tối đa 10 ảnh vân tay nhị phân thô (`/intruder/fp_<epoch>.raw`) theo cơ chế FIFO xoay vòng. Đồng bộ mốc thời gian bằng `ESP32Time` một lần duy nhất qua BLE khi kết nối. Tận dụng `ImageBuffer` của R503 ngay sau khi `fingerSearch()` trả về `FINGERPRINT_NOTFOUND` để trích xuất ảnh `UpImage` lưu vào Flash mà không cần quét lại.

**Tech Stack:** C++ / Arduino / PlatformIO, ESP32-C3, `fbiego/ESP32Time@^2.0.6`, `LittleFS`, Adafruit Fingerprint Sensor Library.

---

### Task 1: Cấu hình thư viện `ESP32Time` và LittleFS trong `platformio.ini`

**Files:**
- Modify: `firmware/esp32 c3/platformio.ini`

- [x] **Step 1: Thêm dependency `fbiego/ESP32Time@^2.0.6` vào `lib_deps` của môi trường `esp32-c3`**
- [x] **Step 2: Chạy `pio pkg install` kiểm tra thư viện được tải thành công**

---

### Task 2: Module lưu trữ ảnh vân tay lạ LittleFS & Hàng đợi FIFO

**Files:**
- Modify: `firmware/esp32 c3/src/main.cpp`

- [x] **Step 1: Khởi tạo LittleFS trong `setup()`**
- [x] **Step 2: Viết hàm quét đếm danh sách file `/intruder/` và xóa file cũ nhất khi số lượng $\ge 10$ (FIFO)**
- [x] **Step 3: Viết hàm `saveIntruderFingerprintToFlash(const uint8_t* rawData, size_t len, uint32_t timestamp)`**
- [x] **Step 4: Viết hàm `getIntruderLogCount()` và `getIntruderListString()`**

---

### Task 3: Tích hợp `ESP32Time` và Giao thức Đồng Bộ Giờ 1 Lần Qua BLE

**Files:**
- Modify: `firmware/esp32 c3/src/main.cpp`

- [x] **Step 1: Khai báo đối tượng toàn cục `ESP32Time rtc;` và biến cờ `bool hasSyncedRtc = false;`**
- [x] **Step 2: Xử lý lệnh BLE `SYNC_TIME|<epoch>` trong `handleBleCommand()`**
- [x] **Step 3: Gửi số lượng vụ xâm nhập (`FB|INTRUDER_COUNT|<n>`) ngay khi kết nối BLE**

---

### Task 4: Hook Bắt Quả Tang Vân Tay Lạ Khi Xe Đang Khóa

**Files:**
- Modify: `firmware/esp32 c3/src/main.cpp:handleFingerprintTouch()`

- [x] **Step 1: Định vị nhánh `FINGERPRINT_NOTFOUND` khi `!isUnlocked`**
- [x] **Step 2: Bật còi cảnh báo + đèn đỏ, đồng thời kích hoạt trích xuất `UpImage (0x0A)` từ `ImageBuffer` R503**
- [x] **Step 3: Ghi dữ liệu 12.800 bytes vừa trích xuất vào LittleFS với mốc thời gian hiện tại**

---

### Task 5: Giao thức BLE Trích Xuất Ảnh Xâm Nhập Lên App

**Files:**
- Modify: `firmware/esp32 c3/src/main.cpp:handleBleCommand()`

- [x] **Step 1: Bổ sung xử lý lệnh `GET_INTRUDER_LIST` -> trả về danh sách các file kèm ngày giờ**
- [x] **Step 2: Bổ sung xử lý lệnh `FETCH_INTRUDER_IMG|<filename>` -> đọc file từ Flash và stream qua BLE theo indexed chunks**
- [x] **Step 3: Bổ sung xử lý lệnh `CLEAR_INTRUDER_LOGS` -> dọn sạch toàn bộ file trong thư mục `/intruder/`**

---

### Task 6: Biên Dịch Firmware & Kiểm Thử Nghiệm Thu

**Files:**
- Test: Chạy build PlatformIO `env:esp32-c3`

- [x] **Step 1: Biên dịch firmware `platformio run -e esp32-c3` xác nhận 0 lỗi**
- [x] **Step 2: Kiểm tra dung lượng RAM/Flash sau khi tích hợp LittleFS và ESP32Time**
