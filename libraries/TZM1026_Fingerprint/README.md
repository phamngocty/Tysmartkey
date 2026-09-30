# Thư Viện TZM1026_Fingerprint (Arduino & PlatformIO Driver)

Thư viện mã nguồn mở C++ hiệu năng cao, độc lập, tối ưu bộ nhớ dùng cho cảm biến vân tay bán dẫn điện dung phẳng **BIOSEC TM1026M** (Silkscreen `TZM1026_V1.0`, chip điều khiển `BIOSEC TA0702 / TA0982`, mắt đọc cảm biến `TS1026M` 160x160 pixels 508 DPI).

Hỗ trợ đầy đủ tập lệnh giao thức **BIOSEC SFM-V1.7** (khung 8-byte Header `0xF5`, Tail `0xF5`, mã kiểm tra Checksum XOR).

---

## 🌟 TÍNH NĂNG NỔI BẬT

1. **So khớp siêu tốc (Fast 1:N & 1:1 Matching)**: Quét nhận diện trong < 0.3s.
2. **Quy trình đăng ký 3C3R & 5C5R NCNR**:
   - `enroll3C3R`: Đăng ký chuẩn 3 lần chạm (Cơ bản).
   - `enroll5C5R`: Đăng ký nâng cao 5 lần chạm đa góc độ (tâm, mép trái, mép phải, chóp ngón, đốt dưới) tạo composite template bao phủ toàn diện, cực nhạy mọi góc đặt.
3. **Quản trị phân quyền người dùng (User Roles)**:
   - `Role 1`: Chủ xe (Admin - Toàn quyền)
   - `Role 2`: Người nhà (Normal User)
   - `Role 3`: Khách / Người mượn xe (Guest - Có thể lập lịch tự hủy)
4. **Đồng bộ danh bạ lên Mobile App (CMD 0x2B)**:
   - Hàm `getUsersJson()` trích xuất toàn bộ danh bạ thành mảng JSON sẵn sàng đẩy lên Mobile App qua Bluetooth BLE hoặc WiFi/HTTP.
5. **Tự động tìm User ID trống nhỏ nhất (CMD 0x0D)**:
   - Hàm `getNextFreeId()` cho phép cảm biến tự động quét bộ nhớ từ 1 đến 100 và trả về ID trống, không cần quản lý biến đếm trên vi điều khiển.
6. **Trích xuất & Đóng gói ảnh vân tay (CMD 0x24)**:
   - Đọc mảng điểm ảnh thô Grayscale 8-bit (160x160 = 25,600 bytes).
   - Tự động đóng gói thành file ảnh chuẩn Windows Bitmap (`.BMP` 1078-byte Header) xem được trên mọi máy tính và thiết bị di động.
7. **Lưu vĩnh viễn cấu hình Baudrate vào EEPROM (CMD 0x21)**:
   - Đổi tốc độ UART (9600, 19200, 38400, 57600, 115200) và lưu vào bộ nhớ không bay hơi của chip.
8. **Kích hoạt AI tự học thích ứng (Self-Learning Adaptation - CMD 0x3F)**:
   - Tự động học và mở rộng biên vân tay sau mỗi lần quét đúng, càng dùng lâu càng nhạy.
9. **Cơ chế ngắt khẩn cấp (Emergency Break - CMD 0xFE)**:
   - Hủy tức thì các tác vụ treo/chờ khi người dùng không đặt ngón tay hoặc bị timeout.

---

## 🔌 SƠ ĐỒ CHÂN KẾT NỐI (PINOUT)

Jack cắm cảm biến là chuẩn **MX1.25-6P** (6 chân):

| Chân (Pin) | Ký hiệu | Màu dây gợi ý | Nối tới ESP32-C3 / MCU | Chức năng kỹ thuật |
| :---: | :---: | :---: | :---: | :--- |
| **1** | **V_TOUCH** | Đỏ / Nâu | **3.3V (Liên tục 24/7)** | Nguồn nuôi vi mạch cảm ứng điện dung tĩnh (~5-10 µA). |
| **2** | **TOUCH_OUT** | Vàng | **GPIO 3** (hoặc GPIO ngắt) | Tín hiệu ngắt báo chạm tay: **ACTIVE HIGH** (Chưa chạm = 0V, Chạm = 3.3V). |
| **3** | **VCC** | Đỏ | **3.3V** (hoặc qua MOSFET) | Nguồn chính nuôi chip TA0702 (~30-45 mA khi hoạt động). |
| **4** | **TXD** | Xanh lá | **GPIO 0 (RX của MCU)** | Chân UART TX truyền dữ liệu từ cảm biến về vi điều khiển. |
| **5** | **RXD** | Xanh dương | **GPIO 1 (TX của MCU)** | Chân UART RX nhận lệnh điều khiển từ vi điều khiển. |
| **6** | **GND** | Đen | **GND chung** | Mass nguồn chung của toàn hệ thống. |

