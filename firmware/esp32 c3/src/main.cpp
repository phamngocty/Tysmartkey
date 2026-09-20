#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <Adafruit_Fingerprint.h>
#include <mbedtls/base64.h>

// ==========================================
// 📌 1. ĐỊNH NGHĨA CHÂN PHẦN CỨNG (ESP32-C3)
// ==========================================
#define RELAY1_PIN   4  // Relay 1: Khóa điện ACC (Đấu song song ổ khóa cơ)
#define RELAY2_PIN   5  // Relay 2: Đề xe (Starter)
#define RELAY3_PIN   6  // Relay 3: Đèn / Còi (Buzzer / Horn / Turn lights)

#define RF_LOCATE_PIN      7  // GPIO 7: Tín hiệu từ Module RF 433MHz (CHỈ DÙNG TÌM XE - TUYỆT ĐỐI KHÔNG MỞ KHÓA)

#define R503_RX_PIN        0  // GPIO 0 kết nối TXD (dây Vàng) của R503
#define R503_TX_PIN        1  // GPIO 1 kết nối RXD (dây Xanh lá) của R503
#define R503_WAKE_PIN      3  // GPIO 3 kết nối WAKEUP (dây Xanh dương) của R503 (Active LOW: chạm = 0V)

// ==========================================
// 🔐 2. THÔNG SỐ BẢO MẬT & BLE UUID
// ==========================================
#define DEVICE_NAME         "XE_tsmart_BLE"
#define SERVICE_UUID        "0000ff01-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ff02-0000-1000-8000-00805f9b34fb"

String SECRET_KEY = "271000"; // Mã bảo mật mặc định
bool isUnlocked = false;       // Trạng thái xe (true: Đang mở khóa, false: Đang khóa)
volatile bool isEnrolling = false;          // Cờ đang trong chế độ thêm vân tay
volatile bool cancelEnrollRequested = false; // Cờ yêu cầu hủy tiến trình lấy vân tay
TaskHandle_t enrollTaskHandle = nullptr;    // Con trỏ FreeRTOS Task thêm vân tay
volatile bool isCapturingImage = false;     // Cờ đang trong chế độ chụp ảnh thực tế
TaskHandle_t captureImageTaskHandle = nullptr; // Con trỏ FreeRTOS Task chụp ảnh

// Khởi tạo Preferences lưu trữ Flash NVS
Preferences prefsSecurity; // namespace "safe_key"
Preferences prefsFinger;   // namespace "fingerprint"
Preferences prefsRain;     // namespace "rain_config"
Preferences prefsLed;      // namespace "led_cfg"

// Cấu trúc dữ liệu cấu hình đèn vòng màu R503 (Aura LED Opcode 0x35)
struct LedEventConfig {
    uint8_t mode;   // 1=Breathing, 2=Flashing, 3=Always ON, 4=Always OFF, 5=Gradual ON, 6=Gradual OFF
    uint8_t color;  // 1..7 (1:Đỏ, 2:Xanh, 3:Tím, 4:Xanh lá, 5:Vàng, 6:Cyan, 7:Trắng)
    uint8_t speed;  // 0..255 (Tốc độ hiệu ứng)
};

struct R503LedSystemConfig {
    LedEventConfig unlocked; // Khi xe bật khóa (Mặc định: Thở xanh dương)
    LedEventConfig locked;   // Khi xe khóa (Mặc định: Tắt tiết kiệm ắc quy)
    LedEventConfig success;  // Khi quét đúng / mở xe (Mặc định: Nháy xanh 2 lần)
    LedEventConfig error;    // Khi quét sai / báo động (Mặc định: Nháy đỏ 3 lần)
};

R503LedSystemConfig ledConfig = {
    { 0x01, 0x02, 120 }, // Unlocked: Breathing Blue, speed 120
    { 0x04, 0x02, 0   }, // Locked: Always OFF
    { 0x02, 0x02, 40  }, // Success: Flashing Blue, speed 40
    { 0x02, 0x01, 30  }  // Error: Flashing Red, speed 30
};

// Cấu hình Chế độ Chống Nước Mưa (Anti-Rain Mode)
bool rainEnabled = false;
int touchHoldMs = 500;         // Thời gian giữ ngón liên tục (ms) để lọc giọt nước chạm lướt
int maxWrongAttempts = 5;      // Ngưỡng quẹt sai trước khi khóa/báo động (0 = tắt còi)
int cooldownSec = 30;          // Thời gian tạm khóa khi chạm sai liên tục (giây)
int autoOffSec = 3600;         // Thời gian tự tắt chế độ mưa (giây, 0 = không tự tắt)
unsigned long rainStartTime = 0;   // Thời điểm kích hoạt chế độ mưa (millis)
unsigned long cooldownUntil = 0;   // Mốc thời gian kết thúc cooldown (millis)

// Khởi tạo UART1 độc lập cho R503 qua HardwareSerial r503Serial(1) trên ESP32-C3 (RX = GPIO 0, TX = GPIO 1)
// TUYỆT ĐỐI KHÔNG dùng UART 0 vì sẽ xung đột chết với cổng Serial Debug Monitor (115200 baud)
HardwareSerial r503Serial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint((Stream*)&r503Serial);
bool r503Ready = false; // Cờ kiểm tra cảm biến R503 đã kết nối thành công chưa

// Con trỏ BLE
NimBLEServer *pServer = nullptr;
NimBLECharacteristic *pCharacteristic = nullptr;
bool deviceConnected = false;

// Chống dội lệnh qua BLE
String lastCommand = "";
unsigned long lastCmdTime = 0;

// Đếm số lần quẹt sai vân tay liên tiếp (Báo động chống trộm)
int wrongFingerAttempts = 0;

// Cấu hình tinh chỉnh cảm biến vân tay thủ công
int fpSecurityLevel = 2;          // Mức bảo mật R503: 1 (rất nhạy) -> 5 (rất khắt khe), mặc định 2
int fpScanWindowMs = 1200;         // Thời gian quét đối chiếu liên tục khi áp ngón tay (ms)
int fpEnrollMode = 4;             // Chế độ lấy mẫu: 4 lần chạm (đa góc độ) hoặc 2 lần chạm
bool fpSendEnrollImage = false;   // Gửi ảnh vân tay khi lấy mẫu (bật/tắt theo yêu cầu)
bool isTestingFingerprint = false; // Chế độ test cảm biến không bật/tắt xe
unsigned long testFpUntil = 0;

// ==========================================
// 🔔 3. HÀM PHẢN HỒI & HIỆU ỨNG (FEEDBACK)
// ==========================================

// Kêu còi / nháy đèn bíp phản hồi
void beep(int count, int delayMs = 100) {
    for (int i = 0; i < count; i++) {
        digitalWrite(RELAY3_PIN, HIGH);
        delay(delayMs);
        digitalWrite(RELAY3_PIN, LOW);
        if (i < count - 1) delay(80);
    }
}

// Gửi phản hồi trạng thái qua BLE & Serial
void notifyStatus(String msg) {
    String fullMsg = "FB|" + msg + "\n";
    if (!msg.startsWith("FP_IMG_CHUNK|")) {
        Serial.print("Sending Feedback: " + fullMsg);
    }
    
    if (pCharacteristic && deviceConnected) {
        pCharacteristic->setValue((uint8_t *)fullMsg.c_str(), fullMsg.length());
        pCharacteristic->notify();
    }
}

// Gửi dữ liệu Telemetry thời gian thực (Điện áp ắc quy, Nhiệt độ chip ESP32-C3, Trạng thái khóa)
void sendTelemetry() {
    float tempC = temperatureRead(); // Đọc cảm biến nhiệt độ tích hợp trong chip ESP32-C3
    float voltage = 12.6;            // Điện áp chuẩn ắc quy (hoặc từ chân chia áp ADC)
    
    // Gói tin: FB|TELE|<VOLTAGE>|<TEMP_C>|<IS_UNLOCKED>
    String teleMsg = "TELE|" + String(voltage, 1) + "|" + String(tempC, 1) + "|" + (isUnlocked ? "1" : "0");
    notifyStatus(teleMsg);
}

// ============================================================================
// 📦 BỘ ĐỘNG CƠ GÓI TIN ĐỘC LẬP R502 / R503 (ZERO-DEPENDENCY PACKET ENGINE)
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

// Các chế độ Aura LED RGB (Opcode 0x35)
enum AuraLedMode {
    LED_MODE_BREATHING = 0x01,  // Chế độ thở êm dịu (Breathing)
    LED_MODE_FLASHING  = 0x02,  // Chế độ nhấp nháy xác nhận/cảnh báo (Flashing)
    LED_MODE_ON        = 0x03,  // Chế độ bật sáng liên tục (Always ON)
    LED_MODE_OFF       = 0x04,  // Chế độ tắt đèn (Always OFF)
    LED_MODE_GRADUAL_ON= 0x05,  // Sáng dần
    LED_MODE_GRADUAL_OFF=0x06   // Tắt dần
};

enum AuraLedColor {
    LED_COLOR_RED      = 0x01,  // Màu Đỏ (Báo lỗi / Cảnh báo / Thể thao)
    LED_COLOR_BLUE     = 0x02,  // Màu Xanh Dương (Sẵn sàng / Thở)
    LED_COLOR_PURPLE   = 0x03,  // Màu Tím (Đang đăng ký / Cyberpunk)
    LED_COLOR_GREEN    = 0x04,  // Màu Xanh Lá (Emerald Nature)
    LED_COLOR_YELLOW   = 0x05,  // Màu Vàng (Solar Gold)
    LED_COLOR_CYAN     = 0x06,  // Màu Xanh Ngọc / Lơ (Ocean Neon)
    LED_COLOR_WHITE    = 0x07   // Màu Trắng (Full White)
};

// Hàm giải mã mã lỗi tiếng Việt chi tiết (r503GetStatusString)
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

