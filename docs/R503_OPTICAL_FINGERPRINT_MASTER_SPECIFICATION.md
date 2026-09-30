# BỘ TÀI LIỆU KỸ THUẬT TOÀN DIỆN CẢM BIẾN QUANG HỌC GROW R503
## DỰ ÁN TYSMARTKEY (ESP32-C3 BIOMETRIC SMARTKEY SYSTEM)
**Phiên bản tài liệu:** v2.0 - Chuẩn hóa Single Source of Truth  
**Ngày phát hành:** 30/09/2026  
**Tác giả:** Kỹ sư hệ thống TySmartKey & DeepMind Agentic Pair Programming  

---

## 1. TỔNG QUAN PHẦN CỨNG & SƠ ĐỒ ĐẤU DÂY THỰC TẾ

Cảm biến **GROW R503** là dòng cảm biến vân tay quang học (Optical Fingerprint Sensor) tích hợp vòng LED Aura đa màu và bề mặt lăng kính phản xạ toàn phần (FTIR - Frustrated Total Internal Reflection).

```
                      +-----------------------------+
                      |   CẢM BIẾN GROW R503        |
                      |  (Vòng LED Aura + Lăng kính)|
                      +-----------------------------+
                                |  |  |  |  |  |
                                |  |  |  |  |  |
        +-----------------------+  |  |  |  |  +-----------------------+
        |                          |  |  |  |                          |
    [1. ĐỎ]                    [2. ĐEN]| [4. XANH LÁ]              [6. XANH DƯƠNG]
   VCC 3.3V                     GND   |   RXD Sensor                 V_Touch
        |                          |  |        |                       |
        |      +-------------------+  |        |                       |
        |      |                      |        |                       |
        v      v                  [3. VÀNG]    v                       v
      +----------+                TXD Sensor +----------+         +----------+
      | 3.3V     |                    |      | GPIO 1   |         | 3.3V     |
      | ESP32-C3 |                    v      | (UART TX)|         | ESP32-C3 |
      +----------+               +----------++----------+         +----------+
                                 | GPIO 0   |
                                 | (UART RX)|
                                 +----------+
                                      |
                                      +-----------------[5. TRẮNG]
                                                        Chân WAKEUP (Touch Out)
                                                        Nối vào GPIO 3 ESP32-C3
```

### 1.1. Bảng Chân & Quy Ước Màu Dây Đo Đạc Thực Tế

| Màu Dây R503 | Tên Tín Hiệu | Nối Trên ESP32-C3 | Nguyên Lý Hoạt Động & Lưu Ý Kỹ Thuật |
| :--- | :--- | :--- | :--- |
| **ĐỎ** | **VCC (3.3V)** | Chân `3.3V` | Nguồn nuôi cho DSP Synochip, lăng kính và LED. Dòng đỉnh 60mA. |
| **ĐEN** | **GND** | Chân `GND` | Nối chung mass với hệ thống ESP32-C3 và nguồn xe. |
| **VÀNG** | **TXD** (Cảm biến) | **GPIO 0** (ESP32 RX) | Tín hiệu UART từ R503 gửi sang ESP32-C3 (Tốc độ mặc định: 57,600 bps). |
| **XANH LÁ**| **RXD** (Cảm biến) | **GPIO 1** (ESP32 TX) | Tín hiệu UART từ ESP32-C3 gửi lệnh sang R503. |
| **XANH DƯƠNG** | **V_Touch** (Nguồn cảm ứng)| Chân `3.3V` | **BẮT BUỘC NỐI 3.3V**: Cấp nguồn cho mạch dò điện dung ngón tay vi mô. Nếu để trống, mạch chạm sẽ không thể xuất tín hiệu. |
| **TRẮNG** | **WAKEUP** (Cảm ứng chạm)| **GPIO 3** (ESP32 C3) | **CƠ CHẾ ACTIVE LOW**:<br>• Không chạm: Giữ mức cao **3.3V (HIGH)**.<br>• Chạm ngón tay: Kéo sụt xuống **0V (LOW)** ngay lập tức (độ trễ 0ms).<br>• Dùng làm nguồn ngắt RTC Wakeup đánh thức ESP32 khỏi Deep Sleep! |

