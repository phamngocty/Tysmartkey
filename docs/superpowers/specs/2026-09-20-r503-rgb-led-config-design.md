# Đặc Tả Thiết Kế: Cấu Hình Đèn Vòng Màu RGB R503 & Bánh Xe Màu Tương Tác Android

**Ngày lập**: 2026-09-20  
**Trạng thái**: Đã phê duyệt ý tưởng (Design Approved)  
**Tác giả**: Antigravity & User  

---

## 1. Tổng Quan & Mục Tiêu

Tính năng cung cấp giải pháp toàn diện cho phép người dùng tùy biến màu sắc và hiệu ứng ánh sáng của vòng đèn LED hào quang (Aura LED) trên cảm biến vân tay R503/R503-M22:
- **Tương tác trực quan**: Tích hợp vòng tròn bánh xe màu RGB 360° tương tác chạm/vuốt mượt mà trên ứng dụng Android.
- **Tùy biến chuyên sâu 4 sự kiện**:
  1. `EVT_UNLOCKED`: Khi xe đang mở khóa (Vận hành trên đường).
  2. `EVT_LOCKED`: Khi xe đang khóa (Đỗ xe, bảo vệ bình ắc quy).
  3. `EVT_SUCCESS`: Khi mở khóa hoặc quét vân tay thành công.
  4. `EVT_ERROR`: Khi quét vân tay sai hoặc phát hiện xâm nhập trái phép.
- **Bộ phong cách chọn nhanh (Style Presets)**: Cung cấp các hồ sơ cấu hình sẵn (Cyberpunk, Ocean Neon, Sport Racing, Solar Gold, Stealth Eco).
- **Thử nghiệm tức thời (Live Test)**: Chạm màu trên App, đèn R503 lập tức phát sáng thực tế để kiểm tra trước khi lưu.
- **Lưu trữ an toàn**: Toàn bộ cấu hình được ghi vào bộ nhớ NVS Flash của ESP32, đảm bảo giữ nguyên trạng thái khi tắt/bật nguồn xe.

---

## 2. Đặc Tính Kỹ Thuật Phần Cứng Cảm Biến R503 (Opcode 0x35)

Theo tài liệu kỹ thuật Hangzhou Grow R503 User Manual V1.4.1 (Mục *Aura control AuraLedConfig 0x35*), vi điều khiển bên trong cảm biến điều khiển cụm LED RGB phần cứng qua các tham số:

### 2.1. Mã Màu Phần Cứng (ColorIndex)
| ColorIndex | Tên Màu | Mã Hex Hiển Thị Trên App | Trạng Thái Kênh RGB |
|:---:|:---:|:---:|:---:|
| `0x01` | 🔴 **Đỏ (Red)** | `#FF2D55` | R=1, G=0, B=0 |
| `0x02` | 🔵 **Xanh Dương (Blue)** | `#007AFF` | R=0, G=0, B=1 |
| `0x03` | 🟣 **Tím (Purple)** | `#AF52DE` | R=1, G=0, B=1 |
| `0x04` | 🟢 **Xanh Lá (Green)** | `#34C759` | R=0, G=1, B=0 |
| `0x05` | 🟡 **Vàng (Yellow)** | `#FFCC00` | R=1, G=1, B=0 |
| `0x06` | 🐬 **Xanh Ngọc / Lơ (Cyan)** | `#00F5D4` | R=0, G=1, B=1 |
| `0x07` | ⚪ **Trắng (White)** | `#FFFFFF` | R=1, G=1, B=1 |

### 2.2. Kiểu Hiệu Ứng (Control Code / Mode)
- `0x01` (`LED_MODE_BREATHING`): Thở êm dịu (chu kỳ mượt mà).
- `0x02` (`LED_MODE_FLASHING`): Nhấp nháy theo tần số.
- `0x03` (`LED_MODE_ON`): Bật sáng liên tục.
- `0x04` (`LED_MODE_OFF`): Tắt hoàn toàn (tiết kiệm điện năng).
- `0x05` (`LED_MODE_GRADUAL_ON`): Sáng dần.
- `0x06` (`LED_MODE_GRADUAL_OFF`): Tắt dần.

