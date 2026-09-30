/**
 * @file 03_Enroll_5C5R_MultiAngle.ino
 * @brief Ví dụ đăng ký nâng cao 5 lần chạm đa góc độ (5C5R NCNR)
 *        Tạo composite template góc rộng cho nhận diện cực nhạy
 */

#include <Arduino.h>
#include <TZM1026.h>

#define TZM_RX_PIN   0
#define TZM_TX_PIN   1
#define TZM_WAKE_PIN 3

HardwareSerial tzmSerial(1);
TZM1026 fingerprint(&tzmSerial);

void onEnrollProgress(uint8_t step, uint8_t total, const char* msg) {
    Serial.printf("   👉 [BƯỚC %d/%d]: %s\n", step, total, msg);
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n--- [TZM1026 EXAMPLE 03: ENROLL 5C5R MULTI-ANGLE] ---");

    pinMode(TZM_WAKE_PIN, INPUT_PULLDOWN);
    tzmSerial.begin(115200, SERIAL_8N1, TZM_RX_PIN, TZM_TX_PIN);

    if (!fingerprint.handshake()) {
        Serial.println("❌ Không tìm thấy cảm biến TZM1026!");
        while (1) delay(1000);
    }

    Serial.println("Gõ phím 'm' trên Serial Monitor để bắt đầu quy trình 5 lần chạm đa góc...");
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'm' || c == 'M') {
            int16_t nextId = fingerprint.getNextFreeId();
            if (nextId <= 0) {
                Serial.println("❌ Không tìm được ID trống!");
                return;
            }

            uint16_t enrollId = (uint16_t)nextId;
            Serial.println("\n=======================================================");
            Serial.printf("👉 BẮT ĐẦU ĐĂNG KÝ 5 LẦN CHẠM (5C5R) VÀO USER #%d\n", enrollId);
            Serial.println("=======================================================");

            bool ok = fingerprint.enroll5C5R(enrollId, TZM_ROLE_ADMIN, TZM_WAKE_PIN, onEnrollProgress);

            if (ok) {
                Serial.println("\n🎉 [HOÀN HẢO] Đã lưu mẫu đa góc độ thành công!");
                Serial.println("   Vân tay hiện đã được phủ biên rộng, nhận diện cực nhạy mọi góc!");
            } else {
                Serial.println("\n❌ [THẤT BẠI] Quá trình lấy mẫu bị hủy.");
            }
        }
    }
}
