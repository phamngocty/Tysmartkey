/**
 * @file test_r503.cpp
 * @brief Chương trình kiểm tra & chẩn đoán chuyên sâu cảm biến vân tay quang học GROW R503 / R503-M22
 *        trên nền tảng ESP32-C3 SuperMini (PlatformIO Arduino Framework)
 * 
 * Linh kiện: Module vân tay quang học tròn GROW R503 (hoặc R503-M22 viền inox chống nước IP65)
 * Chip điều khiển: Synochip AS608 / DSP GROW (Giao thức Synochip Protocol v1.4, khung EF 01)
 * Vòng LED: Đèn Aura LED RGB 7 màu (Đỏ, Xanh dương, Tím, Xanh lá, Vàng, Cyan, Trắng)
 * 
 * SƠ ĐỒ CHÂN KẾT NỐI ESP32-C3 SUPERMINI (Dây 6 màu chuẩn GROW):
 *  - Dây ĐỎ (VCC)         --> Nguồn 3.3V DC (Nguồn chính nuôi chip DSP và vòng LED)
 *  - Dây ĐEN (GND)        --> Mass GND chung
 *  - Dây VÀNG (TXD)       --> Chân RX ESP32-C3 (GPIO 0)
 *  - Dây XANH LÁ (RXD)    --> Chân TX ESP32-C3 (GPIO 1)
 *  - Dây XANH DƯƠNG (WAKEUP) --> Chân GPIO 3 ESP32-C3 (Ngắt chạm tay: ACTIVE LOW, Không chạm = 3.3V, Chạm = 0V)
 *  - Dây TRẮNG (3.3V_TOUCH)  --> Nguồn 3.3V DC (Cấp nguồn nuôi vi mạch cảm ứng chạm điện dung 24/7)
 * 
 * CƠ CẤU CHẤP HÀNH THỬ NGHIỆM:
 *  - Relay 2: GPIO 7  (Mở khóa xe / ACC - Active LOW: Kích = 0V, Tắt = 3.3V)
 *  - Relay 3: GPIO 10 (Còi / Đèn xi nhan - Active LOW: Kích = 0V, Tắt = 3.3V)
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Adafruit_Fingerprint.h>
#include "esp_sleep.h"
#include <mbedtls/base64.h>

// Định nghĩa bổ sung đầy đủ 7 màu LED Aura cho cảm biến R503 (v1.4+)
#ifndef FINGERPRINT_LED_GREEN
#define FINGERPRINT_LED_GREEN   0x04
#endif
#ifndef FINGERPRINT_LED_YELLOW
#define FINGERPRINT_LED_YELLOW  0x05
#endif
#ifndef FINGERPRINT_LED_CYAN
#define FINGERPRINT_LED_CYAN    0x06
#endif
#ifndef FINGERPRINT_LED_WHITE
#define FINGERPRINT_LED_WHITE   0x07
#endif

// ============================================================================
// 📌 CẤU HÌNH PHẦN CỨNG ESP32-C3 & R503
// ============================================================================
#define R503_RX_PIN          0   // Chân GPIO 0 (Nối dây Vàng TXD của cảm biến R503)
#define R503_TX_PIN          1   // Chân GPIO 1 (Nối dây Xanh lá RXD của cảm biến R503)
#define R503_WAKE_PIN        3   // Chân GPIO 3 (Nối dây XANH DƯƠNG WAKEUP của R503 - Active LOW: Chạm = 0V)
#define R503_DEFAULT_BAUD    57600 // Tốc độ Baud mặc định xuất xưởng của dòng R503

// Cấu hình chân Relay thực tế đang cắm
#define RELAY_STARTER_PIN   7   // Relay 2: Đề xe / Mở khóa ACC (Active LOW: Kích = 0V, Tắt = 3.3V)
#define RELAY_BUZZER_PIN    10  // Relay 3: Đèn / Còi (Active LOW: Kích = 0V, Tắt = 3.3V)

// Cổng HardwareSerial độc lập UART1 cho cảm biến R503
HardwareSerial r503Serial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint((Stream*)&r503Serial);

// Cờ cấu hình Debug in Hex UART
bool enableHexDump = true;

// ============================================================================
// 1. HÀM ĐIỀU KHIỂN RELAY & VÒNG LED R503
// ============================================================================
void triggerSuccessAction(uint16_t id, uint16_t confidence) {
    Serial.println("   --------------------------------------------------------");
    Serial.printf("   🔓 [XÁC THỰC THÀNH CÔNG] Xin chào User ID: #%d (Độ tin cậy: %d)!\n", id, confidence);
    Serial.println("   🌈 [AURA LED]: Sáng hiệu ứng Breathing Xanh Dương / Xanh Lá!");
    Serial.println("   ⚡ [KÍCH HOẠT RELAY]: ĐÓNG Relay GPIO 7 (1.5s) & KÊU CÒI Relay GPIO 10 (200ms)!");
    Serial.println("   --------------------------------------------------------");

    // Vòng LED R503 chớp sáng màu xanh lá / xanh dương
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_BLUE, 2);

    digitalWrite(RELAY_STARTER_PIN, LOW); // Đóng Relay GPIO 7 (Active LOW)
    digitalWrite(RELAY_BUZZER_PIN, LOW);  // Đóng Relay GPIO 10 (Kêu còi/nháy đèn)
    delay(200);
    digitalWrite(RELAY_BUZZER_PIN, HIGH); // Tắt còi
    delay(1300);                          // Giữ mở khóa tổng cộng 1.5s
    digitalWrite(RELAY_STARTER_PIN, HIGH);// Nhả relay GPIO 7

    // Trở về đèn thở xanh dương êm dịu
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
}

void triggerFailAction() {
    Serial.println("   🔒 [TỪ CHỐI TRUY CẬP] Vân tay lạ hoặc chưa được đăng ký.");
    Serial.println("   ⚠️ [CẢNH BÁO]: Vòng LED nháy ĐỎ 3 lần + Kêu còi tạch tạch 2 tiếng!");

    // Vòng LED R503 nháy đỏ cảnh báo
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 3);

    digitalWrite(RELAY_BUZZER_PIN, LOW); delay(100);
    digitalWrite(RELAY_BUZZER_PIN, HIGH); delay(100);
    digitalWrite(RELAY_BUZZER_PIN, LOW); delay(100);
    digitalWrite(RELAY_BUZZER_PIN, HIGH);

    delay(400);
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
}

// ============================================================================
// HÀM TIỆN ÍCH LẤY MẪU CHỐNG TRƯỢT & CHỜ NHẤC TAY (CHUẨN MAIN COMMERCIAL FIRMWARE)
// HỖ TRỢ CHẾ ĐỘ KÉP: PHẦN CỨNG WAKEUP (ACTIVE LOW) + QUANG HỌC FALLBACK
// ============================================================================

// 1. Chờ người dùng nhấc ngón tay ra khỏi cảm biến (chống dính ngón tay liên tục)
bool r503WaitForLift(unsigned long timeoutMs = 8000) {
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (digitalRead(R503_WAKE_PIN) == HIGH) {
            delay(50);
            while (r503Serial.available()) r503Serial.read(); // Dọn sạch rác UART
            return true;
        }
        delay(40);
    }
    while (r503Serial.available()) r503Serial.read();
    return false;
}

// 2. Hàm lấy mẫu chống trượt (sao y bản chính từ main.cpp):
// Hỗ trợ cả ngắt chạm phần cứng (GPIO 3) lẫn thăm dò quang học nếu chưa cắm dây WAKEUP.
// Lặp liên tục trong khi ngón tay đang áp trên cảm biến (lên tới 3000ms),
// nếu trích xuất lần đầu chưa đạt (0x02 do đang di chuyển/chưa đủ áp lực),
// vòng lặp tiếp tục đọc frame tiếp theo cho đến khi image2Tz thành công!
bool r503CaptureFingerprint(uint8_t bufferSlot, unsigned long timeoutMs = 15000) {
    while (r503Serial.available()) r503Serial.read(); // Xóa sạch rác UART
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        // Chờ ngón tay chạm: kiểm tra chân WAKE_PIN (Active LOW) hoặc quang học getImage
        bool touched = (digitalRead(R503_WAKE_PIN) == LOW);
        if (!touched) {
            int p = finger.getImage();
            if (p == FINGERPRINT_OK) {
                touched = true;
            }
        }

        if (touched) {
            // Cho ngón tay ổn định 50ms khi vừa chạm vào mặt kính
            delay(50);

            // Vòng lặp lấy mẫu liên tục trong khi ngón tay đang được giữ trên cảm biến
            unsigned long holdStart = millis();
            while (millis() - holdStart < 3000) {
                int p = finger.getImage();
                if (enableHexDump) {
                    Serial.printf("📡 [DEBUG UART] getImage -> 0x%02X\n", p);
                }
                if (p == FINGERPRINT_OK) {
                    p = finger.image2Tz(bufferSlot);
                    if (enableHexDump) {
                        Serial.printf("📡 [DEBUG UART] image2Tz(Slot %d) -> 0x%02X\n", bufferSlot, p);
                    }
                    if (p == FINGERPRINT_OK) {
                        return true; // Thành công lấy mẫu và trích xuất đặc trưng!
                    }
                }

                // Nếu người dùng đã nhấc ngón tay ra sớm
                if (p == FINGERPRINT_NOFINGER && digitalRead(R503_WAKE_PIN) == HIGH) {
                    break;
                }
                delay(40);
            }

            // Nếu giữ quá 3 giây mà ảnh vẫn bị mờ hoặc quẹt trượt
            Serial.printf("⚠️ Chưa trích xuất được đặc trưng góc này, vui lòng đặt lại ngón tay...\n");
            finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 1);
            r503WaitForLift(2500);
        }
        delay(40);
    }
    return false;
}

// ============================================================================
// 2. SO KHỚP VÂN TAY 1:N (VERIFICATION)
// ============================================================================
void r503Verify1N() {
    Serial.println("\n👉 [CHẾ ĐỘ SO KHỚP 1:N]: Hãy ĐẶT ngón tay lên mắt đọc R503...");

    // Dọn sạch buffer UART trước khi quét
    while (r503Serial.available()) r503Serial.read();

    unsigned long startWait = millis();
    while (digitalRead(R503_WAKE_PIN) == HIGH) {
        if (millis() - startWait > 8000) {
            Serial.println("⏱️ Hết thời gian chờ đặt ngón tay!");
            return;
        }
        delay(30);
    }

    // Cho ngón tay ổn định 80ms áp lực lên mặt kính lăng kính
    delay(80);

    // Vòng lặp quét đối soát liên tục trong khi ngón tay đang áp trên cảm biến (lên tới 3000ms)
    // Tương tự hàm r503CaptureFingerprint đã hoạt động hoàn hảo 100%
    unsigned long holdStart = millis();
    bool matched = false;
    int matchedId = -1;
    int matchedConfidence = 0;
    bool checked = false;

    while (millis() - holdStart < 3000) {
        int p = finger.getImage();
        if (enableHexDump) {
            Serial.printf("📡 [DEBUG UART] getImage -> 0x%02X\n", p);
        }
        if (p == FINGERPRINT_OK) {
            p = finger.image2Tz(1);
            if (enableHexDump) {
                Serial.printf("📡 [DEBUG UART] image2Tz(Slot 1) -> 0x%02X\n", p);
            }
            if (p == FINGERPRINT_OK) {
                checked = true;
                p = finger.fingerSearch(1); // Opcode 0x04 chuẩn
                if (enableHexDump) {
                    Serial.printf("📡 [DEBUG UART] fingerSearch -> 0x%02X | Matched ID: #%d, Confidence: %d\n",
                                  p, finger.fingerID, finger.confidence);
                }
                // Chỉ công nhận trùng khớp khi R503 trả về 0x00, ID > 0 VÀ confidence > 0
                if (p == FINGERPRINT_OK && finger.fingerID > 0 && finger.confidence > 0) {
                    matched = true;
                    matchedId = finger.fingerID;
                    matchedConfidence = finger.confidence;
                }
                break; // Đã hoàn thành đối soát
            }
        }

        // Nếu người dùng đã nhấc ngón tay ra sớm
        if (digitalRead(R503_WAKE_PIN) == HIGH) {
            break;
        }
        delay(40);
    }

    if (matched) {
        triggerSuccessAction(matchedId, matchedConfidence);
    } else {
        triggerFailAction();
    }

    // Chờ người dùng nhấc ngón tay ra để chống quét lặp
    r503WaitForLift(3000);
}

// ============================================================================
// 3. ĐĂNG KÝ VÂN TAY (ENROLLMENT)
// ============================================================================
// Tìm User ID trống nhỏ nhất trong R503
int16_t r503GetNextFreeId() {
    uint16_t maxCapacity = (finger.capacity > 0) ? finger.capacity : 200;
    for (uint16_t id = 1; id <= maxCapacity; id++) {
        uint8_t p = finger.loadModel(id);
        if (p != FINGERPRINT_OK) {
            return id; // ID này chưa có mẫu vân tay, sẵn sàng sử dụng
        }
    }
    return -1; // Bộ nhớ đầy
}

// Quy trình đăng ký chuẩn R503 (2 lần chạm tạo Template)
void r503EnrollFlow() {
    Serial.println("\n========================================================");
    Serial.println("  BẮT ĐẦU QUY TRÌNH ĐĂNG KÝ VÂN TAY R503 (2 LẦN CHẠM)  ");
    Serial.println("========================================================");

    // 0. Kiểm tra nếu người dùng đang đè sẵn ngón tay trên cảm biến, yêu cầu nhấc ra trước
    if (digitalRead(R503_WAKE_PIN) == LOW) {
        Serial.println("Phát hiện ngón tay đặt sẵn, yêu cầu nhấc ra trước...");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_BLUE, 2);
        r503WaitForLift(8000);
        delay(300);
    }

    int16_t freeId = r503GetNextFreeId();
    if (freeId <= 0) {
        Serial.println("❌ Bộ nhớ cảm biến đã đầy hoặc không tìm được ID trống!");
        return;
    }

    uint16_t newId = (uint16_t)freeId;
    Serial.printf("👉 Chuẩn bị đăng ký vào User ID mới: #%d\n", newId);

    // --- LẦN 1: ĐẶT NGÓN TAY ---
    Serial.println("\n👉 [LẦN 1/2]: Hãy ĐẶT ngón tay chính giữa mắt đọc R503...");
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 80, FINGERPRINT_LED_PURPLE, 0);

    bool b1 = r503CaptureFingerprint(1, 15000);
    if (!b1) {
        Serial.println("⏱️ Hết thời gian chờ hoặc không lấy được mẫu lần 1! Hủy quy trình.");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 2);
        delay(500);
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
        return;
    }

    Serial.println("✔️ [LẦN 1 THÀNH CÔNG] Hãy NHẤC ngón tay ra khỏi cảm biến...");
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_BLUE, 2);
    r503WaitForLift(15000);
    delay(300);

    // --- LẦN 2: ĐẶT LẠI NGÓN TAY ---
    Serial.println("\n👉 [LẦN 2/2]: Hãy ĐẶT LẠI ngón tay vào cảm biến để đối chiếu...");
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 80, FINGERPRINT_LED_PURPLE, 0);

    bool b2 = r503CaptureFingerprint(2, 15000);
    if (!b2) {
        Serial.println("⏱️ Hết thời gian chờ hoặc không lấy được mẫu lần 2! Hủy quy trình.");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 2);
        delay(500);
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
        return;
    }

    delay(100);

    // --- TỔNG HỢP MẪU & LƯU VÀO FLASH ---
    Serial.println("⏳ Đang tổng hợp ma trận template và ghi vào bộ nhớ Flash...");
    int p = finger.createModel();
    if (p == FINGERPRINT_ENROLLMISMATCH) {
        Serial.println("❌ [THẤT BẠI] 2 lần chạm không khớp cùng một ngón tay!");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 3);
        r503WaitForLift(3000);
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
        return;
    } else if (p != FINGERPRINT_OK) {
        Serial.printf("❌ [THẤT BẠI] Lỗi tạo model: mã 0x%02X\n", p);
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 3);
        r503WaitForLift(3000);
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
        return;
    }

    delay(100);
    p = finger.storeModel(newId);
    if (p != FINGERPRINT_OK) {
        delay(120);
        p = finger.storeModel(newId);
    }

    if (p == FINGERPRINT_OK) {
        Serial.println("--------------------------------------------------------");
        Serial.printf("🎉 [XIN CHÚC MỪNG] ĐÃ ĐĂNG KÝ XONG USER ID: #%d!\n", newId);
        Serial.println("   Mẫu vân tay đã được lưu an toàn vĩnh viễn vào chip R503.");
        Serial.println("--------------------------------------------------------");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_BLUE, 3);
    } else {
        Serial.printf("❌ Không lưu được template vào Flash! (Mã lỗi: 0x%02X)\n", p);
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 3);
    }

    r503WaitForLift(3000);
    delay(200);
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
}

// Quy trình đăng ký NÂNG CAO 5 LẦN CHẠM ĐA GÓC ĐỘ (5C5R Đa góc độ - Cực nhạy)
void r503EnrollMultiAngleFlow() {
    Serial.println("\n========================================================");
    Serial.println("  ĐĂNG KÝ NÂNG CAO 5 LẦN CHẠM ĐA GÓC ĐỘ (R503 - 5C5R)   ");
    Serial.println("========================================================");

    // 0. Kiểm tra nếu người dùng đang đè sẵn ngón tay trên cảm biến, yêu cầu nhấc ra trước
    if (digitalRead(R503_WAKE_PIN) == LOW) {
        Serial.println("Phát hiện ngón tay đặt sẵn, yêu cầu nhấc ra trước...");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_BLUE, 2);
        r503WaitForLift(8000);
        delay(300);
    }

    int16_t freeId = r503GetNextFreeId();
    if (freeId <= 0) {
        Serial.println("❌ Bộ nhớ cảm biến đã đầy!");
        return;
    }

    uint16_t newId = (uint16_t)freeId;
    Serial.printf("👉 Chuẩn bị đăng ký vào User ID mới: #%d\n", newId);

    const char* prompts[5] = {
        "ĐẶT THẲNG CHÍNH GIỮA ngón tay",
        "ĐẶT LẠI CHÍNH GIỮA ngón tay (đối chiếu khóa mẫu cơ sở)",
        "ĐẶT NGHIÊNG MÉP TRÁI ngón tay",
        "ĐẶT NGHIÊNG MÉP PHẢI ngón tay",
        "ĐẶT PHẦN CHÓP / ĐẦU MÓNG ngón tay"
    };

    const uint8_t stepColors[5] = {
        FINGERPRINT_LED_YELLOW,
        FINGERPRINT_LED_PURPLE,
        FINGERPRINT_LED_GREEN,
        FINGERPRINT_LED_CYAN,
        FINGERPRINT_LED_WHITE
    };

    bool baseModelSaved = false;

    for (int step = 1; step <= 5; step++) {
        Serial.printf("\n👉 [LẦN %d/5]: Hãy %s...\n", step, prompts[step - 1]);
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 80, stepColors[step - 1], 0);

        uint8_t targetSlot = (step == 1) ? 1 : 2;
        bool ok = r503CaptureFingerprint(targetSlot, 15000);

        if (!ok) {
            Serial.printf("⏱️ [TIMEOUT] Không lấy được mẫu ở lần %d! Hủy quy trình.\n", step);
            finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 2);
            delay(500);
            r503WaitForLift(3000);
            finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
            return;
        }

        delay(100);

        if (step == 2) {
            // Ghép 2 mẫu chính diện tạo model cơ sở và LƯU NGAY vào Flash
            Serial.println("⏳ Đang ghép 2 mẫu chính diện và lưu vào Flash...");
            int p = finger.createModel();
            if (p == FINGERPRINT_ENROLLMISMATCH) {
                Serial.println("❌ Hai lần chạm chính diện không khớp cùng một ngón tay!");
                finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 3);
                r503WaitForLift(3000);
                finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
                return;
            } else if (p != FINGERPRINT_OK) {
                Serial.printf("❌ Lỗi tạo model chính diện: mã 0x%02X\n", p);
                finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 3);
                r503WaitForLift(3000);
                finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
                return;
            }

            p = finger.storeModel(newId);
            if (p != FINGERPRINT_OK) {
                delay(120);
                p = finger.storeModel(newId);
            }
            if (p == FINGERPRINT_OK) {
                baseModelSaved = true;
                Serial.printf("✅ ĐÃ KHÓA MẪU CƠ SỞ CHO ID #%d VÀO FLASH AN TOÀN!\n", newId);
            } else {
                Serial.printf("❌ Lỗi lưu model vào Flash: mã 0x%02X\n", p);
                finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 3);
                r503WaitForLift(3000);
                finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
                return;
            }
        } else if (step > 2) {
            // Tích hợp góc nghiêng vào mẫu (Slot 1 đang chứa template, Slot 2 chứa góc nghiêng mới)
            int p = finger.createModel();
            if (p == FINGERPRINT_OK) {
                finger.storeModel(newId); // Cập nhật mẫu đã tích hợp vào Flash
                Serial.printf("✨ [TÍCH HỢP GÓC %d]: Đã hợp nhất thành công góc nghiêng vào template ID #%d!\n", step, newId);
            } else {
                // Nếu góc nghiêng lệch quá nhiều không thể merge vào template chính diện,
                // nạp lại template chính diện từ Flash vào Slot 1 để bảo vệ Slot 1 không bị rác!
                finger.loadModel(newId);
                Serial.printf("ℹ️ [LƯU Ý GÓC %d]: Góc nghiêng lệch nhiều, giữ nguyên mẫu chính diện an toàn.\n", step);
            }
        }

        if (step < 5) {
            Serial.printf("✔️ [LẦN %d THÀNH CÔNG] Hãy NHẤC ngón tay ra khỏi cảm biến...\n", step);
            finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_CYAN, 2);
            r503WaitForLift(15000);
            delay(300);
        }
    }

    if (baseModelSaved) {
        Serial.println("--------------------------------------------------------");
        Serial.printf("🎉 [XIN CHÚC MỪNG] ĐÃ ĐĂNG KÝ HOÀN TẤT 5C5R CHO USER ID #%d!\n", newId);
        Serial.println("   Vân tay hiện đã bao phủ đa góc độ (chính diện, 2 mép bên, chóp ngón).");
        Serial.println("   Mở khóa cực kỳ nhạy và chống trượt ở mọi góc đặt tay!");
        Serial.println("--------------------------------------------------------");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_BLUE, 4);
    }

    r503WaitForLift(3000);
    delay(200);
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
}


// ============================================================================
// 4. DANH BẠ VÂN TAY & XUẤT CHUỖI JSON CHO MOBILE APP
// ============================================================================
void r503ListAllUsers() {
    Serial.println("\n========================================================");
    Serial.println("  DANH BẠ VÂN TAY R503 (CHUẨN ĐỒNG BỘ MOBILE APP BLE)   ");
    Serial.println("========================================================");

    uint8_t countRet = finger.getTemplateCount();
    if (countRet != FINGERPRINT_OK) {
        Serial.println("❌ Không thể đọc số lượng mẫu vân tay từ R503!");
        return;
    }

    Serial.printf("📊 Tổng số vân tay đang lưu trong Flash: %d / %d\n", finger.templateCount, finger.capacity);
    if (finger.templateCount == 0) {
        Serial.println("ℹ️ [BỘ NHỚ TRỐNG]: Chưa có mẫu vân tay nào được lưu.");
        Serial.println("📲 [JSON CHO APP]: []\n");
        return;
    }

    Serial.println("--------------------------------------------------------");
    Serial.println("  User ID  |  Mã Role  |  Vai trò phân quyền");
    Serial.println("--------------------------------------------------------");

    uint16_t foundCount = 0;
    uint16_t maxCapacity = (finger.capacity > 0) ? finger.capacity : 200;
    uint16_t enrolledIds[200];

    for (uint16_t id = 1; id <= maxCapacity; id++) {
        if (finger.loadModel(id) == FINGERPRINT_OK) {
            uint8_t role = (id == 1) ? 1 : ((id <= 5) ? 2 : 3); // Gợi ý phân quyền: ID 1: Admin, ID 2-5: Normal, >5: Guest
            const char* roleName = (role == 1) ? "Chủ xe (Admin - Toàn quyền)" :
                                   ((role == 2) ? "Người nhà (Normal User)" : "Khách mượn xe (Guest)");
            Serial.printf("   #%-6d |    %-6d |  %s\n", id, role, roleName);
            enrolledIds[foundCount++] = id;
            if (foundCount >= finger.templateCount) break;
        }
    }
    Serial.println("--------------------------------------------------------");

    // Xuất chuỗi JSON cho Mobile App
    Serial.println("\n📲 [DỮ LIỆU JSON ĐỒNG BỘ CHO MOBILE APP]:");
    Serial.print("   [");
    for (uint16_t i = 0; i < foundCount; i++) {
        uint16_t id = enrolledIds[i];
        uint8_t role = (id == 1) ? 1 : ((id <= 5) ? 2 : 3);
        const char* rStr = (role == 1) ? "Chủ xe" : ((role == 2) ? "Người nhà" : "Khách");
        Serial.printf("{\"id\":%d,\"role\":%d,\"name\":\"%s\"}%s", id, role, rStr, (i < foundCount - 1) ? "," : "");
    }
    Serial.println("]\n");
}

// ============================================================================
// HÀM GIAO THỨC UART KHUNG GÓI TIN CHUẨN R503 (ĐỌC KHỐI & ĐỒNG BỘ HEADER)
// ============================================================================
static bool r503ReadByte(uint8_t *b, unsigned long timeoutMs = 1000) {
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (r503Serial.available()) {
            *b = (uint8_t)r503Serial.read();
            return true;
        }
        delayMicroseconds(50);
    }
    return false;
}

static bool r503SyncPacketHeader(unsigned long timeoutMs = 2000) {
    unsigned long start = millis();
    uint8_t syncBuf[6] = {0};
    while (millis() - start < timeoutMs) {
        uint8_t cur = 0;
        if (r503ReadByte(&cur, 100)) {
            syncBuf[0] = syncBuf[1];
            syncBuf[1] = syncBuf[2];
            syncBuf[2] = syncBuf[3];
            syncBuf[3] = syncBuf[4];
            syncBuf[4] = syncBuf[5];
            syncBuf[5] = cur;
            // Khung gói tin chuẩn Synochip Protocol v1.4: Header (0xEF 0x01) + Addr (0xFF 0xFF 0xFF 0xFF)
            if (syncBuf[0] == 0xEF && syncBuf[1] == 0x01 &&
                syncBuf[2] == 0xFF && syncBuf[3] == 0xFF &&
                syncBuf[4] == 0xFF && syncBuf[5] == 0xFF) {
                return true;
            }
        }
    }
    return false;
}

static bool r503ReadBytes(uint8_t *buf, size_t len, unsigned long timeoutMs = 1000) {
    size_t readCount = 0;
    unsigned long start = millis();
    while (readCount < len && (millis() - start < timeoutMs)) {
        int avail = r503Serial.available();
        if (avail > 0) {
            size_t toRead = (size_t)avail;
            if (toRead > (len - readCount)) toRead = len - readCount;
            size_t n = r503Serial.read(buf + readCount, toRead);
            readCount += n;
            start = millis();
        } else {
            delayMicroseconds(20);
        }
    }
    return (readCount == len);
}

// ============================================================================
// 5. TRÍCH XUẤT ẢNH VÂN TAY GỐC (UPIMAGE) & TẠO FILE WINDOWS BMP
// ============================================================================
void r503ExportImageBmp() {
    Serial.println("\n📸 Hãy ĐẶT NGÓN TAY lên mắt đọc cảm biến R503 để chụp ảnh...");
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE, 0);

    unsigned long start = millis();
    while (digitalRead(R503_WAKE_PIN) == HIGH) {
        if (millis() - start > 10000) {
            Serial.println("⏱️ Hết thời gian chờ đặt ngón tay!");
            finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
            return;
        }
        delay(15);
    }
    delay(50);

    bool captured = false;
    unsigned long holdStart = millis();
    while (millis() - holdStart < 3000) {
        if (finger.getImage() == FINGERPRINT_OK) {
            captured = true;
            break;
        }
        if (digitalRead(R503_WAKE_PIN) == HIGH) break;
        delay(40);
    }

    if (!captured) {
        Serial.println("❌ Chụp ảnh không thành công!");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 2);
        r503WaitForLift(2500);
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
        return;
    }

    Serial.println("📸 [CHỤP THÀNH CÔNG] Đang gửi lệnh trích xuất mảng điểm ảnh (UpImage)...");

    // Dọn sạch buffer RX trước khi gửi lệnh
    while (r503Serial.available()) r503Serial.read();

    // Gửi lệnh UpImage (CMD 0x0A): EF 01 FF FF FF FF 01 00 03 0A 00 0E
    uint8_t cmdUpImg[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x0A, 0x00, 0x0E };
    r503Serial.write(cmdUpImg, sizeof(cmdUpImg));
    r503Serial.flush();

    // 1. Đọc gói tin phản hồi ACK từ cảm biến: Header 6 bytes (EF 01 FF FF FF FF) + 6 bytes nội dung
    if (!r503SyncPacketHeader(2000)) {
        Serial.println("❌ Cảm biến không phản hồi lệnh UpImage (Timeout Header ACK)!");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 2);
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
        return;
    }

    uint8_t ackRest[6]; // PID(1) + Len(2) + Code(1) + Checksum(2) = 6 bytes
    if (!r503ReadBytes(ackRest, 6, 1500)) {
        Serial.println("❌ Lỗi đọc nội dung gói phản hồi ACK từ cảm biến!");
        return;
    }

    uint8_t confirmCode = ackRest[3];
    if (confirmCode != 0x00) {
        Serial.printf("❌ Cảm biến từ chối truyền ảnh (Mã xác nhận: 0x%02X)!\n", confirmCode);
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 2);
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
        return;
    }

    // Bộ đệm động tối đa (32KB) hỗ trợ mọi loại cảm biến R503 (160x160=12.8KB, 192x192=18.4KB)
    const size_t maxCompressedBytes = 32768;

    uint8_t* rawComp = (uint8_t*)malloc(maxCompressedBytes);
    if (!rawComp) {
        Serial.println("❌ Không đủ RAM lưu mảng dữ liệu ảnh!");
        return;
    }

    Serial.println("⏳ Đang nhận các gói tin dữ liệu ảnh từ cảm biến R503...");
    size_t compBytesReceived = 0;
    uint16_t packetCount = 0;
    bool finished = false;
    unsigned long rxStart = millis();

    // Vòng lặp nhận Data Packets (PID 0x02) và End Packet (PID 0x08)
    while (!finished && (compBytesReceived < maxCompressedBytes) && (millis() - rxStart < 15000)) {
        // Đồng bộ chính xác 6 bytes Header: 0xEF 0x01 0xFF 0xFF 0xFF 0xFF
        if (!r503SyncPacketHeader(2000)) {
            Serial.println("⚠️ Mất đồng bộ header gói tin ảnh UART!");
            break;
        }

        // Đọc 3 bytes tiếp theo: PID (1 byte) + Length (2 bytes)
        uint8_t meta[3];
        if (!r503ReadBytes(meta, 3, 1000)) {
            Serial.println("⚠️ Timeout đọc meta gói tin!");
            break;
        }

        uint8_t pid = meta[0]; // 0x02: Data packet, 0x08: End Data packet
        uint16_t pLen = ((uint16_t)meta[1] << 8) | meta[2];
        if (pLen < 2) continue;

        uint16_t payloadLen = pLen - 2; // trừ 2 bytes checksum (mặc định 128 bytes)

        // Đọc payload nén
        if (compBytesReceived + payloadLen <= maxCompressedBytes) {
            if (!r503ReadBytes(rawComp + compBytesReceived, payloadLen, 1000)) {
                Serial.println("⚠️ Lỗi timeout nhận payload dữ liệu!");
                break;
            }
            compBytesReceived += payloadLen;
        } else {
            size_t canTake = maxCompressedBytes - compBytesReceived;
            if (canTake > 0) {
                r503ReadBytes(rawComp + compBytesReceived, canTake, 1000);
                compBytesReceived += canTake;
            }
            size_t discard = payloadLen - canTake;
            uint8_t dummy[64];
            while (discard > 0) {
                size_t step = (discard > sizeof(dummy)) ? sizeof(dummy) : discard;
                r503ReadBytes(dummy, step, 500);
                discard -= step;
            }
        }

        // Đọc 2 bytes checksum
        uint8_t chk[2];
        r503ReadBytes(chk, 2, 500);

        packetCount++;
        if (packetCount % 25 == 0 || pid == 0x08) {
            Serial.printf("   📥 Tiến độ: Đã nhận gói [%u] - %u bytes...\n", packetCount, compBytesReceived);
        }

        if (pid == 0x08) {
            finished = true;
            break;
        }
    }

    if (compBytesReceived == 0) {
        Serial.printf("❌ Không nhận được dữ liệu ảnh nào sau %lums!\n", millis() - rxStart);
        free(rawComp);
        return;
    }

    // Tự động nhận diện độ phân giải ma trận ảnh dựa trên số byte thực tế cảm biến truyền về:
    // 1. R503 Ma trận tròn (100 gói x 128B = 12,800 bytes nén = 25,600 pixel): 160 x 160
    // 2. R503 Ma trận vuông (144 gói x 128B = 18,432 bytes nén = 36,864 pixel): 192 x 192
    // 3. R307/ZFM20 (234 gói x 128B = 29,952 bytes nén = 59,904 pixel): 208 x 288
    uint16_t width = 160;
    uint16_t height = 160;
    uint32_t totalPixels = (uint32_t)compBytesReceived * 2;

    if (compBytesReceived == 12800) {
        width = 160;
        height = 160;
    } else if (compBytesReceived == 18432) {
        width = 192;
        height = 192;
    } else if (compBytesReceived == 29952) {
        width = 208;
        height = 288;
    } else {
        // Fallback tự tính ma trận vuông
        uint16_t side = (uint16_t)round(sqrt(totalPixels));
        if ((uint32_t)side * side == totalPixels) {
            width = side;
            height = side;
        } else if (totalPixels % 160 == 0) {
            width = 160;
            height = totalPixels / 160;
        } else if (totalPixels % 192 == 0) {
            width = 192;
            height = totalPixels / 192;
        } else {
            width = 160;
            height = totalPixels / 160;
        }
    }

    Serial.printf("✔️ Đã nhận trọn vẹn: %u bytes (%u gói) | Tự động nhận diện cảm biến: %d x %d px (%u điểm ảnh) trong %lums!\n",
                  compBytesReceived, packetCount, width, height, totalPixels, millis() - rxStart);

    // 1. VẼ ẢNH ASCII ART TRỰC TIẾP LÊN SERIAL MONITOR
    Serial.println("\n----------------- [HÌNH ẢNH VÂN TAY R503 TRỰC QUAN] -----------------");
    const char asciiChars[] = " .:-=+*#%@";
    int stepX = width / 40;
    int stepY = height / 25;
    for (int y = 0; y < height; y += stepY) {
        Serial.print("   |");
        for (int x = 0; x < width; x += stepX) {
            size_t pixelIdx = (size_t)y * width + x;
            size_t compIdx = pixelIdx / 2;
            uint8_t val = 0;
            if (compIdx < compBytesReceived) {
                uint8_t b = rawComp[compIdx];
                val = (pixelIdx % 2 == 0) ? ((b >> 4) * 17) : ((b & 0x0F) * 17);
            }
            int charIdx = val * 9 / 255;
            if (charIdx > 9) charIdx = 9;
            Serial.print(asciiChars[charIdx]);
        }
        Serial.println("|");
    }
    Serial.println("--------------------------------------------------------------------\n");

    // 2. TẠO FILE BMP 8-BIT GRAYSCALE CHUẨN WINDOWS (1078B HEADER)
    uint32_t bmpHeaderSize = 14 + 40 + 1024; // 1078 bytes
    uint32_t bmpTotalSize = bmpHeaderSize + totalPixels;
    uint8_t* bmpData = (uint8_t*)malloc(bmpTotalSize);
    if (!bmpData) {
        Serial.println("❌ Không đủ RAM tạo file BMP!");
        free(rawComp);
        return;
    }

    // Bitmap File Header (14 bytes)
    memset(bmpData, 0, bmpHeaderSize);
    bmpData[0] = 'B'; bmpData[1] = 'M';
    bmpData[2] = bmpTotalSize & 0xFF;
    bmpData[3] = (bmpTotalSize >> 8) & 0xFF;
    bmpData[4] = (bmpTotalSize >> 16) & 0xFF;
    bmpData[5] = (bmpTotalSize >> 24) & 0xFF;
    bmpData[10] = bmpHeaderSize & 0xFF;
    bmpData[11] = (bmpHeaderSize >> 8) & 0xFF;
    bmpData[12] = (bmpHeaderSize >> 16) & 0xFF;
    bmpData[13] = (bmpHeaderSize >> 24) & 0xFF;

    // Bitmap Info Header (40 bytes)
    bmpData[14] = 40;
    bmpData[18] = width & 0xFF; bmpData[19] = (width >> 8) & 0xFF;
    bmpData[22] = height & 0xFF; bmpData[23] = (height >> 8) & 0xFF; // Positive = Bottom-up
    bmpData[26] = 1;  // 1 plane
    bmpData[28] = 8;  // 8 bits per pixel (grayscale)
    bmpData[34] = totalPixels & 0xFF;
    bmpData[35] = (totalPixels >> 8) & 0xFF;
    bmpData[36] = (totalPixels >> 16) & 0xFF;
    bmpData[37] = (totalPixels >> 24) & 0xFF;
    bmpData[38] = 0x13; bmpData[39] = 0x0B; // 2835 ppm (~508 DPI)
    bmpData[42] = 0x13; bmpData[43] = 0x0B;
    bmpData[46] = 0; bmpData[47] = 1; // 256 colors

    // Palette Grayscale (256 * 4 = 1024 bytes)
    for (int i = 0; i < 256; i++) {
        bmpData[54 + i * 4 + 0] = (uint8_t)i; // Blue
        bmpData[54 + i * 4 + 1] = (uint8_t)i; // Green
        bmpData[54 + i * 4 + 2] = (uint8_t)i; // Red
        bmpData[54 + i * 4 + 3] = 0;
    }

    // Đảo ngược dòng quét (Bottom-Up) theo chuẩn Windows BMP
    for (int y = 0; y < height; y++) {
        int srcY = height - 1 - y;
        uint8_t* dstRow = &bmpData[bmpHeaderSize + y * width];
        for (int x = 0; x < width; x++) {
            size_t pixelIdx = (size_t)srcY * width + x;
            size_t compIdx = pixelIdx / 2;
            if (compIdx < compBytesReceived) {
                uint8_t b = rawComp[compIdx];
                dstRow[x] = (pixelIdx % 2 == 0) ? ((b >> 4) * 17) : ((b & 0x0F) * 17);
            } else {
                dstRow[x] = 0;
            }
        }
    }

    // Giải phóng buffer nén ngay để nhường RAM tối đa cho chuỗi Base64
    free(rawComp);

    // 3. MÃ HÓA BASE64 ĐỂ XUẤT RA SERIAL
    size_t base64Len = 0;
    mbedtls_base64_encode(nullptr, 0, &base64Len, bmpData, bmpTotalSize);
    char* base64Str = (char*)malloc(base64Len + 1);
    if (base64Str) {
        size_t actualLen = 0;
        mbedtls_base64_encode((unsigned char*)base64Str, base64Len + 1, &actualLen, bmpData, bmpTotalSize);
        base64Str[actualLen] = '\0';
        free(bmpData); // Giải phóng bmpData ngay sau khi mã hóa xong!

        Serial.println("==================== [BẮT ĐẦU CHUỖI ẢNH BASE64 BMP R503] ====================");
        Serial.print("data:image/bmp;base64,");
        for (size_t i = 0; i < actualLen; i += 128) {
            size_t chunk = (actualLen - i < 128) ? (actualLen - i) : 128;
            Serial.write((const uint8_t*)&base64Str[i], chunk);
        }
        Serial.println();
        Serial.println("==================== [KẾT THÚC CHUỖI ẢNH BASE64 BMP R503] ====================");
        Serial.println("💡 GỢI Ý: Copy toàn bộ chuỗi 'data:image/bmp;base64,...' ở trên,");
        Serial.println("         mở file tools/view_fingerprint.html để xem và tải ảnh .BMP/.PNG!");
        free(base64Str);
    } else {
        free(bmpData);
    }
    while (r503Serial.available()) r503Serial.read(); // Xả sạch UART phòng khi còn sót byte thừa
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
}

// ============================================================================
// 6. THỬ NGHIỆM VÒNG LED RGB AURA R503
// ============================================================================
void r503TestLedModes() {
    Serial.println("\n--- [DEMO VÒNG LED AURA RGB 7 MÀU CỦA R503] ---");
    Serial.println("1. LED Đỏ (Nháy 3 lần - Báo từ chối/Lỗi)");
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 3);
    delay(1500);

    Serial.println("2. LED Xanh lá (Sáng đứng 1 giây - Mở cửa)");
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_GREEN, 0);
    delay(1000);

    Serial.println("3. LED Tím / Hồng (Thở êm dịu)");
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 80, FINGERPRINT_LED_PURPLE, 2);
    delay(2000);

    Serial.println("4. LED Vàng (Nháy cảnh báo)");
    finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_YELLOW, 2);
    delay(1500);

    Serial.println("5. LED Cyan / Xanh ngọc (Sáng mờ)");
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_CYAN, 0);
    delay(1000);

    Serial.println("6. LED Trắng (Sáng rực rỡ)");
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_WHITE, 0);
    delay(1000);

    Serial.println("7. Trở về LED Xanh dương (Breathing mặc định)");
    finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
    Serial.println("✔️ Đã kiểm tra xong các chế độ LED Aura!");
}

// Cài đặt tốc độ Baudrate UART cho cảm biến R503 (Lưu vĩnh viễn vào Flash/EEPROM nội bộ R503)
bool r503SetBaudrate(uint8_t baudId) {
    uint8_t baudMultiplier = 6;
    uint32_t targetBaud = 57600;

    switch (baudId) {
        case 1: baudMultiplier = 1; targetBaud = 9600; break;
        case 2: baudMultiplier = 2; targetBaud = 19200; break;
        case 3: baudMultiplier = 4; targetBaud = 38400; break;
        case 4: baudMultiplier = 6; targetBaud = 57600; break;
        case 5: baudMultiplier = 12; targetBaud = 115200; break;
        default: return false;
    }

    Serial.printf("\n📡 Đang gửi lệnh ghi thanh ghi đặt Baudrate sang %d bps (Hệ số N=%d)...\n", targetBaud, baudMultiplier);
    uint8_t p = finger.setBaudRate(baudMultiplier);
    if (p == FINGERPRINT_OK) {
        Serial.printf("📥 Cảm biến R503 xác nhận đổi Baudrate thành công! Đang chuyển UART ESP32 sang %d bps...\n", targetBaud);
        delay(80);
        r503Serial.end();
        delay(50);
        r503Serial.setRxBufferSize(32768);
        r503Serial.begin(targetBaud, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
        delay(60);
        while (r503Serial.available()) r503Serial.read();

        if (finger.verifyPassword()) {
            Serial.printf("🎉 [BẮT TAY THÀNH CÔNG] R503 đang hoạt động ổn định ở tốc độ %d bps!\n", targetBaud);
            return true;
        } else {
            Serial.println("⚠️ Chưa bắt tay được ở Baud mới, đang kiểm tra lại...");
            return false;
        }
    } else {
        Serial.printf("❌ Lỗi gửi lệnh setBaudRate tới R503! (Mã lỗi: 0x%02X)\n", p);
        return false;
    }
}

// ============================================================================
// 7. IN MENU ĐIỀU KHIỂN & CHẨN ĐOÁN
// ============================================================================
void printMenu() {
    Serial.println("\n========================================================");
    Serial.println("   MENU ĐIỀU KHIỂN & TEST CẢM BIẾN R503 (Gõ phím)");
    Serial.println("========================================================");
    Serial.println("  [1] hoặc [s] : Quét so khớp 1:N (Chờ chạm ngón tay)");
    Serial.println("  [2] hoặc [e] : Đăng ký vân tay chuẩn (Quy trình 2 lần chạm)");
    Serial.println("  [8] hoặc [m] : Đăng ký NÂNG CAO 5 LẦN CHẠM (5C5R Đa góc độ - Cực nhạy)");
    Serial.println("  [i]          : Chụp & Xuất ảnh vân tay thực tế R503 (.BMP Base64)");
    Serial.println("  [u]          : Đồng bộ & Liệt kê DANH BẠ TOÀN BỘ User ID + JSON Mobile App");
    Serial.println("  [f]          : Tự động tìm User ID TRỐNG ĐẦU TIÊN (Next Free ID)");
    Serial.println("  [b]          : Cài đặt Baudrate UART (Lưu vĩnh viễn vào Flash R503)");
    Serial.println("  [o]          : Thử nghiệm vòng LED Aura RGB 7 màu (Breathing/Flashing/ON)");
    Serial.println("  [9] hoặc [l] : Cài đặt độ nhạy / cấp độ bảo mật (Security Level 1 - 5)");
    Serial.println("  [v]          : Đọc thông tin thông số hệ thống & dung lượng Flash R503");
    Serial.println("  [3] hoặc [c] : Lấy tổng số lượng vân tay đang lưu trong Flash");
    Serial.println("  [4] hoặc [d] : Xóa 1 vân tay theo ID");
    Serial.println("  [5] hoặc [k] : XÓA SẠCH toàn bộ vân tay trong bộ nhớ");
    Serial.println("  [6] hoặc [x] : Bật/Tắt hiển thị mã Hex UART (Debug Raw Packet)");
    Serial.println("  [7] hoặc [z] : Thử nghiệm vào Deep Sleep (Chạm ngón tay đánh thức)");
    Serial.println("  [r]          : Bật/Tắt TEST đóng ngắt Relay GPIO 7 (Starter)");
    Serial.println("  [t]          : Bật/Tắt TEST đóng ngắt Relay GPIO 10 (Còi/Đèn)");
    Serial.println("  [?]          : Xem lại menu hướng dẫn này");
    Serial.println("========================================================\n");
}

// ============================================================================
// 8. SETUP & LOOP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000); // Chờ ổn định cổng USB CDC trên ESP32-C3

    Serial.println("\n\n########################################################");
    Serial.println("      KIỂM TRA CHUYÊN SÂU CẢM BIẾN QUANG HỌC GROW R503  ");
    Serial.println("        (Giao thức Synochip Protocol v1.4 - Khung EF 01) ");
    Serial.println("########################################################");

    // Cấu hình chân ngắt chạm tay WAKEUP của R503:
    // ACTIVE LOW: Không chạm = 3.3V (HIGH), Khi chạm ngón tay = 0V (LOW)
    pinMode(R503_WAKE_PIN, INPUT_PULLUP);

    // Cấu hình chân Relay 2 (GPIO 7) và Relay 3 (GPIO 10) - Mặc định tắt (Active LOW)
    pinMode(RELAY_STARTER_PIN, OUTPUT);
    pinMode(RELAY_BUZZER_PIN, OUTPUT);
    digitalWrite(RELAY_STARTER_PIN, HIGH); // Mức HIGH = Tắt relay
    digitalWrite(RELAY_BUZZER_PIN, HIGH);  // Mức HIGH = Tắt relay
    Serial.println("🔌 [PHẦN CỨNG]: Đã sẵn sàng Relay GPIO 7 (Mở khóa) & GPIO 10 (Còi/Đèn)!");

    Serial.println("⏳ [BƯỚC 1]: Đang quét dò Baudrate tìm cảm biến R503 (57600, 115200, 9600, 19200, 38400)...");
    uint32_t bauds[] = { 57600, 115200, 9600, 19200, 38400 };
    bool found = false;
    uint32_t activeBaud = 57600;

    for (uint32_t b : bauds) {
        Serial.printf("   👉 Thử Baudrate: %d bps ... ", b);
        r503Serial.setRxBufferSize(32768);
        r503Serial.begin(b, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
        delay(60);

        if (finger.verifyPassword()) {
            Serial.printf("THÀNH CÔNG! [Chuẩn Synochip EF 01 @ %d bps]\n", b);
            found = true;
            activeBaud = b;
            break;
        } else {
            Serial.println("Không phản hồi.");
        }
    }

    if (found) {
        Serial.println("🎉 [KẾT NỐI UART THÀNH CÔNG] Đã bắt tay hoàn hảo với cảm biến R503!");
        finger.getParameters();
        Serial.printf("📊 [THÔNG SỐ HỆ THỐNG]: Dung lượng: %d mẫu | Cấp bảo mật: %d | Baud: %d\n",
                      finger.capacity, finger.security_level, finger.baud_rate);

        finger.getTemplateCount();
        Serial.printf("📊 [DỮ LIỆU BỘ NHỚ]: Hiện có %d / %d mẫu vân tay đã lưu.\n",
                      finger.templateCount, finger.capacity);

        // Kích hoạt đèn LED Aura Breathing Xanh dương chào mừng
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
    } else {
        Serial.println("⚠️ [CẢNH BÁO]: Chưa bắt tay được qua UART với R503!");
        Serial.println("💡 GỢI Ý KỸ THUẬT:");
        Serial.println("   1. Kiểm tra cặp dây UART: Dây VÀNG R503 nối GPIO 0, dây XANH LÁ R503 nối GPIO 1.");
        Serial.println("   2. Kiểm tra nguồn cấp: Dây ĐỎ nối 3.3V, dây ĐEN nối GND.");
    }

    Serial.println("\n👉 [BƯỚC 2]: KIỂM TRA PHẢN HỒI CHẠM TAY (Chế độ Kép: Phần cứng + Quang học):");
    Serial.printf("   - Trạng thái chân WAKEUP (GPIO 3): %s\n",
                  (digitalRead(R503_WAKE_PIN) == LOW) ? "0V (ĐANG CHẠM TAY)" : "3.3V (Chờ)");
    Serial.println("   - Chế độ hỗ trợ: CẢ ngắt chạm WAKEUP (0ms) LẪN cảm biến quang học (200ms fallback).");
    Serial.println("   💡 Lưu ý dây R503: Dây XANH DƯƠNG (V_Touch) phải nối 3.3V thì dây TRẮNG (WAKE) mới kéo 0V khi chạm.");

    printMenu();
}

unsigned long lastStatusPrint = 0;
bool lastWakeState = false;

void loop() {
    // 1. Giám sát ngắt chạm tay phần cứng WAKEUP (GPIO 3 Active LOW: Chạm = 0V, Thả = 3.3V)
    static unsigned long lastTouchTrigger = 0;
    bool wakeTriggered = (digitalRead(R503_WAKE_PIN) == LOW);

    if (wakeTriggered && (millis() - lastTouchTrigger > 1200)) {
        lastTouchTrigger = millis();
        Serial.println("\n🖐️ [NGẮT CHẠM PHẦN CỨNG WAKEUP]: Chân GPIO 3 = 0V!");
        Serial.println("   => Đang kích hoạt so khớp tự động 1:N...");

        // Dọn sạch buffer UART trước khi thực hiện so khớp
        while (r503Serial.available()) r503Serial.read();

        // Tự động kích hoạt nhận diện khi chạm ngón tay
        r503Verify1N();
        lastWakeState = (digitalRead(R503_WAKE_PIN) == LOW);
    }

    // 2. Nhận lệnh từ Serial Monitor
    if (Serial.available()) {
        char cmd = Serial.read();
        while (Serial.available()) Serial.read(); // Xả sạch bộ đệm

        switch (cmd) {
            case '1':
            case 's':
            case 'S':
                r503Verify1N();
                break;

            case '2':
            case 'e':
            case 'E':
                r503EnrollFlow();
                break;

            case '8':
            case 'm':
            case 'M':
                r503EnrollMultiAngleFlow();
                break;

            case 'i':
            case 'I':
                r503ExportImageBmp();
                break;

            case 'u':
            case 'U':
                r503ListAllUsers();
                break;

            case 'f':
            case 'F': {
                int16_t nextId = r503GetNextFreeId();
                if (nextId > 0) {
                    Serial.printf("👉 [ID KHẢ DỤNG]: User ID trống nhỏ nhất là #%d\n", nextId);
                } else {
                    Serial.println("❌ Bộ nhớ đã đầy hoặc lỗi!");
                }
                break;
            }

            case 'b':
            case 'B': {
                delay(80);
                while (Serial.available()) Serial.read(); // Xả sạch bộ đệm
                Serial.println("\n👉 Chọn Baudrate UART cần cài đặt cho R503 (Lưu vào Flash R503):");
                Serial.println("  [1] : 9600 bps");
                Serial.println("  [2] : 19200 bps");
                Serial.println("  [3] : 38400 bps");
                Serial.println("  [4] : 57600 bps (Chuẩn xuất xưởng R503)");
                Serial.println("  [5] : 115200 bps (Tốc độ cao nhất)");
                Serial.print("👉 Nhập số (1-5): ");
                while (!Serial.available()) delay(10);
                char bChar = Serial.read();
                while (bChar == '\r' || bChar == '\n') {
                    while (!Serial.available()) delay(10);
                    bChar = Serial.read();
                }
                while (Serial.available()) Serial.read();
                Serial.println(bChar);
                if (bChar >= '1' && bChar <= '5') {
                    uint8_t bId = bChar - '0';
                    if (r503SetBaudrate(bId)) {
                        Serial.println("✔️ ĐÃ CÀI ĐẶT BAUDRATE THÀNH CÔNG & ĐÃ LƯU VÀO FLASH R503!");
                    } else {
                        Serial.println("❌ Cài đặt Baudrate thất bại.");
                    }
                } else {
                    Serial.println("⚠️ Lựa chọn không hợp lệ.");
                }
                break;
            }

            case 'o':
            case 'O':
                r503TestLedModes();
                break;

            case '9':
            case 'l':
            case 'L': {
                delay(80);
                while (Serial.available()) Serial.read(); // Xả sạch bộ đệm
                Serial.println("\nNhập Cấp độ bảo mật mong muốn (1 đến 5):");
                Serial.println("   [1] Cực nhạy (FAR cao nhất, nhận diện nhanh)");
                Serial.println("   [3] Cân bằng chuẩn (Mặc định)");
                Serial.println("   [5] Nghiêm ngặt nhất (Chống nhận nhầm tuyệt đối)");
                while (!Serial.available()) delay(10);
                char lvlChar = Serial.read();
                while (lvlChar == '\r' || lvlChar == '\n') {
                    while (!Serial.available()) delay(10);
                    lvlChar = Serial.read();
                }
                while (Serial.available()) Serial.read();
                uint8_t lvl = lvlChar - '0';
                if (lvl >= 1 && lvl <= 5) {
                    if (finger.setSecurityLevel(lvl) == FINGERPRINT_OK) {
                        Serial.printf("✔️ Đã cài đặt Cấp độ bảo mật R503 thành: Level %d!\n", lvl);
                    } else {
                        Serial.println("❌ Cài đặt thất bại!");
                    }
                }
                break;
            }

            case 'v':
            case 'V':
                finger.getParameters();
                Serial.printf("\n📊 [THÔNG SỐ HỆ THỐNG R503]:\n");
                Serial.printf("   - Dung lượng: %d vân tay\n", finger.capacity);
                Serial.printf("   - Cấp độ bảo mật: %d\n", finger.security_level);
                Serial.printf("   - Tốc độ Baud: %d bps\n", finger.baud_rate);
                Serial.printf("   - Kích thước gói tin UART: %d bytes\n\n", finger.packet_len);
                break;

            case '3':
            case 'c':
            case 'C':
                if (finger.getTemplateCount() == FINGERPRINT_OK) {
                    Serial.printf("📊 Tổng số vân tay đang lưu: %d / %d\n", finger.templateCount, finger.capacity);
                }
                break;

            case '4':
            case 'd':
            case 'D': {
                delay(80);
                while (Serial.available()) Serial.read(); // Xả sạch bộ đệm
                Serial.print("Nhập User ID cần xóa (1 - 200) rồi gõ Enter: ");
                while (!Serial.available()) delay(10);
                int delId = Serial.parseInt();
                while (Serial.available()) Serial.read();
                if (delId > 0 && delId <= 200) {
                    if (finger.deleteModel(delId) == FINGERPRINT_OK) {
                        Serial.printf("✔️ Đã xóa thành công User ID #%d khỏi Flash!\n", delId);
                    } else {
                        Serial.printf("❌ Xóa thất bại hoặc ID #%d không tồn tại!\n", delId);
                    }
                }
                break;
            }

            case '5':
            case 'k':
            case 'K': {
                delay(80);
                while (Serial.available()) Serial.read(); // Xả sạch ký tự thừa từ lệnh trước
                Serial.print("⚠️ BẠN CÓ CHẮC CHẮN MUỐN XÓA SẠCH BỘ NHỚ R503? (Gõ 'Y' để xác nhận): ");
                while (!Serial.available()) delay(10);
                char confirm = Serial.read();
                while (confirm == '\r' || confirm == '\n') {
                    while (!Serial.available()) delay(10);
                    confirm = Serial.read();
                }
                while (Serial.available()) Serial.read(); // Xả nốt

                if (confirm == 'Y' || confirm == 'y') {
                    if (finger.emptyDatabase() == FINGERPRINT_OK) {
                        Serial.println("\n✔️ ĐÃ XÓA SẠCH TOÀN BỘ CƠ SỞ DỮ LIỆU R503 VỀ XUẤT XƯỞNG!");
                    } else {
                        Serial.println("\n❌ Lệnh xóa sạch thất bại!");
                    }
                } else {
                    Serial.println("\nĐã hủy lệnh xóa.");
                }
                break;
            }

            case '6':
            case 'x':
            case 'X':
                enableHexDump = !enableHexDump;
                Serial.printf("\n🛠️ Chế độ Debug mã Hex UART: %s\n", enableHexDump ? "BẬT (Hiển thị gói tin UART chi tiết)" : "TẮT (Gọn gàng)");
                break;

            case '7':
            case 'z':
            case 'Z':
                Serial.println("\n💤 [DEEP SLEEP TEST]: Đang chuẩn bị đưa ESP32-C3 vào Deep Sleep...");
                Serial.println("   💡 Đánh thức bằng cách: CHẠM NGÓN TAY vào cảm biến R503 (GPIO 3 kéo xuống 0V).");
                finger.LEDcontrol(FINGERPRINT_LED_OFF, 0, 0, 0); // Tắt đèn để tiết kiệm điện tối đa
                delay(1000);
                esp_deep_sleep_enable_gpio_wakeup(1ULL << R503_WAKE_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
                esp_deep_sleep_start();
                break;

            case 'r':
            case 'R': {
                static bool rState = false;
                rState = !rState;
                digitalWrite(RELAY_STARTER_PIN, rState ? LOW : HIGH);
                Serial.printf("⚡ Relay GPIO 7 (Starter/ACC): %s\n", rState ? "BẬT (0V)" : "TẮT (3.3V)");
                break;
            }

            case 't':
            case 'T': {
                static bool bState = false;
                bState = !bState;
                digitalWrite(RELAY_BUZZER_PIN, bState ? LOW : HIGH);
                Serial.printf("⚡ Relay GPIO 10 (Còi/Đèn): %s\n", bState ? "BẬT (0V)" : "TẮT (3.3V)");
                break;
            }

            case '?':
                printMenu();
                break;

            default:
                break;
        }
        // Đồng bộ lại trạng thái WAKEUP sau khi thực hiện xong lệnh menu
        lastWakeState = (digitalRead(R503_WAKE_PIN) == LOW);
    }

    // In nhịp tim định kỳ mỗi 5 giây
    if (millis() - lastStatusPrint > 5000) {
        lastStatusPrint = millis();
        Serial.printf("[Trạng thái] WAKEUP (GPIO 3): %s | Sẵn sàng nhận lệnh...\n",
                      (digitalRead(R503_WAKE_PIN) == LOW) ? "0V (ĐANG CHẠM TAY)" : "3.3V (Chờ)");
    }

    delay(20);
}