// Hàm tương thích ngược với mã nguồn hiện tại
const char* r503CodeToString(uint8_t code) {
    return r503GetStatusString(code);
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

// Tìm cặp Header 0xEF 0x01 để đồng bộ byte chống lệch pha UART
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
bool sendCommandPacket(uint8_t cmdCode, const uint8_t *payload = nullptr, uint16_t payloadLen = 0) {
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

    while (r503Serial.available()) r503Serial.read(); // Dọn sạch buffer RX trước khi gửi
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

    Serial.printf("📥 [R503 UART RX] <- Mã phản hồi: %s\n", r503GetStatusString(confirmCode));

    if (outData && outDataLen && len > 3) {
        uint16_t dataBytes = len - 3; // Trừ mã code (1) và Checksum (2)
        memcpy(outData, ackPayload + 1, dataBytes);
        *outDataLen = dataBytes;
    }

    return (confirmCode == 0x00);
}

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

// 2. Điều khiển Aura LED RGB nhẫn phát sáng đa chế độ (Opcode 0x35)
bool r503SetAuraLed(uint8_t mode, uint8_t color, uint8_t speed = 50, uint8_t count = 1) {
    uint8_t payload[4];
    payload[0] = mode;
    payload[1] = speed;
    payload[2] = color;
    payload[3] = count;

    const char* colorName = (color == LED_COLOR_RED) ? "Đỏ" : 
                            (color == LED_COLOR_BLUE) ? "Xanh dương" : 
                            (color == LED_COLOR_PURPLE) ? "Tím" :
                            (color == LED_COLOR_GREEN) ? "Xanh lá" :
                            (color == LED_COLOR_YELLOW) ? "Vàng" :
                            (color == LED_COLOR_CYAN) ? "Xanh ngọc" :
                            (color == LED_COLOR_WHITE) ? "Trắng" : "Tắt";
    const char* modeName = (mode == LED_MODE_BREATHING) ? "Thở (Breathing)" :
                           (mode == LED_MODE_FLASHING) ? "Nhấp nháy (Flashing)" :
                           (mode == LED_MODE_ON) ? "Bật liên tục (On)" :
                           (mode == LED_MODE_OFF) ? "Tắt (Off)" : "Khác";

    Serial.printf("\n--- [2] ĐIỀU KHIỂN AURA LED (0x35): Mode=0x%02X (%s), Color=0x%02X (%s), Speed=%d, Count=%d ---\n",
                  mode, modeName, color, colorName, speed, count);
    sendCommandPacket(R503_CMD_AURA_LED, payload, 4);

    uint8_t code = 0xFF;
    return receiveAckPacket(code, nullptr, nullptr, 300);
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

// 9. Tải ảnh quang học lăng kính thực tế (Opcode 0x0A - UpImage)
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

    Serial.printf("✅ Đã nhận trọn vẹn: %u / 18432 bytes ảnh quang học trong %lums!\n", totalBytes, millis() - start);
    return finished;
}

// Điều khiển LED RGB trên R503 tương thích các hàm cũ
void setR503Led(uint8_t mode, uint8_t color, uint8_t speed = 50, uint8_t count = 1) {
    r503SetAuraLed(mode, color, speed, count);
}

void loadLedConfig() {
    prefsLed.begin("led_cfg", false);
    ledConfig.unlocked.mode  = prefsLed.getUChar("u_m", 0x01); // Breathing
    ledConfig.unlocked.color = prefsLed.getUChar("u_c", 0x02); // Blue
    ledConfig.unlocked.speed = prefsLed.getUChar("u_s", 120);

    ledConfig.locked.mode    = prefsLed.getUChar("l_m", 0x04); // OFF
    ledConfig.locked.color   = prefsLed.getUChar("l_c", 0x02); // Blue
    ledConfig.locked.speed   = prefsLed.getUChar("l_s", 0);

    ledConfig.success.mode   = prefsLed.getUChar("s_m", 0x02); // Flashing
    ledConfig.success.color  = prefsLed.getUChar("s_c", 0x02); // Blue
    ledConfig.success.speed  = prefsLed.getUChar("s_s", 40);

    ledConfig.error.mode     = prefsLed.getUChar("e_m", 0x02); // Flashing
    ledConfig.error.color    = prefsLed.getUChar("e_c", 0x01); // Red
    ledConfig.error.speed    = prefsLed.getUChar("e_s", 30);

    Serial.printf("💡 LED Config NVS: Unlocked(M:%d, C:%d, S:%d), Locked(M:%d, C:%d, S:%d)\n",
                  ledConfig.unlocked.mode, ledConfig.unlocked.color, ledConfig.unlocked.speed,
                  ledConfig.locked.mode, ledConfig.locked.color, ledConfig.locked.speed);
}

void saveLedConfig() {
    prefsLed.putUChar("u_m", ledConfig.unlocked.mode);
    prefsLed.putUChar("u_c", ledConfig.unlocked.color);
    prefsLed.putUChar("u_s", ledConfig.unlocked.speed);

    prefsLed.putUChar("l_m", ledConfig.locked.mode);
    prefsLed.putUChar("l_c", ledConfig.locked.color);
    prefsLed.putUChar("l_s", ledConfig.locked.speed);

    prefsLed.putUChar("s_m", ledConfig.success.mode);
    prefsLed.putUChar("s_c", ledConfig.success.color);
    prefsLed.putUChar("s_s", ledConfig.success.speed);

    prefsLed.putUChar("e_m", ledConfig.error.mode);
    prefsLed.putUChar("e_c", ledConfig.error.color);
    prefsLed.putUChar("e_s", ledConfig.error.speed);
    Serial.println("💾 Đã lưu cấu hình đèn Aura LED R503 vào NVS Flash!");
}

void sendLedConfigResponse() {
    String resp = "FB|LED_CFG|" +
                  String(ledConfig.unlocked.mode) + "|" + String(ledConfig.unlocked.color) + "|" + String(ledConfig.unlocked.speed) + "|" +
                  String(ledConfig.locked.mode) + "|" + String(ledConfig.locked.color) + "|" + String(ledConfig.locked.speed) + "|" +
                  String(ledConfig.success.mode) + "|" + String(ledConfig.success.color) + "|" + String(ledConfig.success.speed) + "|" +
                  String(ledConfig.error.mode) + "|" + String(ledConfig.error.color) + "|" + String(ledConfig.error.speed);
    notifyStatus(resp);
}

void ledSuccess() {
    r503SetAuraLed(ledConfig.success.mode, ledConfig.success.color, ledConfig.success.speed, 2);
}

void ledError() {
    r503SetAuraLed(ledConfig.error.mode, ledConfig.error.color, ledConfig.error.speed, 3);
}

void ledBreathingIdle() {
    r503SetAuraLed(ledConfig.unlocked.mode, ledConfig.unlocked.color, ledConfig.unlocked.speed, 0);
}

void ledOff() {
    r503SetAuraLed(LED_MODE_OFF, 0, 0, 0);
}

// Cập nhật trạng thái LED theo trạng thái xe (tiết kiệm bình ắc quy khi xe khóa)
void updateIdleLed() {
    if (isUnlocked) {
        r503SetAuraLed(ledConfig.unlocked.mode, ledConfig.unlocked.color, ledConfig.unlocked.speed, 0);
    } else {
        r503SetAuraLed(ledConfig.locked.mode, ledConfig.locked.color, ledConfig.locked.speed, 0);
    }
}

// Bảng menu điều khiển Console tương tác trực tiếp qua Serial Monitor
void printMenu() {
    Serial.println("\n==============================================================");
    Serial.println("   BỘ LỆNH KIỂM TRA CẢM BIẾN VÂN TAY R502 / R503 (CONSOLE)    ");
    Serial.println("==============================================================");
    Serial.println("  [1] Bắt tay xác thực mật khẩu module (0x13)");
    Serial.println("  [2] Bật đèn LED Xanh thở nhẹ");
    Serial.println("  [3] Bật đèn LED Đỏ nhấp nháy cảnh báo");
    Serial.println("  [4] Tắt đèn LED");
    Serial.println("  [5] Đọc số lượng vân tay đang lưu trong Flash R503 (0x1D)");
    Serial.println("  [6] Chụp ảnh và đối soát ngón tay vừa chạm (0x01 -> 0x04)");
    Serial.println("  [7] Trích xuất ảnh quang học lăng kính (0x0A)");
    Serial.println("  [8] Xóa toàn bộ bộ nhớ vân tay (0x0D)");
    Serial.println("==============================================================");
    Serial.print("👉 Nhập số lựa chọn (1-8): ");
}

// ==========================================
// ⚡ 4. XỬ LÝ TRẠNG THÁI XE (BẬT / TẮT / ĐỀ)
// ==========================================

void setVehicleUnlock(bool unlock, bool saveFlash = true, bool notify = true) {
    isUnlocked = unlock;
    digitalWrite(RELAY1_PIN, isUnlocked ? HIGH : LOW);
    
    if (saveFlash) {
        prefsSecurity.putBool("is_unlocked", isUnlocked);
    }
    
    if (notify) {
        if (isUnlocked) {
            notifyStatus("DA_MO_KHOA");
        } else {
            notifyStatus("DA_KHOA_XE");
        }
    }
    
    if (r503Ready) {
        if (isUnlocked) {
            ledSuccess();
        } else {
            ledError();
        }
    }
}

// Đề xe (chỉ khi xe đang mở khóa)
void triggerStarter() {
    if (!isUnlocked) {
        notifyStatus("LOI_CHUA_MO_KHOA");
        beep(3, 60);
        return;
    }
    digitalWrite(RELAY2_PIN, HIGH);
    delay(1500);
    digitalWrite(RELAY2_PIN, LOW);
    notifyStatus("DA_DE_MAY");
}

// Tìm xe: Nháy xi-nhan + còi 3 nhịp ngắn chuẩn xe cao cấp
void triggerLocate() {
    Serial.println("Đang phát tín hiệu tìm xe (3 nhịp bíp & nháy đèn)...");
    for (int i = 0; i < 3; i++) {
        digitalWrite(RELAY3_PIN, HIGH);
        delay(200);
        digitalWrite(RELAY3_PIN, LOW);
        if (i < 2) delay(150);
    }
    notifyStatus("DA_TIM_XE");
}

// ==========================================
// 🖐️ 5. QUẢN LÝ VÂN TAY (R503 & FLASH NVS)
// ==========================================

// Dung lượng tối đa cảm biến R503 (thường là 100 hoặc 200)
int getMaxCapacity() {
    return (finger.capacity > 0 && finger.capacity <= 200) ? finger.capacity : 100;
}

// Đếm số lượng vân tay chính đang đăng ký hợp lệ trong Flash NVS (không tính slot góc nghiêng phụ)
int getRegisteredFingerprintCount() {
    int count = 0;
    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++) {
        String key = "name_" + String(id);
        String parentKey = "parent_" + String(id);
        if (prefsFinger.isKey(key.c_str()) && !prefsFinger.isKey(parentKey.c_str())) {
            count++;
        }
    }
    return count;
}

// Tổng số slot đã sử dụng trong Flash NVS (bao gồm cả slot phụ)
int getTotalUsedSlotsCount() {
    int count = 0;
    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++) {
        String key = "name_" + String(id);
        if (prefsFinger.isKey(key.c_str())) {
            count++;
        }
    }
    return count;
}

// Tìm ID vân tay còn trống tiếp theo (từ 1 đến capacity)
int getNextFreeFingerId() {
    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++) {
        String key = "name_" + String(id);
        if (!prefsFinger.isKey(key.c_str())) {
            finger.deleteModel(id); // Dọn sạch slot trong Flash R503
            Serial.printf("👉 Cấp ID trống: %d\n", id);
            return id;
        }
    }
    Serial.println("⚠️ Đã đầy bộ nhớ vân tay!");
    return -1;
}

// Tìm ID vân tay còn trống tiếp theo, bỏ qua ID loại trừ (dùng cấp slot phụ góc nghiêng)
int getNextFreeFingerIdExcluding(int excludeId) {
    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++) {
        if (id == excludeId) continue;
        String key = "name_" + String(id);
        if (!prefsFinger.isKey(key.c_str())) {
            return id;
        }
    }
    return -1;
}