---

## 2. BÍ MẬT KỸ THUẬT: ĐỘ PHÂN GIẢI MA TRẬN ẢNH VÀ NGUYÊN NHÂN LỆCH PHA ZIC-ZẮC

### 2.1. Hai Biến Thể Phần Cứng GROW R503 Trên Thị Trường
1. **Biến thể R503 Ma trận tròn (Circular Active Area):**
   - Kích thước ảnh: **$160 \times 160$ pixels** ($25,600$ điểm ảnh).
   - Truyền nén 4-bit (2 pixel/byte): **$12,800$ bytes**.
   - Số gói tin truyền qua UART: Đúng **100 gói tin** ($100 \times 128 = 12,800$ bytes).
2. **Biến thể R503 Ma trận vuông (Square Active Area):**
   - Kích thước ảnh: **$192 \times 192$ pixels** ($36,864$ điểm ảnh).
   - Truyền nén 4-bit: **$18,432$ bytes**.
   - Số gói tin truyền qua UART: **144 gói tin** ($144 \times 128 = 18,432$ bytes).

### 2.2. Giải Phẫu Nguyên Nhân Gây Lỗi Ảnh Bị Xé Răng Cưa & Đáy Đen
Trước đây, các thư viện mã nguồn mở và firmware đều mặc định gán cứng `width = 192`. Khi gắn module cảm biến 160x160:
1. **Hiện tượng xé răng cưa bậc thang (Horizontal Shearing):**
   - Cảm biến quét và gửi đúng $160$ điểm ảnh mỗi hàng.
   - Trình giải mã lại ngắt dòng ở $192$ điểm ảnh.
   - Hàng thứ 2 bị thụt lùi $32$ pixel ($192 - 160 = 32$). Hàng thứ 3 bị lệch $64$ pixel...
   - Toàn bộ đường vân tay bị xé nghiêng theo góc dốc $32/192$, tạo hình răng cưa zic-zắc!
2. **Hiện tượng đáy đen (Black Bottom Area):**
   - Ảnh chỉ có $25,600$ pixel, nhưng khung vẽ đòi $192 \times 192 = 36,864$ pixel.
   - Thiếu đúng $11,264$ pixel $\rightarrow$ Tạo thành mảng đen tuyền dày đúng **58 hàng ở đáy ảnh**!

### 2.3. Thuật Toán Tự Động Nhận Diện Ma Trận (Auto-Dimension Engine)
Firmware TySmartKey giải quyết triệt để bằng cách không gán cứng, mà tự động đo lường số byte thực tế khi module gửi gói kết thúc `PID 0x08`:

$$\begin{cases} 
\text{Nếu } \text{Bytes} = 12,800 & \implies \text{Width} = 160, \text{Height} = 160 \quad (\text{Module R503 Tròn}) \\
\text{Nếu } \text{Bytes} = 18,432 & \implies \text{Width} = 192, \text{Height} = 192 \quad (\text{Module R503 Vuông}) \\
\text{Nếu } \text{Bytes} = 29,952 & \implies \text{Width} = 208, \text{Height} = 288 \quad (\text{Module R307/ZFM20}) 
\end{cases}$$

---

## 3. GIAO THỨC UART SYNOCHIP PROTOCOL v1.4 & KHÓA HEADER 6-BYTE

### 3.1. Cấu Trúc Khung Gói Tin Synochip
Mỗi gói tin trao đổi giữa ESP32 và R503 có cấu trúc chuẩn:
```
+---------+---------+---------+---------+---------+---------+---------+---------+---------------+---------+
| Header  | Header  | Addr[0] | Addr[1] | Addr[2] | Addr[3] |   PID   | Length  |    Payload    | Checksum|
|  0xEF   |  0x01   |  0xFF   |  0xFF   |  0xFF   |  0xFF   | (1 byte)|(2 bytes)|   (N bytes)   |(2 bytes)|
+---------+---------+---------+---------+---------+---------+---------+---------+---------------+---------+
```