> ⚠️ **LƯU Ý CỰC KỲ QUAN TRỌNG VỀ ĐIỆN ÁP & CỰC TÍNH**:
> - Điện áp hoạt động: **Chuẩn 3.3V DC** (Tuyệt đối không cấp 5V trực tiếp vào chân UART).
> - Chân WAKEUP `TOUCH_OUT` của TZM1026 là **ACTIVE HIGH (Chạm = 3.3V)**, ngược lại hoàn toàn với cảm biến quang học R503 (Active LOW).

---

## 📦 CÁCH CÀI ĐẶT THƯ VIỆN

### Cách 1: Sử dụng trong PlatformIO
Sao chép thư mục `TZM1026_Fingerprint` vào thư mục `lib/` trong project PlatformIO của bạn:
```
my_project/
├── lib/
│   └── TZM1026_Fingerprint/
│       ├── library.json
│       ├── src/
│       │   ├── TZM1026.h
│       │   └── TZM1026.cpp
│       └── examples/
├── src/
│   └── main.cpp
└── platformio.ini
```

### Cách 2: Sử dụng trong Arduino IDE
1. Nén thư mục `TZM1026_Fingerprint` thành file `TZM1026_Fingerprint.zip`.
2. Mở Arduino IDE $\rightarrow$ **Sketch** $\rightarrow$ **Include Library** $\rightarrow$ **Add .ZIP Library...**
3. Chọn file zip vừa nén.
4. Mở menu **File** $\rightarrow$ **Examples** $\rightarrow$ **TZM1026_Fingerprint** để chạy các ví dụ mẫu.

---

## 🚀 HƯỚNG DẪN SỬ DỤNG NHANH (QUICK START)

```cpp
#include <Arduino.h>
#include <TZM1026.h>

HardwareSerial tzmSerial(1);
TZM1026 fingerprint(&tzmSerial);

void setup() {
    Serial.begin(115200);
    tzmSerial.begin(115200, SERIAL_8N1, 0 /*RX*/, 1 /*TX*/);
    pinMode(3, INPUT_PULLDOWN); // Chân ngắt TOUCH_OUT

    if (fingerprint.handshake()) {
        Serial.println("Cảm biến TZM1026 sẵn sàng!");
        fingerprint.enableSelfLearning(); // Bật AI tự học thích ứng
    }
}

void loop() {
    // Chạm tay đánh thức
    if (digitalRead(3) == HIGH) {
        uint16_t id = 0;
        uint8_t role = 0;
        if (fingerprint.verify(id, role)) {
            Serial.printf("Mở khóa thành công cho User #%d (Role %d)!\n", id, role);
        } else {
            Serial.println("Từ chối truy cập!");
        }
        while (digitalRead(3) == HIGH) delay(50); // Chờ nhấc tay
    }
}
```

---

## 📚 TÀI LIỆU API CHI TIẾT

| Tên hàm (Method) | Ý nghĩa chức năng |
| :--- | :--- |
| `bool handshake(timeoutMs)` | Kiểm tra kết nối và bắt tay với chip cảm biến |
| `int16_t getUserCount()` | Đọc tổng số mẫu vân tay đang lưu (0-100) |
| `int16_t getNextFreeId()` | Tự động quét và trả về User ID trống nhỏ nhất |
| `bool verify(id, role, timeoutMs)` | So khớp 1:N với toàn bộ cơ sở dữ liệu |
| `bool verify1to1(id, timeoutMs)` | So khớp 1:1 với đích danh User ID |
| `bool enroll3C3R(id, role, wakePin, cb)` | Đăng ký vân tay chuẩn 3 lần chạm |
| `bool enroll5C5R(id, role, wakePin, cb)` | Đăng ký vân tay nâng cao 5 lần chạm đa góc độ (cực nhạy) |
| `bool deleteUser(id)` | Xóa đích danh 1 User ID |
| `bool clearAll()` | Xóa sạch toàn bộ dữ liệu trong bộ nhớ Flash |
| `bool getAllUsers(list, max, total)` | Đọc danh bạ tất cả User ID và Role |
| `size_t getUsersJson(buf, maxLen)` | Xuất danh bạ thành chuỗi JSON chuẩn Mobile App |
| `bool captureRawImage(buf, w, h, max)` | Chụp mảng điểm ảnh thô 160x160 (CMD 0x24) |
| `generateBmp(raw, w, h, out, size)` | Đóng gói điểm ảnh thô thành file `.BMP` chuẩn Windows |
| `bool setSecurityLevel(level)` | Cài đặt độ nhạy (Level 0: Nhạy nhất, 1: Cân bằng, 2: Nghiêm ngặt) |
| `bool enableSelfLearning()` | Kích hoạt AI tự động bổ sung biên vân tay sau mỗi lần chạm đúng |
| `bool setBaudrate(baudId, permanent)` | Cài đặt Baudrate (permanent = true: lưu vĩnh viễn vào EEPROM) |
| `bool getFirmwareVersion(buf, len)` | Đọc chuỗi thông số firmware và model cảm biến |
| `void sfmBreak()` | Gửi lệnh ngắt khẩn cấp để giải phóng cảm biến |

---

## 📄 GIẤY PHÉP & BẢN QUYỀN
Phát hành theo giấy phép **MIT License**. Tự do sử dụng, chỉnh sửa và tích hợp vào các sản phẩm thương mại.