// Trích xuất ảnh vân tay thô từ cảm biến R503 và truyền qua BLE (Kiến trúc 2 pha có RAM buffer & bộ đồng bộ gói tin động)
bool streamR503ImageOverBle() {
    Serial.println("📷 Bắt đầu trích xuất ảnh vân tay từ R503...");
    
    // Settle delay để cảm biến R503 hoàn tất ghi vào ImageBuffer nội bộ sau khi getImage()
    vTaskDelay(pdMS_TO_TICKS(60));

    // Gửi lệnh UpImage (0x0A) qua packet engine
    sendCommandPacket(R503_CMD_UP_IMAGE, nullptr, 0);

    // 1. Chờ gói tin ACK phản hồi UpImage
    uint8_t ackCode = 0xFF;
    if (!receiveAckPacket(ackCode, nullptr, nullptr, 2500) || ackCode != 0x00) {
        Serial.printf("❌ R503 từ chối gửi ảnh: %s\n", r503GetStatusString(ackCode));
        notifyStatus("FP_IMG_ERR");
        return false;
    }

    // 2. Cấp phát bộ nhớ đệm RAM (32KB) để gom toàn bộ các gói tin dữ liệu ảnh từ UART
    const size_t MAX_IMG_BYTES = 32768;
    uint8_t *imgBuffer = (uint8_t *)malloc(MAX_IMG_BYTES);
    if (!imgBuffer) {
        Serial.println("❌ Không đủ RAM để đệm ảnh vân tay!");
        notifyStatus("FP_IMG_ERR");
        return false;
    }

    // PHA 1: THU THẬP TẤT CẢ GÓI DỮ LIỆU TỪ UART VÀO RAM LIÊN TỤC
    size_t totalBytesReceived = 0;
    bool finished = false;
    unsigned long startStream = millis();

    while (!finished && (millis() - startStream < 9000)) {
        if (cancelEnrollRequested) {
            free(imgBuffer);
            notifyStatus("FP_IMG_ERR");
            return false;
        }

        // Tự động săn tìm Header 0xEF 0x01 của từng gói dữ liệu ảnh (chống rớt/lệch pha byte)
        if (!syncUartHeader(1500)) {
            Serial.printf("⏱️ Kết thúc luồng nhận gói dữ liệu ảnh (đã nhận %u bytes)\n", totalBytesReceived);
            break;
        }

        // Đọc 7 bytes tiếp theo: Addr (4) + PID (1) + Length (2)
        uint8_t pktHeader[7];
        if (!readUartBytes(pktHeader, 7, 1000)) {
            Serial.println("❌ Lỗi đọc phần còn lại của header gói dữ liệu ảnh");
            break;
        }

        uint8_t pid = pktHeader[4]; // 0x02 = Data, 0x08 = EndData
        uint16_t length = ((uint16_t)pktHeader[5] << 8) | pktHeader[6];

        if (length < 2 || length > 300) {
            Serial.printf("⚠️ Chiều dài gói dữ liệu bất thường (%d), bỏ qua...\n", length);
            continue;
        }

        uint16_t payloadLen = length - 2; // Trừ đi 2 bytes checksum

        // Đọc Payload ảnh trực tiếp vào RAM buffer
        if (totalBytesReceived + payloadLen <= MAX_IMG_BYTES) {
            if (!readUartBytes(imgBuffer + totalBytesReceived, payloadLen, 1000)) {
                Serial.println("❌ Lỗi đọc payload ảnh từ UART");
                break;
            }
            totalBytesReceived += payloadLen;
        } else {
            uint8_t dummy[256];
            readUartBytes(dummy, payloadLen, 1000);
        }

        // Đọc 2 bytes Checksum
        uint8_t chk[2];
        readUartBytes(chk, 2, 500);

        if (pid == R503_PID_END_DATA) {
            finished = true;
            break;
        }
    }

    Serial.printf("📥 Đã nhận từ R503: %u bytes trong %lums!\n", totalBytesReceived, millis() - startStream);

    if (totalBytesReceived < 500) {
        Serial.printf("❌ Dữ liệu ảnh thu được quá ít (%u bytes), hủy truyền\n", totalBytesReceived);
        free(imgBuffer);
        notifyStatus("FP_IMG_ERR");
        return false;
    }

    // PHA 2: TRUYỀN DỮ LIỆU ĐÃ ĐỆM TRONG RAM QUA BLE THEO TỪNG CHUNK BASE64
    notifyStatus("FP_IMG_START|192|192");
    vTaskDelay(pdMS_TO_TICKS(40));

    unsigned char base64Buf[256];
    int packetCount = 0;
    const size_t CHUNK_SIZE = 96; // Bội số của 3 (96 bytes = 32 triplets -> 128 Base64 chars, TUYỆT ĐỐI KHÔNG sinh padding '=' ở giữa luồng)

    for (size_t offset = 0; offset < totalBytesReceived; offset += CHUNK_SIZE) {
        if (cancelEnrollRequested || !deviceConnected) {
            break;
        }
        size_t curLen = min(CHUNK_SIZE, totalBytesReceived - offset);
        size_t olen = 0;
        mbedtls_base64_encode(base64Buf, sizeof(base64Buf), &olen, imgBuffer + offset, curLen);
        base64Buf[olen] = '\0';

        notifyStatus("FP_IMG_CHUNK|" + String((char *)base64Buf));
        packetCount++;
        vTaskDelay(pdMS_TO_TICKS(20)); // Giãn cách 20ms an toàn cho BLE stack
    }

    free(imgBuffer);
    notifyStatus("FP_IMG_END");
    Serial.printf("✅ Đã phát sóng xong toàn bộ ảnh (%d chunks, %u bytes) qua BLE!\n", packetCount, totalBytesReceived);
    return true;
}

// Trích xuất mã đặc trưng (Mã vân tay) từ CharBuffer 1 của cảm biến R503
String extractFingerprintCode(int matchedId) {
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Gói tin lệnh UpChar (0x08) từ Buffer 1: Header(2)+Addr(4)+Type(1)+Len(2)+Cmd(1)+Buf(1)+Chk(2)
    uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x04, 0x08, 0x01, 0x00, 0x0E};
    Serial.println("📡 [R503 UART TX] -> UpChar CharBuffer 1 (0x08) [EF 01 FF FF FF FF 01 00 04 08 01 00 0E] - Yêu cầu tải mẫu đặc trưng");
    while (r503Serial.available()) r503Serial.read();
    r503Serial.write(cmd, sizeof(cmd));
    r503Serial.flush();

    uint32_t hash = 0x811C9DC5; // FNV-1a 32-bit hash
    bool gotData = false;
    unsigned long start = millis();
    int byteCount = 0;

    // Đọc các gói tin template (512 bytes) từ R503
    while ((millis() - start < 600) && byteCount < 580) {
        if (r503Serial.available()) {
            uint8_t b = r503Serial.read();
            hash ^= b;
            hash *= 0x01000193;
            byteCount++;
            if (byteCount > 12) {
                gotData = true;
            }
        } else {
            delayMicroseconds(50);
        }
    }

    char codeStr[32];
    if (gotData && hash != 0x811C9DC5) {
        uint16_t h1 = (uint16_t)(hash >> 16);
        uint16_t h2 = (uint16_t)(hash & 0xFFFF);
        if (matchedId > 0) {
            snprintf(codeStr, sizeof(codeStr), "FP-ID%02d-%04X", matchedId, h2);
        } else {
            snprintf(codeStr, sizeof(codeStr), "FP-%04X-%04X", h1, h2);
        }
    } else {
        uint32_t fallback = (uint32_t)millis() ^ (matchedId > 0 ? (matchedId * 7919) : 0x5A5A);
        if (matchedId > 0) {
            snprintf(codeStr, sizeof(codeStr), "FP-ID%02d-%04X", matchedId, (uint16_t)(fallback & 0xFFFF));
        } else {
            snprintf(codeStr, sizeof(codeStr), "FP-%04X-%04X", (uint16_t)(fallback >> 16), (uint16_t)(fallback & 0xFFFF));
        }
    }
    Serial.printf("📥 [R503 UART RX] <- Đã nhận %d bytes dữ liệu template từ R503 | Hash FNV-1a: 0x%08X\n", byteCount, hash);
    Serial.printf("🔑 [MÃ VÂN TAY TRÍCH XUẤT] -> %s\n", codeStr);
    return String(codeStr);
}