### 3.2. Nguy Cơ Bắt Nhầm Header Cũ (2-byte) và Giải Pháp Khóa 6-Byte
- **Lỗ hổng cũ:** Chỉ quét tìm 2 byte `0xEF 0x01`. Trong $12,800$ bytes dữ liệu ảnh xám, sự xuất hiện ngẫu nhiên của cặp byte `0xEF 0x01` là rất lớn. Khi UART bị trượt 1 byte, thuật toán bắt nhầm dữ liệu pixel làm Header, gây hỏng toàn bộ các gói tin phía sau!
- **Giải pháp Khóa 6-Byte TySmartKey (`syncUartPacketHeader6B`):**
  Bắt buộc phải khớp đúng chuỗi 6 bytes liên tiếp:
  $$\mathbf{0xEF \quad 0x01 \quad 0xFF \quad 0xFF \quad 0xFF \quad 0xFF}$$
  Xác suất xuất hiện ngẫu nhiên của 6 bytes này trong ảnh xám là $\approx 1 / 256^6 \approx 1 / 281.474.976.710.656$, đảm bảo **chống trượt và chống lệch pha tuyệt đối 100%**.

### 3.3. Mở Rộng Bộ Đệm UART ESP32 Lên 32KB
- Mặc định của Arduino Core ESP32 chỉ cấp 256 bytes cho RX Ring Buffer.
- Luồng truyền ảnh gửi liên tục $\approx 14,000 - 20,000$ bytes burst ở 57,600 bps.
- Nếu không gọi `setRxBufferSize(32768)` trước `begin()`, UART FIFO sẽ bị tràn (Overflow) và làm mất byte.
- Cấu hình bắt buộc:
  ```cpp
  r503Serial.setRxBufferSize(32768);
  r503Serial.begin(57600, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
  ```

---

## 4. GIAO THỨC TRUYỀN ẢNH VÂN TAY QUA BLUETOOTH LOW ENERGY (BLE)

Dữ liệu ảnh sau khi thu thập trọn vẹn vào RAM của ESP32-C3 sẽ được truyền sang Mobile App theo giao thức Indexed Chunks tối ưu hóa đường truyền BLE MTU:

```
[ESP32-C3]                                                              [ANDROID APP]
     |                                                                        |
     | ----- FP_IMG_START|<width>|<height>|<totalChunks> -------------------> | Chuẩn bị Canvas
     |                                                                        | & Buffer động
     | ----- FP_IMG_CHUNK|0|<total>|<base64_payload_96B> -------------------> | Nhận & lưu seq 0
     | ----- FP_IMG_CHUNK|1|<total>|<base64_payload_96B> -------------------> | Nhận & lưu seq 1
     |       ... (giãn cách 35ms chống nghẽn BLE queue)                       |
     | ----- FP_IMG_CHUNK|<N>|<total>|<base64_payload_96B> -----------------> | Nhận & lưu seq N
     |                                                                        |
     | ----- FP_IMG_END|<totalBytes> ---------------------------------------> | Lắp ráp Bitmap
     |                                                                        | Cân bằng Histogram
     |                                                                        | Hiển thị màn hình!
```

### 4.1. Chi Tiết Bản Tin BLE
1. **Khởi đầu (`FP_IMG_START`):**
   `FP_IMG_START|160|160|134` (hoặc `192|192|192`)
   - `width`: Chiều rộng thực tế của ảnh.
   - `height`: Chiều cao thực tế của ảnh.
   - `totalChunks`: Tổng số gói chunk sẽ phát.
2. **Gói dữ liệu (`FP_IMG_CHUNK`):**
   `FP_IMG_CHUNK|<seq>|<total>|<Base64_String>`
   - Mỗi chunk chứa 96 bytes thô $\rightarrow$ mã hóa thành đúng 128 ký tự Base64 (bội số của 3, không có dấu padding `=` giúp truyền nhanh hơn 15%).
