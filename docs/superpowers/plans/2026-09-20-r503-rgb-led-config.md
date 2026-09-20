# Kế Hoạch Triển Khai: Cấu Hình Đèn Vòng Màu RGB R503 & Bánh Xe Màu Tương Tác Android

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tích hợp tính năng cấu hình đèn vòng màu RGB R503 toàn diện: bánh xe màu 360° tương tác trên Android, tùy biến 4 sự kiện xe (Bật, Khóa, Quét đúng, Quét sai), bộ Style Presets 1 chạm, thử màu tức thời (Live Test), lưu trữ NVS Flash trên ESP32-C3.

**Architecture:** Mở rộng firmware ESP32-C3 hỗ trợ đủ 7 màu RGB phần cứng của R503 (Opcode 0x35) và lưu trữ NVS Flash qua `prefsLed`. Bổ sung giao thức BLE (`GET_LED_CFG`, `SET_LED_CFG`, `TEST_LED`). Trên Android xây dựng Custom View `RgbColorWheelView` vẽ dải màu quang phổ `SweepGradient` với bộ tính toán góc và ánh xạ màu, tích hợp trong hộp thoại cài đặt tiện ích chuyên sâu.

**Tech Stack:** C++ / PlatformIO (ESP32-C3, Preferences NVS, HardwareSerial UART, NimBLE), Kotlin / Android SDK (Canvas, Paint, SweepGradient, AlertDialog, Custom View).

---

### Task 1: Cấu Trúc Dữ Liệu & Hỗ Trợ 7 Màu RGB NVS Flash Trên ESP32-C3

**Files:**
- Modify: `firmware/esp32 c3/src/main.cpp`

- [ ] **Step 1: Mở rộng định nghĩa màu phần cứng R503 đầy đủ 7 màu**
  Cập nhật enum `AuraLedColor` trong `main.cpp` để hỗ trợ đầy đủ từ 0x01 đến 0x07:
  ```cpp
  enum AuraLedColor {
      LED_COLOR_RED      = 0x01,  // Đỏ (Red)
      LED_COLOR_BLUE     = 0x02,  // Xanh dương (Blue)
      LED_COLOR_PURPLE   = 0x03,  // Tím (Purple)
      LED_COLOR_GREEN    = 0x04,  // Xanh lá (Green)
      LED_COLOR_YELLOW   = 0x05,  // Vàng (Yellow)
      LED_COLOR_CYAN     = 0x06,  // Xanh ngọc / Lơ (Cyan)
      LED_COLOR_WHITE    = 0x07   // Trắng (White)
  };
  ```

- [ ] **Step 2: Khai báo struct cấu hình & Preferences `prefsLed`**
  ```cpp
  struct LedEventConfig {
      uint8_t mode;   // 1=Breathing, 2=Flashing, 3=Always ON, 4=Always OFF
      uint8_t color;  // 1..7
      uint8_t speed;  // 0..255
  };

  struct R503LedSystemConfig {
      LedEventConfig unlocked; // Khi xe bật khóa
      LedEventConfig locked;   // Khi xe khóa
      LedEventConfig success;  // Khi quét đúng
      LedEventConfig error;    // Khi quét sai
  };

  R503LedSystemConfig ledConfig = {
      { LED_MODE_BREATHING, LED_COLOR_BLUE, 120 },
      { LED_MODE_OFF,       LED_COLOR_BLUE, 0   },
      { LED_MODE_FLASHING,  LED_COLOR_BLUE, 40  },
      { LED_MODE_FLASHING,  LED_COLOR_RED,  30  }
  };

  Preferences prefsLed;
  ```

- [ ] **Step 3: Viết hàm `loadLedConfig()`, `saveLedConfig()`, và cập nhật `updateIdleLed()`, `ledSuccess()`, `ledError()`**
  Tải và lưu cấu hình vào namespace `"led_cfg"`, cập nhật các hàm hiệu ứng gọi theo `ledConfig`.

- [ ] **Step 4: Biên dịch thử nghiệm PlatformIO**
  Run: `pio run -d "firmware/esp32 c3"`
  Expected: `[SUCCESS]`

---

### Task 2: Bộ Xử Lý Lệnh BLE (`GET_LED_CFG`, `SET_LED_CFG`, `TEST_LED`)

**Files:**
- Modify: `firmware/esp32 c3/src/main.cpp`