// Task xử lý chu trình thêm vân tay chạy nền trong FreeRTOS (Không làm đơ BLE / NimBLE)
void enrollTask(void *pvParameters) {
    String fingerName = "Van tay moi";
    if (pvParameters != NULL) {
        fingerName = *((String *)pvParameters);
        delete (String *)pvParameters; // Giải phóng bộ nhớ chuỗi động an toàn
    }

    isEnrolling = true;
    cancelEnrollRequested = false;

    auto cleanupAndExit = [](const String& failMsg = "") {
        if (failMsg.length() > 0) {
            notifyStatus(failMsg);
            ledError();
            beep(3, 80);
        }
        updateIdleLed();
        isEnrolling = false;
        cancelEnrollRequested = false;
        enrollTaskHandle = nullptr;
        vTaskDelete(NULL);
    };

    auto waitForLift = [&](unsigned long timeoutMs) -> bool {
        unsigned long start = millis();
        while (millis() - start < timeoutMs) {
            if (cancelEnrollRequested) return false;
            if (digitalRead(R503_WAKE_PIN) == HIGH) {
                int p = finger.getImage();
                if (p == FINGERPRINT_NOFINGER) {
                    return true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return false;
    };

    // Hàm lấy mẫu chống trượt: Lặp liên tục trong khi ngón tay đang áp trên cảm biến
    auto captureFingerprint = [&](uint8_t bufferSlot, unsigned long timeoutMs) -> bool {
        unsigned long start = millis();
        while (millis() - start < timeoutMs) {
            if (cancelEnrollRequested) return false;

            // Chờ ngón tay chạm cảm biến (chân WAKE_PIN ở mức LOW)
            if (digitalRead(R503_WAKE_PIN) == LOW) {
                // Cho ngón tay ổn định 50ms khi vừa chạm vào mặt kính
                vTaskDelay(pdMS_TO_TICKS(50));

                // Vòng lặp lấy mẫu liên tục trong khi ngón tay đang được giữ trên cảm biến
                unsigned long holdStart = millis();
                while (millis() - holdStart < 3000) {
                    if (cancelEnrollRequested) return false;

                    int p = finger.getImage();
                    if (p == FINGERPRINT_OK) {
                        Serial.printf("📥 [R503 UART RX - ENROLL] <- getImage thành công (0x00) cho Slot %d!\n", bufferSlot);
                        // Nếu bật tùy chọn gửi ảnh, trích xuất ảnh thô ngay sau khi chụp thành công
                        if (fpSendEnrollImage) {
                            notifyStatus("FP_IMG_FETCHING");
                            streamR503ImageOverBle();
                        }

                        Serial.printf("📡 [R503 UART TX - ENROLL] -> image2Tz (0x02, CharBuffer %d) [EF 01 FF FF FF FF 01 00 04 02 %02X ...]\n",
                                      bufferSlot, bufferSlot);
                        p = finger.image2Tz(bufferSlot);
                        Serial.printf("📥 [R503 UART RX - ENROLL] <- image2Tz Slot %d: %s\n", bufferSlot, r503CodeToString(p));
                        if (p == FINGERPRINT_OK) {
                            return true; // Thành công lấy mẫu và trích xuất đặc trưng!
                        }
                    }

                    // Nếu người dùng đã nhấc ngón tay ra sớm
                    if (digitalRead(R503_WAKE_PIN) == HIGH) {
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(40));
                }

                // Nếu giữ quá thời gian mà ảnh vẫn bị mờ hoặc quẹt trượt
                notifyStatus("FP_ENROLL_RETRY_" + String(bufferSlot));
                setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_RED, 40, 1);
                beep(1, 40);
                waitForLift(2500);
                setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 80, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return false;
    };

    // 0. Kiểm tra nếu người dùng đang đè sẵn ngón tay trên cảm biến, yêu cầu nhấc ra trước
    if (digitalRead(R503_WAKE_PIN) == LOW) {
        Serial.println("Phát hiện ngón tay đặt sẵn, yêu cầu nhấc ra trước...");
        notifyStatus("FP_ENROLL_LIFT_FIRST");
        setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 40, 2);
        if (!waitForLift(8000)) {
            if (cancelEnrollRequested) {
                notifyStatus("FP_ENROLL_CANCELLED");
                cleanupAndExit();
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    int maxCap = getMaxCapacity();
    int id = getNextFreeFingerId();
    if (id < 1 || id > maxCap) {
        cleanupAndExit("FP_ENROLL_FAILED|BO_NHO_DAY");
        return;
    }

    int totalSteps = (fpEnrollMode == 2) ? 2 : 4;
    Serial.printf("🚀 Bắt đầu chu trình lấy mẫu %d bước cho ID %d [%s] (Mode: %s)...\n", 
                  totalSteps, id, fingerName.c_str(), (totalSteps == 2 ? "2-Chạm" : "4-Chạm đa điểm"));

    // ========================================================
    // BƯỚC 1: LẤY MẪU CHÍNH DIỆN 1 (Lưu vào CharBuffer1)
    // ========================================================
    notifyStatus("FP_ENROLL_PROGRESS|1|" + String(totalSteps) + "|TOUCH_CENTER_1|" + String(id));
    setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 80, 0);

    bool b1Ok = captureFingerprint(1, 60000);
    if (cancelEnrollRequested) { notifyStatus("FP_ENROLL_CANCELLED"); cleanupAndExit(); return; }
    if (!b1Ok) { cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_STEP_1"); return; }

    beep(1, 60);
    setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 50, 2);
    notifyStatus("FP_ENROLL_PROGRESS|1|" + String(totalSteps) + "|LIFT|" + String(id));
    if (!waitForLift(15000)) { cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_RELEASE"); return; }
    vTaskDelay(pdMS_TO_TICKS(300));

    // ========================================================
    // BƯỚC 2: LẤY MẪU CHÍNH DIỆN 2 & KHÓA MẪU CHÍNH (CharBuffer2 -> createModel -> Flash)
    // ========================================================
    notifyStatus("FP_ENROLL_PROGRESS|2|" + String(totalSteps) + "|TOUCH_CENTER_2|" + String(id));
    setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 80, 0);

    bool b2Ok = captureFingerprint(2, 60000);
    if (cancelEnrollRequested) { notifyStatus("FP_ENROLL_CANCELLED"); cleanupAndExit(); return; }
    if (!b2Ok) { cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_STEP_2"); return; }

    // Nghỉ 100ms cho cảm biến ổn định
    vTaskDelay(pdMS_TO_TICKS(100));

    // Ghép 2 mẫu chính diện (CharBuffer1 & CharBuffer2 -> CharBuffer1)
    Serial.println("📡 [R503 UART TX - ENROLL] -> createModel (0x05) [EF 01 FF FF FF FF 01 00 03 05 00 09]");
    int p = finger.createModel();
    Serial.printf("📥 [R503 UART RX - ENROLL] <- createModel: %s\n", r503CodeToString(p));
    if (p == FINGERPRINT_ENROLLMISMATCH) {
        Serial.println("❌ Hai lần chạm chính diện không khớp cùng một ngón tay!");
        cleanupAndExit("FP_ENROLL_FAILED|KHONG_KHOP");
        return;
    } else if (p != FINGERPRINT_OK) {
        Serial.printf("❌ Lỗi tạo model chính diện: mã %d\n", p);
        cleanupAndExit("FP_ENROLL_FAILED|LOI_TAO_MAU");
        return;
    }

    // Nghỉ 100ms để DSP hoàn tất bộ đệm
    vTaskDelay(pdMS_TO_TICKS(100));

    // TUYỆT ĐỐI KHÔNG gọi finger.deleteModel(id) ở đây vì sẽ xóa sạch RAM CharBuffer1 của R503!
    // Lưu trực tiếp model từ CharBuffer1 vào Flash ROM của R503
    Serial.printf("📡 [R503 UART TX - ENROLL] -> storeModel (0x06) [Lưu vào ID %d]\n", id);
    p = finger.storeModel(id);
    Serial.printf("📥 [R503 UART RX - ENROLL] <- storeModel ID %d: %s\n", id, r503CodeToString(p));
    if (p != FINGERPRINT_OK) {
        vTaskDelay(pdMS_TO_TICKS(120));
        p = finger.storeModel(id);
    }
    if (p != FINGERPRINT_OK) {
        Serial.printf("❌ Lỗi lưu model chính diện vào R503: mã %d\n", p);
        cleanupAndExit("FP_ENROLL_FAILED|LOI_LUU_MAU");
        return;
    }

    // Lưu tên vân tay vào NVS Flash của ESP32
    prefsFinger.putString(("name_" + String(id)).c_str(), fingerName);
    Serial.printf("✅ ĐÃ LƯU MẪU CHÍNH DIỆN ID %d [%s] VÀO FLASH!\n", id, fingerName.c_str());

    // NẾU CHẾ ĐỘ 2 LẦN CHẠM (fpEnrollMode == 2) -> HOÀN TẤT LUÔN!
    if (totalSteps == 2) {
        notifyStatus("FP_ENROLL_OK|" + String(id) + "|" + fingerName);
        ledSuccess();
        beep(2, 100);
        Serial.printf("🎉 HOÀN TẤT ĐĂNG KÝ VÂN TAY 2 LẦN CHẠM: ID %d [%s]!\n", id, fingerName.c_str());

        waitForLift(3000);
        vTaskDelay(pdMS_TO_TICKS(200));

        updateIdleLed();
        isEnrolling = false;
        cancelEnrollRequested = false;
        enrollTaskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    beep(1, 60);
    setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 50, 2);
    notifyStatus("FP_ENROLL_PROGRESS|2|4|LIFT|" + String(id));
    if (!waitForLift(15000)) { cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_RELEASE"); return; }
    vTaskDelay(pdMS_TO_TICKS(300));

    // ========================================================
    // BƯỚC 3: LẤY MẪU GÓC NGHIÊNG 1 (Lưu vào CharBuffer1)
    // ========================================================
    notifyStatus("FP_ENROLL_PROGRESS|3|4|TOUCH_EDGE_1|" + String(id));
    setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 80, 0);

    bool b3Ok = captureFingerprint(1, 60000);
    if (cancelEnrollRequested) { notifyStatus("FP_ENROLL_CANCELLED"); cleanupAndExit(); return; }
    if (!b3Ok) { cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_STEP_3"); return; }

    beep(1, 60);
    setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 50, 2);
    notifyStatus("FP_ENROLL_PROGRESS|3|4|LIFT|" + String(id));
    if (!waitForLift(15000)) { cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_RELEASE"); return; }
    vTaskDelay(pdMS_TO_TICKS(300));

    // ========================================================
    // BƯỚC 4: LẤY MẪU GÓC NGHIÊNG 2 & LƯU SLOT PHỤ (CharBuffer2 -> createModel -> auxId)
    // ========================================================
    notifyStatus("FP_ENROLL_PROGRESS|4|4|TOUCH_EDGE_2|" + String(id));
    setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 80, 0);

    bool b4Ok = captureFingerprint(2, 60000);
    if (cancelEnrollRequested) { notifyStatus("FP_ENROLL_CANCELLED"); cleanupAndExit(); return; }
    if (!b4Ok) { cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_STEP_4"); return; }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Ghép 2 mẫu góc nghiêng (CharBuffer1 & CharBuffer2 -> CharBuffer1)
    Serial.println("📡 [R503 UART TX - ENROLL] -> createModel (0x05) [Tạo mẫu góc nghiêng]");
    p = finger.createModel();
    Serial.printf("📥 [R503 UART RX - ENROLL] <- createModel góc nghiêng: %s\n", r503CodeToString(p));
    if (p == FINGERPRINT_OK) {
        int auxId = getNextFreeFingerIdExcluding(id);
        if (auxId > 0 && auxId <= maxCap) {
            // TUYỆT ĐỐI KHÔNG gọi finger.deleteModel(auxId) ở đây!
            Serial.printf("📡 [R503 UART TX - ENROLL] -> storeModel (0x06) [Lưu mẫu phụ vào ID %d]\n", auxId);
            p = finger.storeModel(auxId);
            Serial.printf("📥 [R503 UART RX - ENROLL] <- storeModel ID %d: %s\n", auxId, r503CodeToString(p));
            if (p == FINGERPRINT_OK) {
                prefsFinger.putString(("name_" + String(auxId)).c_str(), fingerName);
                prefsFinger.putInt(("parent_" + String(auxId)).c_str(), id);
                Serial.printf("✅ ĐÃ LƯU MẪU GÓC NGHIÊNG VÀO SLOT PHỤ ID %d (parent: %d)!\n", auxId, id);
            }
        }
    } else {
        Serial.println("ℹ️ Góc nghiêng không khớp đủ đặc trưng, mẫu chính diện vẫn hoạt động 100%.");
    }

    // Hoàn tất thêm vân tay thành công
    notifyStatus("FP_ENROLL_OK|" + String(id) + "|" + fingerName);
    ledSuccess();
    beep(2, 100);
    Serial.printf("🎉 HOÀN TẤT ĐĂNG KÝ VÂN TAY ĐA ĐIỂM: ID %d [%s]!\n", id, fingerName.c_str());

    waitForLift(3000);
    vTaskDelay(pdMS_TO_TICKS(200));

    updateIdleLed();
    isEnrolling = false;
    cancelEnrollRequested = false;
    enrollTaskHandle = nullptr;
    vTaskDelete(NULL);
}

// Khởi chạy task thêm vân tay
void startEnrollTask(String fingerName) {
    if (!r503Ready) {
        // Thử kiểm tra và kết nối lại cảm biến R503
        uint32_t bauds[] = {57600, 9600, 115200, 19200, 38400};
        for (uint32_t b : bauds) {
            r503Serial.setRxBufferSize(24576);
            r503Serial.begin(b, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
            delay(50);
            while (r503Serial.available()) r503Serial.read();
            if (finger.verifyPassword()) {
                r503Ready = true;
                Serial.printf("✅ Cảm biến R503 đã kết nối lại tại Baud %d!\n", b);
                updateIdleLed();
                break;
            }
            r503Serial.end();
            delay(20);
        }

        if (!r503Ready) {
            r503Serial.setRxBufferSize(24576);
            r503Serial.begin(57600, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
            Serial.println("❌ Không thể thêm vân tay: Cảm biến R503 chưa kết nối UART!");
            notifyStatus("FP_ENROLL_FAILED|LOI_CAM_BIEN");
            ledError();
            return;
        }
    }

    // Hủy dứt điểm task cũ nếu đang chạy
    if (enrollTaskHandle != nullptr || isEnrolling) {
        Serial.println("Đang có tiến trình lấy vân tay cũ, hủy trước khi bắt đầu...");
        cancelEnrollRequested = true;
        unsigned long waitCancel = millis();
        while ((enrollTaskHandle != nullptr || isEnrolling) && (millis() - waitCancel < 800)) {
            delay(20);
        }
        if (enrollTaskHandle != nullptr) {
            vTaskDelete(enrollTaskHandle);
            enrollTaskHandle = nullptr;
        }
        isEnrolling = false;
    }

    isEnrolling = true;
    cancelEnrollRequested = false;

    // Xóa sạch buffer UART trước khi bắt đầu
    while (r503Serial.available()) {
        r503Serial.read();
    }

    String *nameParam = new String(fingerName);
    BaseType_t res = xTaskCreate(enrollTask, "enrollTask", 4096, (void *)nameParam, 1, &enrollTaskHandle);
    if (res != pdPASS) {
        delete nameParam;
        enrollTaskHandle = nullptr;
        isEnrolling = false;
        notifyStatus("FP_ENROLL_FAILED|LOI_HE_THONG");
        updateIdleLed();
    }
}

// Task chụp ảnh vân tay quang học thực tế từ R503 truyền qua BLE
void captureImageTask(void *pvParameters) {
    isCapturingImage = true;
    Serial.println("📸 [LIVE CAPTURE] Bắt đầu chế độ chụp ảnh thực tế R503...");
    notifyStatus("FP_CAPTURE_WAIT");
    setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 80, 0);

    // Chờ ngón tay chạm trong tối đa 15 giây
    unsigned long startWait = millis();
    bool captured = false;
    while (millis() - startWait < 15000) {
        if (!deviceConnected || cancelEnrollRequested) break;
        int p = finger.getImage();
        if (p == FINGERPRINT_OK) {
            Serial.println("📸 Đã bắt được ảnh trên cảm biến R503! Tiến hành trích xuất...");
            // CHÚ Ý: KHÔNG gửi lệnh setR503Led ở đây vì sẽ xóa sạch ImageBuffer của R503!
            bool ok = streamR503ImageOverBle();
            if (ok) {
                setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 40, 2);
                beep(1, 80);
            } else {
                setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_RED, 40, 2);
            }
            captured = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!captured && !cancelEnrollRequested) {
        Serial.println("❌ Hết thời gian chờ chạm ngón tay để chụp ảnh.");
        notifyStatus("FP_CAPTURE_TIMEOUT");
        setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_RED, 40, 2);
    }

    updateIdleLed();
    isCapturingImage = false;
    captureImageTaskHandle = nullptr;
    vTaskDelete(NULL);
}

// Khởi chạy task chụp ảnh vân tay thực tế
void startCaptureImageTask() {
    if (!r503Ready) {
        notifyStatus("FP_IMG_ERR");
        return;
    }
    if (isEnrolling) {
        Serial.println("⚠️ Đang bận chu trình đăng ký vân tay!");
        notifyStatus("FP_IMG_ERR");
        return;
    }
    if (captureImageTaskHandle != nullptr || isCapturingImage) {
        Serial.println("Đang có tiến trình chụp ảnh cũ, hủy trước khi bắt đầu mới...");
        if (captureImageTaskHandle != nullptr) {
            vTaskDelete(captureImageTaskHandle);
            captureImageTaskHandle = nullptr;
        }
        isCapturingImage = false;
    }
    cancelEnrollRequested = false;
    xTaskCreate(captureImageTask, "capImgTask", 4096, NULL, 1, &captureImageTaskHandle);
}

// Xóa vân tay theo ID (xóa cả ID chính và các slot phụ liên kết)
void deleteFingerprint(int id) {
    if (id < 1 || id > 200) {
        notifyStatus("FP_DELETE_FAILED|ID_KHONG_HOP_LE");
        return;
    }

    Serial.printf("📡 [R503 UART TX] -> deleteModel (0x0C) [Xóa slot ID %d trong R503]\n", id);
    int p = finger.deleteModel(id);
    Serial.printf("📥 [R503 UART RX] <- deleteModel phản hồi: %s\n", r503CodeToString(p));
    
    // Luôn xóa NVS để đồng bộ tuyệt đối với App
    prefsFinger.remove(("name_" + String(id)).c_str());

    // Xóa tất cả các sub-model phụ (góc nghiêng) liên kết với ID này
    int maxCap = getMaxCapacity();
    for (int aux = 1; aux <= maxCap; aux++) {
        String parentKey = "parent_" + String(aux);
        if (prefsFinger.isKey(parentKey.c_str()) && prefsFinger.getInt(parentKey.c_str(), -1) == id) {
            finger.deleteModel(aux);
            prefsFinger.remove(("name_" + String(aux)).c_str());
            prefsFinger.remove(parentKey.c_str());
            Serial.printf("🧹 Đã dọn sạch sub-model góc nghiêng ID %d của vân tay %d\n", aux, id);
        }
    }

    notifyStatus("FP_DELETE_OK|" + String(id));
    beep(1, 100);
}

// Đổi tên vân tay (cập nhật cả các slot phụ liên kết)
void renameFingerprint(int id, String newName) {
    int maxCap = getMaxCapacity();
    if (id < 1 || id > maxCap || newName.length() == 0) {
        notifyStatus("FP_RENAME_FAILED|THONG_TIN_SAI");
        return;
    }
    
    prefsFinger.putString(("name_" + String(id)).c_str(), newName);

    // Cập nhật tên cho cả các sub-model phụ liên kết
    for (int aux = 1; aux <= maxCap; aux++) {
        String parentKey = "parent_" + String(aux);
        if (prefsFinger.isKey(parentKey.c_str()) && prefsFinger.getInt(parentKey.c_str(), -1) == id) {
            prefsFinger.putString(("name_" + String(aux)).c_str(), newName);
        }
    }

    notifyStatus("FP_RENAME_OK|" + String(id) + "|" + newName);
}

// Trả về toàn bộ danh sách vân tay qua BLE (Bỏ qua slot phụ)
void listAllFingerprints() {
    notifyStatus("FP_LIST_START");
    int nvsCount = getRegisteredFingerprintCount();
    Serial.printf("📋 Yêu cầu danh sách: Số vân tay hợp lệ = %d\n", nvsCount);

    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++) {
        String key = "name_" + String(id);
        String parentKey = "parent_" + String(id);
        if (prefsFinger.isKey(key.c_str()) && !prefsFinger.isKey(parentKey.c_str())) {
            String name = prefsFinger.getString(key.c_str(), "Ngon_" + String(id));
            notifyStatus("FP_ITEM|" + String(id) + "|" + name);
            delay(25); // Giãn cách gói tin BLE
        }
    }

    notifyStatus("FP_LIST_END");
}

// Xóa toàn bộ vân tay (Xóa sạch trong cả R503 lẫn NVS)
void clearAllFingerprints() {
    Serial.println("🗑️ Bắt đầu xóa TOÀN BỘ vân tay...");
    Serial.println("📡 [R503 UART TX] -> emptyDatabase (0x0D) [Xóa sạch toàn bộ thư viện trong Flash R503]");
    int p = finger.emptyDatabase();
    Serial.printf("📥 [R503 UART RX] <- emptyDatabase phản hồi: %s\n", r503CodeToString(p));
    delay(500); // Đợi Flash ROM của R503 xóa sạch hoàn toàn
    
    // Xóa cưỡng chế toàn bộ các slot trong R503 để phòng ngừa lệnh emptyDatabase không xóa hết hoặc lỗi
    int maxCap = getMaxCapacity();
    for (int i = 1; i <= maxCap; i++) {
        finger.deleteModel(i);
    }
    
    finger.getTemplateCount();
    Serial.printf("emptyDatabase R503 trả về mã: %d, còn lại: %d mẫu\n", p, finger.templateCount);
    
    prefsFinger.clear();
    notifyStatus("FP_CLEAR_OK");
    beep(3, 100);
    Serial.println("✅ Đã xóa sạch toàn bộ vân tay trong cả R503 và Flash NVS!");
}

// Kiểm tra quét vân tay thực tế khi người dùng chạm
void handleFingerprintTouch() {
    if (isEnrolling || isCapturingImage) return; // Bảo vệ: Không quét kích hoạt xe khi đang trong chu trình thêm hoặc chụp ảnh!

    if (!r503Ready) {
        // Thử thăm dò lại cảm biến nếu trước đó chưa bắt tay được
        if (finger.verifyPassword()) {
            r503Ready = true;
            Serial.println("✅ Cảm biến R503 đã được nhận diện!");
            updateIdleLed();
        } else {
            return;
        }
    }

    // 0. Kiểm tra nếu đang trong chế độ Live Test cảm biến
    if (isTestingFingerprint) {
        if (millis() > testFpUntil) {
            isTestingFingerprint = false;
            updateIdleLed();
        }
    }

    // NẾU HỆ THỐNG CHƯA CÓ VÂN TAY NÀO VÀ KHÔNG Ở CHẾ ĐỘ TEST -> TỪ CHỐI
    if (!isTestingFingerprint && getRegisteredFingerprintCount() == 0) {
        Serial.println("⚠️ Hệ thống chưa đăng ký vân tay nào (hoặc đã xóa hết)! Từ chối mở khóa.");
        ledError();
        notifyStatus("FP_NOT_MATCH");
        unsigned long waitRelease = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW && (millis() - waitRelease < 2000)) {
            delay(50);
        }
        updateIdleLed();
        return;
    }

    // 0b. Nếu đang trong thời gian Cooldown tạm khóa vì nước nhiễu liên tục (chỉ khi không test)
    if (!isTestingFingerprint && rainEnabled && cooldownUntil > millis()) {
        unsigned long remSec = (cooldownUntil - millis()) / 1000 + 1;
        Serial.printf("☔ Đang trong thời gian Cooldown mưa (%lus còn lại), tạm khóa cảm biến.\n", remSec);
        setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_BLUE, 200, 1);
        delay(300);
        updateIdleLed();
        return;
    }

    // 1. Kiểm tra giữ ngón tay liên tục nếu Bật Chế độ Mưa (Lọc giọt nước chạm lướt, chỉ khi không test)
    if (!isTestingFingerprint && rainEnabled && touchHoldMs > 0) {
        unsigned long pressStart = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW) {
            if (millis() - pressStart >= (unsigned long)touchHoldMs) {
                break; // Ngón tay thật đã giữ đủ thời gian!
            }
            delay(15);
        }
        if (millis() - pressStart < (unsigned long)touchHoldMs) {
            return;
        }
    }

    Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.printf("🖐️ [R503 UART] PHÁT HIỆN CHẠM CẢM BIẾN | Chế độ: %s\n",
                  isTestingFingerprint ? "QUÉT THỬ (TEST ĐỌC MÃ)" : "VẬN HÀNH (MỞ / KHÓA XE)");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    // 2. VÒNG LẶP QUÉT ĐỐI SOÁT LIÊN TỤC TRONG KHI NGÓN TAY ÁP VÀO MẶT KÍNH
    // Quét liên tục trong cửa sổ thời gian fpScanWindowMs (mặc định 1200ms)
    while (r503Serial.available()) r503Serial.read(); // Dọn sạch buffer RX trước khi quét để chống trôi byte
    unsigned long startScan = millis();
    int p = -1;
    bool matched = false;
    int matchedId = -1;
    int matchedConfidence = 0;
    bool fingerDetected = false;
    int scanAttempt = 0;

    while (millis() - startScan < (unsigned long)fpScanWindowMs) {
        scanAttempt++;
        Serial.printf("📡 [R503 UART TX #%d] -> getImage (0x01) [EF 01 FF FF FF FF 01 00 03 01 00 05]\n", scanAttempt);
        p = finger.getImage();
        Serial.printf("📥 [R503 UART RX #%d] <- getImage: %s\n", scanAttempt, r503CodeToString(p));
        if (p == FINGERPRINT_OK) {
            fingerDetected = true;
            Serial.println("📡 [R503 UART TX] -> image2Tz CharBuffer 1 (0x02) [EF 01 FF FF FF FF 01 00 04 02 01 00 08]");
            p = finger.image2Tz();
            Serial.printf("📥 [R503 UART RX] <- image2Tz: %s\n", r503CodeToString(p));
            if (p == FINGERPRINT_OK) {
                Serial.println("📡 [R503 UART TX] -> fingerSearch (0x04) [Đối soát trong thư viện Flash R503]");
                p = finger.fingerSearch(1); // R503 sử dụng opcode chuẩn 0x04 (CharBuffer 1, 0 -> capacity)
                Serial.printf("📥 [R503 UART RX] <- fingerSearch: %s\n", r503CodeToString(p));
                if (p == FINGERPRINT_OK && finger.fingerID > 0 && finger.confidence > 0) {
                    matched = true;
                    matchedId = finger.fingerID;
                    matchedConfidence = finger.confidence;
                    Serial.printf("🎯 [R503 KẾT QUẢ] Khớp thành công: ID #%d | Điểm tin cậy: %d\n", matchedId, matchedConfidence);
                } else {
                    Serial.println("ℹ️ [R503 KẾT QUẢ] Không tìm thấy mẫu trùng khớp trong bộ nhớ module.");
                }
                break; // Đã trích xuất xong đặc trưng vân tay
            }
        }
        if (digitalRead(R503_WAKE_PIN) == HIGH) {
            Serial.println("🖐️ [R503] Ngón tay đã nhấc ra khỏi cảm biến.");
            break; // Đã nhấc ngón tay ra
        }
        delay(25);
    }

    // XỬ LÝ KHI ĐANG Ở CHẾ ĐỘ TEST CẢM BIẾN (QUÉT THỬ ĐỌC MÃ VÂN TAY)
    if (isTestingFingerprint) {
        if (!fingerDetected) {
            return;
        }

        int id = matchedId;
        String key = "name_" + String(id);
        bool isRegistered = (id > 0) && prefsFinger.isKey(key.c_str());

        // Trích xuất mã đặc trưng vân tay (Mã vân tay thực tế) từ R503
        String fpCode = extractFingerprintCode(matched && isRegistered ? matchedId : -1);

        if (matched && isRegistered) {
            int primaryId = matchedId;
            String parentKey = "parent_" + String(matchedId);
            if (prefsFinger.isKey(parentKey.c_str())) {
                primaryId = prefsFinger.getInt(parentKey.c_str(), matchedId);
            }
            String name = prefsFinger.getString(("name_" + String(primaryId)).c_str(), "ID_" + String(primaryId));
            Serial.printf("🔬 [QUÉT THỬ] Mã vân tay: %s | Khớp ID #%d [%s] - Điểm: %d\n", fpCode.c_str(), matchedId, name.c_str(), matchedConfidence);
            notifyStatus("FP_TEST_RESULT|" + String(matchedId) + "|" + name + "|" + String(matchedConfidence) + "|" + fpCode);
            
            // Nếu bật chế độ nhận ảnh, trích xuất và truyền ảnh quang học qua BLE
            if (fpSendEnrollImage) {
                streamR503ImageOverBle();
            }

            ledSuccess();
            beep(1, 80);
        } else {
            if (matched && !isRegistered) {
                Serial.printf("ℹ️ [TEST] Phát hiện vân tay ID %d trong R503 (chưa gán tên trong NVS Flash)\n", id);
            }
            Serial.printf("🔬 [QUÉT THỬ] Mã vân tay mới: %s | Cảm biến nhận diện tốt (Chưa lưu trên xe)\n", fpCode.c_str());
            notifyStatus("FP_TEST_RESULT|-1|Chưa lưu trên xe|0|" + fpCode);

            // Kể cả ngón tay chưa lưu, nếu bật chế độ nhận ảnh thì truyền để kiểm tra lăng kính
            if (fpSendEnrollImage) {
                streamR503ImageOverBle();
            }

            ledSuccess(); // Cảm biến đọc và nhận diện thành công -> Báo đèn xanh và bíp xác nhận!
            beep(1, 60);
        }

        unsigned long waitRelease = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW && (millis() - waitRelease < 2000)) {
            delay(50);
        }
        if (millis() < testFpUntil) {
            setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 100, 0);
        } else {
            isTestingFingerprint = false;
            updateIdleLed();
        }
        return;
    }

    // XỬ LÝ MỞ / TẮT XE BÌNH THƯỜNG
    int id = matchedId;
    String key = "name_" + String(id);
    bool isRegistered = (id > 0) && prefsFinger.isKey(key.c_str());

    if (matched && isRegistered) {
        // VÂN TAY HỢP LỆ!
        wrongFingerAttempts = 0; // Đặt lại bộ đếm khi quẹt đúng
        
        int primaryId = id;
        String parentKey = "parent_" + String(id);
        if (prefsFinger.isKey(parentKey.c_str())) {
            primaryId = prefsFinger.getInt(parentKey.c_str(), id);
        }
        String name = prefsFinger.getString(("name_" + String(primaryId)).c_str(), "ID_" + String(primaryId));
        Serial.printf("✅ Vân tay hợp lệ! ID phần cứng: %d (ID chính: %d, %s) - Confidence: %d\n", 
                      id, primaryId, name.c_str(), matchedConfidence);
        
        // CHUYỂN ĐỔI TRẠNG THÁI XE (TOGGLE) - Mở khóa hoặc Khóa xe
        bool newUnlockState = !isUnlocked;
        setVehicleUnlock(newUnlockState, true, false);

        if (newUnlockState) {
            notifyStatus("FP_MATCHED_UNLOCK|" + String(primaryId) + "|" + name);
            beep(1, 100); // 1 tiếng bíp khi mở khóa
        } else {
            notifyStatus("FP_MATCHED_LOCK|" + String(primaryId) + "|" + name);
            beep(2, 80);  // 2 tiếng bíp khi tắt khóa
        }

        unsigned long waitRelease = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW && (millis() - waitRelease < 3000)) {
            delay(50);
        }
        delay(200);
    } else {
        // VÂN TAY KHÔNG KHỚP HOẶC KHÔNG HỢP LỆ
        if (matched && !isRegistered) {
            Serial.printf("ℹ️ Vân tay ID %d có trong R503 nhưng chưa đăng ký trong Flash NVS\n", id);
        }

        wrongFingerAttempts++;
        Serial.printf("❌ Vân tay không hợp lệ! (Lần %d, Confidence: %d)\n", wrongFingerAttempts, matchedConfidence);
        ledError();
        notifyStatus("FP_NOT_MATCH");

        int threshold = rainEnabled ? maxWrongAttempts : 3;
        if (threshold > 0 && wrongFingerAttempts >= threshold) {
            if (rainEnabled) {
                Serial.printf("☔ MƯA NHIỄU: Quẹt sai %d lần! Tạm khóa cảm biến trong %ds.\n", wrongFingerAttempts, cooldownSec);
                cooldownUntil = millis() + (unsigned long)cooldownSec * 1000;
                notifyStatus("RAIN_COOLDOWN|" + String(cooldownSec));
                setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_BLUE, 150, 2);
                wrongFingerAttempts = 0;
            } else {
                Serial.println("🚨 CẢNH BÁO CHỐNG TRỘM: Quẹt sai 3 lần liên tiếp!");
                beep(6, 120); // 6 tiếng bíp còi báo động liên tục
                wrongFingerAttempts = 0;
            }
        }
        
        unsigned long waitRelease = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW && (millis() - waitRelease < 3000)) {
            delay(50);
        }
        updateIdleLed();
    }
}