### 2.3. Tốc Độ (Speed) & Số Lần (Count)
- `Speed`: `0..255` (Thở mượt đặt `100..150`, nháy nhanh đặt `30..50`).
- `Count`: `0` là lặp vô hạn (dành cho chế độ Idle xe), hoặc `1..255` lần (dành cho báo hiệu).

---

## 3. Giao Thức Truyền Thông BLE (App $\leftrightarrow$ ESP32)

Tuân thủ định dạng gói tin bảo mật hiện có: `<KEY>|<COMMAND>[|<PARAMS>]`.

### 3.1. Đọc Cấu Hình Đèn
- **App gửi**: `<KEY>|GET_LED_CFG`
- **ESP32 phản hồi**:  
  `FB|LED_CFG|<u_m>|<u_c>|<u_s>|<l_m>|<l_c>|<l_s>|<s_m>|<s_c>|<s_s>|<e_m>|<e_c>|<e_s>`  
  *(Trong đó: `u_*` = Unlocked, `l_*` = Locked, `s_*` = Success, `e_*` = Error; `m` = mode, `c` = color, `s` = speed).*

### 3.2. Ghi & Lưu Cấu Hình Đèn
- **App gửi**:  
  `<KEY>|SET_LED_CFG|<u_m>|<u_c>|<u_s>|<l_m>|<l_c>|<l_s>|<s_m>|<s_c>|<s_s>|<e_m>|<e_c>|<e_s>`
- **Xử lý trên ESP32**:
  1. Kiểm tra Secret Key hợp lệ.
  2. Lưu 12 tham số vào NVS Flash (`prefsLed`).
  3. Cập nhật tức thời trạng thái đèn R503 theo trạng thái xe hiện tại (`updateIdleLed()`).
  4. Phản hồi: `FB|LED_CFG_OK`.

### 3.3. Thử Nghiệm Màu Nhanh (Live Test)
- **App gửi**: `<KEY>|TEST_LED|<mode>|<color>|<speed>|<count>`
- **Xử lý trên ESP32**:
  - Phát lệnh `r503SetAuraLed(mode, color, speed, count)`.
  - Phản hồi: `FB|LED_TEST_OK`.

---

## 4. Thiết Kế Firmware ESP32-C3

### 4.1. Cấu Trúc Dữ Liệu
```cpp
struct LedEventConfig {
    uint8_t mode;   // 1=Thở, 2=Nháy, 3=Sáng, 4=Tắt, 5=Sáng dần, 6=Tắt dần
    uint8_t color;  // 1=Đỏ, 2=Xanh, 3=Tím, 4=Xanh lá, 5=Vàng, 6=Cyan, 7=Trắng
    uint8_t speed;  // 0..255
};

struct R503LedSystemConfig {
    LedEventConfig unlocked; // Mặc định: mode=1, color=2 (Xanh), speed=120
    LedEventConfig locked;   // Mặc định: mode=4 (Tắt), color=2, speed=0
    LedEventConfig success;  // Mặc định: mode=2 (Nháy), color=2, speed=40
    LedEventConfig error;    // Mặc định: mode=2 (Nháy), color=1 (Đỏ), speed=30
};
```

### 4.2. Lưu & Tải NVS Flash (`Preferences prefsLed`)
- Khởi tạo namespace: `prefsLed.begin("led_cfg", false)`.
- Hàm `loadLedConfig()`: Tải các giá trị từ NVS, nếu chưa có thì gán giá trị mặc định tối ưu.
- Hàm `saveLedConfig()`: Ghi các giá trị vào NVS.
- Hàm `updateIdleLed()`: Gọi `r503SetAuraLed` theo cấu hình `unlocked` (nếu `isUnlocked == true`) hoặc `locked` (nếu `isUnlocked == false`).
- Hàm `ledSuccess()` và `ledError()`: Gọi theo cấu hình `success` và `error`.

---

## 5. Thiết Kế Giao Diện Android (APP Controlesp)

