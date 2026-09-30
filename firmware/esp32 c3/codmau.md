/**
 * @file codmau.ino
 * @brief Chế độ mẫu kiểm tra & giao tiếp chuyên sâu cảm biến vân tay GROW R502 / R503 trên ESP32-C3
 * 
 * Tham khảo kiến trúc và giao thức từ 2 nguồn:
 * 1. mpagnoulle/R503-Fingerprint-Sensor-Library (ESP32 Arduino SDK, Aura LED RGB, quản lý bộ nhớ)
 *    https://github.com/mpagnoulle/R503-Fingerprint-Sensor-Library
 * 2. JasonFevang/R502-interface (ESP-IDF Component, cấu trúc gói tin R502_DataPkg_t, xử lý checksum, UpImage)
 *    https://github.com/JasonFevang/R502-interface
 * 
 * Sơ đồ chân nối ESP32-C3:
 * - Dây ĐỎ   (VCC)      -> Nguồn 3.3V
 * - Dây ĐEN  (GND)      -> Mass GND
 * - Dây VÀNG (TX cảm biến) -> Chân RX ESP32-C3 (GPIO 0)
 * - Dây XANH/NÂU (RX cảm biến) -> Chân TX ESP32-C3 (GPIO 1)
 * - Dây TRẮNG (Cảm ứng) -> Nguồn 3.3V
 * - Dây XANH DƯƠNG (WAKE) -> Chân ngắt chạm WAKE (GPIO 2, Active LOW: chạm = 0V)
 */

#include <Arduino.h>
#include <HardwareSerial.h>

// Cấu hình chân UART & WAKE cho ESP32-C3
#define R503_RX_PIN        0
#define R503_TX_PIN        1
#define R503_WAKE_PIN      2
#define R503_DEFAULT_BAUD  57600

// Cổng HardwareSerial UART1 độc lập giao tiếp với cảm biến (tránh đè UART0 USB CDC/Serial)
HardwareSerial r503Serial(1);

// ============================================================================
// 1. ĐỊNH NGHĨA GIAO THỨC GÓI TIN R502 / R503 (Tham khảo JasonFevang/R502-interface)
// ============================================================================

#define R503_START_CODE         0xEF01
#define R503_DEFAULT_ADDRESS    0xFFFFFFFF

// Package Identifiers (PID)
#define R503_PID_COMMAND        0x01  // Gói lệnh gửi từ MCU tới cảm biến
#define R503_PID_DATA           0x02  // Gói tin chứa khối dữ liệu (Data packet)
#define R503_PID_ACK            0x07  // Gói tin phản hồi trạng thái từ cảm biến (Acknowledge)
#define R503_PID_END_DATA       0x08  // Gói tin dữ liệu cuối cùng kết thúc luồng (End of data)

// Bảng mã lệnh Opcode (Instruction Codes)
#define R503_CMD_GET_IMAGE      0x01  // Lấy ảnh vân tay vào ImageBuffer
#define R503_CMD_IMAGE_2_TZ     0x02  // Sinh đặc trưng từ ImageBuffer lưu vào CharBuffer 1 hoặc 2
#define R503_CMD_MATCH          0x03  // So khớp đặc trưng giữa CharBuffer 1 và CharBuffer 2
#define R503_CMD_SEARCH         0x04  // Tìm kiếm đối soát nhanh trong thư viện Flash
#define R503_CMD_REG_MODEL      0x05  // Ghép đặc trưng (CharBuffer 1 + 2) tạo mẫu vân tay hoàn chỉnh
#define R503_CMD_STORE          0x06  // Lưu mẫu vân tay vào vị trí ID trong Flash ROM
#define R503_CMD_LOAD_CHAR      0x07  // Nạp mẫu vân tay từ Flash vào CharBuffer
#define R503_CMD_UP_CHAR        0x08  // Tải mẫu đặc trưng từ CharBuffer lên MCU
#define R503_CMD_DOWN_CHAR      0x09  // Tải mẫu đặc trưng từ MCU xuống CharBuffer
#define R503_CMD_UP_IMAGE       0x0A  // Tải toàn bộ ảnh quang học lăng kính từ ImageBuffer lên MCU
#define R503_CMD_DELETE         0x0C  // Xóa mẫu vân tay theo ID
#define R503_CMD_EMPTY          0x0D  // Xóa sạch toàn bộ thư viện vân tay trong cảm biến
#define R503_CMD_READ_SYS_PARA  0x0F  // Đọc tham số hệ thống cảm biến
#define R503_CMD_VFY_PWD        0x13  // Xác thực mật khẩu bắt tay cảm biến
#define R503_CMD_TEMPLATE_COUNT 0x1D  // Đọc số lượng vân tay hợp lệ hiện có
#define R503_CMD_AURA_LED       0x35  // Điều khiển đèn LED nhẫn hào quang (Aura LED)