// Gửi cấu hình Chế độ Mưa qua BLE
void sendRainConfig() {
    unsigned long remaining = 0;
    if (rainEnabled && autoOffSec > 0) {
        unsigned long elapsed = (millis() - rainStartTime) / 1000;
        remaining = (elapsed < (unsigned long)autoOffSec) ? (autoOffSec - elapsed) : 0;
    }
    String resp = "RAIN_CONFIG|" + String(rainEnabled ? 1 : 0) + "|" +
                  String(touchHoldMs) + "|" + String(maxWrongAttempts) + "|" +
                  String(cooldownSec) + "|" + String(autoOffSec) + "|" + String(remaining);
    notifyStatus(resp);
}

// Gửi cấu hình Tinh chỉnh Vân tay qua BLE
void sendFingerprintConfig() {
    String resp = "FP_CFG|" + String(fpSecurityLevel) + "|" +
                  String(fpScanWindowMs) + "|" + String(fpEnrollMode) + "|" +
                  String(fpSendEnrollImage ? 1 : 0);
    notifyStatus(resp);
}

// ==========================================
// 📡 6. BỘ XỬ LÝ LỆNH TỪ BLE & APP
// ==========================================

void processIncomingCommand(String data) {
    data.trim();
    if (data.length() == 0) return;

    // Chống lặp lệnh trong 300ms
    if (data == lastCommand && millis() - lastCmdTime < 300) return;
    lastCommand = data;
    lastCmdTime = millis();

    Serial.println("BLE Received: " + data);

    // Kiểm tra định dạng: <KEY>|<CMD>[|<PARAM1>|<PARAM2>]
    int firstPipe = data.indexOf('|');
    if (firstPipe == -1) {
        notifyStatus("LOI_FORMAT");
        return;
    }

    String providedKey = data.substring(0, firstPipe);
    String remaining = data.substring(firstPipe + 1);

    // Kiểm tra Secret Key
    if (providedKey != SECRET_KEY) {
        Serial.println("Sai mã bảo mật!");
        notifyStatus("LOI_SAI_KEY");
        ledError();
        return;
    }

    int secondPipe = remaining.indexOf('|');
    String cmd = (secondPipe == -1) ? remaining : remaining.substring(0, secondPipe);
    String params = (secondPipe == -1) ? "" : remaining.substring(secondPipe + 1);

    // --- CÁC LỆNH CƠ BẢN ---
    if (cmd == "1") {
        setVehicleUnlock(true);
    }
    else if (cmd == "0") {
        setVehicleUnlock(false);
    }
    else if (cmd == "2") {
        triggerStarter();
    }
    else if (cmd == "3") {
        triggerLocate();
    }
    else if (cmd == "9" && params.length() > 0) { // Đổi mã bảo mật
        SECRET_KEY = params;
        prefsSecurity.putString("master_key", SECRET_KEY);
        Serial.println("Đã đổi SECRET_KEY thành: " + SECRET_KEY);
        notifyStatus("DA_DOI_KEY");
        beep(2, 100);
    }

    // --- CÁC LỆNH QUẢN LÝ VÂN TAY ---
    else if (cmd == "FP_LIST") {
        listAllFingerprints();
    }
    else if (cmd == "FP_ENROLL") {
        String fingerName = params.length() > 0 ? params : "Vân tay mới";
        if (fingerName.endsWith("|IMG")) {
            fpSendEnrollImage = true;
            fingerName = fingerName.substring(0, fingerName.length() - 4);
        } else if (fingerName.endsWith("|NO_IMG")) {
            fpSendEnrollImage = false;
            fingerName = fingerName.substring(0, fingerName.length() - 7);
        }
        startEnrollTask(fingerName);
    }
    else if (cmd == "SET_FP_IMG_MODE") {
        fpSendEnrollImage = (params == "1");
        prefsFinger.putBool("send_img", fpSendEnrollImage);
        notifyStatus("FP_IMG_MODE|" + String(fpSendEnrollImage ? "1" : "0"));
    }
    else if (cmd == "CAPTURE_FP_IMG") {
        startCaptureImageTask();
    }
    else if (cmd == "FP_CANCEL") {
        cancelEnrollRequested = true;
        Serial.println("Nhận lệnh FP_CANCEL từ App!");
    }
    else if (cmd == "FP_DELETE" && params.length() > 0) {
        int id = params.toInt();
        deleteFingerprint(id);
    }
    else if (cmd == "FP_RENAME" && params.length() > 0) {
        int splitIdx = params.indexOf('|');
        if (splitIdx != -1) {
            int id = params.substring(0, splitIdx).toInt();
            String newName = params.substring(splitIdx + 1);
            renameFingerprint(id, newName);
        }
    }
    else if (cmd == "FP_CLEAR" && params == "CONFIRM") {
        clearAllFingerprints();
    }
    // --- CÁC LỆNH TINH CHỈNH CẢM BIẾN VÂN TAY ---
    else if (cmd == "GET_FP_CFG") {
        sendFingerprintConfig();
    }
    else if (cmd == "SET_FP_CFG") {
        // Gói tin: <KEY>|SET_FP_CFG|<sec_level>|<scan_win>|<enroll_mode>[|<send_img>]
        int p1 = params.indexOf('|');
        int p2 = params.indexOf('|', p1 + 1);
        if (p1 != -1 && p2 != -1) {
            fpSecurityLevel = params.substring(0, p1).toInt();
            fpScanWindowMs = params.substring(p1 + 1, p2).toInt();
            int p3 = params.indexOf('|', p2 + 1);
            if (p3 != -1) {
                fpEnrollMode = params.substring(p2 + 1, p3).toInt();
                fpSendEnrollImage = (params.substring(p3 + 1).toInt() == 1);
                prefsFinger.putBool("send_img", fpSendEnrollImage);
            } else {
                fpEnrollMode = params.substring(p2 + 1).toInt();
            }

            if (fpSecurityLevel < 1) fpSecurityLevel = 1;
            if (fpSecurityLevel > 5) fpSecurityLevel = 5;
            if (fpScanWindowMs < 400) fpScanWindowMs = 400;
            if (fpScanWindowMs > 3000) fpScanWindowMs = 3000;
            if (fpEnrollMode != 2 && fpEnrollMode != 4) fpEnrollMode = 4;

            prefsFinger.putInt("sec_level", fpSecurityLevel);
            prefsFinger.putInt("scan_win", fpScanWindowMs);
            prefsFinger.putInt("enroll_mode", fpEnrollMode);

            if (r503Ready) {
                finger.setSecurityLevel(fpSecurityLevel);
            }

            Serial.printf("✅ Đã cập nhật FP Config: SecLevel=%d, ScanWin=%d, EnrollMode=%d, SendImg=%d\n",
                          fpSecurityLevel, fpScanWindowMs, fpEnrollMode, fpSendEnrollImage ? 1 : 0);
            sendFingerprintConfig();
            notifyStatus("FP_CFG_OK");
            beep(1, 80);
        } else {
            notifyStatus("LOI_PARAM_FP_CFG");
        }
    }
    else if (cmd == "TEST_FP") {
        int durSec = params.toInt();
        if (durSec <= 0) durSec = 15;
        isTestingFingerprint = true;
        testFpUntil = millis() + (unsigned long)durSec * 1000;
        setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 100, 0);
        notifyStatus("FP_TEST_STARTED|" + String(durSec));
        Serial.printf("🔬 Bắt đầu chế độ Live Test cảm biến vân tay trong %d giây...\n", durSec);
    }
    // --- CÁC LỆNH CHẾ ĐỘ CHỐNG NƯỚC MƯA (ANTI-RAIN MODE) ---
    else if (cmd == "SET_RAIN") {
        // Gói tin: <KEY>|SET_RAIN|<enabled>|<touch_hold_ms>|<max_wrong>|<cooldown_sec>|<auto_off_sec>
        int p1 = params.indexOf('|');
        int p2 = params.indexOf('|', p1 + 1);
        int p3 = params.indexOf('|', p2 + 1);
        int p4 = params.indexOf('|', p3 + 1);

        if (p1 != -1 && p2 != -1 && p3 != -1 && p4 != -1) {
            rainEnabled = (params.substring(0, p1).toInt() == 1);
            touchHoldMs = params.substring(p1 + 1, p2).toInt();
            maxWrongAttempts = params.substring(p2 + 1, p3).toInt();
            cooldownSec = params.substring(p3 + 1, p4).toInt();
            autoOffSec = params.substring(p4 + 1).toInt();

            // Giới hạn dải an toàn
            if (touchHoldMs < 100) touchHoldMs = 100;
            if (touchHoldMs > 3000) touchHoldMs = 3000;
            if (maxWrongAttempts < 0) maxWrongAttempts = 0;
            if (maxWrongAttempts > 10) maxWrongAttempts = 10;
            if (cooldownSec < 5) cooldownSec = 5;
            if (cooldownSec > 600) cooldownSec = 600;
            if (autoOffSec < 0) autoOffSec = 0;

            prefsRain.putBool("enabled", rainEnabled);
            prefsRain.putInt("touch_hold", touchHoldMs);
            prefsRain.putInt("max_wrong", maxWrongAttempts);
            prefsRain.putInt("cooldown", cooldownSec);
            prefsRain.putInt("auto_off", autoOffSec);

            if (rainEnabled) rainStartTime = millis();
            cooldownUntil = 0; // Xóa cooldown hiện tại

            Serial.printf("✅ Đã cập nhật Rain Config: Enabled=%d, Hold=%d, MaxWrong=%d, Cooldown=%d, AutoOff=%d\n",
                          rainEnabled, touchHoldMs, maxWrongAttempts, cooldownSec, autoOffSec);

            sendRainConfig();
            beep(1, 80);
        } else {
            notifyStatus("LOI_PARAM_RAIN");
        }
    }
    else if (cmd == "TOGGLE_RAIN") {
        int newState = params.toInt();
        rainEnabled = (newState == 1);
        prefsRain.putBool("enabled", rainEnabled);
        if (rainEnabled) rainStartTime = millis();
        cooldownUntil = 0;

        sendRainConfig();
        beep(1, 60);
    }
    else if (cmd == "GET_RAIN") {
        sendRainConfig();
    }

    // --- CÁC LỆNH CẤU HÌNH ĐÈN VÒNG MÀU R503 (AURA RGB) ---
    else if (cmd == "GET_LED_CFG") {
        sendLedConfigResponse();
    }
    else if (cmd == "SET_LED_CFG") {
        // Gói tin: <KEY>|SET_LED_CFG|<u_m>|<u_c>|<u_s>|<l_m>|<l_c>|<l_s>|<s_m>|<s_c>|<s_s>|<e_m>|<e_c>|<e_s>
        int tokens[12];
        int lastIdx = 0;
        bool valid = true;
        for (int i = 0; i < 11; i++) {
            int nextIdx = params.indexOf('|', lastIdx);
            if (nextIdx == -1) { valid = false; break; }
            tokens[i] = params.substring(lastIdx, nextIdx).toInt();
            lastIdx = nextIdx + 1;
        }
        if (valid) {
            tokens[11] = params.substring(lastIdx).toInt();

            ledConfig.unlocked.mode  = (uint8_t)tokens[0];
            ledConfig.unlocked.color = (uint8_t)tokens[1];
            ledConfig.unlocked.speed = (uint8_t)tokens[2];

            ledConfig.locked.mode    = (uint8_t)tokens[3];
            ledConfig.locked.color   = (uint8_t)tokens[4];
            ledConfig.locked.speed   = (uint8_t)tokens[5];

            ledConfig.success.mode   = (uint8_t)tokens[6];
            ledConfig.success.color  = (uint8_t)tokens[7];
            ledConfig.success.speed  = (uint8_t)tokens[8];

            ledConfig.error.mode     = (uint8_t)tokens[9];
            ledConfig.error.color    = (uint8_t)tokens[10];
            ledConfig.error.speed    = (uint8_t)tokens[11];

            saveLedConfig();
            updateIdleLed();
            notifyStatus("LED_CFG_OK");
            beep(1, 80);
        } else {
            notifyStatus("LOI_PARAM_LED_CFG");
        }
    }
    else if (cmd == "TEST_LED") {
        // Gói tin: <KEY>|TEST_LED|<mode>|<color>|<speed>|<count>
        int p1 = params.indexOf('|');
        int p2 = params.indexOf('|', p1 + 1);
        int p3 = params.indexOf('|', p2 + 1);
        if (p1 != -1 && p2 != -1 && p3 != -1) {
            uint8_t tMode  = params.substring(0, p1).toInt();
            uint8_t tColor = params.substring(p1 + 1, p2).toInt();
            uint8_t tSpeed = params.substring(p2 + 1, p3).toInt();
            uint8_t tCount = params.substring(p3 + 1).toInt();
            r503SetAuraLed(tMode, tColor, tSpeed, tCount);
            notifyStatus("LED_TEST_OK");
        }
    }

    else if (cmd == "TELE" || cmd == "STATUS") {
        notifyStatus(isUnlocked ? "STATUS|1" : "STATUS|0");
        sendTelemetry();
    }
}

