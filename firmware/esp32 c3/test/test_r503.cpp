#include <Arduino.h>
#include <Adafruit_Fingerprint.h>

// ==========================================
// 📌 CHÂN KẾT NỐI ESP32-C3 SUPERMINI
// ==========================================
#define R503_RX_PIN   0  // Chân GPIO 0 (Nối dây Vàng TXD của R503)
#define R503_TX_PIN   1  // Chân GPIO 1 (Nối dây Xanh lá RXD của R503)
#define R503_WAKE_PIN 3  // Chân GPIO 3 (Nối dây Xanh dương WAKEUP của R503)

HardwareSerial r503Serial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint((Stream*)&r503Serial);

void setup() {
    Serial.begin(115200);
    delay(2000); // Đợi cổng USB CDC kết nối ổn định

    Serial.println("\n\n================================================");
    Serial.println("     KIỂM TRA CẢM BIẾN R503 (CHẨN ĐOÁN CHI TIẾT)  ");
    Serial.println("================================================");
    Serial.println("LƯU Ý: Vòng LED R503 mặc định KHÔNG TỰ SÁNG khi cắm nguồn.");
    Serial.println("Nó chỉ sáng khi ESP32 gửi lệnh bật LED thành công qua UART,");
    Serial.println("hoặc khi phát hiện có ngón tay chạm vào cảm biến!");
    Serial.println("------------------------------------------------");

    pinMode(R503_WAKE_PIN, INPUT_PULLUP);

    Serial.println("⏳ Đang quét Baudrate tìm cảm biến R503 (57600, 9600, 115200, 19200, 38400)...");
    uint32_t bauds[] = {57600, 9600, 115200, 19200, 38400};
    bool found = false;

    for (uint32_t b : bauds) {
        Serial.printf("   👉 Thử Baudrate: %d ... ", b);
        r503Serial.begin(b, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
        delay(60); // Đợi UART ổn định, KHÔNG gọi finger.begin(b) để tránh reset chân GPIO và tránh delay 1s
        if (finger.verifyPassword()) {
            Serial.printf("THÀNH CÔNG! (Baud = %d)\n", b);
            found = true;
            break;
        } else {
            Serial.println("Không phản hồi.");
        }
    }

    if (found) {
        Serial.println("🎉 KẾT NỐI UART THÀNH CÔNG VỚI R503!");
        Serial.println("🌈 Đang kích hoạt vòng LED R503...");
        finger.LEDcontrol(FINGERPRINT_LED_BREATHING, 100, FINGERPRINT_LED_BLUE, 0);
    } else {
        Serial.println("⚠️ Chưa bắt tay được qua UART ở mọi baudrate!");
        Serial.println("💡 GỢI Ý: Hãy thử ĐỔI CHÉO 2 DÂY VÀNG (GPIO 1) và XANH LÁ (GPIO 0).");
    }

    Serial.println("\n👉 HÃY CHẠM NGÓN TAY VÀO MẶT CẢM BIẾN R503 ĐỂ TEST!");
    Serial.println("------------------------------------------------");
}

unsigned long lastPrintTime = 0;
bool lastWake = false;

void loop() {
    // 1. Kiểm tra chân cảm ứng WAKEUP (ACTIVE LOW: Không chạm = 3.2V / HIGH, Chạm = 0V / LOW)
    bool isTouched = (digitalRead(R503_WAKE_PIN) == LOW);
    if (isTouched != lastWake) {
        lastWake = isTouched;
        if (isTouched) {
            Serial.println("🖐️ [PHÁT HIỆN CHẠM TAY! Chân WAKEUP = 0V/LOW]");
            Serial.println("   => Cảm biến R503 CÒN SỐNG 100%, mạch cảm ứng hoạt động cực nhạy!");
            // Thử gửi lệnh bật đèn ngay khi chạm
            finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_PURPLE, 2);
        } else {
            Serial.println("🖐️ [NHẤC TAY RA] Chân WAKEUP về 3.2V (HIGH).");
        }
    }

    // 2. Nếu UART thông, thử chụp ảnh vân tay
    int p = finger.getImage();
    if (p == FINGERPRINT_OK) {
        Serial.println("📸 [ẢNH VÂN TAY ĐÃ CHỤP THÀNH CÔNG!]");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_BLUE, 2);
        delay(500);
    }

    // 3. In nhịp tim hệ thống mỗi 3 giây
    if (millis() - lastPrintTime > 3000) {
        lastPrintTime = millis();
        Serial.printf("[Trạng thái] WAKEUP (GPIO 3): %s | Đang chờ bạn chạm ngón tay...\n", 
                      (digitalRead(R503_WAKE_PIN) == LOW) ? "0V / LOW (ĐANG CHẠM TAY)" : "3.2V / HIGH (Chưa chạm)");
    }

    delay(50);
}
