# BÁO CÁO ĐÁNH GIÁ KỸ THUẬT & PHƯƠNG ÁN TÍCH HỢP CẢM BIẾN VÂN TAY BIOSEC TM1026M (TZM1026_V1.0) THAY THẾ R503 CHO DỰ ÁN TYSMARTKEY (ESP32-C3)

---

## 1. TỔNG QUAN PHẦN CỨNG & XUẤT XỨ LINH KIỆN

* **Mã module thương mại:** **BIOSEC TM1026M** (hoặc series TS1071M / TZM1026).
* **Silkscreen trên bo mạch:** `TZM1026_V1.0` (mặt sau có ký hiệu lô sản xuất `YH`, `TZ1824`).
* **Hãng sản xuất:** **Shanghai Tuozheng Information Technology Co., Ltd.** (*上海图正信息科技股份有限公司* - viết tắt **BIOSEC**).
* **Cảm biến vân tay:** Cảm biến điện dung bán dẫn phẳng **TS1026M** (*Semiconductor Capacitive Fingerprint Sensor*).
* **Chip vi điều khiển / DSP:** **BIOSEC TA0702** (hoặc TA0982). Chip QFN tích hợp sẵn ROM thuật toán trích xuất đặc trưng sinh trắc học, mã hóa chống giả mạo và bộ nhớ lưu trữ vân tay offline trên chip.
* **Nguồn gốc thị trường (Lô hàng 32.000đ Shopee):** Module rã xác / tháo máy từ các dòng **khóa cửa điện tử thông minh cao cấp (Smart Door Lock)** nội địa Trung Quốc. Chất lượng phần cứng chuẩn công nghiệp, bề mặt cảm ứng chống xước và độ nhạy rất cao.

---

## 2. BẢNG SO SÁNH KỸ THUẬT: BIOSEC TM1026M VS. R503

| Tiêu chí | GROW R503 (Hiện tại) | BIOSEC TM1026M / TZM1026 (Thay thế) | Đánh giá & Lưu ý khi chuyển đổi |
| :--- | :--- | :--- | :--- |
| **Công nghệ cảm biến** | Điện dung tròn (Capacitive) | Bán dẫn điện dung phẳng (Semiconductor Capacitive) | TZM1026 nhận diện góc nghiêng & tay ướt rất nhạy |
| **Độ phân giải** | 508 DPI | **508 DPI (Ma trận 160 × 160 pixels)** | Tương đương độ chi tiết |
| **Kích thước & Hình dáng** | Tròn $\varnothing$ 28mm (khoét lỗ tròn 25mm) | **Chữ nhật 33mm × 20mm × 6.6mm** | Cần thiết kế lại pat gá/khung viền xe máy |
| **Hiệu ứng LED** | Vòng tròn LED RGB đổi màu (Xanh/Đỏ/Tím) | **Không có vòng LED tròn đổi màu** | Cần tận dụng còi/đèn xe để phản hồi trạng thái |
| **Điện áp hoạt động** | 3.3V DC | **3.3V DC** (V_TOUCH 2.5–5.5V, VCC 2.7–3.6V) | **100% tương thích điện áp ESP32-C3** |
| **Dòng tĩnh chờ ngón tay** | ~15–20 µA | **5 – 10 µA (cực kỳ tiết kiệm ắc quy xe)** | **Vượt trội cho Smartkey xe máy** |
| **Dòng hoạt động quét** | ~35–45 mA | **20 – 45 mA** | ESP32-C3 LDO cấp nguồn trực tiếp an toàn |
| **Cơ chế chân Wakeup (Chạm tay)** | **Active LOW** (Chạm kéo về 0V/LOW) | **Active HIGH** (Chạm đẩy lên **3.3V/HIGH**) | **Cực kỳ quan trọng: Đảo cấu hình ngắt** |
| **Giao tiếp & Baudrate** | UART 57600 bps | **UART 115200 bps** (hoặc 57600 tùy ROM) | Tốc độ truyền nhanh hơn gấp đôi |
| **Giao thức truyền thông** | Synochip / Grow (`0xEF 0x01...`) | **BIOSEC SFM Series (Khung 8 byte `0xF5`)** | Không dùng chung được thư viện cũ |
| **Thư viện tương thích** | `Adafruit_Fingerprint.h` | **Driver C++ độc lập `TM1026.h`** | Không cần cài thêm thư viện ngoài nặng máy |

---

## 3. SƠ ĐỒ CHÂN (PINOUT) & CÁCH ĐẤU NỐI VỚI ESP32-C3 SUPERMINI

### 3.1. Nhận diện thứ tự chân connector MX1.25-6P

```
              [ MẶT TRÊN: MẮT ĐỌC VÂN TAY ]
    ===================================================
    [ Pin 1 ] [ Pin 2 ] [ Pin 3 ] [ Pin 4 ] [ Pin 5 ] [ Pin 6 ]  (P1 Connector)
```