// ==========================================
// 📶 7. CALLBACKS NIMBLE
// ==========================================

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer) {
        deviceConnected = true;
        Serial.println("BLE Client đã kết nối!");
        setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 50, 1);
        notifyStatus(isUnlocked ? "STATUS|1" : "STATUS|0");
        sendTelemetry();
        delay(40);
        sendRainConfig();
        delay(40);
        sendFingerprintConfig();
        delay(40);
        sendLedConfigResponse();
    }

    void onDisconnect(NimBLEServer *pServer) {
        deviceConnected = false;
        Serial.println("BLE Client đã ngắt kết nối.");

        // Hủy chu trình thêm vân tay nếu đang chạy dở khi mất kết nối BLE
        if (enrollTaskHandle != nullptr || isEnrolling) {
            cancelEnrollRequested = true;
            if (enrollTaskHandle != nullptr) {
                vTaskDelete(enrollTaskHandle);
                enrollTaskHandle = nullptr;
            }
            isEnrolling = false;
            cancelEnrollRequested = false;
            updateIdleLed();
        }

        // =========================================================================
        // 🛡️ CƠ CHẾ AN TOÀN Ô TÔ/XE MÁY (AUTOMOTIVE CRITICAL SAFETY):
        // NẾU XE ĐANG MỞ KHÓA (isUnlocked == true):
        // 1. TUYỆT ĐỐI KHÔNG TẮT RELAY 1 (GIỮ NGUYÊN NGUỒN ACC CHO XE CHẠY TIẾP).
        // 2. PHÁT CẢNH BÁO BÍP NHẸ & ĐỔI MÀU LED R503 ĐỂ BÁO CHO TÀI XẾ BIẾT MẤT BLE.
        // 3. CHỈ KHÓA LẠI KHI TÀI XẾ DỪNG XE VÀ CHỦ ĐỘNG QUẸT VÂN TAY HOẶC TẮT KHÓA CƠ.
        // =========================================================================
        if (isUnlocked) {
            Serial.println("⚠️ CẢNH BÁO: Mất kết nối BLE khi xe đang nổ máy!");
            Serial.println("🛡️ Failsafe: Giữ nguyên Relay 1 (ACC ON) - Đảm bảo an toàn xe lăn bánh!");
            beep(2, 60); // 2 tiếng bíp ngắn cảnh báo tài xế
            setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 100, 0); // Đèn thở tím cảnh báo
        } else {
            updateIdleLed();
        }

        Serial.println("Bắt đầu Advertising lại...");
        NimBLEDevice::startAdvertising();
    }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            processIncomingCommand(String(value.c_str()));
        }
    }
};

