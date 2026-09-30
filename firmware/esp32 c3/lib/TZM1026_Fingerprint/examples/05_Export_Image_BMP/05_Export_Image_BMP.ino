/**
 * @file 05_Export_Image_BMP.ino
 * @brief Ví dụ chụp ảnh vân tay đồ họa thô (Raw Bitmap 160x160)
 *        và đóng gói thành file ảnh chuẩn Windows BMP (.BMP Base64)
 */

#include <Arduino.h>
#include <TZM1026.h>
#include <mbedtls/base64.h>

#define TZM_RX_PIN   0
#define TZM_TX_PIN   1
#define TZM_WAKE_PIN 3

HardwareSerial tzmSerial(1);
TZM1026 fingerprint(&tzmSerial);

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n--- [TZM1026 EXAMPLE 05: EXPORT IMAGE BMP] ---");

    pinMode(TZM_WAKE_PIN, INPUT_PULLDOWN);
    tzmSerial.begin(115200, SERIAL_8N1, TZM_RX_PIN, TZM_TX_PIN);

    if (!fingerprint.handshake()) {
        Serial.println("❌ Không tìm thấy cảm biến TZM1026!");
        while (1) delay(1000);
    }

    Serial.println("Gõ phím 'i' và ĐẶT NGÓN TAY LÊN MẮT ĐỌC để chụp ảnh...");
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'i' || c == 'I') {
            Serial.println("\n📸 Hãy ĐẶT NGÓN TAY lên mắt đọc cảm biến...");

            // Chờ ngón tay chạm
            uint32_t waitStart = millis();
            while (digitalRead(TZM_WAKE_PIN) == LOW) {
                if (millis() - waitStart > 6000) {
                    Serial.println("⏱️ Hết thời gian chờ đặt ngón tay!");
                    return;
                }
                delay(10);
            }
            delay(150); // Chờ tiếp xúc ổn định

            // Cấp phát buffer chứa điểm ảnh thô (160x160 = 25,600 bytes)
            const size_t maxPixelBytes = 35000;
            uint8_t* rawPixels = (uint8_t*)malloc(maxPixelBytes);
            if (!rawPixels) {
                Serial.println("❌ Không đủ bộ nhớ RAM cho điểm ảnh!");
                return;
            }

            uint16_t width = 0;
            uint16_t height = 0;
            Serial.println("⏳ Đang chụp và truyền mảng điểm ảnh thô qua UART...");

            if (fingerprint.captureRawImage(rawPixels, width, height, maxPixelBytes)) {
                uint32_t totalPixels = (uint32_t)width * height;
                Serial.printf("✔️ Đã nhận trọn vẹn ảnh: %d x %d pixels (%d bytes)!\n", width, height, totalPixels);

                // 1. Vẽ ASCII Art trực quan lên Serial Monitor
                Serial.println("\n----------------- [ẢNH VÂN TAY TRỰC QUAN] -----------------");
                const char asciiChars[] = " .:-=+*#%@";
                int stepX = (width > 60) ? (width / 40) : 1;
                int stepY = (height > 60) ? (height / 25) : 1;
                for (int y = 0; y < height; y += stepY) {
                    Serial.print("   |");
                    for (int x = 0; x < width; x += stepX) {
                        uint8_t val = rawPixels[y * width + x];
                        int charIdx = val * 9 / 255;
                        if (charIdx > 9) charIdx = 9;
                        Serial.print(asciiChars[charIdx]);
                    }
                    Serial.println("|");
                }
                Serial.println("-----------------------------------------------------------\n");

                // 2. Đóng gói thành file BMP 8-bit Grayscale chuẩn Windows
                size_t bmpTotalSize = 0;
                uint8_t* bmpData = (uint8_t*)malloc(totalPixels + 1078);
                if (bmpData && TZM1026::generateBmp(rawPixels, width, height, bmpData, bmpTotalSize)) {
                    // Mã hóa Base64 để người dùng copy xem trên file HTML
                    size_t base64Len = 0;
                    mbedtls_base64_encode(nullptr, 0, &base64Len, bmpData, bmpTotalSize);
                    char* base64Str = (char*)malloc(base64Len + 1);
                    if (base64Str) {
                        size_t actualLen = 0;
                        mbedtls_base64_encode((unsigned char*)base64Str, base64Len + 1, &actualLen, bmpData, bmpTotalSize);
                        base64Str[actualLen] = '\0';

                        Serial.println("================== [CHUỖI BASE64 BMP ĐỂ XEM TRÊN WEB] ==================");
                        Serial.print("data:image/bmp;base64,");
                        for (size_t i = 0; i < actualLen; i += 128) {
                            size_t chunk = (actualLen - i < 128) ? (actualLen - i) : 128;
                            Serial.write((const uint8_t*)&base64Str[i], chunk);
                        }
                        Serial.println();
                        Serial.println("========================================================================");
                        Serial.println("💡 Copy chuỗi trên và dán vào file tools/view_fingerprint.html để tải file .BMP/.PNG!");
                        free(base64Str);
                    }
                    free(bmpData);
                }
            } else {
                Serial.println("❌ Chụp ảnh hoặc truyền dữ liệu thất bại!");
            }
            free(rawPixels);
        }
    }
}