| Pin # | Tên chân | Điện áp | Chân ESP32-C3 SuperMini | Chức năng chi tiết |
| :---: | :--- | :---: | :---: | :--- |
| **1** | **V_TOUCH** | 3.3V | **3.3V (Nguồn chính)** | Cấp 3.3V liên tục 24/7 để nuôi mạch cảm ứng chạm (chỉ ăn 5–10 µA). |
| **2** | **TOUCH_OUT** | 3.3V | **GPIO 3** | **Ngắt đánh thức (Wakeup IRQ):** Chưa chạm = 0V, Chạm tay vào viền kim loại = **3.3V**. |
| **3** | **VCC** | 3.3V | **3.3V (hoặc qua MOSFET)** | Nguồn nuôi chip DSP TA0702 và quét ảnh (20–45 mA). |
| **4** | **TX** | 3.3V | **GPIO 0 (ESP32 RX)** | Truyền dữ liệu nối tiếp từ Module $\rightarrow$ ESP32. |
| **5** | **RX** | 3.3V | **GPIO 1 (ESP32 TX)** | Nhận lệnh điều khiển từ ESP32 $\rightarrow$ Module. |
| **6** | **GND** | 0V | **GND (Mass)** | Nối chung với Mass của xe và ESP32-C3. |

> ⚠️ **LƯU Ý CÁP NỐI THỰC TẾ:** Mạch dùng jack cắm **MX 1.25mm 6 pin**. Nếu không có đầu cáp, bạn có thể hàn trực tiếp vào hàng pad hàn P2 hoặc các test pad ở hai mép bo mạch (đo VOM để xác định GND và VCC trước khi hàn).

---

## 4. BÓC TÁCH CÁC HIỂU LẦM VỀ THƯ VIỆN & TÀI LIỆU

1. **Hiểu lầm về repo `Weixiang/TM1026-Fingerprint-Sensor-Library`:**
   * Tác giả *pqhuy87it* đã cảnh báo và kiểm chứng mã nguồn thực tế: Repo này chỉ là bản Fork đổi tên từ `Adafruit-Fingerprint-Sensor-Library`.
   * Bên trong mã nguồn vẫn giữ nguyên cấu trúc gói tin `0xEF 0x01` của Synochip. Nếu nạp repo này, module TZM1026 **sẽ không phản hồi**.
2. **Giao thức chuẩn xác của chip TA0702 (BIOSEC SFM-V1.7):**
   * Tất cả các gói tin gửi (CMD) và nhận (ACK) đều cố định đúng **8 byte**:
     `[0xF5] [TYPE] [P1] [P2] [P3] [0x00] [CHK] [0xF5]`
   * Checksum $\text{CHK} = \text{TYPE} \oplus \text{P1} \oplus \text{P2} \oplus \text{P3} \oplus 0x00$ (Phép XOR).
   * Lệnh so khớp 1:N chỉ tốn 8 byte: `F5 0C 00 00 00 00 0C F5`.
   * Lệnh đếm tổng số user: `F5 09 00 00 00 00 09 F5`.
   * Lệnh xóa toàn bộ: `F5 05 00 00 00 00 05 F5`.

---

## 5. PHƯƠNG ÁN TÍCH HỢP CỤ THỂ VÀO DỰ ÁN TYSMARTKEY

### 5.1. Quản lý nguồn Deep Sleep tiết kiệm ắc quy xe máy
* Khi xe tắt máy, ESP32-C3 đi vào Deep Sleep, ngắt phần lớn ngoại vi:
  ```cpp
  // Chân WAKEUP (GPIO 3) nhảy lên HIGH khi chạm tay vào viền kim loại:
  esp_deep_sleep_enable_gpio_wakeup(1ULL << GPIO_NUM_3, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
  ```
* Dòng tiêu thụ toàn hệ thống khi tắt khóa: **< 15 µA**, để xe nhiều tháng không lo cạn bình ắc quy.

### 5.2. Môi trường Test độc lập đã thiết lập trong PlatformIO
* Thư mục test: `firmware/esp32 c3/test/test_tzm1026/`
  * `TM1026.h`: Driver độc lập 8-byte C++ siêu nhẹ.
  * `test_tzm1026.cpp`: Chương trình chẩn đoán tự động quét baudrate, bắt tay F5/Syno, bắt ngắt chạm tay và hiển thị menu đăng ký/xóa vân tay.
* Chuyển môi trường nạp trong `platformio.ini`:
  ```ini
  [env:test-tzm1026]
  platform = espressif32
  board = esp32-c3-devkitm-1
  framework = arduino
  monitor_speed = 115200
  build_src_filter = +<../test/test_tzm1026/test_tzm1026.cpp>
  ```
* Lệnh nạp kiểm tra trực tiếp:
  ```bash
  pio run -e test-tzm1026 -t upload && pio device monitor
  ```