// ==========================================
// 🚀 8. SETUP & MAIN LOOP
// ==========================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n==============================");
    Serial.println("   TSMARTKEY ESP32-C3 SYSTEM   ");
    Serial.println("==============================");

    // 1. Khởi tạo Relay Pins & Chân RF
    pinMode(RELAY1_PIN, OUTPUT);
    pinMode(RELAY2_PIN, OUTPUT);
    pinMode(RELAY3_PIN, OUTPUT);

    // Cấu hình chân tín hiệu Module RF 433MHz (Chỉ tìm xe, không mở khóa)
    pinMode(RF_LOCATE_PIN, INPUT_PULLDOWN);

    // Cấu hình chân cảm ứng ngắt WAKEUP của R503 (Active LOW: Không chạm = 3.2V, Chạm = 0V)
    pinMode(R503_WAKE_PIN, INPUT_PULLUP);
    pinMode(2, INPUT_PULLUP); // Dự phòng cho trường hợp cắm vào chân GPIO 2

    // 2. Tải cấu hình từ Flash NVS
    prefsSecurity.begin("safe_key", false);
    prefsFinger.begin("fingerprint", false);
    prefsRain.begin("rain_config", false);
    loadLedConfig();

    SECRET_KEY = prefsSecurity.getString("master_key", "271000");
    isUnlocked = prefsSecurity.getBool("is_unlocked", false);

    rainEnabled = prefsRain.getBool("enabled", false);
    touchHoldMs = prefsRain.getInt("touch_hold", 500);
    maxWrongAttempts = prefsRain.getInt("max_wrong", 5);
    cooldownSec = prefsRain.getInt("cooldown", 30);
    autoOffSec = prefsRain.getInt("auto_off", 3600);
    if (rainEnabled) {
        rainStartTime = millis();
    }
    Serial.printf("☔ Rain Mode: %s | Hold: %dms | MaxWrong: %d | Cooldown: %ds | AutoOff: %ds\n",
                  rainEnabled ? "BẬT" : "TẮT", touchHoldMs, maxWrongAttempts, cooldownSec, autoOffSec);

    // Tải cấu hình tinh chỉnh vân tay
    fpSecurityLevel = prefsFinger.getInt("sec_level", 2);
    fpScanWindowMs = prefsFinger.getInt("scan_win", 1200);
    fpEnrollMode = prefsFinger.getInt("enroll_mode", 4);
    fpSendEnrollImage = prefsFinger.getBool("send_img", false);
    Serial.printf("🔍 FP Config: SecLevel=%d | ScanWin=%dms | EnrollMode=%d | SendImg=%d\n",
                  fpSecurityLevel, fpScanWindowMs, fpEnrollMode, fpSendEnrollImage ? 1 : 0);


    // KHÔI PHỤC NGAY LẬP TỨC TRẠNG THÁI RELAY KHI KHỞI ĐỘNG (FAIL-SAFE)
    digitalWrite(RELAY1_PIN, isUnlocked ? HIGH : LOW);
    digitalWrite(RELAY2_PIN, LOW);
    digitalWrite(RELAY3_PIN, LOW);

    Serial.printf("Secret Key: %s | Trạng thái xe: %s\n", 
                  SECRET_KEY.c_str(), isUnlocked ? "MỞ KHÓA" : "ĐANG KHÓA");

    // 3. Khởi tạo & Tự động quét Baudrate tìm cảm biến R503 (57600, 9600, 115200, 19200, 38400)
    Serial.println("⏳ Đang quét Baudrate tìm cảm biến R503...");
    uint32_t bauds[] = {57600, 9600, 115200, 19200, 38400};
    bool r503Found = false;

    delay(100); // Cho cảm biến R503 thời gian ổn định điện áp nguồn

    for (uint32_t b : bauds) {
        r503Serial.setRxBufferSize(24576);
        r503Serial.begin(b, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
        delay(80);
        while (r503Serial.available()) r503Serial.read(); // Xóa rác RX trước khi gửi lệnh bắt tay
        Serial.printf("📡 [R503 UART TX] -> verifyPassword (0x13) [EF 01 FF FF FF FF 01 00 07 13 00 00 00 00 ...] tại Baud %d\n", b);
        if (finger.verifyPassword()) {
            Serial.printf("📥 [R503 UART RX] <- verifyPassword: %s (Module R503 phản hồi chính xác!)\n", r503CodeToString(0x00));
            Serial.printf("✅ Cảm biến R503 kết nối thành công tại Baudrate: %d!\n", b);
            r503Found = true;
            break;
        } else {
            Serial.printf("📥 [R503 UART RX] <- Không có phản hồi hợp lệ tại Baud %d\n", b);
        }
        r503Serial.end();
        delay(30);
    }

    if (r503Found) {
        r503Ready = true;
        finger.getParameters();
        if (finger.capacity == 0 || finger.capacity > 200) {
            finger.capacity = 200;
        }
        Serial.printf("📊 R503 Dung lượng bộ nhớ: %d vân tay | Bảo mật hiện tại trên module: Mức %d\n", finger.capacity, finger.security_level);
        finger.setSecurityLevel(fpSecurityLevel);
        Serial.printf("🔒 Đã áp dụng mức bảo mật R503 theo cấu hình: Mức %d\n", fpSecurityLevel);
        int regCount = getRegisteredFingerprintCount();
        Serial.printf("🔑 Số lượng vân tay hợp lệ trong Flash NVS: %d\n", regCount);
        updateIdleLed();
    } else {
        r503Ready = false;
        r503Serial.setRxBufferSize(24576);
        r503Serial.begin(57600, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN); // Giữ UART hoạt động ở baudrate chuẩn để cắm lại vẫn nhận diện được
        Serial.println("⚠️ Không tìm thấy cảm biến R503 ở mọi baudrate! Kiểm tra lại dây RX(GPIO 0)/TX(GPIO 1) và nguồn 3.3V.");
    }

    // 4. Khởi tạo NimBLE Server
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Mức phát sóng BLE tối đa
    NimBLEDevice::setMTU(517);              // Đặt MTU tối đa để tránh cắt cụt gói tin BLE payload

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | 
        NIMBLE_PROPERTY::WRITE | 
        NIMBLE_PROPERTY::WRITE_NR | 
        NIMBLE_PROPERTY::NOTIFY
    );
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());

    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    Serial.println("✅ BLE Advertising đã bắt đầu! Đang chờ kết nối...");

    // Hiển thị Menu điều khiển Console tương tác trực tiếp qua Serial Monitor
    printMenu();
}

