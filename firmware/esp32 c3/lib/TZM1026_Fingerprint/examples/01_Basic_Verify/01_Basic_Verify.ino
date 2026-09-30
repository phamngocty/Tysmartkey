/**
 * @file 01_Basic_Verify.ino
 * @brief Ví dụ quét so khớp 1:N cơ bản sử dụng thư viện TZM1026
 *        Tương thích ESP32, ESP32-C3, STM32, Arduino.
 */

#include <Arduino.h>
#include <TZM1026.h>

#if defined(ESP32)
  #define TZM_RX_PIN   0   // Nối chân TX cảm biến
  #define TZM_TX_PIN   1   // Nối chân RX cảm biến
  #define TZM_WAKE_PIN 3   // Nối TOUCH_OUT (Active HIGH)
  HardwareSerial tzmSerial(1);
#else
  // Cho Arduino Uno / Mega / Nano
  #define TZM_WAKE_PIN 2
  #define tzmSerial Serial1
#endif

TZM1026 fingerprint(&tzmSerial);

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n--- [TZM1026 EXAMPLE 01: BASIC VERIFY] ---");

    pinMode(TZM_WAKE_PIN, INPUT_PULLDOWN);

#if defined(ESP32)
    tzmSerial.begin(115200, SERIAL_8N1, TZM_RX_PIN, TZM_TX_PIN);
#else
    tzmSerial.begin(115200);
#endif

    // Kiểm tra kết nối
    if (fingerprint.handshake()) {
        Serial.println("✔️ Kết nối cảm biến TZM1026 thành công!");
    } else {
        Serial.println("❌ Không tìm thấy cảm biến! Vui lòng kiểm tra dây nối UART.");
        while (1) delay(1000);
    }

    int16_t count = fingerprint.getUserCount();
    Serial.printf("📊 Số lượng vân tay đang lưu: %d / 100\n", count);

    // Kích hoạt AI tự học thích ứng (càng dùng càng nhạy)
    fingerprint.enableSelfLearning();

    Serial.println("👉 Hãy chạm ngón tay vào cảm biến để mở khóa...");
}

void loop() {
    // Kiểm tra chân ngắt chạm tay WAKEUP (TOUCH_OUT lên mức HIGH = 3.3V khi chạm)
    if (digitalRead(TZM_WAKE_PIN) == HIGH) {
        Serial.println("\n👉 Phát hiện ngón tay! Đang nhận diện...");

        uint16_t matchedId = 0;
        uint8_t role = 0;
        if (fingerprint.verify(matchedId, role, 2000)) {
            const char* roleName = (role == TZM_ROLE_ADMIN) ? "Chủ xe (Admin)" :
                                   ((role == TZM_ROLE_NORMAL) ? "Người nhà" : "Khách");
            Serial.printf("🎉 [MỞ KHÓA THÀNH CÔNG] Xin chào User #%d | Vai trò: %s\n", matchedId, roleName);
        } else {
            Serial.println("🔒 [TỪ CHỐI] Vân tay không hợp lệ hoặc chưa được đăng ký!");
        }

        // Chờ người dùng nhấc ngón tay ra để tránh quét lặp liên tục
        while (digitalRead(TZM_WAKE_PIN) == HIGH) {
            delay(50);
        }
        delay(300);
        Serial.println("👉 Sẵn sàng chờ lần quét tiếp theo...");
    }
    delay(20);
}