// Các chế độ Aura LED (Tham khảo mpagnoulle/R503-Fingerprint-Sensor-Library)
enum AuraLedMode {
    LED_MODE_BREATHING = 0x01,  // Chế độ thở êm dịu (Breathing)
    LED_MODE_FLASHING  = 0x02,  // Chế độ nhấp nháy xác nhận/cảnh báo (Flashing)
    LED_MODE_ON        = 0x03,  // Chế độ bật sáng liên tục (Always ON)
    LED_MODE_OFF       = 0x04,  // Chế độ tắt đèn (Always OFF)
    LED_MODE_GRADUAL_ON= 0x05,  // Sáng dần
    LED_MODE_GRADUAL_OFF=0x06   // Tắt dần
};

enum AuraLedColor {
    LED_COLOR_RED      = 0x01,  // Màu Đỏ (Báo lỗi / Cảnh báo)
    LED_COLOR_BLUE     = 0x02,  // Màu Xanh Dương (Sẵn sàng / Thở)
    LED_COLOR_PURPLE   = 0x03   // Màu Tím (Đang đăng ký / Chờ quét)
};

// ============================================================================
// 2. HÀM TIỆN ÍCH DỊCH MÃ PHẢN HỒI (CONFIRMATION CODE DECODER)
// ============================================================================

const char* r503GetStatusString(uint8_t code) {
    switch (code) {
        case 0x00: return "0x00 [OK]: Lệnh thực thi thành công hoàn hảo";
        case 0x01: return "0x01 [PACKET_ERR]: Lỗi nhận gói tin UART (Checksum hoặc Header sai)";
        case 0x02: return "0x02 [NO_FINGER]: Không phát hiện ngón tay trên mặt cảm biến";
        case 0x03: return "0x03 [FAIL_IMAGE]: Chụp ảnh vân tay quang học thất bại";
        case 0x04: return "0x04 [TOO_DRY]: Ảnh vân tay quá khô hoặc mờ";
        case 0x05: return "0x05 [TOO_WET]: Ảnh vân tay quá ướt hoặc đọng nước";
        case 0x06: return "0x06 [DISORDER]: Ảnh quá nhiễu, không thể trích xuất đặc trưng";
        case 0x07: return "0x07 [FEAT_FAIL]: Diện tích tiếp xúc quá nhỏ, thiếu điểm đặc trưng";
        case 0x08: return "0x08 [NOT_MATCH]: Hai mẫu vân tay không khớp với nhau";
        case 0x09: return "0x09 [NOT_FOUND]: Không tìm thấy mẫu trùng khớp trong thư viện R503";
        case 0x0A: return "0x0A [MERGE_FAIL]: Ghép các lần chạm thất bại (ngón tay di chuyển lệch)";
        case 0x0B: return "0x0B [ADDR_OVER]: ID vị trí lưu trữ vượt quá dung lượng Flash";
        case 0x0C: return "0x0C [READ_ERR]: Lỗi đọc dữ liệu mẫu từ bộ nhớ Flash R503";
        case 0x0D: return "0x0D [UP_ERR]: Lỗi truyền tải mẫu đặc trưng lên MCU";
        case 0x0E: return "0x0E [RECV_ERR]: Cảm biến không nhận được gói dữ liệu tiếp theo";
        case 0x0F: return "0x0F [UP_IMG_ERR]: Lỗi truyền tải ảnh quang học từ ImageBuffer";
        case 0x10: return "0x10 [DEL_ERR]: Lỗi xóa mẫu vân tay trong bộ nhớ";
        case 0x11: return "0x11 [CLEAR_ERR]: Lỗi dọn sạch toàn bộ cơ sở dữ liệu Flash";
        case 0x13: return "0x13 [PWD_ERR]: Mật khẩu xác thực bắt tay cảm biến bị sai";
        case 0x15: return "0x15 [IMG_INVALID]: Bộ đệm ImageBuffer rỗng (chưa chụp hoặc bị lệnh đèn xóa)";
        case 0x18: return "0x18 [FLASH_ERR]: Lỗi truy xuất phần cứng bộ nhớ Flash R503";
        default:   return "0xFF [UNKNOWN]: Mã trạng thái chưa định nghĩa";
    }
}