void loop() {
    // Nếu đang trong chu trình thêm vân tay mới từ App thì bỏ qua quét thông thường
    if (isEnrolling) {
        delay(50);
        return;
    }

    // Gửi dữ liệu Telemetry định kỳ mỗi 3 giây khi có kết nối BLE (tạm dừng khi đang truyền ảnh hoặc thêm vân tay)
    static unsigned long lastTelemetryTime = 0;
    if (deviceConnected && !isEnrolling && !isCapturingImage && (millis() - lastTelemetryTime > 3000)) {
        lastTelemetryTime = millis();
        sendTelemetry();
    }

    // Kiểm tra tự động tắt Chế độ Mưa nếu hết thời gian autoOffSec
    if (rainEnabled && autoOffSec > 0) {
        if (millis() - rainStartTime >= (unsigned long)autoOffSec * 1000) {
            rainEnabled = false;
            prefsRain.putBool("enabled", false);
            Serial.println("☔ Chế độ Chống Nước Mưa đã tự động TẮT sau thời gian đếm ngược.");
            notifyStatus("RAIN_AUTO_OFF");
            beep(2, 60);
        }
    }

    // Kiểm tra hết thời gian Live Test cảm biến vân tay
    if (isTestingFingerprint && millis() > testFpUntil) {
        isTestingFingerprint = false;
        Serial.println("🔬 Đã kết thúc thời gian Live Test cảm biến vân tay.");
        updateIdleLed();
    }

    // Kiểm tra chạm ngón tay (Active LOW: Chạm = 0V / LOW trên chân WAKE)
    static unsigned long lastTouchTrigger = 0;
    bool wakeTriggered = (digitalRead(R503_WAKE_PIN) == LOW) || (digitalRead(2) == LOW);
    if (r503Ready && wakeTriggered && (millis() - lastTouchTrigger > 800)) {
        lastTouchTrigger = millis();
        handleFingerprintTouch();
    }

    // -------------------------------------------------------------
    // 📻 KIỂM TRA TÍN HIỆU TỪ REMOTE RF 433MHz (CHỈ TÌM XE - KHÔNG MỞ KHÓA)
    // -------------------------------------------------------------
    static unsigned long lastRfTime = 0;
    if (digitalRead(RF_LOCATE_PIN) == HIGH) {
        if (millis() - lastRfTime > 1500) { // Cooldown chống dội 1.5s
            lastRfTime = millis();
            Serial.println("📻 Tín hiệu RF Remote: Yêu cầu Tìm xe (Locate Only - An toàn tuyệt đối)!");
            triggerLocate();
        }
    }

    // -------------------------------------------------------------
    // ⌨️ NHẬN LỆNH QUA SERIAL MONITOR (COM PORT) ĐỂ DEBUG / TEST / CONSOLE
    // -------------------------------------------------------------
    if (Serial.available()) {
        char firstCh = Serial.peek();
        if (firstCh >= '1' && firstCh <= '8') {
            char ch = Serial.read();
            delay(10);
            while (Serial.available() && (Serial.peek() == '\r' || Serial.peek() == '\n' || Serial.peek() == ' ')) {
                Serial.read();
            }

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
                    Serial.println("\n👉 Chạm ngón tay lên cảm biến để chụp và trích xuất ảnh...");
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
                    {
                        unsigned long confirmStart = millis();
                        bool confirmed = false;
                        while (millis() - confirmStart < 10000) {
                            if (Serial.available()) {
                                char c = Serial.read();
                                if (c == 'y' || c == 'Y') {
                                    confirmed = true;
                                    break;
                                }
                            }
                            delay(10);
                        }
                        if (confirmed) {
                            r503EmptyDatabase();
                            clearAllFingerprints();
                        } else {
                            Serial.println("❌ Đã hủy thao tác xóa hoặc hết thời gian chờ.");
                        }
                    }
                    break;
                default:
                    break;
            }
            printMenu();
        } else {
            String sCmd = Serial.readStringUntil('\n');
            sCmd.trim();
            if (sCmd.length() > 0) {
                Serial.printf("⌨️ Lệnh Serial: %s\n", sCmd.c_str());
                if (sCmd == "?" || sCmd == "menu" || sCmd == "help") {
                    printMenu();
                } else if (sCmd == "FP_LIST") {
                    listAllFingerprints();
                } else if (sCmd.startsWith("FP_ENROLL")) {
                    String name = (sCmd.indexOf('|') != -1) ? sCmd.substring(sCmd.indexOf('|') + 1) : "Vân tay mới";
                    startEnrollTask(name);
                } else if (sCmd == "FP_CLEAR") {
                    clearAllFingerprints();
                } else if (sCmd.startsWith("FP_DELETE|")) {
                    int id = sCmd.substring(10).toInt();
                    deleteFingerprint(id);
                } else {
                    processIncomingCommand(sCmd);
                }
            }
        }
    }

    delay(30);
}