- [ ] **Step 1: Viết hàm `sendLedConfigResponse()`**
  Đóng gói phản hồi:
  `FB|LED_CFG|<u_m>|<u_c>|<u_s>|<l_m>|<l_c>|<l_s>|<s_m>|<s_c>|<s_s>|<e_m>|<e_c>|<e_s>`

- [ ] **Step 2: Thêm các nhánh xử lý lệnh trong `processIncomingCommand`**
  - Nhánh `cmd == "GET_LED_CFG"`
  - Nhánh `cmd == "SET_LED_CFG"`: Phân tích 12 tham số, lưu vào NVS, cập nhật `updateIdleLed()`, phản hồi `FB|LED_CFG_OK`.
  - Nhánh `cmd == "TEST_LED"`: Gọi `r503SetAuraLed(testMode, testColor, testSpeed, testCount)`, phản hồi `FB|LED_TEST_OK`.

- [ ] **Step 3: Biên dịch kiểm tra với PlatformIO**
  Run: `pio run -d "firmware/esp32 c3"`
  Expected: `[SUCCESS]`

---

### Task 3: Xây Dựng Custom View Bánh Xe Màu RGB (`RgbColorWheelView.kt`)

**Files:**
- Create: `APP Controlesp/app/src/main/java/com/example/control_esp/RgbColorWheelView.kt`

- [ ] **Step 1: Tạo class `RgbColorWheelView` kế thừa `View`**
  - Vẽ dải màu quang phổ `SweepGradient` với 7 màu RGB tương ứng với 7 màu R503:
    `Đỏ (#FF2D55) -> Vàng (#FFCC00) -> Xanh Lá (#34C759) -> Cyan (#00F5D4) -> Xanh Dương (#007AFF) -> Tím (#AF52DE) -> Đỏ (#FF2D55)`
  - Vẽ vòng tròn màu trung tâm trắng.
  - Bắt `onTouchEvent(MotionEvent)`: Tính toán góc $\theta = \text{atan2}(y - cy, x - cx)$, cập nhật vị trí con trỏ (thumb).
  - Ánh xạ góc quay sang `ColorIndex` (1..7) và callback `onColorSelected(colorIndex: Int, colorHex: Int, colorName: String)`.

---

### Task 4: Hộp Thoại Cài Đặt Đèn Vòng Màu R503 & Presets Trên Android

**Files:**
- Modify: `APP Controlesp/app/src/main/java/com/example/control_esp/MainActivity.kt`

- [ ] **Step 1: Xây dựng hàm `showR503LedConfigDialog()` trong `MainActivity.kt`**
  - Tích hợp `RgbColorWheelView`.
  - Thanh chọn sự kiện (Xe Bật / Xe Khóa / Quét Đúng / Quét Sai).
  - Chọn hiệu ứng (Thở / Sáng liên tục / Nhấp nháy / Tắt).
  - Thanh trượt Tốc độ (Speed SeekBar).
  - Các nút chọn nhanh Preset phong cách:
    - 🐬 *Ocean Neon*
    - 🟣 *Cyberpunk*
    - 🏎️ *Sport Racing*
    - 🌿 *Emerald Nature*
    - 🛡️ *Stealth Eco*
  - Nút "⚡ Thử ngay trên cảm biến" và nút "💾 Lưu cấu hình".

- [ ] **Step 2: Thêm bộ bắt phản hồi BLE trong `processVehicleStatus`**
  - Nhận `FB|LED_CFG|...` $\rightarrow$ Nạp dữ liệu vào cấu hình trên App.
  - Nhận `FB|LED_CFG_OK` $\rightarrow$ Báo Toast lưu thành công.
  - Nhận `FB|LED_TEST_OK` $\rightarrow$ Báo Toast đèn R503 đang sáng thử.

- [ ] **Step 3: Gắn nút mở hộp thoại cài đặt đèn trên giao diện Tab Cài Đặt / Nút quét vân tay**

---

### Task 5: Biên Dịch & Xác Minh Toàn Hệ Thống

**Files:**
- Output APK: `APP Controlesp/app/build/outputs/apk/debug/app-debug.apk`
- Firmware: `firmware/esp32 c3/.pio/build/esp32-c3-devkitm-1/firmware.bin`

- [ ] **Step 1: Chạy biên dịch Android APK**
  Run: `$env:JAVA_HOME="C:\Program Files\Android\Android Studio\jbr"; ./gradlew assembleDebug`
  Expected: `BUILD SUCCESSFUL`

- [ ] **Step 2: Chạy biên dịch Firmware ESP32-C3**
  Run: `pio run -d "firmware/esp32 c3"`
  Expected: `[SUCCESS]`
