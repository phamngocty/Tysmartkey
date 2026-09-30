/**
 * @file 02_Enroll_3C3R.ino
 * @brief Ví dụ đăng ký vân tay mới theo chuẩn 3 lần chạm (3C3R)
 */

#include <Arduino.h>
#include <TZM1026.h>

#define TZM_RX_PIN   0
#define TZM_TX_PIN   1
#define TZM_WAKE_PIN 3

HardwareSerial tzmSerial(1);
TZM1026 fingerprint(&tzmSerial);

void onEnrollProgress(uint8_t currentStep, uint8_t totalSteps, const char* msg) {
    Serial.printf("   [%d/%d] %s\n", currentStep, totalSteps, msg);
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n--- [TZM1026 EXAMPLE 02: ENROLL 3C3R] ---");

    pinMode(TZM_WAKE_PIN, INPUT_PULLDOWN);
    tzmSerial.begin(115200, SERIAL_8N1, TZM_RX_PIN, TZM_TX_PIN);

    if (!fingerprint.handshake()) {
        Serial.println("❌ Không kết nối được cảm biến!");
        while (1) delay(1000);
    }

    Serial.println("Gõ phím 'e' trên Serial Monitor để bắt đầu đăng ký...");
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'e' || c == 'E') {
            // Tự động tìm ID trống
            int16_t nextId = fingerprint.getNextFreeId();
            if (nextId <= 0) {
                Serial.println("❌ Bộ nhớ đã đầy hoặc lỗi!");
                return;
            }

            uint16_t enrollId = (uint16_t)nextId;
            Serial.printf("\n=======================================================\n");
            Serial.printf("👉 BẮT ĐẦU ĐĂNG KÝ VÂN TAY VÀO USER ID: #%d\n", enrollId);
            Serial.printf("=======================================================\n");

            // Đăng ký với Role = TZM_ROLE_NORMAL (Người nhà)
            bool ok = fingerprint.enroll3C3R(enrollId, TZM_ROLE_NORMAL, TZM_WAKE_PIN, onEnrollProgress);

            if (ok) {
                Serial.printf("\n🎉 [THÀNH CÔNG] Đã lưu mẫu vân tay cho User ID #%d!\n", enrollId);
            } else {
                Serial.println("\n❌ [THẤT BẠI] Quá trình đăng ký bị gián đoạn hoặc lỗi.");
            }
        }
    }
}