// In chuỗi byte hex ra Serial để quan sát chuẩn UART
void printHexBytes(const uint8_t *data, size_t len, const char *prefix = "") {
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

// Đọc 1 byte với timeout microsecond (tránh trễ FreeRTOS)
bool readUartByte(uint8_t *b, unsigned long timeoutMs = 1000) {
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

// Tìm cặp Header 0xEF 0x01 để đồng bộ byte chống lệch pha
bool syncUartHeader(unsigned long timeoutMs = 2000) {
    unsigned long start = millis();
    uint8_t prev = 0;
    while (millis() - start < timeoutMs) {
        uint8_t cur = 0;
        if (readUartByte(&cur, 100)) {
            if (prev == 0xEF && cur == 0x01) {
                return true;
            }
            prev = cur;
        }
    }
    return false;
}

// Đọc chính xác N bytes từ UART
bool readUartBytes(uint8_t *buf, size_t len, unsigned long timeoutMs = 1000) {
    for (size_t i = 0; i < len; i++) {
        if (!readUartByte(&buf[i], timeoutMs)) return false;
    }
    return true;
}

// Gửi một gói tin lệnh chuẩn R502/R503
bool sendCommandPacket(uint8_t cmdCode, const uint8_t *payload, uint16_t payloadLen) {
    // Độ dài gói tin data = cmdCode (1 byte) + payloadLen + Checksum (2 bytes)
    uint16_t packetLength = 1 + payloadLen + 2;
    uint8_t txBuf[32];
    
    txBuf[0] = 0xEF; // Header
    txBuf[1] = 0x01;
    txBuf[2] = 0xFF; // Device Address
    txBuf[3] = 0xFF;
    txBuf[4] = 0xFF;
    txBuf[5] = 0xFF;
    txBuf[6] = R503_PID_COMMAND; // PID
    txBuf[7] = (uint8_t)(packetLength >> 8);
    txBuf[8] = (uint8_t)(packetLength & 0xFF);
    txBuf[9] = cmdCode;

    uint16_t sum = R503_PID_COMMAND + (packetLength >> 8) + (packetLength & 0xFF) + cmdCode;

    for (uint16_t i = 0; i < payloadLen; i++) {
        txBuf[10 + i] = payload[i];
        sum += payload[i];
    }

    uint16_t chkOffset = 10 + payloadLen;
    txBuf[chkOffset] = (uint8_t)(sum >> 8);
    txBuf[chkOffset + 1] = (uint8_t)(sum & 0xFF);

    size_t totalSend = chkOffset + 2;

    while (r503Serial.available()) r503Serial.read(); // Dọn sạch rác RX trước khi phát
    r503Serial.write(txBuf, totalSend);
    r503Serial.flush();

    printHexBytes(txBuf, totalSend, "📡 [R503 UART TX] -> ");
    return true;
}

// Đọc phản hồi ACK từ cảm biến
bool receiveAckPacket(uint8_t &confirmCode, uint8_t *outData = nullptr, uint16_t *outDataLen = nullptr, unsigned long timeoutMs = 1500) {
    if (!syncUartHeader(timeoutMs)) {
        Serial.println("❌ [R503 UART RX] Lỗi: Không tìm thấy Header 0xEF 0x01 phản hồi!");
        return false;
    }

    uint8_t hdr[7]; // Addr (4) + PID (1) + Len (2)
    if (!readUartBytes(hdr, 7, timeoutMs)) {
        Serial.println("❌ [R503 UART RX] Lỗi: Hết thời gian đọc Header gói phản hồi!");
        return false;
    }

    uint8_t pid = hdr[4];
    uint16_t len = ((uint16_t)hdr[5] << 8) | hdr[6];

    if (pid != R503_PID_ACK || len < 3) {
        Serial.printf("⚠️ [R503 UART RX] Gói tin không phải ACK chuẩn (PID=0x%02X, Len=%d)\n", pid, len);
        return false;
    }

    uint8_t ackPayload[64];
    if (!readUartBytes(ackPayload, len, timeoutMs)) {
        Serial.println("❌ [R503 UART RX] Lỗi: Không đọc đủ nội dung gói tin phản hồi!");
        return false;
    }

    confirmCode = ackPayload[0]; // Byte đầu tiên của payload chính là Confirmation Code!

    // In gói tin phản hồi ra Serial
    Serial.printf("📥 [R503 UART RX] <- Mã phản hồi: %s\n", r503GetStatusString(confirmCode));

    if (outData && outDataLen && len > 3) {
        uint16_t dataBytes = len - 3; // Trừ mã code (1) và Checksum (2)
        memcpy(outData, ackPayload + 1, dataBytes);
        *outDataLen = dataBytes;
    }

    return (confirmCode == 0x00);
}

// ============================================================================
// 4. CÁC HÀM ĐIỀU KHIỂN CHỨC NĂNG CẢM BIẾN R502 / R503
// ============================================================================

// 1. Xác thực mật khẩu bắt tay cảm biến (Opcode 0x13)
bool r503VerifyPassword(uint32_t password = 0x00000000) {
    uint8_t payload[4];
    payload[0] = (uint8_t)(password >> 24);
    payload[1] = (uint8_t)(password >> 16);
    payload[2] = (uint8_t)(password >> 8);
    payload[3] = (uint8_t)(password & 0xFF);

    Serial.println("\n--- [1] BẮT TAY XÁC THỰC MẬT KHẨU (0x13) ---");
    sendCommandPacket(R503_CMD_VFY_PWD, payload, 4);

    uint8_t code = 0xFF;
    return receiveAckPacket(code);
}

// 2. Điều khiển Aura LED RGB nhẫn phát sáng (Opcode 0x35)
bool r503SetAuraLed(uint8_t mode, uint8_t color, uint8_t speed = 50, uint8_t count = 1) {
    uint8_t payload[4];
    payload[0] = mode;
    payload[1] = speed;
    payload[2] = color;
    payload[3] = count;

    Serial.printf("\n--- [2] ĐIỀU KHIỂN AURA LED (0x35): Mode=0x%02X, Color=0x%02X, Speed=%d, Count=%d ---\n",
                  mode, color, speed, count);
    sendCommandPacket(R503_CMD_AURA_LED, payload, 4);

    uint8_t code = 0xFF;
    return receiveAckPacket(code);
}

// 3. Đọc số lượng vân tay đã lưu trong cảm biến (Opcode 0x1D)
int r503GetTemplateCount() {
    Serial.println("\n--- [3] ĐỌC SỐ LƯỢNG VÂN TAY TRONG CẢM BIẾN (0x1D) ---");
    sendCommandPacket(R503_CMD_TEMPLATE_COUNT, nullptr, 0);

    uint8_t code = 0xFF;
    uint8_t data[8];
    uint16_t len = 0;
    if (receiveAckPacket(code, data, &len) && len >= 2) {
        uint16_t count = ((uint16_t)data[0] << 8) | data[1];
        Serial.printf("📊 Số lượng vân tay hợp lệ trong Flash: %d mẫu\n", count);
        return count;
    }
    return -1;
}

// 4. Lấy ảnh vân tay vào ImageBuffer (Opcode 0x01)
uint8_t r503GetImage() {
    sendCommandPacket(R503_CMD_GET_IMAGE, nullptr, 0);
    uint8_t code = 0xFF;
    receiveAckPacket(code, nullptr, nullptr, 800);
    return code;
}

// 5. Sinh đặc trưng từ ImageBuffer vào CharBuffer 1 hoặc 2 (Opcode 0x02)
uint8_t r503Image2Tz(uint8_t bufferSlot = 1) {
    uint8_t payload[1] = { bufferSlot };
    sendCommandPacket(R503_CMD_IMAGE_2_TZ, payload, 1);
    uint8_t code = 0xFF;
    receiveAckPacket(code, nullptr, nullptr, 1000);
    return code;
}

// 6. Tìm kiếm đối soát nhanh trong thư viện (Opcode 0x04)
bool r503FastSearch(uint8_t bufferSlot, uint16_t startPage, uint16_t pageNum, uint16_t &matchedId, uint16_t &confidence) {
    uint8_t payload[5];
    payload[0] = bufferSlot;
    payload[1] = (uint8_t)(startPage >> 8);
    payload[2] = (uint8_t)(startPage & 0xFF);
    payload[3] = (uint8_t)(pageNum >> 8);
    payload[4] = (uint8_t)(pageNum & 0xFF);

    Serial.println("\n--- [4] TÌM KIẾM ĐỐI SOÁT NHANH (0x04) ---");
    sendCommandPacket(R503_CMD_SEARCH, payload, 5);

    uint8_t code = 0xFF;
    uint8_t data[8];
    uint16_t len = 0;
    if (receiveAckPacket(code, data, &len) && len >= 4) {
        matchedId = ((uint16_t)data[0] << 8) | data[1];
        confidence = ((uint16_t)data[2] << 8) | data[3];
        Serial.printf("🎯 KHỚP THÀNH CÔNG -> ID: #%d | Điểm tin cậy (Confidence): %d\n", matchedId, confidence);
        return true;
    }
    return false;
}

// 7. Xóa mẫu vân tay theo ID (Opcode 0x0C)
bool r503DeleteModel(uint16_t id, uint16_t count = 1) {
    uint8_t payload[4];
    payload[0] = (uint8_t)(id >> 8);
    payload[1] = (uint8_t)(id & 0xFF);
    payload[2] = (uint8_t)(count >> 8);
    payload[3] = (uint8_t)(count & 0xFF);

    Serial.printf("\n--- [5] XÓA MẪU VÂN TAY ID #%d (0x0C) ---\n", id);
    sendCommandPacket(R503_CMD_DELETE, payload, 4);

    uint8_t code = 0xFF;
    return receiveAckPacket(code);
}

// 8. Xóa sạch toàn bộ thư viện vân tay (Opcode 0x0D)
bool r503EmptyDatabase() {
    Serial.println("\n--- [6] XÓA SẠCH TOÀN BỘ THƯ VIỆN FLASH (0x0D) ---");
    sendCommandPacket(R503_CMD_EMPTY, nullptr, 0);

    uint8_t code = 0xFF;
    return receiveAckPacket(code);
}

// 9. Tải ảnh quang học lăng kính thực tế (Opcode 0x0A) - Kiến trúc 2 pha chuẩn
bool r503StreamRawImage() {
    Serial.println("\n--- [7] TRÍCH XUẤT ẢNH QUANG HỌC THỰC TẾ (0x0A) ---");
    
    // Gửi lệnh UpImage (0x0A)
    sendCommandPacket(R503_CMD_UP_IMAGE, nullptr, 0);

    // Chờ gói phản hồi ACK
    uint8_t code = 0xFF;
    if (!receiveAckPacket(code, nullptr, nullptr, 2500) || code != 0x00) {
        Serial.println("❌ Cảm biến từ chối truyền ảnh (ImageBuffer rỗng hoặc lỗi)!");
        return false;
    }

    Serial.println("📥 Cảm biến đã đồng ý! Đang nhận các gói Data Packets (PID: 0x02 / 0x08)...");
    
    size_t totalBytes = 0;
    bool finished = false;
    unsigned long start = millis();

    while (!finished && (millis() - start < 8000)) {
        if (!syncUartHeader(1500)) break;

        uint8_t hdr[7];
        if (!readUartBytes(hdr, 7, 800)) break;

        uint8_t pid = hdr[4]; // 0x02: Data, 0x08: End Data
        uint16_t len = ((uint16_t)hdr[5] << 8) | hdr[6];
        if (len < 2) continue;

        uint16_t payloadLen = len - 2; // trừ 2 byte checksum
        uint8_t dummy[256];
        readUartBytes(dummy, payloadLen, 800);

        // Đọc 2 byte checksum
        uint8_t chk[2];
        readUartBytes(chk, 2, 500);

        totalBytes += payloadLen;
        if (pid == R503_PID_END_DATA) {
            finished = true;
            break;
        }
    }

    Serial.printf("✅ Đã nhận trọn vẹn: %u bytes ảnh quang học trong %lums!\n", totalBytes, millis() - start);
    return finished;
}

// ============================================================================
// 5. VÒNG LẶP KIỂM TRA TƯƠNG TÁC SERIAL MENU (INTERACTIVE CONSOLE)
// ============================================================================

void printMenu() {
    Serial.println("\n==============================================================");
    Serial.println("   BỘ LỆNH KIỂM TRA CẢM BIẾN VÂN TAY R502 / R503 (CONSOLE)    ");
    Serial.println("==============================================================");
    Serial.println("  [1] Xác thực mật khẩu & Bắt tay (0x13)");
    Serial.println("  [2] Bật đèn Aura LED Xanh (Thở nhẹ)");
    Serial.println("  [3] Bật đèn Aura LED Đỏ (Nhấp nháy báo động)");
    Serial.println("  [4] Tắt đèn Aura LED");
    Serial.println("  [5] Đọc số lượng vân tay đang lưu (0x1D)");
    Serial.println("  [6] Chụp ảnh & Đối soát nhanh ngón tay vừa chạm (0x01 -> 0x04)");
    Serial.println("  [7] Trích xuất ảnh quang học lăng kính (0x0A)");
    Serial.println("  [8] Xóa toàn bộ bộ nhớ vân tay (0x0D)");
    Serial.println("==============================================================");
    Serial.print("👉 Nhập số lựa chọn (1-8): ");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n╔════════════════════════════════════════════════════════════╗");
    Serial.println("║   TSMARTKEY - R502 / R503 ADVANCED INTERFACE TEST SUITE    ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");

    // Khởi tạo chân WAKE ngắt chạm
    pinMode(R503_WAKE_PIN, INPUT_PULLUP);

    // Khởi tạo UART phần cứng với RX buffer 24KB
    r503Serial.setRxBufferSize(24576);
    r503Serial.begin(R503_DEFAULT_BAUD, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);

    Serial.printf("📡 Đã mở HardwareSerial Baudrate %d (RX: GPIO %d, TX: GPIO %d)\n", 
                  R503_DEFAULT_BAUD, R503_RX_PIN, R503_TX_PIN);

    // Kiểm tra kết nối
    if (r503VerifyPassword(0x00000000)) {
        Serial.println("✅ CẢM BIẾN R503 PHẢN HỒI CHUẨN XÁC! SẴN SÀNG HOẠT ĐỘNG.");
        r503SetAuraLed(LED_MODE_BREATHING, LED_COLOR_BLUE, 150, 1);
        r503GetTemplateCount();
    } else {
        Serial.println("⚠️ Cảnh báo: Không nhận được phản hồi từ cảm biến R503. Kiểm tra dây RX/TX!");
    }

    printMenu();
}

void loop() {
    // 1. Tự động kiểm tra chạm cảm biến qua chân ngắt WAKE
    if (digitalRead(R503_WAKE_PIN) == LOW) {
        Serial.println("\n🖐️ [WAKE DETECTED] Phát hiện ngón tay chạm cảm biến! Bắt đầu kiểm tra...");
        delay(40); // Ổn định bề mặt chạm

        uint8_t p = r503GetImage();
        if (p == 0x00) {
            Serial.println("📸 Đã chụp thành công ảnh vân tay!");
            p = r503Image2Tz(1);
            if (p == 0x00) {
                uint16_t id = 0, conf = 0;
                if (r503FastSearch(1, 1, 200, id, conf)) {
                    r503SetAuraLed(LED_MODE_FLASHING, LED_COLOR_BLUE, 40, 2);
                } else {
                    r503SetAuraLed(LED_MODE_FLASHING, LED_COLOR_RED, 40, 2);
                }
            }
        }
        
        // Chờ nhấc ngón tay ra
        while (digitalRead(R503_WAKE_PIN) == LOW) delay(30);
        delay(300);
        r503SetAuraLed(LED_MODE_BREATHING, LED_COLOR_BLUE, 150, 1);
        printMenu();
    }

    // 2. Xử lý lệnh nhập từ Serial Monitor
    if (Serial.available()) {
        char ch = Serial.read();
        while (Serial.available()) Serial.read(); // Bỏ qua ký tự xuống dòng thừa

        switch (ch) {
            case '1':
                r503VerifyPassword(0x00000000);
                break;
            case '2':
                r503SetAuraLed(LED_MODE_BREATHING, LED_COLOR_BLUE, 120, 0);
                break;
            case '3':
                r503SetAuraLed(LED_MODE_FLASHING, LED_COLOR_RED, 40, 3);
                break;
            case '4':
                r503SetAuraLed(LED_MODE_OFF, 0, 0, 0);
                break;
            case '5':
                r503GetTemplateCount();
                break;
            case '6':
                Serial.println("\n👉 Hãy chạm ngón tay lên cảm biến R503 ngay bây giờ...");
                {
                    unsigned long startWait = millis();
                    bool touched = false;
                    while (millis() - startWait < 8000) {
                        if (r503GetImage() == 0x00) {
                            touched = true;
                            break;
                        }
                        delay(50);
                    }
                    if (touched) {
                        r503Image2Tz(1);
                        uint16_t id = 0, conf = 0;
                        r503FastSearch(1, 1, 200, id, conf);
                    } else {
                        Serial.println("⏱️ Hết thời gian chờ chạm ngón tay!");
                    }
                }
                break;
            case '7':
                Serial.println("\n👉 Chạm ngón tay lên cảm biến để chụp và đọc ảnh...");
                {
                    unsigned long startWait = millis();
                    bool touched = false;
                    while (millis() - startWait < 8000) {
                        if (r503GetImage() == 0x00) {
                            touched = true;
                            break;
                        }
                        delay(50);
                    }
                    if (touched) {
                        r503StreamRawImage();
                    } else {
                        Serial.println("⏱️ Hết thời gian chờ chạm ngón tay!");
                    }
                }
                break;
            case '8':
                Serial.println("\n⚠️ BẠN CÓ CHẮC MUỐN XÓA TOÀN BỘ VÂN TAY TRONG R503? (Gõ 'y' để xác nhận)");
                while (!Serial.available()) delay(10);
                if (Serial.read() == 'y') {
                    r503EmptyDatabase();
                } else {
                    Serial.println("❌ Đã hủy thao tác xóa.");
                }
                break;
            default:
                break;
        }
        printMenu();
    }
}
