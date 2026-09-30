/**
 * @file test_tzm1026.cpp
 * @brief Chương trình kiểm tra & chẩn đoán chuyên sâu cảm biến vân tay BIOSEC TM1026M (TZM1026_V1.0)
 *        trên nền tảng ESP32-C3 (PlatformIO Arduino Framework)
 * 
 * Linh kiện: Module vân tay điện dung bán dẫn phẳng BIOSEC TM1026M
 * Silkscreen: TZM1026_V1.0 (YH / TZ1824)
 * Chip điều khiển: BIOSEC TA0702 / TA0982 (Cảm biến TS1026M 160x160 pixels 508 DPI)
 * 
 * Tài liệu tham khảo:
 * 1. BIOSEC-TM1026M Specification V1.0 (Shanghai Tuozheng Information Technology Co., Ltd.)
 * 2. SFM-V1.7 Fingerprint Module Communication Protocol Overview
 * 3. Nghiên cứu thực nghiệm mạch Shopee từ pqhuy87it:
 *    https://github.com/pqhuy87it/ESP32/blob/main/M%E1%BA%A1ch%20%C4%91i%E1%BB%87n%20tr%C3%AAn%20shopee/M%E1%BA%A1ch%20v%C3%A2n%20tay.md
 * 
 * SƠ ĐỒ CHÂN KẾT NỐI ESP32-C3 SUPERMINI (Đầu jack MX1.25-6P):
 *  - Chân 1 (V_TOUCH)    --> Nguồn 3.3V (Cấp liên tục 24/7 nuôi cảm ứng chạm, dòng tĩnh ~5-10 uA)
 *  - Chân 2 (TOUCH_OUT)  --> Chân GPIO 3 (hoặc GPIO 2) ESP32-C3 (Ngắt chạm tay: ACTIVE HIGH, Chạm = 3.3V)
 *  - Chân 3 (VCC)        --> Nguồn 3.3V (Nguồn chính nuôi chip TA0702, dòng quét ~30-45 mA)
 *  - Chân 4 (TX cảm biến)--> Chân RX ESP32-C3 (GPIO 0)
 *  - Chân 5 (RX cảm biến)--> Chân TX ESP32-C3 (GPIO 1)
 *  - Chân 6 (GND)        --> Mass GND chung
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include "esp_sleep.h"
#include <mbedtls/base64.h>

// ============================================================================
// 📌 CẤU HÌNH PHẦN CỨNG ESP32-C3
// ============================================================================
#define TZM_RX_PIN          0   // Chân GPIO 0 (Nối TX của cảm biến)
#define TZM_TX_PIN          1   // Chân GPIO 1 (Nối RX của cảm biến)
#define TZM_WAKE_PIN        3   // Chân GPIO 3 (Nối TOUCH_OUT của cảm biến - Active HIGH)
#define TZM_DEFAULT_BAUD    115200 // Tốc độ Baud mặc định của chip TA0702

// Cấu hình chân Relay thực tế đang cắm
#define RELAY_STARTER_PIN   7   // Relay 2: Đề xe / Mở khóa ACC (Active LOW: Kích = 0V, Tắt = 3.3V)
#define RELAY_BUZZER_PIN    10  // Relay 3: Đèn / Còi (Active LOW: Kích = 0V, Tắt = 3.3V)

// Cổng HardwareSerial độc lập UART1 cho cảm biến (tránh trùng UART0 USB CDC)
HardwareSerial tzmSerial(1);

// Cờ cấu hình Debug in Hex Packet
bool enableHexDump = true;

// ============================================================================
// 1. ĐỊNH NGHĨA GIAO THỨC BIOSEC SFM-V1.7 (KHUNG 8-BYTE)
// ============================================================================
#define SFM_PACKET_HEADER       0xF5
#define SFM_PACKET_TAIL         0xF5

// Bảng mã lệnh Opcode chuẩn SFM-V1.7
#define SFM_CMD_ENROLL_1        0x01  // Đăng ký vân tay lần 1 (Chỉ định UserID & Role)
#define SFM_CMD_ENROLL_2        0x02  // Đăng ký vân tay lần 2
#define SFM_CMD_ENROLL_3        0x03  // Đăng ký vân tay lần 3 (Tổng hợp đặc trưng & Lưu)
#define SFM_CMD_DEL_USER        0x04  // Xóa vân tay chỉ định theo ID
#define SFM_CMD_CLEAR_ALL       0x05  // Xóa sạch toàn bộ thư viện vân tay
#define SFM_CMD_USER_COUNT      0x09  // Lấy tổng số lượng vân tay đang lưu
#define SFM_CMD_CHECK_USER      0x0A  // Kiểm tra ID đã tồn tại hay chưa
#define SFM_CMD_MATCH_1_1       0x0B  // So khớp 1:1 với ID chỉ định
#define SFM_CMD_MATCH_1_N       0x0C  // So khớp 1:N với toàn bộ thư viện
#define SFM_CMD_GET_FREE_ID     0x0D  // Lấy ID trống đầu tiên chưa sử dụng
#define SFM_CMD_SET_BAUDRATE    0x21  // Cài đặt tốc độ Baudrate UART (Flag: 0=Tạm, 1=Vĩnh viễn EEPROM)
#define SFM_CMD_GET_IMAGE       0x24  // Chụp & Truyền ảnh đồ họa vân tay gốc (Raw Bitmap)
#define SFM_CMD_GET_VERSION     0x26  // Đọc phiên bản firmware module
#define SFM_CMD_SECURITY_LEVEL  0x28  // Cài đặt / Đọc mức độ bảo mật so khớp (0, 1, 2)
#define SFM_CMD_LIST_USERS      0x2B  // Đọc danh bạ tất cả User ID & Phân quyền (Roles)
#define SFM_CMD_SLEEP           0x2C  // Đưa cảm biến vào chế độ ngủ sâu
#define SFM_CMD_FINGER_DETECT   0x30  // Kiểm tra có ngón tay đang chạm không
#define SFM_CMD_CONFIG_FEATURE  0x3F  // Cấu hình tính năng nâng cao (NCNR, Tự học, Đồng nguyên)
#define SFM_CMD_BREAK           0xFE  // Lệnh ngắt khẩn cấp / Hủy lệnh đang chờ quét (Break)

// Bảng mã phản hồi trạng thái ACK (Q3)
#define SFM_ACK_SUCCESS         0x00  // Thực thi thành công
#define SFM_ACK_FAIL            0x01  // Thực thi thất bại / Không khớp
#define SFM_ACK_FULL            0x04  // Thư viện vân tay đã đầy (225 mẫu)
#define SFM_ACK_NOUSER          0x05  // User ID không tồn tại
#define SFM_ACK_USER_EXIST      0x07  // User ID này đã có người đăng ký
#define SFM_ACK_TIMEOUT         0x08  // Hết thời gian chờ đặt ngón tay
#define SFM_ACK_HARDWARE_ERR    0x0A  // Lỗi phần cứng cảm biến
#define SFM_ACK_IMAGE_ERR       0x10  // Lỗi chất lượng ảnh vân tay
#define SFM_ACK_ALGORITHM_FAIL  0x11  // Phát hiện tấn công màng phim / Ngón tay giả
#define SFM_ACK_HOMOLOGY_FAIL   0x12  // Lỗi đồng nguyên (3 lần ấn là 3 ngón tay khác nhau)
#define SFM_ACK_BREAK           0x18  // Lệnh bị hủy ngang

// Gói tin kiểm tra nhanh giao thức Syno/Grow (Adafruit R503)
const uint8_t synoProbe[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x07, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1B };

// ============================================================================
// 2. HÀM TIỆN ÍCH DỊCH MÃ PHẢN HỒI (CONFIRMATION CODE DECODER)
// ============================================================================
const char* sfmGetStatusString(uint8_t code) {
    switch (code) {
        case SFM_ACK_SUCCESS:        return "0x00 [THÀNH CÔNG]: Lệnh hoàn thành tốt đẹp";
        case SFM_ACK_FAIL:           return "0x01 [THẤT BẠI]: Không khớp vân tay hoặc xử lý lỗi";
        case SFM_ACK_FULL:           return "0x04 [BỘ NHỚ ĐẦY]: Đã lưu đủ 225 vân tay";
        case SFM_ACK_NOUSER:         return "0x05 [KHÔNG TỒN TẠI]: User ID này chưa được đăng ký";
        case SFM_ACK_USER_EXIST:     return "0x07 [ĐÃ TỒN TẠI]: User ID này đã có mẫu vân tay";
        case SFM_ACK_TIMEOUT:        return "0x08 [TIMEOUT]: Quá thời gian chờ đặt ngón tay";
        case SFM_ACK_HARDWARE_ERR:   return "0x0A [LỖI PHẦN CỨNG]: Mạch hoặc mắt đọc bị lỗi vật lý";
        case SFM_ACK_IMAGE_ERR:      return "0x10 [LỖI ẢNH]: Vùng chạm quá nhỏ hoặc mờ";
        case SFM_ACK_ALGORITHM_FAIL: return "0x11 [BẢO MẬT]: Nghi ngờ ngón tay giả / Màng phim cao su";
        case SFM_ACK_HOMOLOGY_FAIL:  return "0x12 [LỆCH NGÓN]: 3 lần chạm không cùng một ngón tay";
        case SFM_ACK_BREAK:          return "0x18 [HỦY LỆNH]: Quá trình bị ngắt bởi lệnh mới";
        default:                     return "Mã trạng thái chưa định nghĩa";
    }
}

void printHexBytes(const uint8_t *data, size_t len, const char *prefix = "") {
    if (!enableHexDump) return;
    Serial.print(prefix);
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

// ============================================================================
// 3. BỘ GỬI & NHẬN GÓI TIN UART NGUYÊN BẢN (LOW-LEVEL PACKET ENGINE)
// ============================================================================

// Checksum XOR chuẩn SFM: Byte 1 ^ Byte 2 ^ Byte 3 ^ Byte 4 ^ Byte 5
uint8_t sfmCalcChecksum(const uint8_t *buf) {
    return buf[1] ^ buf[2] ^ buf[3] ^ buf[4] ^ buf[5];
}

// Gửi gói tin chuẩn 8-byte
void sfmSendPacket(uint8_t type, uint8_t p1 = 0, uint8_t p2 = 0, uint8_t p3 = 0) {
    uint8_t pkt[8] = { SFM_PACKET_HEADER, type, p1, p2, p3, 0x00, 0x00, SFM_PACKET_TAIL };
    pkt[6] = sfmCalcChecksum(pkt);
    printHexBytes(pkt, 8, "   [UART TX >>] ");
    tzmSerial.write(pkt, 8);
}

// Đọc gói tin phản hồi chuẩn 8-byte
bool sfmReceivePacket(uint8_t *resp, unsigned long timeoutMs = 2500) {
    unsigned long start = millis();
    uint8_t idx = 0;

    while (millis() - start < timeoutMs) {
        while (tzmSerial.available()) {
            uint8_t c = (uint8_t)tzmSerial.read();
            if (idx == 0 && c != SFM_PACKET_HEADER) continue; // Đồng bộ Header 0xF5
            resp[idx++] = c;
            if (idx == 8) {
                printHexBytes(resp, 8, "   [UART RX <<] ");
                if (resp[7] == SFM_PACKET_TAIL && resp[6] == sfmCalcChecksum(resp)) {
                    return true;
                } else {
                    Serial.println("   ⚠️ [LỖI] Sai Tail hoặc Sai Checksum gói phản hồi!");
                    return false;
                }
            }
        }
        delayMicroseconds(50);
    }
    if (idx > 0) {
        printHexBytes(resp, idx, "   ⚠️ [UART RX DỞ DANG <<] ");
    }
    return false;
}

// Lệnh ngắt khẩn cấp (BREAK 0xFE): Ngay lập tức đưa module về trạng thái IDLE rảnh rỗi
void sfmBreak() {
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_BREAK);
    uint8_t ack[8];
    sfmReceivePacket(ack, 400);
}

// ============================================================================
// 4. CÁC HÀM CHỨC NĂNG NGHIỆP VỤ VÂN TAY (API CẤP CAO)
// ============================================================================

// Kiểm tra bắt tay Handshake
bool sfmHandshake(unsigned long timeoutMs = 600) {
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_USER_COUNT);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, timeoutMs)) {
        return (ack[1] == SFM_CMD_USER_COUNT && ack[4] == SFM_ACK_SUCCESS);
    }
    return false;
}

// Lấy tổng số lượng vân tay đã đăng ký
int16_t sfmGetUserCount() {
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_USER_COUNT);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, 1500)) {
        if (ack[1] == SFM_CMD_USER_COUNT && ack[4] == SFM_ACK_SUCCESS) {
            return ((uint16_t)ack[2] << 8) | ack[3];
        }
    }
    return -1;
}

// So khớp 1:N (Quét tìm kiếm trong toàn bộ cơ sở dữ liệu)
int16_t sfmVerify1N(unsigned long timeoutMs = 4000) {
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_MATCH_1_N);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, timeoutMs)) {
        if (ack[1] == SFM_CMD_MATCH_1_N) {
            uint16_t id = ((uint16_t)ack[2] << 8) | ack[3];
            uint8_t roleOrStatus = ack[4];
            
            // Theo đặc tả giao thức BIOSEC SFM-V1.7 mục 3.7:
            // - Khớp THÀNH CÔNG: Module trả về ID (> 0) và ack[4] là User Role (1, 2, hoặc 3).
            //   Ví dụ bạn quét được: F5 0C 00 03 01 00 0E F5 => ID = #3, Role = 1.
            // - KHÔNG KHỚP (vân tay lạ): Trả về ID = 00 00 (F5 0C 00 00 00 00 0C F5).
            // - TIMEOUT: Trả về ID = 00 00 và status = 0x08 (F5 0C 00 00 08 00 04 F5).
            if (id > 0) {
                return id; // Trùng khớp thành công!
            } else {
                if (roleOrStatus == SFM_ACK_TIMEOUT) {
                    Serial.println("   ℹ️ [TIMEOUT]: Hết thời gian chờ đặt ngón tay.");
                } else {
                    Serial.println("   ℹ️ [KHÔNG KHỚP]: Vân tay lạ hoặc chưa được đăng ký.");
                }
            }
        }
    }
    return -1;
}

// Đăng ký vân tay mới theo quy trình 3C3R chuẩn
// Đăng ký vân tay mới theo quy trình 3C3R chuẩn (Khuyên dùng)
void sfmEnrollFlow() {
    Serial.println("\n========================================================");
    Serial.println("  BẮT ĐẦU ĐĂNG KÝ VÂN TAY MỚI (QUY TRÌNH 3C3R - 3 LẦN CHẠM) ");
    Serial.println("  👉 Mẹo lấy mẫu siêu nhạy: Mỗi lần chạm nghiêng 1 góc khác nhau!");
    Serial.println("========================================================");

    int16_t count = sfmGetUserCount();
    uint16_t newId = (count >= 0) ? (count + 1) : 1;
    Serial.printf("👉 Chuẩn bị đăng ký vào User ID mới: #%d\n", newId);
    uint8_t ack[8];

    // --- LẦN 1 ---
    Serial.println("\n👉 [LẦN 1/3]: Hãy ĐẶT THẲNG CHÍNH GIỮA ngón tay vào cảm biến...");
    while (digitalRead(TZM_WAKE_PIN) == LOW) { delay(10); } // Chờ chạm tay
    delay(150); // Chờ ngón tay ép ổn định lên bề mặt
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_ENROLL_1, (newId >> 8) & 0xFF, newId & 0xFF, 0x01); // Role = 1

    if (!sfmReceivePacket(ack, 8000) || ack[4] != SFM_ACK_SUCCESS) {
        Serial.printf("❌ [LẦN 1 THẤT BẠI] Mã lỗi: %s\n", sfmGetStatusString(ack[4]));
        sfmBreak();
        return;
    }
    Serial.println("✔️ [LẦN 1 THÀNH CÔNG] Hãy NHẤC ngón tay ra...");
    while (digitalRead(TZM_WAKE_PIN) == HIGH) { delay(40); }
    delay(400);

    // --- LẦN 2 ---
    Serial.println("\n👉 [LẦN 2/3]: Hãy ĐẶT HƠI NGHIÊNG MÉP TRÁI ngón tay...");
    while (digitalRead(TZM_WAKE_PIN) == LOW) { delay(10); }
    delay(150);
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_ENROLL_2);

    if (!sfmReceivePacket(ack, 8000) || ack[4] != SFM_ACK_SUCCESS) {
        Serial.printf("❌ [LẦN 2 THẤT BẠI] Mã lỗi: %s\n", sfmGetStatusString(ack[4]));
        sfmBreak();
        return;
    }
    Serial.println("✔️ [LẦN 2 THÀNH CÔNG] Hãy NHẤC ngón tay ra...");
    while (digitalRead(TZM_WAKE_PIN) == HIGH) { delay(40); }
    delay(400);

    // --- LẦN 3 ---
    Serial.println("\n👉 [LẦN 3/3]: Hãy ĐẶT HƠI NGHIÊNG MÉP PHẢI ngón tay để hoàn tất...");
    while (digitalRead(TZM_WAKE_PIN) == LOW) { delay(10); }
    delay(150);
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_ENROLL_3);

    if (sfmReceivePacket(ack, 8000) && ack[4] == SFM_ACK_SUCCESS) {
        Serial.println("--------------------------------------------------------");
        Serial.printf("🎉 [XIN CHÚC MỪNG] Đã đăng ký thành công ngón tay vào ID: #%d!\n", newId);
        Serial.println("   Mẫu vân tay đã ghép trọn vẹn cả tâm và 2 mép, nhận diện cực nhạy!");
        Serial.println("--------------------------------------------------------");
    } else {
        Serial.printf("❌ [LẦN 3 THẤT BẠI] Mã lỗi: %s\n", sfmGetStatusString(ack[4]));
        sfmBreak();
    }
    while (digitalRead(TZM_WAKE_PIN) == HIGH) { delay(40); }
}

// Cài đặt mức độ bảo mật / độ nhạy so sánh
// level: 0 (FAR 1/100,000 - Cực nhạy, dễ nhận), 1 (FAR 1/500,000 - Cân bằng), 2 (FAR 1/1,000,000 - Mặc định, nghiêm ngặt)
bool sfmSetSecurityLevel(uint8_t level) {
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_SECURITY_LEVEL, 0x00, level, 0x00);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, 1500) && ack[4] == SFM_ACK_SUCCESS) {
        return true;
    }
    return false;
}

// Bật tính năng AI Tự học thích ứng (Self-Learning Adaptation)
// Sau mỗi lần so khớp đúng, chip TA0702 tự động bổ sung biên vân tay để càng dùng càng nhạy
bool sfmEnableSelfLearning() {
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_CONFIG_FEATURE, 0x00, 0x40, 0x00);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, 1500) && ack[4] == SFM_ACK_SUCCESS) {
        return true;
    }
    return false;
}

// Đăng ký vân tay NÂNG CAO N LẦN CHẠM (NCNR: Mặc định 5 lần)
// Hướng dẫn đổi góc chạm (tâm, mép trái, mép phải, chóp ngón, đốt dưới) tạo composite template bao phủ toàn diện
void sfmEnrollNCNRFlow(uint8_t totalSamples = 5) {
    Serial.println("\n========================================================");
    Serial.printf("  ĐĂNG KÝ NÂNG CAO %d LẦN CHẠM (NCNR - ĐA GÓC ĐỘ CỰC NHẠY)\n", totalSamples);
    Serial.println("========================================================");

    // 1. Cấu hình module sang chế độ NCNR
    uint8_t ack[8];
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_CONFIG_FEATURE, 0x00, 0x00, 0x06); // NCNR Mode
    if (!sfmReceivePacket(ack, 1500) || ack[4] != SFM_ACK_SUCCESS) {
        Serial.println("❌ Không thể chuyển sang chế độ NCNR!");
        return;
    }

    // 2. Mở rộng Homology = 0 (Cho phép xoay nghiêng ngón tay lấy mẫu rộng)
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_CONFIG_FEATURE, 0x00, 0x01, 0x00);
    sfmReceivePacket(ack, 1500);

    // 3. Đặt số lần lấy mẫu N
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_CONFIG_FEATURE, 0x00, 0x03, totalSamples);
    if (!sfmReceivePacket(ack, 1500) || ack[4] != SFM_ACK_SUCCESS) {
        Serial.println("❌ Không thể cài đặt số lần lấy mẫu!");
        return;
    }

    int16_t count = sfmGetUserCount();
    uint16_t newId = (count >= 0) ? (count + 1) : 1;
    Serial.printf("👉 Chuẩn bị đăng ký vào User ID mới: #%d\n", newId);

    const char* touchGuide[] = {
        "ĐẶT THẲNG CHÍNH GIỮA ngón tay",
        "ĐẶT NGHIÊNG MÉP TRÁI ngón tay",
        "ĐẶT NGHIÊNG MÉP PHẢI ngón tay",
        "ĐẶT CHÓP / ĐẦU ngón tay",
        "ĐẶT ĐỐT DƯỚI ngón tay",
        "ĐẶT XOAY GÓC CHÉO ngón tay"
    };

    for (uint8_t step = 1; step <= totalSamples; step++) {
        const char* guide = (step <= 6) ? touchGuide[step - 1] : "ĐẶT TIẾP ngón tay";
        Serial.printf("\n👉 [LẦN %d/%d]: Hãy %s...\n", step, totalSamples, guide);

        while (digitalRead(TZM_WAKE_PIN) == LOW) { delay(10); } // Chờ chạm ngón tay
        delay(150); // Chờ ngón tay ép ổn định
        while (tzmSerial.available()) tzmSerial.read();

        if (step == 1) {
            sfmSendPacket(SFM_CMD_ENROLL_1, (newId >> 8) & 0xFF, newId & 0xFF, 0x01);
        } else {
            sfmSendPacket(SFM_CMD_ENROLL_1, 0x00, 0x00, 0x00);
        }

        if (!sfmReceivePacket(ack, 8000)) {
            Serial.printf("❌ [LẦN %d THẤT BẠI] Hết thời gian chờ phản hồi!\n", step);
            sfmBreak();
            return;
        }

        if (step < totalSamples) {
            Serial.printf("✔️ [LẦN %d THÀNH CÔNG] Hãy NHẤC ngón tay ra...\n", step);
            while (digitalRead(TZM_WAKE_PIN) == HIGH) { delay(40); }
            delay(400);
        } else {
            // Lần cuối cùng
            if ((ack[1] == 0x03 && ack[4] == SFM_ACK_SUCCESS) || ack[4] == SFM_ACK_SUCCESS) {
                Serial.println("--------------------------------------------------------");
                Serial.printf("🎉 [XIN CHÚC MỪNG] ĐÃ ĐĂNG KÝ XONG USER ID #%d (MẪU %d LẦN CHẠM)!\n", newId, totalSamples);
                Serial.println("   Vân tay hiện đã bao phủ đa góc độ, nhận diện siêu nhạy!");
                Serial.println("--------------------------------------------------------");
            } else {
                Serial.printf("❌ [TỔNG HỢP MẪU THẤT BẠI] Mã lỗi: %s\n", sfmGetStatusString(ack[4]));
            }
            while (digitalRead(TZM_WAKE_PIN) == HIGH) { delay(40); }
        }
    }
}

// Xóa một vân tay theo ID
bool sfmDeleteUser(uint16_t id) {
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_DEL_USER, (id >> 8) & 0xFF, id & 0xFF, 0x00);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, 2000)) {
        return (ack[4] == SFM_ACK_SUCCESS);
    }
    return false;
}

// Xóa sạch toàn bộ cơ sở dữ liệu Flash
bool sfmClearAll() {
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_CLEAR_ALL);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, 3500)) {
        return (ack[4] == SFM_ACK_SUCCESS);
    }
    return false;
}

void triggerSuccessRelay(uint16_t id) {
    Serial.println("   --------------------------------------------------------");
    Serial.printf("   🔓 [XÁC THỰC THÀNH CÔNG] Xin chào User ID: #%d (MỞ KHÓA XE)!\n", id);
    Serial.println("   ⚡ [KÍCH HOẠT RELAY]: ĐÓNG Relay GPIO 7 (1.5s) & KÊU CÒI Relay GPIO 10 (200ms)!");
    Serial.println("   --------------------------------------------------------");
    digitalWrite(RELAY_STARTER_PIN, LOW); // Đóng Relay GPIO 7 (Active LOW)
    digitalWrite(RELAY_BUZZER_PIN, LOW);  // Đóng Relay GPIO 10 (Kêu còi/nháy đèn)
    delay(200);
    digitalWrite(RELAY_BUZZER_PIN, HIGH); // Tắt còi
    delay(1300);                          // Giữ mở khóa tổng cộng 1.5s
    digitalWrite(RELAY_STARTER_PIN, HIGH);// Nhả relay GPIO 7
}

void triggerFailRelay() {
    Serial.println("   🔒 [TỪ CHỐI TRUY CẬP] Vân tay lạ hoặc chưa được đăng ký.");
    Serial.println("   ⚠️ [CẢNH BÁO]: Nháy Relay GPIO 10 tạch tạch 2 tiếng báo động!");
    digitalWrite(RELAY_BUZZER_PIN, LOW); delay(100);
    digitalWrite(RELAY_BUZZER_PIN, HIGH); delay(100);
    digitalWrite(RELAY_BUZZER_PIN, LOW); delay(100);
    digitalWrite(RELAY_BUZZER_PIN, HIGH);
}

void sfmPrintVersion() {
    Serial.println("\nĐang đọc thông tin phiên bản firmware của module...");
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_GET_VERSION);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, 2000)) {
        if (ack[4] == SFM_ACK_SUCCESS) {
            uint16_t len = ((uint16_t)ack[2] << 8) | ack[3];
            Serial.printf("   [Thông tin] Độ dài chuỗi Version: %d bytes\n", len);
            unsigned long start = millis();
            uint8_t verBuf[200];
            uint16_t idx = 0;
            while (millis() - start < 1500 && idx < sizeof(verBuf) - 1) {
                while (tzmSerial.available() && idx < sizeof(verBuf) - 1) {
                    uint8_t c = (uint8_t)tzmSerial.read();
                    verBuf[idx++] = c;
                }
            }
            while (tzmSerial.available()) tzmSerial.read(); // Xả sạch dữ liệu thừa
            verBuf[idx] = '\0';
            Serial.print("   [FIRMWARE / CẢM BIẾN]: ");
            for (int i = 0; i < idx; i++) {
                if (verBuf[i] >= 32 && verBuf[i] <= 126) Serial.print((char)verBuf[i]);
                else Serial.printf("[%02X]", verBuf[i]);
            }
            Serial.println();
        } else {
            Serial.printf("❌ Module từ chối lệnh đọc phiên bản: Mã lỗi = 0x%02X\n", ack[4]);
        }
    } else {
        Serial.println("❌ Không nhận được phản hồi từ module.");
    }
}

// Chụp và xuất ảnh vân tay raw sang định dạng Windows BMP 8-bit Grayscale & Base64
void sfmCaptureAndExportBmp() {
    Serial.println("\n========================================================");
    Serial.println("   CHỤP & XUẤT ẢNH VÂN TAY THỰC TẾ (RAW BMP EXPORT)");
    Serial.println("========================================================");
    Serial.println("👉 Vui lòng ĐẶT & GIỮ CHẮC NGÓN TAY lên mặt kính cảm biến...");

    // Chờ người dùng chạm tay vào viền/mặt kính
    unsigned long waitTouch = millis();
    while (digitalRead(TZM_WAKE_PIN) == LOW && millis() - waitTouch < 6000) { delay(10); }
    delay(200); // Đợi ngón tay ép phẳng và ổn định

    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_GET_IMAGE);

    uint8_t ack[8];
    if (!sfmReceivePacket(ack, 8000)) {
        Serial.println("❌ Hết thời gian chờ chụp ảnh (Timeout)!");
        sfmBreak();
        return;
    }

    if (ack[4] != SFM_ACK_SUCCESS) {
        Serial.printf("❌ Chụp ảnh thất bại! Mã trạng thái: %s\n", sfmGetStatusString(ack[4]));
        sfmBreak();
        return;
    }

    // Đọc kích thước ảnh từ Data Head
    uint16_t width = (uint16_t)ack[2] << 2;
    uint16_t height = (uint16_t)ack[3] << 2;
    uint32_t totalPixels = (uint32_t)width * height;
    Serial.printf("📸 [CHỤP THÀNH CÔNG] Cảm biến TS10xx: %d x %d pixels (%d bytes)\n", width, height, totalPixels);
    Serial.println("⏳ Đang truyền mảng dữ liệu điểm ảnh thô từ cảm biến về ESP32...");

    // Cấp phát bộ nhớ cho mảng pixel ảnh
    uint8_t* rawPixels = (uint8_t*)malloc(totalPixels);
    if (!rawPixels) {
        Serial.println("❌ Không đủ bộ nhớ RAM cho ảnh!");
        sfmBreak();
        return;
    }

    // Chờ Header 0xF5 của khối Data package
    unsigned long start = millis();
    while (millis() - start < 2000 && !tzmSerial.available()) { delay(1); }
    if (tzmSerial.available() && tzmSerial.peek() == 0xF5) {
        tzmSerial.read(); // Bỏ qua Header 0xF5
    }

    // Đọc toàn bộ totalPixels
    size_t readCount = 0;
    start = millis();
    while (readCount < totalPixels && millis() - start < 8000) {
        while (tzmSerial.available() && readCount < totalPixels) {
            rawPixels[readCount++] = (uint8_t)tzmSerial.read();
        }
    }

    // Đọc xả Checksum và Tail nếu còn
    delay(20);
    while (tzmSerial.available()) tzmSerial.read();

    if (readCount < totalPixels) {
        Serial.printf("❌ Dữ liệu ảnh nhận bị thiếu: %d / %d bytes!\n", readCount, totalPixels);
        free(rawPixels);
        sfmBreak();
        return;
    }

    Serial.printf("✔️ Đã nhận trọn vẹn 100%% dữ liệu ảnh (%d bytes)!\n", readCount);

    // 1. VẼ ẢNH ASCII ART TRỰC TIẾP TRÊN SERIAL MONITOR
    Serial.println("\n----------------- [HÌNH ẢNH VÂN TAY TRỰC QUAN] -----------------");
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
    Serial.println("----------------------------------------------------------------\n");

    // 2. TẠO FILE BMP 8-BIT GRAYSCALE CHUẨN WINDOWS
    uint32_t bmpHeaderSize = 14 + 40 + 1024; // 1078 bytes
    uint32_t bmpTotalSize = bmpHeaderSize + totalPixels;
    uint8_t* bmpData = (uint8_t*)malloc(bmpTotalSize);
    if (!bmpData) {
        Serial.println("❌ Không đủ RAM tạo file BMP!");
        free(rawPixels);
        return;
    }

    // Ghi Bitmap File Header (14 bytes)
    bmpData[0] = 'B'; bmpData[1] = 'M';
    bmpData[2] = bmpTotalSize & 0xFF;
    bmpData[3] = (bmpTotalSize >> 8) & 0xFF;
    bmpData[4] = (bmpTotalSize >> 16) & 0xFF;
    bmpData[5] = (bmpTotalSize >> 24) & 0xFF;
    bmpData[6] = 0; bmpData[7] = 0; bmpData[8] = 0; bmpData[9] = 0;
    bmpData[10] = bmpHeaderSize & 0xFF;
    bmpData[11] = (bmpHeaderSize >> 8) & 0xFF;
    bmpData[12] = (bmpHeaderSize >> 16) & 0xFF;
    bmpData[13] = (bmpHeaderSize >> 24) & 0xFF;

    // Ghi Bitmap Info Header (40 bytes)
    bmpData[14] = 40; bmpData[15] = 0; bmpData[16] = 0; bmpData[17] = 0;
    bmpData[18] = width & 0xFF; bmpData[19] = (width >> 8) & 0xFF; bmpData[20] = 0; bmpData[21] = 0;
    bmpData[22] = height & 0xFF; bmpData[23] = (height >> 8) & 0xFF; bmpData[24] = 0; bmpData[25] = 0; // Positive = Bottom-up
    bmpData[26] = 1; bmpData[27] = 0;  // 1 plane
    bmpData[28] = 8; bmpData[29] = 0;  // 8 bits per pixel (grayscale)
    bmpData[30] = 0; bmpData[31] = 0; bmpData[32] = 0; bmpData[33] = 0; // BI_RGB
    bmpData[34] = totalPixels & 0xFF;
    bmpData[35] = (totalPixels >> 8) & 0xFF;
    bmpData[36] = (totalPixels >> 16) & 0xFF;
    bmpData[37] = (totalPixels >> 24) & 0xFF;
    bmpData[38] = 0x13; bmpData[39] = 0x0B; bmpData[40] = 0; bmpData[41] = 0; // 2835 DPI (~508 DPI)
    bmpData[42] = 0x13; bmpData[43] = 0x0B; bmpData[44] = 0; bmpData[45] = 0;
    bmpData[46] = 0; bmpData[47] = 1; bmpData[48] = 0; bmpData[49] = 0; // 256 colors
    bmpData[50] = 0; bmpData[51] = 0; bmpData[52] = 0; bmpData[53] = 0;

    // Ghi Bảng màu Palette Grayscale (256 * 4 = 1024 bytes)
    for (int i = 0; i < 256; i++) {
        bmpData[54 + i * 4 + 0] = (uint8_t)i; // Blue
        bmpData[54 + i * 4 + 1] = (uint8_t)i; // Green
        bmpData[54 + i * 4 + 2] = (uint8_t)i; // Red
        bmpData[54 + i * 4 + 3] = 0;
    }

    // Đảo hàng từ dưới lên trên (Bottom-Up) theo chuẩn Windows BMP
    for (int y = 0; y < height; y++) {
        int srcRow = (height - 1 - y) * width;
        int dstOffset = bmpHeaderSize + y * width;
        memcpy(&bmpData[dstOffset], &rawPixels[srcRow], width);
    }
    free(rawPixels);

    // 3. MÃ HÓA BASE64 ĐỂ XUẤT RA SERIAL
    size_t base64Len = 0;
    mbedtls_base64_encode(nullptr, 0, &base64Len, bmpData, bmpTotalSize);
    char* base64Str = (char*)malloc(base64Len + 1);
    if (base64Str) {
        size_t actualLen = 0;
        mbedtls_base64_encode((unsigned char*)base64Str, base64Len + 1, &actualLen, bmpData, bmpTotalSize);
        base64Str[actualLen] = '\0';

        Serial.println("==================== [BẮT ĐẦU CHUỖI ẢNH BASE64 BMP] ====================");
        Serial.print("data:image/bmp;base64,");
        for (size_t i = 0; i < actualLen; i += 128) {
            size_t chunk = (actualLen - i < 128) ? (actualLen - i) : 128;
            Serial.write((const uint8_t*)&base64Str[i], chunk);
        }
        Serial.println();
        Serial.println("==================== [KẾT THÚC CHUỖI ẢNH BASE64 BMP] ====================");
        Serial.println("💡 GỢI Ý: Hãy copy toàn bộ chuỗi 'data:image/bmp;base64,...' ở trên,");
        Serial.println("         mở file tools/view_fingerprint.html để xem và tải ảnh .BMP/.PNG!");
        free(base64Str);
    }
    free(bmpData);
}

// 1. Tự động lấy User ID trống đầu tiên chưa sử dụng (CMD 0x0D)
int16_t sfmGetNextFreeId() {
    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_GET_FREE_ID);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, 1500) && ack[4] == SFM_ACK_SUCCESS) {
        return ((uint16_t)ack[2] << 8) | ack[3];
    }
    return -1;
}

// 2. Liệt kê danh bạ toàn bộ User ID và Cấp bậc phân quyền (CMD 0x2B)
void sfmListAllUsers() {
    Serial.println("\n========================================================");
    Serial.println("  DANH BẠ VÂN TAY & PHÂN QUYỀN (CHUẨN ĐỒNG BỘ APP BLE)");
    Serial.println("========================================================");

    while (tzmSerial.available()) tzmSerial.read();
    sfmSendPacket(SFM_CMD_LIST_USERS);
    uint8_t ack[8];
    if (!sfmReceivePacket(ack, 2000)) {
        Serial.println("❌ Không nhận được phản hồi danh bạ từ cảm biến (Timeout)!");
        sfmBreak();
        return;
    }

    if (ack[4] != SFM_ACK_SUCCESS) {
        Serial.println("ℹ️ [BỘ NHỚ TRỐNG]: Hiện chưa có vân tay nào được đăng ký trong cảm biến.");
        return;
    }

    uint16_t len = ((uint16_t)ack[2] << 8) | ack[3];
    if (len < 2) {
        Serial.println("ℹ️ [BỘ NHỚ TRỐNG]: Hiện chưa có vân tay nào được lưu.");
        return;
    }

    uint8_t* buf = (uint8_t*)malloc(len + 4);
    if (!buf) {
        Serial.println("❌ Lỗi cấp phát bộ nhớ RAM!");
        return;
    }

    unsigned long start = millis();
    while (millis() - start < 1500 && !tzmSerial.available()) { delay(1); }
    if (tzmSerial.available() && tzmSerial.peek() == 0xF5) {
        tzmSerial.read(); // Bỏ qua Header 0xF5
    }

    size_t readCount = 0;
    start = millis();
    while (readCount < len && millis() - start < 2000) {
        while (tzmSerial.available() && readCount < len) {
            buf[readCount++] = (uint8_t)tzmSerial.read();
        }
    }
    delay(20);
    while (tzmSerial.available()) tzmSerial.read(); // Xả Checksum và Tail

    if (readCount < len) {
        Serial.printf("❌ Đọc dữ liệu danh bạ bị thiếu (%d / %d bytes)!\n", readCount, len);
        free(buf);
        return;
    }

    uint16_t userCount = ((uint16_t)buf[0] << 8) | buf[1];
    Serial.printf("📊 Tìm thấy %d người dùng đã đăng ký:\n", userCount);
    Serial.println("--------------------------------------------------------");
    Serial.println("  User ID  |  Mã Role  |  Vai trò phân quyền");
    Serial.println("--------------------------------------------------------");

    for (uint16_t i = 0; i < userCount; i++) {
        uint16_t offset = 2 + i * 3;
        if (offset + 2 >= len) break;
        uint16_t uId = ((uint16_t)buf[offset] << 8) | buf[offset + 1];
        uint8_t role = buf[offset + 2];
        const char* roleName = "Chưa rõ";
        if (role == 1) roleName = "Chủ xe (Admin - Toàn quyền)";
        else if (role == 2) roleName = "Người nhà (Normal User)";
        else if (role == 3) roleName = "Khách / Người mượn xe (Guest)";

        Serial.printf("   #%-6d |    %-6d |  %s\n", uId, role, roleName);
    }
    Serial.println("--------------------------------------------------------");

    // Xuất chuỗi định dạng JSON chuẩn cho Mobile App
    Serial.println("\n📲 [DỮ LIỆU JSON ĐỒNG BỘ CHO MOBILE APP]:");
    Serial.print("   [");
    for (uint16_t i = 0; i < userCount; i++) {
        uint16_t offset = 2 + i * 3;
        if (offset + 2 >= len) break;
        uint16_t uId = ((uint16_t)buf[offset] << 8) | buf[offset + 1];
        uint8_t role = buf[offset + 2];
        const char* rStr = (role == 1) ? "Chủ xe" : ((role == 2) ? "Người nhà" : "Khách");
        Serial.printf("{\"id\":%d,\"role\":%d,\"name\":\"%s\"}%s", uId, role, rStr, (i < userCount - 1) ? "," : "");
    }
    Serial.println("]\n");

    free(buf);
}

// 3. Cài đặt tốc độ Baudrate UART và lưu vĩnh viễn vào EEPROM (CMD 0x21)
bool sfmSetBaudrate(uint8_t baudId, bool permanent) {
    while (tzmSerial.available()) tzmSerial.read();
    uint8_t flag = permanent ? 0x01 : 0x00;
    sfmSendPacket(SFM_CMD_SET_BAUDRATE, 0x00, baudId, flag);
    uint8_t ack[8];
    if (sfmReceivePacket(ack, 1500) && ack[4] == SFM_ACK_SUCCESS) {
        return true;
    }
    return false;
}

// In menu điều khiển tương tác
void printMenu() {
    Serial.println("\n========================================================");
    Serial.println("   MENU ĐIỀU KHIỂN & TEST CẢM BIẾN TZM1026 (Gõ phím)");
    Serial.println("========================================================");
    Serial.println("  [1] hoặc [s] : Quét so khớp 1:N (Chờ đặt ngón tay)");
    Serial.println("  [2] hoặc [e] : Đăng ký vân tay chuẩn (Quy trình 3C3R - 3 lần chạm)");
    Serial.println("  [8] hoặc [m] : Đăng ký NÂNG CAO 5 LẦN CHẠM (5C5R Đa góc độ - Cực nhạy)");
    Serial.println("  [i]          : Chụp & Xuất ảnh vân tay thực tế (.BMP Base64)");
    Serial.println("  [u]          : Đồng bộ & Liệt kê DANH BẠ TOÀN BỘ User ID + Phân quyền (Roles)");
    Serial.println("  [f]          : Tự động tìm User ID TRỐNG ĐẦU TIÊN (Next Free ID)");
    Serial.println("  [b]          : Cài đặt Baudrate UART (Lưu vĩnh viễn vào EEPROM chip)");
    Serial.println("  [9] hoặc [l] : Cài đặt độ nhạy / cấp độ bảo mật (Level 0: Nhạy nhất, 1, 2)");
    Serial.println("  [a]          : Bật tính năng AI Tự học thích ứng (Self-Learning)");
    Serial.println("  [v]          : Đọc thông tin phiên bản Firmware module");
    Serial.println("  [3] hoặc [c] : Lấy tổng số lượng vân tay đang lưu trong Flash");
    Serial.println("  [4] hoặc [d] : Xóa 1 vân tay theo ID");
    Serial.println("  [5] hoặc [k] : XÓA SẠCH toàn bộ vân tay trong bộ nhớ");
    Serial.println("  [6] hoặc [x] : Bật/Tắt hiển thị mã Hex UART (Debug Raw Packet)");
    Serial.println("  [7] hoặc [z] : Thử nghiệm vào Deep Sleep (Chạm tay đánh thức)");
    Serial.println("  [r]          : Bật/Tắt TEST đóng ngắt Relay GPIO 7");
    Serial.println("  [t]          : Bật/Tắt TEST đóng ngắt Relay GPIO 10 (Còi/Đèn)");
    Serial.println("  [?]          : Xem lại menu hướng dẫn này");
    Serial.println("========================================================\n");
}

// ============================================================================
// 5. THIẾT LẬP HỆ THỐNG & CHẨN ĐOÁN KHỞI ĐỘNG (SETUP)
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000); // Đợi ổn định cổng USB CDC trên ESP32-C3

    Serial.println("\n\n########################################################");
    Serial.println("      KIỂM TRA CHUYÊN SÂU CẢM BIẾN BIOSEC TM1026M       ");
    Serial.println("       (Silkscreen TZM1026_V1.0 - Chip BIOSEC TA0702)   ");
    Serial.println("########################################################");

    // Cấu hình chân ngắt chạm tay WAKEUP (ACTIVE HIGH: Chưa chạm = 0V, Chạm = 3.3V)
    pinMode(TZM_WAKE_PIN, INPUT_PULLDOWN);

    // Cấu hình chân Relay 2 (GPIO 7) và Relay 3 (GPIO 10) - Mặc định tắt (Active LOW)
    pinMode(RELAY_STARTER_PIN, OUTPUT);
    pinMode(RELAY_BUZZER_PIN, OUTPUT);
    digitalWrite(RELAY_STARTER_PIN, HIGH); // Mức HIGH = Tắt relay
    digitalWrite(RELAY_BUZZER_PIN, HIGH);  // Mức HIGH = Tắt relay
    Serial.println("🔌 [PHẦN CỨNG]: Đã sẵn sàng Relay GPIO 7 (Mở khóa) & GPIO 10 (Còi/Đèn)!");

    Serial.println("⏳ [BƯỚC 1]: Đang quét dò Baudrate & Kiểm tra Giao thức...");
    uint32_t bauds[] = {115200, 57600, 9600};
    bool foundF5 = false;
    bool foundSyno = false;
    uint32_t activeBaud = 115200;

    for (uint32_t b : bauds) {
        Serial.printf("   👉 Thử Baudrate: %d bps ... ", b);
        tzmSerial.begin(b, SERIAL_8N1, TZM_RX_PIN, TZM_TX_PIN);
        delay(80);

        // 1. Kiểm tra chuẩn SFM F5 của BIOSEC
        if (sfmHandshake(500)) {
            Serial.printf("THÀNH CÔNG! [Chuẩn BIOSEC SFM-V1.7 F5 @ %d]\n", b);
            foundF5 = true;
            activeBaud = b;
            break;
        }

        // 2. Thử bắt tay phòng trường hợp ROM tương thích Synochip/Grow
        while (tzmSerial.available()) tzmSerial.read();
        tzmSerial.write(synoProbe, sizeof(synoProbe));
        delay(80);
        if (tzmSerial.available() >= 9 && tzmSerial.peek() == 0xEF) {
            Serial.printf("THÀNH CÔNG! [Chuẩn Synochip/Grow @ %d]\n", b);
            foundSyno = true;
            activeBaud = b;
            break;
        }
        Serial.println("Không phản hồi.");
    }

    if (foundF5) {
        Serial.println("\n🎉 [KẾT NỐI UART THÀNH CÔNG] Đã bắt tay hoàn hảo với chip TA0702!");
        sfmBreak(); // Đưa cảm biến về trạng thái IDLE sẵn sàng nhận lệnh
        delay(50);
        int16_t total = sfmGetUserCount();
        if (total >= 0) {
            Serial.printf("📊 [DỮ LIỆU BỘ NHỚ] Hiện có %d / 225 mẫu vân tay đã lưu.\n", total);
        }
    } else if (foundSyno) {
        Serial.println("\nℹ️ [CHÚ Ý] Cảm biến này chạy firmware OEM tương thích Synochip (0xEF 0x01)!");
    } else {
        Serial.println("\n⚠️ [CẢNH BÁO: CHƯA THÔNG UART] Kiểm tra các bước sau:");
        Serial.println("   1. Đảo chéo 2 dây: GPIO 0 (RX) và GPIO 1 (TX).");
        Serial.println("   2. Đảm bảo cấp đủ 3.3V vào Chân 1 (V_TOUCH) và Chân 3 (VCC).");
        Serial.println("   3. Đảm bảo nối chung Mass GND giữa ESP32-C3 và module vân tay.");
    }

    Serial.println("\n👉 [BƯỚC 2]: Hãy CHẠM ĐẦU NGÓN TAY vào mặt cảm biến để test mạch WAKEUP!");
    Serial.printf("[Trạng thái ban đầu] WAKEUP (GPIO %d): 0V (Chờ) | Sẵn sàng nhận lệnh...\n", TZM_WAKE_PIN);
    printMenu();
}

// ============================================================================
// 6. VÒNG LẶP CHÍNH & XỬ LÝ SỰ KIỆN (LOOP)
// ============================================================================
bool lastWakeState = false;

void loop() {
    // 1. Theo dõi chân cảm ứng chạm ngón tay WAKEUP (Active HIGH: Chạm = 3.3V/HIGH)
    bool isTouched = (digitalRead(TZM_WAKE_PIN) == HIGH);
    if (isTouched != lastWakeState) {
        lastWakeState = isTouched;
        if (isTouched) {
            Serial.println("\n🖐️ [PHÁT HIỆN CHẠM TAY! Chân WAKEUP = 3.3V (HIGH)]");
            Serial.println("   => Cảm biến TZM1026 phản hồi cực nhạy! Đang kích hoạt so khớp 1:N...");
            int16_t id = sfmVerify1N(2500);
            if (id > 0) {
                triggerSuccessRelay(id);
            } else {
                triggerFailRelay();
            }
        } else {
            Serial.println("🖐️ [NHẤC TAY RA] Chân WAKEUP trở về 0V (LOW).");
        }
    }

    // 2. Tiếp nhận và xử lý lệnh từ bàn phím qua Serial Monitor
    if (Serial.available()) {
        char cmd = Serial.read();
        while (Serial.available()) Serial.read(); // Xóa sạch bộ đệm phím thừa

        if (cmd == '1' || cmd == 's' || cmd == 'S') {
            Serial.println("\n👉 [LỆNH SO KHỚP 1:N]: Vui lòng đặt ngón tay lên cảm biến...");
            int16_t id = sfmVerify1N(4000);
            if (id > 0) {
                triggerSuccessRelay(id);
            } else {
                triggerFailRelay();
            }
        } else if (cmd == '2' || cmd == 'e' || cmd == 'E') {
            sfmEnrollFlow();
        } else if (cmd == '8' || cmd == 'm' || cmd == 'M') {
            sfmEnrollNCNRFlow(5); // 5 lần lấy mẫu đa góc độ
        } else if (cmd == 'i' || cmd == 'I') {
            sfmCaptureAndExportBmp(); // Chụp và xuất ảnh BMP
        } else if (cmd == 'u' || cmd == 'U') {
            sfmListAllUsers(); // Danh bạ vân tay & Roles
        } else if (cmd == 'f' || cmd == 'F') {
            int16_t freeId = sfmGetNextFreeId();
            if (freeId > 0) {
                Serial.printf("\n🎯 [TÌM KIẾM TỰ ĐỘNG]: User ID trống đầu tiên có thể dùng là: #%d\n", freeId);
            } else {
                Serial.println("❌ Không tìm được User ID trống hoặc bộ nhớ đã đầy.");
            }
        } else if (cmd == 'b' || cmd == 'B') {
            Serial.println("\n👉 Chọn Baudrate UART cần cài đặt:");
            Serial.println("  [1] : 9600 bps");
            Serial.println("  [2] : 19200 bps");
            Serial.println("  [3] : 38400 bps");
            Serial.println("  [4] : 57600 bps");
            Serial.println("  [5] : 115200 bps (Khuyến nghị)");
            Serial.print("👉 Nhập số (1-5): ");
            while (!Serial.available()) { delay(10); }
            char bChar = Serial.read();
            Serial.println(bChar);
            if (bChar >= '1' && bChar <= '5') {
                uint8_t bId = bChar - '0';
                Serial.print("Bạn có muốn LƯU VĨNH VIỄN VÀO EEPROM? (Gõ 'Y' = Vĩnh viễn, 'N' = Tạm thời): ");
                while (!Serial.available()) { delay(10); }
                char perm = Serial.read();
                Serial.println(perm);
                bool isPerm = (perm == 'y' || perm == 'Y');
                if (sfmSetBaudrate(bId, isPerm)) {
                    Serial.printf("✔️ ĐÃ CÀI ĐẶT BAUDRATE THÀNH CÔNG (%s)!\n", isPerm ? "Lưu vĩnh viễn EEPROM" : "Tạm thời");
                } else {
                    Serial.println("❌ Cài đặt Baudrate thất bại.");
                }
            } else {
                Serial.println("⚠️ Lựa chọn không hợp lệ.");
            }
        } else if (cmd == '9' || cmd == 'l' || cmd == 'L') {
            Serial.println("\n👉 Chọn cấp độ bảo mật / độ nhạy so sánh:");
            Serial.println("  [0] : Cực nhạy (FAR 1/100,000 - Dễ nhận diện nhất, thích hợp góc lệch/ngón tay bẩn)");
            Serial.println("  [1] : Cân bằng (FAR 1/500,000 - Khuyến nghị cho xe máy)");
            Serial.println("  [2] : Nghiêm ngặt (FAR 1/1,000,000 - Mặc định nhà sản xuất)");
            Serial.print("👉 Hãy nhập mức (0, 1, 2): ");
            while (!Serial.available()) { delay(10); }
            char lvlChar = Serial.read();
            Serial.println(lvlChar);
            if (lvlChar >= '0' && lvlChar <= '2') {
                uint8_t lvl = lvlChar - '0';
                if (sfmSetSecurityLevel(lvl)) {
                    Serial.printf("✔️ ĐÃ CÀI ĐẶT THÀNH CÔNG: Độ nhạy so sánh = Cấp %d!\n", lvl);
                } else {
                    Serial.println("❌ Cài đặt cấp độ bảo mật thất bại.");
                }
            } else {
                Serial.println("⚠️ Cấp độ không hợp lệ.");
            }
        } else if (cmd == 'a' || cmd == 'A') {
            Serial.println("\nĐang gửi lệnh kích hoạt AI Tự học thích ứng (Self-Learning)...");
            if (sfmEnableSelfLearning()) {
                Serial.println("✔️ ĐÃ BẬT TỰ HỌC THÀNH CÔNG! Mỗi lần quét mở khóa đúng, chip TA0702 sẽ tự cập nhật thêm dữ liệu vân tay.");
            } else {
                Serial.println("❌ Bật tính năng tự học thất bại.");
            }
        } else if (cmd == '3' || cmd == 'c' || cmd == 'C') {
            int16_t count = sfmGetUserCount();
            if (count >= 0) {
                Serial.printf("📊 [BÁO CÁO BỘ NHỚ]: Đang lưu trữ %d / 225 mẫu vân tay.\n", count);
            } else {
                Serial.println("❌ Không đọc được số lượng vân tay.");
            }
        } else if (cmd == '4' || cmd == 'd' || cmd == 'D') {
            Serial.print("Nhập User ID cần xóa (1 - 225): ");
            while (!Serial.available()) { delay(10); }
            int idToDel = Serial.parseInt();
            Serial.println(idToDel);
            if (idToDel > 0 && idToDel <= 225) {
                if (sfmDeleteUser(idToDel)) {
                    Serial.printf("✔️ Đã xóa thành công User ID #%d khỏi Flash!\n", idToDel);
                } else {
                    Serial.printf("❌ Xóa thất bại hoặc User ID #%d không tồn tại.\n", idToDel);
                }
            } else {
                Serial.println("⚠️ ID không hợp lệ.");
            }
        } else if (cmd == '5' || cmd == 'k' || cmd == 'K') {
            Serial.println("\n⚠️ [CẢNH BÁO]: Bạn có chắc chắn muốn XÓA SẠCH toàn bộ vân tay? (Gõ 'Y' để xác nhận): ");
            while (!Serial.available()) { delay(10); }
            char confirm = Serial.read();
            if (confirm == 'y' || confirm == 'Y') {
                Serial.println("Đang xóa Flash...");
                if (sfmClearAll()) {
                    Serial.println("✔️ ĐÃ DỌN SẠCH TOÀN BỘ CƠ SỞ DỮ LIỆU VÂN TAY!");
                } else {
                    Serial.println("❌ Xóa toàn bộ thất bại.");
                }
            } else {
                Serial.println("Đã hủy thao tác xóa.");
            }
        } else if (cmd == 'v' || cmd == 'V') {
            sfmPrintVersion();
        } else if (cmd == '6' || cmd == 'x' || cmd == 'X') {
            enableHexDump = !enableHexDump;
            Serial.printf("ℹ️ Chế độ in Hex Packet UART: %s\n", enableHexDump ? "BẬT (ON)" : "TẮT (OFF)");
        } else if (cmd == '7' || cmd == 'z' || cmd == 'Z') {
            Serial.println("\n💤 [VÀO DEEP SLEEP TIẾT KIỆM PIN]: ESP32-C3 sẽ ngủ sâu ngay bây giờ.");
            Serial.println("   👉 Hãy CHẠM ĐẦU NGÓN TAY vào viền kim loại để ĐÁNH THỨC ESP32-C3!");
            Serial.flush();
            // Cấu hình ngắt đánh thức mức HIGH trên chân GPIO 3 (Active HIGH)
            esp_deep_sleep_enable_gpio_wakeup(1ULL << TZM_WAKE_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
            esp_deep_sleep_start();
        } else if (cmd == 'r' || cmd == 'R') {
            Serial.println("👉 [TEST THỦ CÔNG]: ĐÓNG Relay GPIO 7 trong 1.5 giây...");
            digitalWrite(RELAY_STARTER_PIN, LOW); // Đóng (Active LOW)
            delay(1500);
            digitalWrite(RELAY_STARTER_PIN, HIGH);// Nhả (Active LOW)
            Serial.println("✔️ Đã NHẢ Relay GPIO 7!");
        } else if (cmd == 't' || cmd == 'T') {
            Serial.println("👉 [TEST THỦ CÔNG]: BẬT Còi/Đèn Relay GPIO 10 trong 400ms...");
            digitalWrite(RELAY_BUZZER_PIN, LOW); // Bật (Active LOW)
            delay(400);
            digitalWrite(RELAY_BUZZER_PIN, HIGH);// Tắt (Active LOW)
            Serial.println("✔️ Đã TẮT Relay GPIO 10!");
        } else if (cmd == '?') {
            printMenu();
        }
    }

    delay(20);
}