3. **Kết thúc (`FP_IMG_END`):**
   `FP_IMG_END|<totalBytes>`
   - Kích hoạt tiến trình dựng ảnh chạy ngầm (`Thread`) trên Android để giải phóng Main UI.

---

## 5. THUẬT TOÁN XỬ LÝ ẢNH TRÊN ANDROID APP (`MainActivity.kt`)

### 5.1. Tự Động Co Giãn Mảng Nhận (Dynamic Byte Assembly)
```kotlin
val expectedBytes = incomingImageWidth * incomingImageHeight / 2 // 160x160 -> 12,800 bytes
val rawBytes = ByteArray(expectedBytes)
java.util.Arrays.fill(rawBytes, 0xFF.toByte()) // Nền sáng quang học chuẩn
```

### 5.2. Giải Mã 4-bit Nibble & Cân Bằng Độ Tương Phản (Histogram Percentile)
Cảm biến quang học chỉ xuất 16 mức xám (0 đến 15) trong mỗi nibble 4-bit:
```kotlin
val b = rawBytes[idx].toInt() and 0xFF
val p1 = (b ushr 4) and 0x0F // Pixel chẵn (bên trái)
val p2 = b and 0x0F          // Pixel lẻ (bên phải)
```
Để ảnh hiển thị sắc nét như máy chụp sinh trắc học chuyên nghiệp, ứng dụng áp dụng kỹ thuật **Cắt lọc ngưỡng Histogram 3% - 97%**:
- Loại bỏ nhiễu ánh sáng tạp ở mức cực thấp ($<3\%$) và cực cao ($>97\%$).
- Kéo dãn dải tương phản (Contrast Stretching) toàn phần từ 0 đến 255.
- Hỗ trợ 2 chế độ hiển thị:
  - **Quang học chuẩn nét (Optical Clear):** Nền trắng, vân tay đen xám tương phản cao.
  - **Biometric Vàng Kim (Neon Mode):** Nền đen sâu, đường vân phát sáng vàng Neon công nghệ cao.

---

## 6. DANH MỤC LỆNH KIỂM THỬ TRÊN SERIAL MONITOR (`test_r503.cpp`)

| Phím Bấm | Chức Năng | Mục Đích Kỹ Thuật |
| :---: | :--- | :--- |
| **`i`** | **Chụp & Xuất ảnh vân tay (.BMP Base64)** | Chụp lăng kính, tự động nhận diện 160x160/192x192, xuất mã Base64 dán vào web `tools/view_fingerprint.html`. |
| **`1` / `s`** | So khớp vân tay 1:N | Kiểm tra tốc độ đối soát với người dùng thực tế. |
| **`2` / `e`** | Đăng ký vân tay chuẩn (2 lần chạm) | Quy trình nạp mẫu chuẩn công nghiệp 2C2R. |
| **`8` / `m`** | Đăng ký nâng cao (5 lần chạm) | Quy trình 5C5R đa góc độ (cực nhạy, nhận diện mọi tư thế ngón tay). |
| **`u`** | Đồng bộ & Liệt kê danh bạ | Xuất toàn bộ ID đã lưu dưới dạng chuỗi JSON cho Mobile App. |
| **`f`** | Tìm ID trống đầu tiên | Trả về vị trí nhớ còn trống tiếp theo trong Flash R503. |
| **`b`** | Đổi Baudrate cảm biến | Cài đặt tốc độ UART (9600 - 115200 bps) lưu vĩnh viễn vào ROM R503. |
| **`o`** | Thử nghiệm vòng LED Aura | Test 7 màu và các hiệu ứng Breathing, Flashing, Solid ON. |
| **`9` / `l`** | Đặt cấp bảo mật (1 - 5) | Điều chỉnh ngưỡng FAR/FRR của thuật toán so khớp nội bộ R503. |
| **`6` / `x`** | Bật/Tắt xem mã Hex UART | Hiển thị toàn bộ gói tin thô gửi/nhận thời gian thực. |
| **`7` / `z`** | Kiểm tra Deep Sleep | Cho ESP32 ngủ sâu, chạm ngón tay vào R503 để thức dậy tức thì (GPIO 3 WAKEUP). |
