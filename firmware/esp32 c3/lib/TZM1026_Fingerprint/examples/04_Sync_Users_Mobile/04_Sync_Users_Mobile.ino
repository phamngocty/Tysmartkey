/**
 * @file 04_Sync_Users_Mobile.ino
 * @brief Ví dụ đồng bộ danh bạ User ID & Cấp bậc phân quyền (Role)
 *        Xuất chuỗi JSON chuẩn để truyền lên Mobile App qua BLE / WiFi
 */

#include <Arduino.h>
#include <TZM1026.h>

#define TZM_RX_PIN   0
#define TZM_TX_PIN   1

HardwareSerial tzmSerial(1);
TZM1026 fingerprint(&tzmSerial);

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n--- [TZM1026 EXAMPLE 04: SYNC USERS TO MOBILE APP] ---");

    tzmSerial.begin(115200, SERIAL_8N1, TZM_RX_PIN, TZM_TX_PIN);

    if (!fingerprint.handshake()) {
        Serial.println("❌ Không tìm thấy cảm biến TZM1026!");
        while (1) delay(1000);
    }

    Serial.println("Gõ phím 'u' để tải danh bạ từ cảm biến và xuất chuỗi JSON...");
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'u' || c == 'U') {
            TZMUser users[100];
            uint16_t total = 0;

            Serial.println("\n⏳ Đang đọc danh bạ vân tay từ chip cảm biến...");
            if (fingerprint.getAllUsers(users, 100, total)) {
                Serial.printf("📊 Tìm thấy %d người dùng đã đăng ký:\n", total);
                Serial.println("-----------------------------------------------------");
                Serial.println("  User ID  |  Role ID  |  Quyền hạn");
                Serial.println("-----------------------------------------------------");
                for (uint16_t i = 0; i < total; i++) {
                    const char* rStr = (users[i].role == TZM_ROLE_ADMIN) ? "Chủ xe (Admin - Toàn quyền)" :
                                       ((users[i].role == TZM_ROLE_NORMAL) ? "Người nhà (Normal User)" : "Khách mượn xe (Guest)");
                    Serial.printf("   #%-6d |    %-6d |  %s\n", users[i].id, users[i].role, rStr);
                }
                Serial.println("-----------------------------------------------------");

                // Xuất mảng JSON cho Mobile App
                char jsonBuf[1024];
                fingerprint.getUsersJson(jsonBuf, sizeof(jsonBuf));
                Serial.println("\n📲 [CHUỖI JSON GỬI QUA BLE / HTTP CHO MOBILE APP]:");
                Serial.println(jsonBuf);
                Serial.println();
            } else {
                Serial.println("❌ Không thể đọc danh bạ người dùng!");
            }
        }
    }
}