### 5.1. Vòng Tròn Màu RGB Tương Tác (Interactive RGB Color Wheel)
- Tạo Custom View hoặc sử dụng Canvas `SweepGradient` kết hợp bộ chọn góc cảm ứng:
  - Hiển thị dải quang phổ tròn 360 độ gồm đầy đủ 7 sắc cầu vồng: Đỏ $\rightarrow$ Vàng $\rightarrow$ Xanh Lá $\rightarrow$ Xanh Ngọc (Cyan) $\rightarrow$ Xanh Dương $\rightarrow$ Tím $\rightarrow$ Đỏ.
  - Vòng trong có tâm màu Trắng tinh khiết.
  - Bộ phát hiện cử chỉ chạm (Touch Drag): Khi người dùng chạm hoặc xoay ngón tay trên bánh xe màu, con trỏ tròn phát sáng (Thumb Glow) di chuyển theo góc ngón tay.
  - App tính toán góc tọa độ $(x, y) \rightarrow \theta$ và tự động ánh xạ góc quay sang `ColorIndex` gần nhất của R503, đồng thời hiển thị tên màu và mã màu nổi bật.

### 5.2. Khung Điều Khiển Sự Kiện & Hiệu Ứng
- **Thanh chọn sự kiện (Tabs / Segmented Buttons)**:
  - [1. Xe Bật] | [2. Xe Khóa] | [3. Quét Đúng] | [4. Quét Sai]
- **Chọn Kiểu Hiệu Ứng (Radio Buttons)**:
  - 🌿 Thở êm dịu (Breathing)
  - 💡 Sáng liên tục (Always ON)
  - ⚡ Nhấp nháy (Flashing)
  - 🌙 Tắt hoàn toàn (Always OFF)
- **Thanh trượt Tốc độ (Speed Slider)**: Từ Chậm (200) đến Nhanh (30).
- **Bộ Presets 1 chạm (Quick Style Presets)**:
  - 🐬 **Ocean Neon**: Xe bật thở Cyan, Quét đúng nháy Xanh dương.
  - 🟣 **Cyberpunk**: Xe bật thở Tím vi mạch, Quét đúng nháy Xanh lá.
  - 🏎️ **Sport Racing**: Xe bật thở Đỏ rực, Quét đúng nháy Vàng gold.
  - 🌿 **Emerald Nature**: Xe bật thở Xanh lá sinh học, Quét đúng nháy Cyan.
  - 🛡️ **Stealth Eco**: Tắt toàn bộ đèn khi xe khóa, bật sáng dịu khi xe mở (Tiết kiệm điện tối đa).
- **Nút Hành Động**:
  - `[⚡ Thử Ngay Trên Cảm Biến]` (Gửi lệnh `TEST_LED` để xem ngay trên xe).
  - `[💾 Lưu Cấu Hình Xuống Xe]` (Gửi lệnh `SET_LED_CFG`).

---

## 6. Kế Hoạch Xác Minh (Verification Plan)

### 6.1. Tự Động Hóa (Build Verification)
1. **Biên dịch Firmware**: Chạy PlatformIO compile cho ESP32-C3:
   ```powershell
   pio run -d "firmware/esp32 c3"
   ```
   Đảm bảo `[SUCCESS]` không có lỗi cú pháp hay thiếu bộ nhớ.
2. **Biên dịch Android App**: Chạy Gradle debug assemble:
   ```powershell
   $env:JAVA_HOME="C:\Program Files\Android\Android Studio\jbr"; ./gradlew assembleDebug
   ```
   Đảm bảo `BUILD SUCCESSFUL` sinh ra file `app-debug.apk` sạch sẽ.

### 6.2. Kiểm Thử Thủ Công Trên Phần Cứng Thực Tế
- Bật App, kết nối Bluetooth với ESP32-C3.
- Mở mục "Cài đặt đèn vòng màu R503":
  - Xoay bánh xe màu RGB $\rightarrow$ Bấm "Thử ngay" $\rightarrow$ Vòng LED trên cảm biến R503 lập tức đổi màu tương ứng (Đỏ, Xanh, Tím, Xanh lá, Vàng, Cyan, Trắng).
  - Thử chọn Preset "Cyberpunk" và bấm Lưu.
  - Mở khóa xe $\rightarrow$ Vòng R503 thở màu tím vi mạch.
  - Khóa xe $\rightarrow$ Vòng R503 tắt hoàn toàn bảo vệ ắc quy.
  - Quét ngón tay đúng $\rightarrow$ Vòng R503 nháy xác nhận theo màu đã chọn.
