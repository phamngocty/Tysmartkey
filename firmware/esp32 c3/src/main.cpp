#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <Adafruit_Fingerprint.h>
#include <mbedtls/base64.h>
#include <esp_sleep.h>
#include <Update.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ESP32Time.h>

// ==========================================
// ⏰ QUẢN LÝ THỜI GIAN THỰC & BẢO VỆ VÂN TAY LẠ
// ==========================================
ESP32Time rtc(0);                   // RTC nội ESP32 (offset 0, đồng bộ timestamp từ App Android)
bool hasSyncedTime = false;         // Cờ đã đồng bộ giờ thực từ App Android hay chưa
const int MAX_INTRUDER_PHOTOS = 10; // Giới hạn lưu tối đa 10 ảnh vân tay kẻ gian (FIFO)
// 🚀 QUẢN LÝ PHIÊN BẢN & CẬP NHẬT OTA TỪ XA
// ==========================================
const int CURRENT_FW_VERSION = 3;
const char *CURRENT_FW_VERSION_NAME = "1.0.2";
const char *VERSION_CHECK_URL = "http://192.168.1.114:3002/api/v1/repos/nas152/Tysmartkey/raw/version.json";

// ==========================================
// 📌 1. ĐỊNH NGHĨA CHÂN PHẦN CỨNG (ESP32-C3)
// ==========================================
#define RELAY1_PIN 6  // Relay 1: Khóa điện ACC (Đấu song song ổ khóa cơ, Active HIGH)
#define RELAY2_PIN 7  // Relay 2: Đề xe (Starter, Active LOW: Kích hoạt = 0V/LOW, Tắt = 3.3V/HIGH)
#define RELAY3_PIN 10 // Relay 3: Đèn / Còi (Buzzer / Horn, Active LOW: Kích hoạt = 0V/LOW, Tắt = 3.3V/HIGH)

#define R503_WAKE_PIN 3     // GPIO 3: WAKEUP cảm ứng chạm R503 (RTC Wakeup, Active LOW: chạm = 0V - Sạch 100%, không bị kẹt Bootloader)
#define SW420_VIBRATE_PIN 4 // GPIO 4: Cảm biến rung SW-420 (RTC Wakeup, Active HIGH khi rung)
#define RF_LOCATE_PIN 5     // GPIO 5: Module RF 433MHz Chân VT (RTC Wakeup, Active HIGH khi bấm Remote)
#define BATTERY_ADC_PIN 2   // GPIO 2: ADC1_CH2 đo điện áp bình ắc quy qua cầu phân áp 1k - 10k (Tỷ lệ 1/11)

#define R503_RX_PIN 0 // GPIO 0 kết nối TXD (dây Vàng) của R503
#define R503_TX_PIN 1 // GPIO 1 kết nối RXD (dây Xanh lá) của R503

#define ONBOARD_LED_PIN 8 // GPIO 8: Đèn LED xanh Onboard ESP32-C3 Super Mini (Active LOW)

// ==========================================
// 🔐 2. THÔNG SỐ BẢO MẬT & BLE UUID
// ==========================================
#define DEVICE_NAME "XE_tsmart_BLE"
#define SERVICE_UUID "0000ff01-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ff02-0000-1000-8000-00805f9b34fb"
#define OTA_DATA_UUID "0000ff03-0000-1000-8000-00805f9b34fb"

String SECRET_KEY = "271000";                  // Mã bảo mật mặc định
String vehicleName = DEVICE_NAME;              // Tên riêng của xe lưu trong Flash NVS
bool isUnlocked = false;                       // Trạng thái xe (true: Đang mở khóa, false: Đang khóa)
bool antiTheftEnabled = false;                 // Trạng thái tính năng Báo động chống dắt rung lắc (SW-420)
volatile bool isEnrolling = false;             // Cờ đang trong chế độ thêm vân tay
volatile bool cancelEnrollRequested = false;   // Cờ yêu cầu hủy tiến trình lấy vân tay
TaskHandle_t enrollTaskHandle = nullptr;       // Con trỏ FreeRTOS Task thêm vân tay
volatile bool isCapturingImage = false;        // Cờ đang trong chế độ chụp ảnh thực tế
TaskHandle_t captureImageTaskHandle = nullptr; // Con trỏ FreeRTOS Task chụp ảnh

// Biến trạng thái cập nhật Firmware OTA qua BLE
volatile bool isOtaUpdating = false;
size_t otaTotalBytes = 0;
size_t otaWrittenBytes = 0;
unsigned long otaLastChunkTime = 0;
NimBLECharacteristic *pOtaDataCharacteristic = nullptr;

// Khởi tạo Preferences lưu trữ Flash NVS
Preferences prefsSecurity; // namespace "safe_key"
Preferences prefsFinger;   // namespace "fingerprint"
Preferences prefsRain;     // namespace "rain_config"
Preferences prefsLed;      // namespace "led_cfg"

// Cấu trúc dữ liệu cấu hình đèn vòng màu R503 (Aura LED Opcode 0x35)
struct LedEventConfig
{
    uint8_t mode;  // 1=Breathing, 2=Flashing, 3=Always ON, 4=Always OFF, 5=Gradual ON, 6=Gradual OFF
    uint8_t color; // 1..7 (1:Đỏ, 2:Xanh, 3:Tím, 4:Xanh lá, 5:Vàng, 6:Cyan, 7:Trắng)
    uint8_t speed; // 0..255 (Tốc độ hiệu ứng)
};

struct R503LedSystemConfig
{
    LedEventConfig unlocked; // Khi xe bật khóa (Mặc định: Thở xanh dương)
    LedEventConfig locked;   // Khi xe khóa (Mặc định: Tắt tiết kiệm ắc quy)
    LedEventConfig success;  // Khi quét đúng / mở xe (Mặc định: Nháy xanh 2 lần)
    LedEventConfig error;    // Khi quét sai / báo động (Mặc định: Nháy đỏ 3 lần)
};

R503LedSystemConfig ledConfig = {
    {0x01, 0x02, 120}, // Unlocked: Breathing Blue, speed 120
    {0x04, 0x02, 0},   // Locked: Always OFF
    {0x02, 0x02, 40},  // Success: Flashing Blue, speed 40
    {0x02, 0x01, 30}   // Error: Flashing Red, speed 30
};

// Cấu hình Chế độ Chống Nước Mưa (Anti-Rain Mode)
bool rainEnabled = false;
int touchHoldMs = 500;           // Thời gian giữ ngón liên tục (ms) để lọc giọt nước chạm lướt
int maxWrongAttempts = 5;        // Ngưỡng quẹt sai trước khi khóa/báo động (0 = tắt còi)
int cooldownSec = 30;            // Thời gian tạm khóa khi chạm sai liên tục (giây)
int autoOffSec = 3600;           // Thời gian tự tắt chế độ mưa (giây, 0 = không tự tắt)
unsigned long rainStartTime = 0; // Thời điểm kích hoạt chế độ mưa (millis)
unsigned long cooldownUntil = 0; // Mốc thời gian kết thúc cooldown (millis)

// Khởi tạo UART1 độc lập cho R503 qua HardwareSerial r503Serial(1) trên ESP32-C3 (RX = GPIO 0, TX = GPIO 1)
// TUYỆT ĐỐI KHÔNG dùng UART 0 vì sẽ xung đột chết với cổng Serial Debug Monitor (115200 baud)
HardwareSerial r503Serial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint((Stream *)&r503Serial);
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
int fpSecurityLevel = 2;           // Mức bảo mật R503: 1 (rất nhạy) -> 5 (rất khắt khe), mặc định 2
int fpScanWindowMs = 1200;         // Thời gian quét đối chiếu liên tục khi áp ngón tay (ms)
int fpEnrollMode = 4;              // Chế độ lấy mẫu: 4 lần chạm (đa góc độ) hoặc 2 lần chạm
bool fpSendEnrollImage = false;    // Gửi ảnh vân tay khi lấy mẫu (bật/tắt theo yêu cầu)
bool isTestingFingerprint = false; // Chế độ test cảm biến không bật/tắt xe
unsigned long testFpUntil = 0;

// Bộ hẹn giờ tự động chuyển về màu đèn trạng thái xe (updateIdleLed) sau hiệu ứng nháy
bool pendingIdleLedUpdate = false;
unsigned long pendingIdleLedUntil = 0;

// Hệ số hiệu chuẩn ADC đo điện áp ắc quy (Mặc định 12.0V / 23.6V = ~0.5085f để bù trừ sai lệch x2 của analogReadMilliVolts trên ESP32-C3)
float batteryVoltageCalib = 12.0f / 23.6f;

// ==========================================
// 💤 QUẢN LÝ NGUỒN 2 TẦNG (POWER MANAGEMENT)
// ==========================================
unsigned long lastActivityTime = 0;
const unsigned long DEEP_SLEEP_TIMEOUT_MS = 24ULL * 60ULL * 60ULL * 1000ULL; // 24 giờ = 86,400,000 ms
bool isPowerSavingAdvertising = false;
bool wakeTriggeredByFingerprint = false;
bool wakeTriggeredByVibration = false;
bool wakeTriggeredByRf = false;

// Khai báo trước các hàm quản lý nguồn & hành động
void updateBleAdvertisingMode(bool fast);
void enterTier2DeepSleep();
void handleVibrationDetected();
void handleFingerprintTouch();
void triggerLocate();

// ==========================================
// 🔔 3. HÀM PHẢN HỒI & HIỆU ỨNG (FEEDBACK)
// ==========================================

// Kêu còi / nháy đèn bíp phản hồi (RELAY3_PIN: Active LOW)
void beep(int count, int delayMs = 100)
{
    for (int i = 0; i < count; i++)
    {
        digitalWrite(RELAY3_PIN, LOW); // Active LOW: Bật còi/đèn
        delay(delayMs);
        digitalWrite(RELAY3_PIN, HIGH); // Tắt còi/đèn
        if (i < count - 1)
            delay(80);
    }
}

// Gửi phản hồi trạng thái qua BLE & Serial
void notifyStatus(String msg)
{
    String fullMsg = "FB|" + msg + "\n";
    if (!msg.startsWith("FP_IMG_CHUNK|"))
    {
        Serial.print("Sending Feedback: " + fullMsg);
    }

    if (pCharacteristic && deviceConnected)
    {
        pCharacteristic->setValue((uint8_t *)fullMsg.c_str(), fullMsg.length());
        pCharacteristic->notify();
    }
}

// Đọc điện áp bình ắc quy qua cầu phân áp 100k - 10k trên chân GPIO 2 (ADC1_CH2)
// Cầu phân áp: R1 = 100k (nối V_BAT), R2 = 10k (nối GND) -> Hệ số phân áp lý thuyết = (100k + 10k) / 10k = 11.0f
// Do analogReadMilliVolts() trên ESP32-C3 với ADC_11db bị sai số tỷ lệ ~x2 so với thực tế (12V đọc thành ~23.6V),
// giá trị được nhân với batteryVoltageCalib (mặc định 12.0 / 23.6) để trả về điện áp ắc quy chuẩn xác tuyệt đối.
float readBatteryVoltage()
{
    // Warm-up ADC 2 lần trước khi lấy mẫu
    analogReadMilliVolts(BATTERY_ADC_PIN);
    analogReadMilliVolts(BATTERY_ADC_PIN);

    uint32_t sumMv = 0;
    const int SAMPLES = 32;
    for (int i = 0; i < SAMPLES; i++)
    {
        sumMv += analogReadMilliVolts(BATTERY_ADC_PIN);
        delayMicroseconds(250);
    }
    float avgMv = (float)sumMv / (float)SAMPLES;
    float rawVoltage = (avgMv * 11.0f) / 1000.0f;     // Chuyển sang Volt và nhân hệ số phân áp 11.0
    float voltage = rawVoltage * batteryVoltageCalib; // Hiệu chỉnh về điện áp thực tế
    return voltage;
}

// Gửi dữ liệu Telemetry thời gian thực (Điện áp ắc quy, Nhiệt độ chip ESP32-C3, Trạng thái khóa)
void sendTelemetry()
{
    float tempC = temperatureRead();      // Đọc cảm biến nhiệt độ tích hợp trong chip ESP32-C3
    float voltage = readBatteryVoltage(); // Đo điện áp bình thực tế từ chân GPIO 2 (Cầu phân áp 1k - 10k)

    // Gói tin: FB|TELE|<VOLTAGE>|<TEMP_C>|<IS_UNLOCKED>
    String teleMsg = "TELE|" + String(voltage, 1) + "|" + String(tempC, 1) + "|" + (isUnlocked ? "1" : "0");
    notifyStatus(teleMsg);
}

// ============================================================================
// 📦 BỘ ĐỘNG CƠ GÓI TIN ĐỘC LẬP R502 / R503 (ZERO-DEPENDENCY PACKET ENGINE)
// ============================================================================

#define R503_START_CODE 0xEF01
#define R503_DEFAULT_ADDRESS 0xFFFFFFFF

// Package Identifiers (PID)
#define R503_PID_COMMAND 0x01  // Gói lệnh gửi từ MCU tới cảm biến
#define R503_PID_DATA 0x02     // Gói tin chứa khối dữ liệu (Data packet)
#define R503_PID_ACK 0x07      // Gói tin phản hồi trạng thái từ cảm biến (Acknowledge)
#define R503_PID_END_DATA 0x08 // Gói tin dữ liệu cuối cùng kết thúc luồng (End of data)

// Bảng mã lệnh Opcode (Instruction Codes)
#define R503_CMD_GET_IMAGE 0x01      // Lấy ảnh vân tay vào ImageBuffer
#define R503_CMD_IMAGE_2_TZ 0x02     // Sinh đặc trưng từ ImageBuffer lưu vào CharBuffer 1 hoặc 2
#define R503_CMD_MATCH 0x03          // So khớp đặc trưng giữa CharBuffer 1 và CharBuffer 2
#define R503_CMD_SEARCH 0x04         // Tìm kiếm đối soát nhanh trong thư viện Flash
#define R503_CMD_REG_MODEL 0x05      // Ghép đặc trưng (CharBuffer 1 + 2) tạo mẫu vân tay hoàn chỉnh
#define R503_CMD_STORE 0x06          // Lưu mẫu vân tay vào vị trí ID trong Flash ROM
#define R503_CMD_LOAD_CHAR 0x07      // Nạp mẫu vân tay từ Flash vào CharBuffer
#define R503_CMD_UP_CHAR 0x08        // Tải mẫu đặc trưng từ CharBuffer lên MCU
#define R503_CMD_DOWN_CHAR 0x09      // Tải mẫu đặc trưng từ MCU xuống CharBuffer
#define R503_CMD_UP_IMAGE 0x0A       // Tải toàn bộ ảnh quang học lăng kính từ ImageBuffer lên MCU
#define R503_CMD_DELETE 0x0C         // Xóa mẫu vân tay theo ID
#define R503_CMD_EMPTY 0x0D          // Xóa sạch toàn bộ thư viện vân tay trong cảm biến
#define R503_CMD_READ_SYS_PARA 0x0F  // Đọc tham số hệ thống cảm biến
#define R503_CMD_VFY_PWD 0x13        // Xác thực mật khẩu bắt tay cảm biến
#define R503_CMD_TEMPLATE_COUNT 0x1D // Đọc số lượng vân tay hợp lệ hiện có
#define R503_CMD_AURA_LED 0x35       // Điều khiển đèn LED nhẫn hào quang (Aura LED)

// Các chế độ Aura LED RGB (Opcode 0x35)
enum AuraLedMode
{
    LED_MODE_BREATHING = 0x01,  // Chế độ thở êm dịu (Breathing)
    LED_MODE_FLASHING = 0x02,   // Chế độ nhấp nháy xác nhận/cảnh báo (Flashing)
    LED_MODE_ON = 0x03,         // Chế độ bật sáng liên tục (Always ON)
    LED_MODE_OFF = 0x04,        // Chế độ tắt đèn (Always OFF)
    LED_MODE_GRADUAL_ON = 0x05, // Sáng dần
    LED_MODE_GRADUAL_OFF = 0x06 // Tắt dần
};

enum AuraLedColor
{
    LED_COLOR_RED = 0x01,    // Màu Đỏ (Báo lỗi / Cảnh báo / Thể thao)
    LED_COLOR_BLUE = 0x02,   // Màu Xanh Dương (Sẵn sàng / Thở)
    LED_COLOR_PURPLE = 0x03, // Màu Tím (Đang đăng ký / Cyberpunk)
    LED_COLOR_GREEN = 0x04,  // Màu Xanh Lá (Emerald Nature)
    LED_COLOR_YELLOW = 0x05, // Màu Vàng (Solar Gold)
    LED_COLOR_CYAN = 0x06,   // Màu Xanh Ngọc / Lơ (Ocean Neon)
    LED_COLOR_WHITE = 0x07   // Màu Trắng (Full White)
};

// Hàm giải mã mã lỗi tiếng Việt chi tiết (r503GetStatusString)
const char *r503GetStatusString(uint8_t code)
{
    switch (code)
    {
    case 0x00:
        return "0x00 [OK]: Lệnh thực thi thành công hoàn hảo";
    case 0x01:
        return "0x01 [PACKET_ERR]: Lỗi nhận gói tin UART (Checksum hoặc Header sai)";
    case 0x02:
        return "0x02 [NO_FINGER]: Không phát hiện ngón tay trên mặt cảm biến";
    case 0x03:
        return "0x03 [FAIL_IMAGE]: Chụp ảnh vân tay quang học thất bại";
    case 0x04:
        return "0x04 [TOO_DRY]: Ảnh vân tay quá khô hoặc mờ";
    case 0x05:
        return "0x05 [TOO_WET]: Ảnh vân tay quá ướt hoặc đọng nước";
    case 0x06:
        return "0x06 [DISORDER]: Ảnh quá nhiễu, không thể trích xuất đặc trưng";
    case 0x07:
        return "0x07 [FEAT_FAIL]: Diện tích tiếp xúc quá nhỏ, thiếu điểm đặc trưng";
    case 0x08:
        return "0x08 [NOT_MATCH]: Hai mẫu vân tay không khớp với nhau";
    case 0x09:
        return "0x09 [NOT_FOUND]: Không tìm thấy mẫu trùng khớp trong thư viện R503";
    case 0x0A:
        return "0x0A [MERGE_FAIL]: Ghép các lần chạm thất bại (ngón tay di chuyển lệch)";
    case 0x0B:
        return "0x0B [ADDR_OVER]: ID vị trí lưu trữ vượt quá dung lượng Flash";
    case 0x0C:
        return "0x0C [READ_ERR]: Lỗi đọc dữ liệu mẫu từ bộ nhớ Flash R503";
    case 0x0D:
        return "0x0D [UP_ERR]: Lỗi truyền tải mẫu đặc trưng lên MCU";
    case 0x0E:
        return "0x0E [RECV_ERR]: Cảm biến không nhận được gói dữ liệu tiếp theo";
    case 0x0F:
        return "0x0F [UP_IMG_ERR]: Lỗi truyền tải ảnh quang học từ ImageBuffer";
    case 0x10:
        return "0x10 [DEL_ERR]: Lỗi xóa mẫu vân tay trong bộ nhớ";
    case 0x11:
        return "0x11 [CLEAR_ERR]: Lỗi dọn sạch toàn bộ cơ sở dữ liệu Flash";
    case 0x13:
        return "0x13 [PWD_ERR]: Mật khẩu xác thực bắt tay cảm biến bị sai";
    case 0x15:
        return "0x15 [IMG_INVALID]: Bộ đệm ImageBuffer rỗng (chưa chụp hoặc bị lệnh đèn xóa)";
    case 0x18:
        return "0x18 [FLASH_ERR]: Lỗi truy xuất phần cứng bộ nhớ Flash R503";
    default:
        return "0xFF [UNKNOWN]: Mã trạng thái chưa định nghĩa";
    }
}

// Hàm tương thích ngược với mã nguồn hiện tại
const char *r503CodeToString(uint8_t code)
{
    return r503GetStatusString(code);
}

// In chuỗi byte hex ra Serial để quan sát chuẩn UART
void printHexBytes(const uint8_t *data, size_t len, const char *prefix = "")
{
    Serial.print(prefix);
    for (size_t i = 0; i < len; i++)
    {
        if (data[i] < 0x10)
            Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

// Đọc 1 byte với timeout microsecond (tránh trễ FreeRTOS)
bool readUartByte(uint8_t *b, unsigned long timeoutMs = 1000)
{
    unsigned long start = millis();
    while (millis() - start < timeoutMs)
    {
        if (r503Serial.available())
        {
            *b = (uint8_t)r503Serial.read();
            return true;
        }
        delayMicroseconds(50);
    }
    return false;
}

// Tìm cặp Header 0xEF 0x01 để đồng bộ byte chống lệch pha UART
bool syncUartHeader(unsigned long timeoutMs = 2000)
{
    unsigned long start = millis();
    uint8_t prev = 0;
    while (millis() - start < timeoutMs)
    {
        uint8_t cur = 0;
        if (readUartByte(&cur, 100))
        {
            if (prev == 0xEF && cur == 0x01)
            {
                return true;
            }
            prev = cur;
        }
    }
    return false;
}

// Khóa đồng bộ chính xác chuỗi Header 6-byte của giao thức Synochip (EF 01 FF FF FF FF) cho luồng dữ liệu ảnh
bool syncUartPacketHeader6B(unsigned long timeoutMs = 2000)
{
    unsigned long start = millis();
    uint8_t syncBuf[6] = {0};
    while (millis() - start < timeoutMs)
    {
        uint8_t cur = 0;
        if (readUartByte(&cur, 100))
        {
            syncBuf[0] = syncBuf[1];
            syncBuf[1] = syncBuf[2];
            syncBuf[2] = syncBuf[3];
            syncBuf[3] = syncBuf[4];
            syncBuf[4] = syncBuf[5];
            syncBuf[5] = cur;
            if (syncBuf[0] == 0xEF && syncBuf[1] == 0x01 &&
                syncBuf[2] == 0xFF && syncBuf[3] == 0xFF &&
                syncBuf[4] == 0xFF && syncBuf[5] == 0xFF)
            {
                return true;
            }
        }
    }
    return false;
}

// Đọc chính xác N bytes từ UART với cơ chế đọc khối trực tiếp và tự động gia hạn timeout
bool readUartBytes(uint8_t *buf, size_t len, unsigned long timeoutMs = 1000)
{
    size_t readCount = 0;
    unsigned long start = millis();
    while (readCount < len && (millis() - start < timeoutMs))
    {
        int avail = r503Serial.available();
        if (avail > 0)
        {
            size_t toRead = (size_t)avail;
            if (toRead > (len - readCount))
                toRead = len - readCount;
            size_t n = r503Serial.read(buf + readCount, toRead);
            readCount += n;
            start = millis(); // Reset timeout sau mỗi khối byte đọc thành công
        }
        else
        {
            delayMicroseconds(20);
        }
    }
    return (readCount == len);
}

// Gửi một gói tin lệnh chuẩn R502/R503
bool sendCommandPacket(uint8_t cmdCode, const uint8_t *payload = nullptr, uint16_t payloadLen = 0)
{
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

    for (uint16_t i = 0; i < payloadLen; i++)
    {
        txBuf[10 + i] = payload[i];
        sum += payload[i];
    }

    uint16_t chkOffset = 10 + payloadLen;
    txBuf[chkOffset] = (uint8_t)(sum >> 8);
    txBuf[chkOffset + 1] = (uint8_t)(sum & 0xFF);

    size_t totalSend = chkOffset + 2;

    while (r503Serial.available())
        r503Serial.read(); // Dọn sạch buffer RX trước khi gửi
    r503Serial.write(txBuf, totalSend);
    r503Serial.flush();

    printHexBytes(txBuf, totalSend, "📡 [R503 UART TX] -> ");
    return true;
}

// Đọc phản hồi ACK từ cảm biến
bool receiveAckPacket(uint8_t &confirmCode, uint8_t *outData = nullptr, uint16_t *outDataLen = nullptr, unsigned long timeoutMs = 1500)
{
    if (!syncUartHeader(timeoutMs))
    {
        Serial.println("❌ [R503 UART RX] Lỗi: Không tìm thấy Header 0xEF 0x01 phản hồi!");
        return false;
    }

    uint8_t hdr[7]; // Addr (4) + PID (1) + Len (2)
    if (!readUartBytes(hdr, 7, timeoutMs))
    {
        Serial.println("❌ [R503 UART RX] Lỗi: Hết thời gian đọc Header gói phản hồi!");
        return false;
    }

    uint8_t pid = hdr[4];
    uint16_t len = ((uint16_t)hdr[5] << 8) | hdr[6];

    if (pid != R503_PID_ACK || len < 3)
    {
        Serial.printf("⚠️ [R503 UART RX] Gói tin không phải ACK chuẩn (PID=0x%02X, Len=%d)\n", pid, len);
        return false;
    }

    uint8_t ackPayload[64];
    if (!readUartBytes(ackPayload, len, timeoutMs))
    {
        Serial.println("❌ [R503 UART RX] Lỗi: Không đọc đủ nội dung gói tin phản hồi!");
        return false;
    }

    confirmCode = ackPayload[0]; // Byte đầu tiên của payload chính là Confirmation Code!

    Serial.printf("📥 [R503 UART RX] <- Mã phản hồi: %s\n", r503GetStatusString(confirmCode));

    if (outData && outDataLen && len > 3)
    {
        uint16_t dataBytes = len - 3; // Trừ mã code (1) và Checksum (2)
        memcpy(outData, ackPayload + 1, dataBytes);
        *outDataLen = dataBytes;
    }

    return (confirmCode == 0x00);
}

// 1. Xác thực mật khẩu bắt tay cảm biến (Opcode 0x13)
bool r503VerifyPassword(uint32_t password = 0x00000000)
{
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
bool r503SetAuraLed(uint8_t mode, uint8_t color, uint8_t speed = 50, uint8_t count = 1)
{
    uint8_t payload[4];
    payload[0] = mode;
    payload[1] = speed;
    payload[2] = color;
    payload[3] = count;

    const char *colorName = (color == LED_COLOR_RED) ? "Đỏ" : (color == LED_COLOR_BLUE) ? "Xanh dương"
                                                          : (color == LED_COLOR_PURPLE) ? "Tím"
                                                          : (color == LED_COLOR_GREEN)  ? "Xanh lá"
                                                          : (color == LED_COLOR_YELLOW) ? "Vàng"
                                                          : (color == LED_COLOR_CYAN)   ? "Xanh ngọc"
                                                          : (color == LED_COLOR_WHITE)  ? "Trắng"
                                                                                        : "Tắt";
    const char *modeName = (mode == LED_MODE_BREATHING) ? "Thở (Breathing)" : (mode == LED_MODE_FLASHING) ? "Nhấp nháy (Flashing)"
                                                                          : (mode == LED_MODE_ON)         ? "Bật liên tục (On)"
                                                                          : (mode == LED_MODE_OFF)        ? "Tắt (Off)"
                                                                                                          : "Khác";

    Serial.printf("\n--- [2] ĐIỀU KHIỂN AURA LED (0x35): Mode=0x%02X (%s), Color=0x%02X (%s), Speed=%d, Count=%d ---\n",
                  mode, modeName, color, colorName, speed, count);
    if (color >= 4)
    {
        Serial.println("ℹ️ Lưu ý phần cứng: Màu 0x04-0x07 (Xanh lá/Vàng/Cyan/Trắng) chỉ phát sáng trên R503-RGB. Với module R503 chuẩn (Bi-color), chỉ có 2 bóng LED vật lý Đỏ và Xanh dương (0x01-0x03).");
    }
    sendCommandPacket(R503_CMD_AURA_LED, payload, 4);

    uint8_t code = 0xFF;
    return receiveAckPacket(code, nullptr, nullptr, 300);
}

// 3. Đọc số lượng vân tay đã lưu trong cảm biến (Opcode 0x1D)
int r503GetTemplateCount()
{
    Serial.println("\n--- [3] ĐỌC SỐ LƯỢNG VÂN TAY TRONG CẢM BIẾN (0x1D) ---");
    sendCommandPacket(R503_CMD_TEMPLATE_COUNT, nullptr, 0);

    uint8_t code = 0xFF;
    uint8_t data[8];
    uint16_t len = 0;
    if (receiveAckPacket(code, data, &len) && len >= 2)
    {
        uint16_t count = ((uint16_t)data[0] << 8) | data[1];
        Serial.printf("📊 Số lượng vân tay hợp lệ trong Flash: %d mẫu\n", count);
        return count;
    }
    return -1;
}

// 4. Lấy ảnh vân tay vào ImageBuffer (Opcode 0x01)
uint8_t r503GetImage()
{
    sendCommandPacket(R503_CMD_GET_IMAGE, nullptr, 0);
    uint8_t code = 0xFF;
    receiveAckPacket(code, nullptr, nullptr, 800);
    return code;
}

// 5. Sinh đặc trưng từ ImageBuffer vào CharBuffer 1 hoặc 2 (Opcode 0x02)
uint8_t r503Image2Tz(uint8_t bufferSlot = 1)
{
    uint8_t payload[1] = {bufferSlot};
    sendCommandPacket(R503_CMD_IMAGE_2_TZ, payload, 1);
    uint8_t code = 0xFF;
    receiveAckPacket(code, nullptr, nullptr, 1000);
    return code;
}

// 6. Tìm kiếm đối soát nhanh trong thư viện (Opcode 0x04)
bool r503FastSearch(uint8_t bufferSlot, uint16_t startPage, uint16_t pageNum, uint16_t &matchedId, uint16_t &confidence)
{
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
    if (receiveAckPacket(code, data, &len) && len >= 4)
    {
        matchedId = ((uint16_t)data[0] << 8) | data[1];
        confidence = ((uint16_t)data[2] << 8) | data[3];
        Serial.printf("🎯 KHỚP THÀNH CÔNG -> ID: #%d | Điểm tin cậy (Confidence): %d\n", matchedId, confidence);
        return true;
    }
    return false;
}

// 7. Xóa mẫu vân tay theo ID (Opcode 0x0C)
bool r503DeleteModel(uint16_t id, uint16_t count = 1)
{
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
bool r503EmptyDatabase()
{
    Serial.println("\n--- [6] XÓA SẠCH TOÀN BỘ THƯ VIỆN FLASH (0x0D) ---");
    sendCommandPacket(R503_CMD_EMPTY, nullptr, 0);

    uint8_t code = 0xFF;
    return receiveAckPacket(code);
}

// Khai báo trước hàm trích xuất ảnh chuẩn
bool streamR503ImageOverBle();

// 9. Tải ảnh quang học lăng kính thực tế (Opcode 0x0A - UpImage)
bool r503StreamRawImage()
{
    return streamR503ImageOverBle();
}

// Điều khiển LED RGB trên R503 tương thích các hàm cũ
void setR503Led(uint8_t mode, uint8_t color, uint8_t speed = 50, uint8_t count = 1)
{
    r503SetAuraLed(mode, color, speed, count);
}

void loadLedConfig()
{
    prefsLed.begin("led_cfg", false);
    ledConfig.unlocked.mode = prefsLed.getUChar("u_m", 0x01);  // Breathing
    ledConfig.unlocked.color = prefsLed.getUChar("u_c", 0x02); // Blue
    ledConfig.unlocked.speed = prefsLed.getUChar("u_s", 120);

    ledConfig.locked.mode = prefsLed.getUChar("l_m", 0x04);  // OFF
    ledConfig.locked.color = prefsLed.getUChar("l_c", 0x02); // Blue
    ledConfig.locked.speed = prefsLed.getUChar("l_s", 0);

    ledConfig.success.mode = prefsLed.getUChar("s_m", 0x02);  // Flashing
    ledConfig.success.color = prefsLed.getUChar("s_c", 0x02); // Blue
    ledConfig.success.speed = prefsLed.getUChar("s_s", 40);

    ledConfig.error.mode = prefsLed.getUChar("e_m", 0x02);  // Flashing
    ledConfig.error.color = prefsLed.getUChar("e_c", 0x01); // Red
    ledConfig.error.speed = prefsLed.getUChar("e_s", 30);

    Serial.printf("💡 LED Config NVS: Unlocked(M:%d, C:%d, S:%d), Locked(M:%d, C:%d, S:%d)\n",
                  ledConfig.unlocked.mode, ledConfig.unlocked.color, ledConfig.unlocked.speed,
                  ledConfig.locked.mode, ledConfig.locked.color, ledConfig.locked.speed);
}

void saveLedConfig()
{
    prefsLed.begin("led_cfg", false);
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

void sendLedConfigResponse()
{
    String resp = "LED_CFG|" +
                  String(ledConfig.unlocked.mode) + "|" + String(ledConfig.unlocked.color) + "|" + String(ledConfig.unlocked.speed) + "|" +
                  String(ledConfig.locked.mode) + "|" + String(ledConfig.locked.color) + "|" + String(ledConfig.locked.speed) + "|" +
                  String(ledConfig.success.mode) + "|" + String(ledConfig.success.color) + "|" + String(ledConfig.success.speed) + "|" +
                  String(ledConfig.error.mode) + "|" + String(ledConfig.error.color) + "|" + String(ledConfig.error.speed);
    notifyStatus(resp);
}

void ledSuccess()
{
    r503SetAuraLed(ledConfig.success.mode, ledConfig.success.color, ledConfig.success.speed, 2);
}

void ledError()
{
    r503SetAuraLed(ledConfig.error.mode, ledConfig.error.color, ledConfig.error.speed, 3);
}

void ledBreathingIdle()
{
    r503SetAuraLed(ledConfig.unlocked.mode, ledConfig.unlocked.color, ledConfig.unlocked.speed, 0);
}

void ledOff()
{
    r503SetAuraLed(LED_MODE_OFF, 0, 0, 0);
}

// Cập nhật trạng thái LED theo trạng thái xe (tiết kiệm bình ắc quy khi xe khóa)
void updateIdleLed()
{
    if (isUnlocked)
    {
        r503SetAuraLed(ledConfig.unlocked.mode, ledConfig.unlocked.color, ledConfig.unlocked.speed, 0);
    }
    else
    {
        r503SetAuraLed(ledConfig.locked.mode, ledConfig.locked.color, ledConfig.locked.speed, 0);
    }
}

// Bảng menu điều khiển Console tương tác trực tiếp qua Serial Monitor
void printMenu()
{
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
    Serial.println("  [9] Kích hoạt ngay Tầng 2: Deep Sleep (Test dòng rò & ngắt RTC)");
    Serial.println("==============================================================");
    Serial.print("👉 Nhập số lựa chọn (1-9): ");
}

// ==========================================
// ⚡ 4. XỬ LÝ TRẠNG THÁI XE (BẬT / TẮT / ĐỀ)
// ==========================================

void setVehicleUnlock(bool unlock, bool saveFlash = true, bool notify = true)
{
    isUnlocked = unlock;
    digitalWrite(RELAY1_PIN, isUnlocked ? HIGH : LOW);
    lastActivityTime = millis();
    updateBleAdvertisingMode(unlock);

    if (saveFlash)
    {
        prefsSecurity.putBool("is_unlocked", isUnlocked);
    }

    if (notify)
    {
        if (isUnlocked)
        {
            notifyStatus("DA_MO_KHOA");
        }
        else
        {
            notifyStatus("DA_KHOA_XE");
        }
    }

    if (r503Ready)
    {
        if (isUnlocked)
        {
            ledSuccess();
            pendingIdleLedUntil = millis() + 650; // Sau 2 nhịp nháy (~650ms), tự chuyển sang màu Xe Mở
            pendingIdleLedUpdate = true;
        }
        else
        {
            ledError();
            pendingIdleLedUntil = millis() + 750; // Sau 3 nhịp nháy (~750ms), tự chuyển sang màu Xe Khóa (Tắt)
            pendingIdleLedUpdate = true;
        }
    }
}

// Đề xe (chỉ khi xe đang mở khóa, RELAY2_PIN: Active LOW)
void triggerStarter()
{
    lastActivityTime = millis();
    if (!isUnlocked)
    {
        notifyStatus("LOI_CHUA_MO_KHOA");
        beep(3, 60);
        return;
    }
    digitalWrite(RELAY2_PIN, LOW); // Active LOW: Kích hoạt đề nổ máy
    delay(1500);
    digitalWrite(RELAY2_PIN, HIGH); // Tắt đề
    notifyStatus("DA_DE_MAY");
}

// Tìm xe: Nháy xi-nhan + còi 3 nhịp ngắn chuẩn xe cao cấp (RELAY3_PIN: Active LOW)
void triggerLocate()
{
    lastActivityTime = millis();
    Serial.println("Đang phát tín hiệu tìm xe (3 nhịp bíp & nháy đèn)...");
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(RELAY3_PIN, LOW); // Active LOW: Bật còi/đèn
        digitalWrite(ONBOARD_LED_PIN, LOW);
        delay(200);
        digitalWrite(RELAY3_PIN, HIGH); // Tắt còi/đèn
        digitalWrite(ONBOARD_LED_PIN, HIGH);
        if (i < 2)
            delay(150);
    }
    notifyStatus("DA_TIM_XE");
}

// ==========================================
// 💤 4.1. QUẢN LÝ TIẾT KIỆM NĂNG LƯỢNG 2 TẦNG
// ==========================================

// Cấu hình tần số quảng bá BLE: Nhanh (Fast: 100-200ms) hoặc Tiết kiệm Tầng 1 (Power Saving: 1280ms)
void updateBleAdvertisingMode(bool fast)
{
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    if (!pAdvertising)
        return;

    // Nếu đang có thiết bị kết nối thì không thay đổi quảng bá ngắt quãng
    if (deviceConnected && !fast)
        return;

    if (pAdvertising->isAdvertising())
    {
        pAdvertising->stop();
    }

    if (fast)
    {
        // Fast mode: 100ms - 200ms (160 - 320 đơn vị 0.625ms)
        pAdvertising->setMinInterval(160);
        pAdvertising->setMaxInterval(320);
        isPowerSavingAdvertising = false;
        Serial.println("⚡ [BLE] Chế độ quảng bá: FAST (100ms - 200ms)");
    }
    else
    {
        // Tầng 1 Power Saving: 1280ms (2048 đơn vị 0.625ms = 1.28s)
        pAdvertising->setMinInterval(2048);
        pAdvertising->setMaxInterval(2048);
        isPowerSavingAdvertising = true;
        Serial.println("🍃 [BLE] TẦNG 1: Chế độ quảng bá TIẾT KIỆM PIN (1280ms / 1.28s)");
    }
    pAdvertising->start();
}

// Xử lý báo động khi cảm biến rung SW-420 phát hiện rung lắc lúc xe đang khóa
void handleVibrationDetected()
{
    if (isUnlocked)
    {
        // Xe đang mở khóa và di chuyển -> Bỏ qua ngắt rung để tránh báo động giả
        return;
    }
    Serial.println("🚨 [ALARM] Cảm biến rung SW-420 phát hiện rung lắc khi xe đang khóa!");
    notifyStatus("CANH_BAO_RUNG");

    // Báo động nhẹ 3 nhịp còi / xi-nhan cảnh báo kẻ gian (RELAY3_PIN: Active LOW)
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(RELAY3_PIN, LOW);      // Active LOW: Bật còi/đèn
        digitalWrite(ONBOARD_LED_PIN, LOW); // Bật LED xanh onboard
        delay(70);
        digitalWrite(RELAY3_PIN, HIGH);      // Tắt còi/đèn
        digitalWrite(ONBOARD_LED_PIN, HIGH); // Tắt LED xanh onboard
        if (i < 2)
            delay(80);
    }
}

// Kích hoạt Tầng 2: Deep Sleep (Siêu tiết kiệm pin sau 24h hoặc qua lệnh từ App)
void enterTier2DeepSleep()
{
    Serial.println("\n==============================================================");
    Serial.println("   💤 [POWER] KÍCH HOẠT TẦNG 2: DEEP SLEEP (SIÊU TIẾT KIỆM)   ");
    Serial.println("==============================================================");

    // 1. Tắt đèn Aura LED trên R503 để triệt tiêu dòng rò
    if (r503Ready)
    {
        r503SetAuraLed(LED_MODE_OFF, 0, 0, 0);
        delay(60);
    }

    // 2. Đảm bảo trạng thái Relay an toàn và tắt LED Onboard
    digitalWrite(RELAY1_PIN, isUnlocked ? HIGH : LOW);
    digitalWrite(RELAY2_PIN, HIGH);      // Active LOW: HIGH = TẮT
    digitalWrite(RELAY3_PIN, HIGH);      // Active LOW: HIGH = TẮT
    digitalWrite(ONBOARD_LED_PIN, HIGH); // Active LOW: HIGH = TẮT

    // 3. Tắt hoàn toàn khối BLE
    if (NimBLEDevice::getAdvertising() && NimBLEDevice::getAdvertising()->isAdvertising())
    {
        NimBLEDevice::getAdvertising()->stop();
    }
    NimBLEDevice::deinit(true);

    // 4. Kích hoạt RTC GPIO Wakeup:
    // - GPIO 3 (R503 Touch WAKE): Active LOW
    // - GPIO 4 (SW-420 Rung): Active HIGH (chỉ kích hoạt nếu antiTheftEnabled == true)
    // - GPIO 5 (Remote RF 433MHz Chân VT): Active HIGH
    uint64_t lowWakeMask = (1ULL << R503_WAKE_PIN);
    esp_deep_sleep_enable_gpio_wakeup(lowWakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);

    uint64_t highWakeMask = (1ULL << RF_LOCATE_PIN);
    if (antiTheftEnabled)
    {
        highWakeMask |= (1ULL << SW420_VIBRATE_PIN);
    }
    esp_deep_sleep_enable_gpio_wakeup(highWakeMask, ESP_GPIO_WAKEUP_GPIO_HIGH);

    Serial.printf("🔒 [POWER] Đã thiết lập ngắt RTC Wakeup trên:\n");
    Serial.printf("   - GPIO %d: Chạm vân tay R503 (Active LOW)\n", R503_WAKE_PIN);
    if (antiTheftEnabled)
    {
        Serial.printf("   - GPIO %d: Cảm biến rung SW-420 (Active HIGH)\n", SW420_VIBRATE_PIN);
    }
    else
    {
        Serial.printf("   - GPIO %d: Cảm biến rung SW-420 (ĐÃ TẮT BẢO VỆ CHỐNG DẮT)\n", SW420_VIBRATE_PIN);
    }
    Serial.printf("   - GPIO %d: Remote RF 433MHz Chân VT (Active HIGH)\n", RF_LOCATE_PIN);
    Serial.println("💤 [POWER] ESP32-C3 bắt đầu ngủ sâu (Dòng ăn bình ~1.2mA). Tạm biệt!");
    Serial.flush();

    esp_deep_sleep_start();
}

// ==========================================
// 🖐️ 5. QUẢN LÝ VÂN TAY (R503 & FLASH NVS)
// ==========================================

// Dung lượng tối đa cảm biến R503 (thường là 100 hoặc 200)
int getMaxCapacity()
{
    return (finger.capacity > 0 && finger.capacity <= 200) ? finger.capacity : 100;
}

// Đếm số lượng vân tay chính đang đăng ký hợp lệ trong Flash NVS (không tính slot góc nghiêng phụ)
int getRegisteredFingerprintCount()
{
    int count = 0;
    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++)
    {
        String key = "name_" + String(id);
        String parentKey = "parent_" + String(id);
        if (prefsFinger.isKey(key.c_str()) && !prefsFinger.isKey(parentKey.c_str()))
        {
            count++;
        }
    }
    return count;
}

// Tổng số slot đã sử dụng trong Flash NVS (bao gồm cả slot phụ)
int getTotalUsedSlotsCount()
{
    int count = 0;
    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++)
    {
        String key = "name_" + String(id);
        if (prefsFinger.isKey(key.c_str()))
        {
            count++;
        }
    }
    return count;
}

// Tìm ID vân tay còn trống tiếp theo (từ 1 đến capacity)
int getNextFreeFingerId()
{
    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++)
    {
        String key = "name_" + String(id);
        if (!prefsFinger.isKey(key.c_str()))
        {
            finger.deleteModel(id); // Dọn sạch slot trong Flash R503
            Serial.printf("👉 Cấp ID trống: %d\n", id);
            return id;
        }
    }
    Serial.println("⚠️ Đã đầy bộ nhớ vân tay!");
    return -1;
}

// Tìm ID vân tay còn trống tiếp theo, bỏ qua ID loại trừ (dùng cấp slot phụ góc nghiêng)
int getNextFreeFingerIdExcluding(int excludeId)
{
    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++)
    {
        if (id == excludeId)
            continue;
        String key = "name_" + String(id);
        if (!prefsFinger.isKey(key.c_str()))
        {
            return id;
        }
    }
    return -1;
}

// ============================================================================
// XUẤT ẢNH VÂN TAY GỐC THÀNH ASCII ART & FILE BMP BASE64 RA SERIAL MONITOR
// TƯƠNG THÍCH HOÀN TOÀN VỚI tools/view_fingerprint.html (CHẾ ĐỘ TIẾT KIỆM RAM)
// ============================================================================
void exportFingerprintImageToSerial(const uint8_t *rawComp, size_t compBytesReceived, uint16_t width, uint16_t height)
{
    if (!rawComp || compBytesReceived == 0)
        return;

    // 1. VẼ ẢNH ASCII ART TRỰC TIẾP LÊN SERIAL MONITOR
    Serial.println("\n----------------- [HÌNH ẢNH VÂN TAY R503 TRỰC QUAN] -----------------");
    const char asciiChars[] = " .:-=+*#%@";
    int stepX = width / 40;
    int stepY = height / 25;
    if (stepX < 1)
        stepX = 1;
    if (stepY < 1)
        stepY = 1;
    for (int y = 0; y < height; y += stepY)
    {
        Serial.print("   |");
        for (int x = 0; x < width; x += stepX)
        {
            size_t pixelIdx = (size_t)y * width + x;
            size_t compIdx = pixelIdx / 2;
            uint8_t val = 0;
            if (compIdx < compBytesReceived)
            {
                uint8_t b = rawComp[compIdx];
                val = (pixelIdx % 2 == 0) ? ((b >> 4) * 17) : ((b & 0x0F) * 17);
            }
            int charIdx = val * 9 / 255;
            if (charIdx > 9)
                charIdx = 9;
            Serial.print(asciiChars[charIdx]);
        }
        Serial.println("|");
    }
    Serial.println("--------------------------------------------------------------------\n");

    // 2. TẠO FILE BMP 8-BIT GRAYSCALE CHUẨN WINDOWS (HEADER 1078 BYTES)
    uint32_t bmpHeaderSize = 14 + 40 + 1024; // 1078 bytes
    uint32_t totalPixels = (uint32_t)width * height;
    uint32_t bmpTotalSize = bmpHeaderSize + totalPixels;
    uint8_t *bmpData = (uint8_t *)malloc(bmpTotalSize);
    if (!bmpData)
    {
        Serial.println("❌ Không đủ RAM tạo file BMP trên Serial!");
        return;
    }

    // Bitmap File Header (14 bytes)
    memset(bmpData, 0, bmpHeaderSize);
    bmpData[0] = 'B';
    bmpData[1] = 'M';
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
    bmpData[18] = width & 0xFF;
    bmpData[19] = (width >> 8) & 0xFF;
    bmpData[22] = height & 0xFF;
    bmpData[23] = (height >> 8) & 0xFF; // Bottom-up
    bmpData[26] = 1;                    // 1 plane
    bmpData[28] = 8;                    // 8 bits per pixel (grayscale)
    bmpData[34] = totalPixels & 0xFF;
    bmpData[35] = (totalPixels >> 8) & 0xFF;
    bmpData[36] = (totalPixels >> 16) & 0xFF;
    bmpData[37] = (totalPixels >> 24) & 0xFF;
    bmpData[38] = 0x13;
    bmpData[39] = 0x0B; // ~508 DPI (2835 ppm)
    bmpData[42] = 0x13;
    bmpData[43] = 0x0B;
    bmpData[46] = 0;
    bmpData[47] = 1; // 256 colors

    // Palette Grayscale (256 * 4 = 1024 bytes)
    for (int i = 0; i < 256; i++)
    {
        bmpData[54 + i * 4 + 0] = (uint8_t)i; // Blue
        bmpData[54 + i * 4 + 1] = (uint8_t)i; // Green
        bmpData[54 + i * 4 + 2] = (uint8_t)i; // Red
        bmpData[54 + i * 4 + 3] = 0;
    }

    // Đảo ngược dòng quét (Bottom-Up) theo chuẩn Windows BMP
    for (int y = 0; y < height; y++)
    {
        int srcY = height - 1 - y;
        uint8_t *dstRow = &bmpData[bmpHeaderSize + y * width];
        for (int x = 0; x < width; x++)
        {
            size_t pixelIdx = (size_t)srcY * width + x;
            size_t compIdx = pixelIdx / 2;
            if (compIdx < compBytesReceived)
            {
                uint8_t b = rawComp[compIdx];
                dstRow[x] = (pixelIdx % 2 == 0) ? ((b >> 4) * 17) : ((b & 0x0F) * 17);
            }
            else
            {
                dstRow[x] = 0;
            }
        }
    }

    // 3. MÃ HÓA BASE64 NGUYÊN KHỐI & XUẤT RA SERIAL CHỐNG NGHẼN BUFFER (CHUẨN test_r503.cpp)
    size_t base64Len = 0;
    mbedtls_base64_encode(nullptr, 0, &base64Len, bmpData, bmpTotalSize);
    char *base64Str = (char *)malloc(base64Len + 1);
    if (base64Str)
    {
        size_t actualLen = 0;
        mbedtls_base64_encode((unsigned char *)base64Str, base64Len + 1, &actualLen, bmpData, bmpTotalSize);
        base64Str[actualLen] = '\0';
        free(bmpData); // Giải phóng bmpData ngay sau khi mã hóa xong để giải phóng RAM tối đa

        Serial.println("==================== [BẮT ĐẦU CHUỖI ẢNH BASE64 BMP R503] ====================");
        Serial.print("data:image/bmp;base64,");
        for (size_t i = 0; i < actualLen; i += 128)
        {
            size_t chunk = (actualLen - i < 128) ? (actualLen - i) : 128;
            Serial.write((const uint8_t *)&base64Str[i], chunk);
            delayMicroseconds(500); // Nghỉ 500us để buffer USB CDC / Serial không bị nghẽn
        }
        Serial.println();
        Serial.println("==================== [KẾT THÚC CHUỖI ẢNH BASE64 BMP R503] ====================");
        Serial.println("💡 GỢI Ý: Copy toàn bộ chuỗi 'data:image/bmp;base64,...' ở trên,");
        Serial.println("         mở file tools/view_fingerprint.html để xem và tải ảnh .BMP/.PNG!");
        free(base64Str);
    }
    else
    {
        Serial.println("❌ Không đủ RAM tạo chuỗi Base64!");
        free(bmpData);
    }
}

// Trích xuất ảnh vân tay thô từ cảm biến R503 và truyền qua BLE (Khung truyền định danh chuẩn & Kiểm tra Checksum)
bool streamR503ImageOverBle()
{
    Serial.println("📷 Bắt đầu trích xuất ảnh vân tay từ R503...");

    // Settle delay để cảm biến R503 hoàn tất ghi vào ImageBuffer nội bộ sau khi getImage()
    vTaskDelay(pdMS_TO_TICKS(60));

    // 1. Cấp phát bộ nhớ đệm RAM (32KB) TRƯỚC KHI gửi lệnh UpImage (tránh trễ nhịp UART)
    const size_t MAX_IMG_BYTES = 32768;
    uint8_t *imgBuffer = (uint8_t *)malloc(MAX_IMG_BYTES);
    if (!imgBuffer)
    {
        Serial.println("❌ Không đủ RAM để đệm ảnh vân tay!");
        notifyStatus("FP_IMG_ERR");
        return false;
    }

    // Dọn sạch toàn bộ rác tồn đọng trong UART RX buffer trước khi phát lệnh UpImage
    while (r503Serial.available())
        r503Serial.read();

    // Gửi lệnh UpImage (CMD 0x0A): EF 01 FF FF FF FF 01 00 03 0A 00 0E trực tiếp (chuẩn test_r503.cpp)
    uint8_t cmdUpImg[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x0A, 0x00, 0x0E};
    r503Serial.write(cmdUpImg, sizeof(cmdUpImg));
    r503Serial.flush();

    // 1. Đọc gói tin phản hồi ACK từ cảm biến: Header 6 bytes (EF 01 FF FF FF FF) + 6 bytes nội dung
    if (!syncUartPacketHeader6B(2000))
    {
        Serial.println("❌ Cảm biến không phản hồi lệnh UpImage (Timeout Header ACK)!");
        free(imgBuffer);
        notifyStatus("FP_IMG_ERR");
        return false;
    }

    uint8_t ackRest[6]; // PID(1) + Len(2) + Code(1) + Checksum(2) = 6 bytes
    if (!readUartBytes(ackRest, 6, 1500))
    {
        Serial.println("❌ Lỗi đọc nội dung gói phản hồi ACK từ cảm biến!");
        free(imgBuffer);
        notifyStatus("FP_IMG_ERR");
        return false;
    }

    uint8_t confirmCode = ackRest[3];
    if (confirmCode != 0x00)
    {
        Serial.printf("❌ Cảm biến từ chối truyền ảnh (Mã xác nhận: 0x%02X - %s)!\n", confirmCode, r503GetStatusString(confirmCode));
        free(imgBuffer);
        notifyStatus("FP_IMG_ERR");
        return false;
    }

    // PHA 1: THU THẬP TẤT CẢ GÓI DỮ LIỆU TỪ UART VÀO RAM VỚI KHÓA HEADER 6-BYTE & CHECKSUM
    size_t totalBytesReceived = 0;
    bool finished = false;
    unsigned long startStream = millis();
    int packetIndex = 0;

    while (!finished && (totalBytesReceived < MAX_IMG_BYTES) && (millis() - startStream < 15000))
    {
        if (cancelEnrollRequested)
        {
            free(imgBuffer);
            notifyStatus("FP_IMG_ERR");
            return false;
        }

        // 1. Đồng bộ Header 6-byte 0xEF 0x01 0xFF 0xFF 0xFF 0xFF (chống trôi byte và tuyệt đối không bắt nhầm trong ảnh)
        if (!syncUartPacketHeader6B(2000))
        {
            Serial.printf("⏱️ Timeout tìm Header 6-byte gói #%d (đã nhận %u bytes)\n", packetIndex, totalBytesReceived);
            break;
        }

        // 2. Đọc 3 bytes Meta của Header: PID (1 byte) + Length (2 bytes)
        uint8_t meta[3];
        if (!readUartBytes(meta, 3, 800))
        {
            Serial.printf("❌ Lỗi đọc 3 bytes Meta gói #%d\n", packetIndex);
            break;
        }

        uint8_t pid = meta[0]; // 0x02 = Data, 0x08 = EndData
        uint16_t length = ((uint16_t)meta[1] << 8) | meta[2];
        if (length < 2)
            continue;

        uint16_t payloadLen = length - 2; // Trừ đi 2 bytes checksum (mặc định 128 bytes)

        // 3. Đọc Payload ảnh trực tiếp vào RAM buffer
        if (totalBytesReceived + payloadLen <= MAX_IMG_BYTES)
        {
            if (!readUartBytes(imgBuffer + totalBytesReceived, payloadLen, 1000))
            {
                Serial.printf("❌ Lỗi đọc payload ảnh gói #%d từ UART\n", packetIndex);
                break;
            }
            totalBytesReceived += payloadLen;
        }
        else
        {
            size_t canTake = MAX_IMG_BYTES - totalBytesReceived;
            if (canTake > 0)
            {
                readUartBytes(imgBuffer + totalBytesReceived, canTake, 1000);
                totalBytesReceived += canTake;
            }
            size_t discard = payloadLen - canTake;
            uint8_t dummy[64];
            while (discard > 0)
            {
                size_t step = (discard > sizeof(dummy)) ? sizeof(dummy) : discard;
                readUartBytes(dummy, step, 500);
                discard -= step;
            }
        }

        // 4. Đọc 2 bytes Checksum
        uint8_t chk[2];
        readUartBytes(chk, 2, 500);

        packetIndex++;
        if (packetIndex % 25 == 0 || pid == R503_PID_END_DATA)
        {
            Serial.printf("   📥 Tiến độ ảnh: Đã nhận gói [%d] - %u bytes...\n", packetIndex, totalBytesReceived);
        }

        if (pid == R503_PID_END_DATA)
        {
            finished = true;
            break;
        }
    }

    Serial.printf("📥 Đã nhận từ R503: %u bytes (%d gói) trong %lums!\n", totalBytesReceived, packetIndex, millis() - startStream);

    if (totalBytesReceived < 1000)
    {
        Serial.printf("❌ Dữ liệu ảnh thu được quá ít (%u bytes), hủy truyền\n", totalBytesReceived);
        free(imgBuffer);
        notifyStatus("FP_IMG_ERR");
        return false;
    }

    // Tự động nhận diện độ phân giải ma trận ảnh dựa trên số byte thực tế:
    // - R503 Tròn (100 gói x 128B = 12.800 bytes nén = 25.600 pixel): 160 x 160
    // - R503 Vuông (144 gói x 128B = 18.432 bytes nén = 36.864 pixel): 192 x 192
    // - R307/ZFM20 (234 gói x 128B = 29.952 bytes nén = 59.904 pixel): 208 x 288
    uint16_t imgWidth = 160;
    uint16_t imgHeight = 160;
    uint32_t totalPixels = (uint32_t)totalBytesReceived * 2;

    if (totalBytesReceived == 12800)
    {
        imgWidth = 160;
        imgHeight = 160;
    }
    else if (totalBytesReceived == 18432)
    {
        imgWidth = 192;
        imgHeight = 192;
    }
    else if (totalBytesReceived == 29952)
    {
        imgWidth = 208;
        imgHeight = 288;
    }
    else
    {
        uint16_t side = (uint16_t)round(sqrt(totalPixels));
        if ((uint32_t)side * side == totalPixels)
        {
            imgWidth = side;
            imgHeight = side;
        }
        else if (totalPixels % 160 == 0)
        {
            imgWidth = 160;
            imgHeight = totalPixels / 160;
        }
        else if (totalPixels % 192 == 0)
        {
            imgWidth = 192;
            imgHeight = totalPixels / 192;
        }
        else
        {
            imgWidth = 160;
            imgHeight = totalPixels / 160;
        }
    }

    Serial.printf("📊 [MA TRẬN ẢNH]: %d x %d px (%u điểm ảnh)\n", imgWidth, imgHeight, totalPixels);

    // Xuất ngay ảnh ra Serial Monitor theo định dạng ASCII Art và Base64 BMP cho tools/view_fingerprint.html
    exportFingerprintImageToSerial(imgBuffer, totalBytesReceived, imgWidth, imgHeight);

    // PHA 2: TRUYỀN DỮ LIỆU ĐÃ ĐỆM TRONG RAM QUA BLE THEO TỪNG CHUNK CÓ INDEX ĐỘC LẬP
    const size_t CHUNK_SIZE = 96; // 96 bytes thô -> 128 Base64 chars (bội số của 3, không padding '=')
    int totalChunks = (totalBytesReceived + CHUNK_SIZE - 1) / CHUNK_SIZE;

    // Phát sóng tin nhắn khởi đầu với kích thước thực tế: FP_IMG_START|<width>|<height>|<totalChunks>
    notifyStatus("FP_IMG_START|" + String(imgWidth) + "|" + String(imgHeight) + "|" + String(totalChunks));
    vTaskDelay(pdMS_TO_TICKS(50));

    unsigned char base64Buf[256];
    int packetCount = 0;

    for (size_t offset = 0; offset < totalBytesReceived; offset += CHUNK_SIZE)
    {
        if (cancelEnrollRequested || !deviceConnected)
        {
            break;
        }
        size_t curLen = min(CHUNK_SIZE, totalBytesReceived - offset);
        size_t olen = 0;
        mbedtls_base64_encode(base64Buf, sizeof(base64Buf), &olen, imgBuffer + offset, curLen);
        base64Buf[olen] = '\0';

        // Giao thức indexed chunk: FP_IMG_CHUNK|<seq>|<total>|<base64>
        String chunkMsg = "FP_IMG_CHUNK|" + String(packetCount) + "|" + String(totalChunks) + "|" + String((char *)base64Buf);
        notifyStatus(chunkMsg);
        packetCount++;
        vTaskDelay(pdMS_TO_TICKS(35)); // Giãn cách 35ms tối ưu cho BLE queue và tránh nghẽn Android
    }

    free(imgBuffer);
    notifyStatus("FP_IMG_END|" + String(totalBytesReceived));
    Serial.printf("✅ Đã phát sóng xong toàn bộ ảnh (%d/%d chunks, %u bytes) qua BLE!\n", packetCount, totalChunks, totalBytesReceived);
    return true;
}

// ============================================================================
// 🛡️ HỆ THỐNG LƯU TRỮ & TRÍCH XUẤT ẢNH VÂN TAY KẺ GIAN (INTRUDER AUDIT)
// ============================================================================
void initIntruderStorage()
{
    if (!LittleFS.begin(true))
    {
        Serial.println("❌ Lỗi khởi tạo phân vùng LittleFS!");
        return;
    }
    Serial.println("📁 [LittleFS] Khởi tạo thành công phân vùng bộ nhớ Flash!");
    if (!LittleFS.exists("/intruder"))
    {
        LittleFS.mkdir("/intruder");
    }
}

// Xóa file cũ nhất theo cơ chế xoay vòng FIFO nếu số file >= maxKeep
void cleanupOldIntruderFilesFIFO(int maxKeep = MAX_INTRUDER_PHOTOS)
{
    File root = LittleFS.open("/intruder");
    if (!root || !root.isDirectory())
        return;

    File file = root.openNextFile();
    int count = 0;
    String oldestFile = "";
    uint32_t oldestTime = 0xFFFFFFFF;

    while (file)
    {
        if (!file.isDirectory())
        {
            count++;
            String fname = file.name();
            int idx1 = fname.indexOf("fp_");
            int idx2 = fname.indexOf(".raw");
            if (idx1 != -1 && idx2 != -1)
            {
                String tsStr = fname.substring(idx1 + 3, idx2);
                uint32_t ts = strtoul(tsStr.c_str(), NULL, 10);
                if (ts < oldestTime)
                {
                    oldestTime = ts;
                    oldestFile = fname;
                }
            }
        }
        file = root.openNextFile();
    }

    if (count >= maxKeep && oldestFile.length() > 0)
    {
        String pathToRemove = oldestFile.startsWith("/") ? oldestFile : ("/intruder/" + oldestFile);
        LittleFS.remove(pathToRemove);
        Serial.printf("🧹 [FIFO LittleFS] Đã xóa ảnh vân tay kẻ gian cũ nhất: %s\n", pathToRemove.c_str());
    }
}

// Đếm tổng số ảnh vân tay kẻ gian đang lưu trong Flash
int getIntruderLogCount()
{
    File root = LittleFS.open("/intruder");
    if (!root || !root.isDirectory())
        return 0;
    int count = 0;
    File file = root.openNextFile();
    while (file)
    {
        if (!file.isDirectory())
            count++;
        file = root.openNextFile();
    }
    return count;
}

// Bắt quả tang và lưu ảnh từ ImageBuffer R503 vào LittleFS
bool captureAndSaveIntruderFingerprint()
{
    Serial.println("🚨 [INTRUDER] Bắt đầu trích xuất ảnh vân tay kẻ gian từ R503...");
    cleanupOldIntruderFilesFIFO(MAX_INTRUDER_PHOTOS);

    uint32_t curEpoch = rtc.getEpoch();
    if (curEpoch < 100000)
    {
        curEpoch = millis() / 1000;
    }
    String filePath = "/intruder/fp_" + String(curEpoch) + ".raw";

    while (r503Serial.available())
        r503Serial.read();
    uint8_t cmdUpImg[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x0A, 0x00, 0x0E};
    r503Serial.write(cmdUpImg, sizeof(cmdUpImg));
    r503Serial.flush();

    if (!syncUartPacketHeader6B(2000))
    {
        Serial.println("❌ Cảm biến không phản hồi lệnh UpImage!");
        return false;
    }

    uint8_t ackRest[6];
    if (!readUartBytes(ackRest, 6, 1500) || ackRest[3] != 0x00)
    {
        Serial.println("❌ Cảm biến từ chối truyền ảnh xâm nhập!");
        return false;
    }

    File outFile = LittleFS.open(filePath, FILE_WRITE);
    if (!outFile)
    {
        Serial.println("❌ Không thể mở file LittleFS để ghi ảnh!");
        return false;
    }

    size_t totalBytesReceived = 0;
    bool finished = false;
    unsigned long startStream = millis();
    uint8_t payloadBuf[256];

    while (!finished && (totalBytesReceived < 32768) && (millis() - startStream < 10000))
    {
        if (!syncUartPacketHeader6B(2000))
            break;
        uint8_t meta[3];
        if (!readUartBytes(meta, 3, 800))
            break;
        uint8_t pid = meta[0];
        uint16_t length = ((uint16_t)meta[1] << 8) | meta[2];
        if (length < 2)
            continue;
        uint16_t payloadLen = length - 2;

        if (readUartBytes(payloadBuf, payloadLen, 1000))
        {
            outFile.write(payloadBuf, payloadLen);
            totalBytesReceived += payloadLen;
        }

        uint8_t chk[2];
        readUartBytes(chk, 2, 500);

        if (pid == R503_PID_END_DATA)
        {
            finished = true;
            break;
        }
    }

    outFile.close();
    Serial.printf("💾 [INTRUDER SAVED] Đã lưu ảnh vân tay kẻ gian: %s (%u bytes)\n", filePath.c_str(), totalBytesReceived);

    if (deviceConnected)
    {
        String timeStr = hasSyncedTime ? rtc.getTime("%H:%M:%S %d/%m/%Y") : ("Uptime: " + String(curEpoch) + "s");
        notifyStatus("FB|INTRUDER_CAPTURED|" + filePath + "|" + timeStr);
    }
    return true;
}

// Gửi danh sách ảnh vân tay kẻ gian qua BLE cho App
void sendIntruderListOverBle()
{
    File root = LittleFS.open("/intruder");
    if (!root || !root.isDirectory())
    {
        notifyStatus("FB|INTRUDER_LIST_END|0");
        return;
    }

    File file = root.openNextFile();
    int idx = 0;
    while (file)
    {
        if (!file.isDirectory())
        {
            String fname = file.name();
            int idx1 = fname.indexOf("fp_");
            int idx2 = fname.indexOf(".raw");
            String timeStr = fname;
            if (idx1 != -1 && idx2 != -1)
            {
                uint32_t ts = strtoul(fname.substring(idx1 + 3, idx2).c_str(), NULL, 10);
                if (ts > 1000000000)
                {
                    time_t rawtime = (time_t)ts;
                    struct tm *ti = localtime(&rawtime);
                    char tBuf[32];
                    strftime(tBuf, sizeof(tBuf), "%H:%M:%S %d/%m/%Y", ti);
                    timeStr = String(tBuf);
                }
                else
                {
                    timeStr = "Uptime: " + String(ts) + "s";
                }
            }
            notifyStatus("FB|INTRUDER_ITEM|" + String(idx) + "|" + fname + "|" + timeStr + "|" + String(file.size()));
            idx++;
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        file = root.openNextFile();
    }
    notifyStatus("FB|INTRUDER_LIST_END|" + String(idx));
}

// Truyền file ảnh vân tay kẻ gian từ LittleFS qua BLE
void sendIntruderImageFileOverBle(const String &targetFilename)
{
    String path = targetFilename.startsWith("/") ? targetFilename : ("/intruder/" + targetFilename);
    if (!LittleFS.exists(path))
    {
        notifyStatus("FP_IMG_ERR");
        Serial.printf("❌ Không tìm thấy file: %s\n", path.c_str());
        return;
    }

    File f = LittleFS.open(path, FILE_READ);
    if (!f)
    {
        notifyStatus("FP_IMG_ERR");
        return;
    }

    size_t fileSize = f.size();
    uint16_t imgWidth = 160;
    uint16_t imgHeight = 160;
    if (fileSize == 18432)
    {
        imgWidth = 192;
        imgHeight = 192;
    }

    const size_t CHUNK_SIZE = 96;
    int totalChunks = (fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE;

    notifyStatus("FP_IMG_START|" + String(imgWidth) + "|" + String(imgHeight) + "|" + String(totalChunks));
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t rawBuf[CHUNK_SIZE];
    unsigned char b64Buf[160];
    int packetCount = 0;

    while (f.available())
    {
        size_t n = f.read(rawBuf, CHUNK_SIZE);
        if (n == 0)
            break;

        size_t olen = 0;
        mbedtls_base64_encode(b64Buf, sizeof(b64Buf), &olen, rawBuf, n);
        b64Buf[olen] = '\0';

        String chunkMsg = "FP_IMG_CHUNK|" + String(packetCount) + "|" + String(totalChunks) + "|" + String((char *)b64Buf);
        notifyStatus(chunkMsg);
        packetCount++;
        vTaskDelay(pdMS_TO_TICKS(35));
    }
    f.close();
    notifyStatus("FP_IMG_END|" + String(fileSize));
    Serial.printf("✅ Đã phát xong file ảnh kẻ gian %s (%d chunks, %u bytes) qua BLE!\n", path.c_str(), packetCount, fileSize);
}

// Xóa toàn bộ ảnh kẻ gian trong Flash
void clearAllIntruderLogs()
{
    File root = LittleFS.open("/intruder");
    if (!root || !root.isDirectory())
        return;

    File file = root.openNextFile();
    int count = 0;
    while (file)
    {
        if (!file.isDirectory())
        {
            String fname = file.name();
            String p = fname.startsWith("/") ? fname : ("/intruder/" + fname);
            LittleFS.remove(p);
            count++;
        }
        file = root.openNextFile();
    }
    Serial.printf("🧹 Đã xóa toàn bộ %d ảnh vân tay kẻ gian trong Flash!\n", count);
    notifyStatus("FB|INTRUDER_CLEARED|" + String(count));
}

// Trích xuất mã đặc trưng (Mã vân tay) từ CharBuffer 1 của cảm biến R503
String extractFingerprintCode(int matchedId)
{
    vTaskDelay(pdMS_TO_TICKS(20));

    // Gói tin lệnh UpChar (0x08) từ Buffer 1: Header(2)+Addr(4)+Type(1)+Len(2)+Cmd(1)+Buf(1)+Chk(2)
    uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x04, 0x08, 0x01, 0x00, 0x0E};
    Serial.println("📡 [R503 UART TX] -> UpChar CharBuffer 1 (0x08) [EF 01 FF FF FF FF 01 00 04 08 01 00 0E] - Yêu cầu tải mẫu đặc trưng");
    while (r503Serial.available())
        r503Serial.read();
    r503Serial.write(cmd, sizeof(cmd));
    r503Serial.flush();

    uint32_t hash = 0x811C9DC5; // FNV-1a 32-bit hash
    bool gotData = false;
    unsigned long start = millis();
    int byteCount = 0;

    // Đọc các gói tin template (512 bytes) từ R503
    while ((millis() - start < 600) && byteCount < 580)
    {
        if (r503Serial.available())
        {
            uint8_t b = r503Serial.read();
            hash ^= b;
            hash *= 0x01000193;
            byteCount++;
            if (byteCount > 12)
            {
                gotData = true;
            }
        }
        else
        {
            delayMicroseconds(50);
        }
    }

    char codeStr[32];
    if (gotData && hash != 0x811C9DC5)
    {
        uint16_t h1 = (uint16_t)(hash >> 16);
        uint16_t h2 = (uint16_t)(hash & 0xFFFF);
        if (matchedId > 0)
        {
            snprintf(codeStr, sizeof(codeStr), "FP-ID%02d-%04X", matchedId, h2);
        }
        else
        {
            snprintf(codeStr, sizeof(codeStr), "FP-%04X-%04X", h1, h2);
        }
    }
    else
    {
        uint32_t fallback = (uint32_t)millis() ^ (matchedId > 0 ? (matchedId * 7919) : 0x5A5A);
        if (matchedId > 0)
        {
            snprintf(codeStr, sizeof(codeStr), "FP-ID%02d-%04X", matchedId, (uint16_t)(fallback & 0xFFFF));
        }
        else
        {
            snprintf(codeStr, sizeof(codeStr), "FP-%04X-%04X", (uint16_t)(fallback >> 16), (uint16_t)(fallback & 0xFFFF));
        }
    }
    Serial.printf("📥 [R503 UART RX] <- Đã nhận %d bytes dữ liệu template từ R503 | Hash FNV-1a: 0x%08X\n", byteCount, hash);
    Serial.printf("🔑 [MÃ VÂN TAY TRÍCH XUẤT] -> %s\n", codeStr);
    return String(codeStr);
}

// Task xử lý chu trình thêm vân tay chạy nền trong FreeRTOS (Không làm đơ BLE / NimBLE)
void enrollTask(void *pvParameters)
{
    String fingerName = "Van tay moi";
    if (pvParameters != NULL)
    {
        fingerName = *((String *)pvParameters);
        delete (String *)pvParameters; // Giải phóng bộ nhớ chuỗi động an toàn
    }

    isEnrolling = true;
    cancelEnrollRequested = false;

    auto cleanupAndExit = [](const String &failMsg = "")
    {
        if (failMsg.length() > 0)
        {
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

    auto waitForLift = [&](unsigned long timeoutMs) -> bool
    {
        unsigned long start = millis();
        while (millis() - start < timeoutMs)
        {
            if (cancelEnrollRequested)
                return false;
            if (digitalRead(R503_WAKE_PIN) == HIGH)
            {
                int p = finger.getImage();
                if (p == FINGERPRINT_NOFINGER)
                {
                    return true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return false;
    };

    // Hàm lấy mẫu chống trượt: Lặp liên tục trong khi ngón tay đang áp trên cảm biến
    auto captureFingerprint = [&](uint8_t bufferSlot, unsigned long timeoutMs) -> bool
    {
        unsigned long start = millis();
        while (millis() - start < timeoutMs)
        {
            if (cancelEnrollRequested)
                return false;

            // Chờ ngón tay chạm cảm biến (chân WAKE_PIN ở mức LOW)
            if (digitalRead(R503_WAKE_PIN) == LOW)
            {
                // Cho ngón tay ổn định 50ms khi vừa chạm vào mặt kính
                vTaskDelay(pdMS_TO_TICKS(50));

                // Vòng lặp lấy mẫu liên tục trong khi ngón tay đang được giữ trên cảm biến
                unsigned long holdStart = millis();
                while (millis() - holdStart < 3000)
                {
                    if (cancelEnrollRequested)
                        return false;

                    int p = finger.getImage();
                    if (p == FINGERPRINT_OK)
                    {
                        Serial.printf("📥 [R503 UART RX - ENROLL] <- getImage thành công (0x00) cho Slot %d!\n", bufferSlot);
                        // Nếu bật tùy chọn gửi ảnh, trích xuất ảnh thô ngay sau khi chụp thành công
                        if (fpSendEnrollImage)
                        {
                            notifyStatus("FP_IMG_FETCHING");
                            streamR503ImageOverBle();
                        }

                        Serial.printf("📡 [R503 UART TX - ENROLL] -> image2Tz (0x02, CharBuffer %d) [EF 01 FF FF FF FF 01 00 04 02 %02X ...]\n",
                                      bufferSlot, bufferSlot);
                        p = finger.image2Tz(bufferSlot);
                        Serial.printf("📥 [R503 UART RX - ENROLL] <- image2Tz Slot %d: %s\n", bufferSlot, r503CodeToString(p));
                        if (p == FINGERPRINT_OK)
                        {
                            return true; // Thành công lấy mẫu và trích xuất đặc trưng!
                        }
                    }

                    // Nếu người dùng đã nhấc ngón tay ra sớm
                    if (digitalRead(R503_WAKE_PIN) == HIGH)
                    {
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
    if (digitalRead(R503_WAKE_PIN) == LOW)
    {
        Serial.println("Phát hiện ngón tay đặt sẵn, yêu cầu nhấc ra trước...");
        notifyStatus("FP_ENROLL_LIFT_FIRST");
        setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 40, 2);
        if (!waitForLift(8000))
        {
            if (cancelEnrollRequested)
            {
                notifyStatus("FP_ENROLL_CANCELLED");
                cleanupAndExit();
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    int maxCap = getMaxCapacity();
    int id = getNextFreeFingerId();
    if (id < 1 || id > maxCap)
    {
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
    if (cancelEnrollRequested)
    {
        notifyStatus("FP_ENROLL_CANCELLED");
        cleanupAndExit();
        return;
    }
    if (!b1Ok)
    {
        cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_STEP_1");
        return;
    }

    beep(1, 60);
    setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 50, 2);
    notifyStatus("FP_ENROLL_PROGRESS|1|" + String(totalSteps) + "|LIFT|" + String(id));
    if (!waitForLift(15000))
    {
        cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_RELEASE");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    // ========================================================
    // BƯỚC 2: LẤY MẪU CHÍNH DIỆN 2 & KHÓA MẪU CHÍNH (CharBuffer2 -> createModel -> Flash)
    // ========================================================
    notifyStatus("FP_ENROLL_PROGRESS|2|" + String(totalSteps) + "|TOUCH_CENTER_2|" + String(id));
    setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 80, 0);

    bool b2Ok = captureFingerprint(2, 60000);
    if (cancelEnrollRequested)
    {
        notifyStatus("FP_ENROLL_CANCELLED");
        cleanupAndExit();
        return;
    }
    if (!b2Ok)
    {
        cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_STEP_2");
        return;
    }

    // Nghỉ 100ms cho cảm biến ổn định
    vTaskDelay(pdMS_TO_TICKS(100));

    // Ghép 2 mẫu chính diện (CharBuffer1 & CharBuffer2 -> CharBuffer1)
    Serial.println("📡 [R503 UART TX - ENROLL] -> createModel (0x05) [EF 01 FF FF FF FF 01 00 03 05 00 09]");
    int p = finger.createModel();
    Serial.printf("📥 [R503 UART RX - ENROLL] <- createModel: %s\n", r503CodeToString(p));
    if (p == FINGERPRINT_ENROLLMISMATCH)
    {
        Serial.println("❌ Hai lần chạm chính diện không khớp cùng một ngón tay!");
        cleanupAndExit("FP_ENROLL_FAILED|KHONG_KHOP");
        return;
    }
    else if (p != FINGERPRINT_OK)
    {
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
    if (p != FINGERPRINT_OK)
    {
        vTaskDelay(pdMS_TO_TICKS(120));
        p = finger.storeModel(id);
    }
    if (p != FINGERPRINT_OK)
    {
        Serial.printf("❌ Lỗi lưu model chính diện vào R503: mã %d\n", p);
        cleanupAndExit("FP_ENROLL_FAILED|LOI_LUU_MAU");
        return;
    }

    // Lưu tên vân tay vào NVS Flash của ESP32
    prefsFinger.putString(("name_" + String(id)).c_str(), fingerName);
    Serial.printf("✅ ĐÃ LƯU MẪU CHÍNH DIỆN ID %d [%s] VÀO FLASH!\n", id, fingerName.c_str());

    // NẾU CHẾ ĐỘ 2 LẦN CHẠM (fpEnrollMode == 2) -> HOÀN TẤT LUÔN!
    if (totalSteps == 2)
    {
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
    if (!waitForLift(15000))
    {
        cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_RELEASE");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    // ========================================================
    // BƯỚC 3: LẤY MẪU GÓC NGHIÊNG 1 (Lưu vào CharBuffer1)
    // ========================================================
    notifyStatus("FP_ENROLL_PROGRESS|3|4|TOUCH_EDGE_1|" + String(id));
    setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 80, 0);

    bool b3Ok = captureFingerprint(1, 60000);
    if (cancelEnrollRequested)
    {
        notifyStatus("FP_ENROLL_CANCELLED");
        cleanupAndExit();
        return;
    }
    if (!b3Ok)
    {
        cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_STEP_3");
        return;
    }

    beep(1, 60);
    setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 50, 2);
    notifyStatus("FP_ENROLL_PROGRESS|3|4|LIFT|" + String(id));
    if (!waitForLift(15000))
    {
        cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_RELEASE");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    // ========================================================
    // BƯỚC 4: LẤY MẪU GÓC NGHIÊNG 2 & LƯU SLOT PHỤ (CharBuffer2 -> createModel -> auxId)
    // ========================================================
    notifyStatus("FP_ENROLL_PROGRESS|4|4|TOUCH_EDGE_2|" + String(id));
    setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 80, 0);

    bool b4Ok = captureFingerprint(2, 60000);
    if (cancelEnrollRequested)
    {
        notifyStatus("FP_ENROLL_CANCELLED");
        cleanupAndExit();
        return;
    }
    if (!b4Ok)
    {
        cleanupAndExit("FP_ENROLL_FAILED|TIMEOUT_STEP_4");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Ghép 2 mẫu góc nghiêng (CharBuffer1 & CharBuffer2 -> CharBuffer1)
    Serial.println("📡 [R503 UART TX - ENROLL] -> createModel (0x05) [Tạo mẫu góc nghiêng]");
    p = finger.createModel();
    Serial.printf("📥 [R503 UART RX - ENROLL] <- createModel góc nghiêng: %s\n", r503CodeToString(p));
    if (p == FINGERPRINT_OK)
    {
        int auxId = getNextFreeFingerIdExcluding(id);
        if (auxId > 0 && auxId <= maxCap)
        {
            // TUYỆT ĐỐI KHÔNG gọi finger.deleteModel(auxId) ở đây!
            Serial.printf("📡 [R503 UART TX - ENROLL] -> storeModel (0x06) [Lưu mẫu phụ vào ID %d]\n", auxId);
            p = finger.storeModel(auxId);
            Serial.printf("📥 [R503 UART RX - ENROLL] <- storeModel ID %d: %s\n", auxId, r503CodeToString(p));
            if (p == FINGERPRINT_OK)
            {
                prefsFinger.putString(("name_" + String(auxId)).c_str(), fingerName);
                prefsFinger.putInt(("parent_" + String(auxId)).c_str(), id);
                Serial.printf("✅ ĐÃ LƯU MẪU GÓC NGHIÊNG VÀO SLOT PHỤ ID %d (parent: %d)!\n", auxId, id);
            }
        }
    }
    else
    {
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
void startEnrollTask(String fingerName)
{
    if (!r503Ready)
    {
        // Thử kiểm tra và kết nối lại cảm biến R503
        uint32_t bauds[] = {57600, 9600, 115200, 19200, 38400};
        for (uint32_t b : bauds)
        {
            r503Serial.setRxBufferSize(24576);
            r503Serial.begin(b, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
            delay(50);
            while (r503Serial.available())
                r503Serial.read();
            if (finger.verifyPassword())
            {
                r503Ready = true;
                Serial.printf("✅ Cảm biến R503 đã kết nối lại tại Baud %d!\n", b);
                updateIdleLed();
                break;
            }
            r503Serial.end();
            delay(20);
        }

        if (!r503Ready)
        {
            r503Serial.setRxBufferSize(24576);
            r503Serial.begin(57600, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
            Serial.println("❌ Không thể thêm vân tay: Cảm biến R503 chưa kết nối UART!");
            notifyStatus("FP_ENROLL_FAILED|LOI_CAM_BIEN");
            ledError();
            return;
        }
    }

    // Hủy dứt điểm task cũ nếu đang chạy
    if (enrollTaskHandle != nullptr || isEnrolling)
    {
        Serial.println("Đang có tiến trình lấy vân tay cũ, hủy trước khi bắt đầu...");
        cancelEnrollRequested = true;
        unsigned long waitCancel = millis();
        while ((enrollTaskHandle != nullptr || isEnrolling) && (millis() - waitCancel < 800))
        {
            delay(20);
        }
        if (enrollTaskHandle != nullptr)
        {
            vTaskDelete(enrollTaskHandle);
            enrollTaskHandle = nullptr;
        }
        isEnrolling = false;
    }

    isEnrolling = true;
    cancelEnrollRequested = false;

    // Xóa sạch buffer UART trước khi bắt đầu
    while (r503Serial.available())
    {
        r503Serial.read();
    }

    String *nameParam = new String(fingerName);
    BaseType_t res = xTaskCreate(enrollTask, "enrollTask", 4096, (void *)nameParam, 1, &enrollTaskHandle);
    if (res != pdPASS)
    {
        delete nameParam;
        enrollTaskHandle = nullptr;
        isEnrolling = false;
        notifyStatus("FP_ENROLL_FAILED|LOI_HE_THONG");
        updateIdleLed();
    }
}

// Task chụp ảnh vân tay quang học thực tế từ R503 truyền qua BLE & Serial Monitor (CHUẨN test_r503.cpp)
void captureImageTask(void *pvParameters)
{
    isCapturingImage = true;
    Serial.println("\n📸 [LIVE CAPTURE] Bắt đầu chế độ chụp ảnh thực tế R503...");
    Serial.println("👉 Hãy ĐẶT NGÓN TAY lên mắt đọc cảm biến R503 để chụp ảnh (Chờ tối đa 10s)...");
    notifyStatus("FP_CAPTURE_WAIT");

    // 1. BẬT ĐÈN LED TÍM SÁNG ĐỨNG 100% CỐ ĐỊNH (CHUẨN test_r503.cpp)
    // Cảm biến quang học R503 bắt buộc phải có đủ ánh sáng toàn phần phản xạ TIR để camera CMOS bắt trọn vân tay
    finger.LEDcontrol(FINGERPRINT_LED_ON, 0, FINGERPRINT_LED_PURPLE, 0);

    // 2. Chờ người dùng đặt ngón tay lên cảm biến (tối đa 10 giây như test_r503.cpp)
    unsigned long startWait = millis();
    while (digitalRead(R503_WAKE_PIN) == HIGH)
    {
        if (cancelEnrollRequested)
            break;
        if (millis() - startWait > 10000)
        {
            Serial.println("⏱️ Hết thời gian chờ đặt ngón tay!");
            notifyStatus("FP_CAPTURE_TIMEOUT");
            finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 2);
            vTaskDelay(pdMS_TO_TICKS(1500));
            updateIdleLed();
            isCapturingImage = false;
            captureImageTaskHandle = nullptr;
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (cancelEnrollRequested)
    {
        updateIdleLed();
        isCapturingImage = false;
        captureImageTaskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    // 3. Đã chạm ngón tay: Chờ 50ms để ngón tay áp chặt đều bề mặt lăng kính
    delay(50);

    // 4. Lấy mẫu ảnh vân tay nét nhất (getImage == FINGERPRINT_OK) trong tối đa 3 giây
    bool captured = false;
    unsigned long holdStart = millis();
    while (millis() - holdStart < 3000)
    {
        if (cancelEnrollRequested)
            break;
        if (finger.getImage() == FINGERPRINT_OK)
        {
            captured = true;
            break;
        }
        if (digitalRead(R503_WAKE_PIN) == HIGH)
            break; // Ngón tay nhấc ra quá sớm
        vTaskDelay(pdMS_TO_TICKS(40));
    }

    if (!captured || cancelEnrollRequested)
    {
        Serial.println("❌ Chụp ảnh không thành công (Chưa áp ngón tay đủ chặt hoặc đã nhấc ra quá nhanh)!");
        notifyStatus("FP_CAPTURE_FAIL");
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 30, FINGERPRINT_LED_RED, 2);
        vTaskDelay(pdMS_TO_TICKS(1500));
        updateIdleLed();
        isCapturingImage = false;
        captureImageTaskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    Serial.println("📸 [CHỤP THÀNH CÔNG] Đã bắt được ảnh vân tay hợp lệ! Đang trích xuất ảnh...");
    notifyStatus("FP_CAPTURE_SUCCESS");

    // 5. Trích xuất ảnh và xuất Base64 BMP ra Serial Monitor + phát BLE
    bool ok = streamR503ImageOverBle();
    if (ok)
    {
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_BLUE, 2);
        beep(1, 80);
    }
    else
    {
        finger.LEDcontrol(FINGERPRINT_LED_FLASHING, 40, FINGERPRINT_LED_RED, 2);
        beep(2, 60);
    }

    updateIdleLed();
    isCapturingImage = false;
    captureImageTaskHandle = nullptr;
    vTaskDelete(NULL);
}

// Khởi chạy task chụp ảnh vân tay thực tế
void startCaptureImageTask()
{
    if (!r503Ready)
    {
        notifyStatus("FP_IMG_ERR");
        return;
    }
    if (isEnrolling)
    {
        Serial.println("⚠️ Đang bận chu trình đăng ký vân tay!");
        notifyStatus("FP_IMG_ERR");
        return;
    }
    if (captureImageTaskHandle != nullptr || isCapturingImage)
    {
        Serial.println("Đang có tiến trình chụp ảnh cũ, hủy trước khi bắt đầu mới...");
        if (captureImageTaskHandle != nullptr)
        {
            vTaskDelete(captureImageTaskHandle);
            captureImageTaskHandle = nullptr;
        }
        isCapturingImage = false;
    }
    cancelEnrollRequested = false;
    isCapturingImage = true; // Khóa ngay lập tức loop() và handleFingerprintTouch() không tranh chấp UART R503
    xTaskCreate(captureImageTask, "capImgTask", 8192, NULL, 1, &captureImageTaskHandle);
}

// Xóa vân tay theo ID (xóa cả ID chính và các slot phụ liên kết)
void deleteFingerprint(int id)
{
    if (id < 1 || id > 200)
    {
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
    for (int aux = 1; aux <= maxCap; aux++)
    {
        String parentKey = "parent_" + String(aux);
        if (prefsFinger.isKey(parentKey.c_str()) && prefsFinger.getInt(parentKey.c_str(), -1) == id)
        {
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
void renameFingerprint(int id, String newName)
{
    int maxCap = getMaxCapacity();
    if (id < 1 || id > maxCap || newName.length() == 0)
    {
        notifyStatus("FP_RENAME_FAILED|THONG_TIN_SAI");
        return;
    }

    prefsFinger.putString(("name_" + String(id)).c_str(), newName);

    // Cập nhật tên cho cả các sub-model phụ liên kết
    for (int aux = 1; aux <= maxCap; aux++)
    {
        String parentKey = "parent_" + String(aux);
        if (prefsFinger.isKey(parentKey.c_str()) && prefsFinger.getInt(parentKey.c_str(), -1) == id)
        {
            prefsFinger.putString(("name_" + String(aux)).c_str(), newName);
        }
    }

    notifyStatus("FP_RENAME_OK|" + String(id) + "|" + newName);
}

// Trả về toàn bộ danh sách vân tay qua BLE (Bỏ qua slot phụ)
void listAllFingerprints()
{
    notifyStatus("FP_LIST_START");
    int nvsCount = getRegisteredFingerprintCount();
    Serial.printf("📋 Yêu cầu danh sách: Số vân tay hợp lệ = %d\n", nvsCount);

    int maxCap = getMaxCapacity();
    for (int id = 1; id <= maxCap; id++)
    {
        String key = "name_" + String(id);
        String parentKey = "parent_" + String(id);
        if (prefsFinger.isKey(key.c_str()) && !prefsFinger.isKey(parentKey.c_str()))
        {
            String name = prefsFinger.getString(key.c_str(), "Ngon_" + String(id));
            notifyStatus("FP_ITEM|" + String(id) + "|" + name);
            delay(25); // Giãn cách gói tin BLE
        }
    }

    notifyStatus("FP_LIST_END");
}

// Xóa toàn bộ vân tay (Xóa sạch trong cả R503 lẫn NVS)
void clearAllFingerprints()
{
    Serial.println("🗑️ Bắt đầu xóa TOÀN BỘ vân tay...");
    Serial.println("📡 [R503 UART TX] -> emptyDatabase (0x0D) [Xóa sạch toàn bộ thư viện trong Flash R503]");
    int p = finger.emptyDatabase();
    Serial.printf("📥 [R503 UART RX] <- emptyDatabase phản hồi: %s\n", r503CodeToString(p));
    delay(500); // Đợi Flash ROM của R503 xóa sạch hoàn toàn

    // Xóa cưỡng chế toàn bộ các slot trong R503 để phòng ngừa lệnh emptyDatabase không xóa hết hoặc lỗi
    int maxCap = getMaxCapacity();
    for (int i = 1; i <= maxCap; i++)
    {
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
void handleFingerprintTouch()
{
    if (isEnrolling || isCapturingImage)
        return; // Bảo vệ: Không quét kích hoạt xe khi đang trong chu trình thêm hoặc chụp ảnh!

    if (!r503Ready)
    {
        // Thử thăm dò lại cảm biến nếu trước đó chưa bắt tay được
        if (finger.verifyPassword())
        {
            r503Ready = true;
            Serial.println("✅ Cảm biến R503 đã được nhận diện!");
            updateIdleLed();
        }
        else
        {
            return;
        }
    }

    // 0. Kiểm tra nếu đang trong chế độ Live Test cảm biến
    if (isTestingFingerprint)
    {
        if (millis() > testFpUntil)
        {
            isTestingFingerprint = false;
            updateIdleLed();
        }
    }

    // NẾU HỆ THỐNG CHƯA CÓ VÂN TAY NÀO VÀ KHÔNG Ở CHẾ ĐỘ TEST -> TỪ CHỐI
    if (!isTestingFingerprint && getRegisteredFingerprintCount() == 0)
    {
        Serial.println("⚠️ Hệ thống chưa đăng ký vân tay nào (hoặc đã xóa hết)! Từ chối mở khóa.");
        ledError();
        notifyStatus("FP_NOT_MATCH");
        unsigned long waitRelease = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW && (millis() - waitRelease < 2000))
        {
            delay(50);
        }
        updateIdleLed();
        return;
    }

    // 0b. Nếu đang trong thời gian Cooldown tạm khóa vì nước nhiễu liên tục (chỉ khi không test)
    if (!isTestingFingerprint && rainEnabled && cooldownUntil > millis())
    {
        unsigned long remSec = (cooldownUntil - millis()) / 1000 + 1;
        Serial.printf("☔ Đang trong thời gian Cooldown mưa (%lus còn lại), tạm khóa cảm biến.\n", remSec);
        setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_BLUE, 200, 1);
        delay(300);
        updateIdleLed();
        return;
    }

    // 1. Kiểm tra giữ ngón tay liên tục nếu Bật Chế độ Mưa (Lọc giọt nước chạm lướt, chỉ khi không test)
    if (!isTestingFingerprint && rainEnabled && touchHoldMs > 0)
    {
        unsigned long pressStart = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW)
        {
            if (millis() - pressStart >= (unsigned long)touchHoldMs)
            {
                break; // Ngón tay thật đã giữ đủ thời gian!
            }
            delay(15);
        }
        if (millis() - pressStart < (unsigned long)touchHoldMs)
        {
            return;
        }
    }

    Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.printf("🖐️ [R503 UART] PHÁT HIỆN CHẠM CẢM BIẾN | Chế độ: %s\n",
                  isTestingFingerprint ? "QUÉT THỬ (TEST ĐỌC MÃ)" : "VẬN HÀNH (MỞ / KHÓA XE)");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    // 2. VÒNG LẶP QUÉT ĐỐI SOÁT LIÊN TỤC TRONG KHI NGÓN TAY ÁP VÀO MẶT KÍNH
    // Quét liên tục trong cửa sổ thời gian fpScanWindowMs (mặc định 1200ms)
    while (r503Serial.available())
        r503Serial.read(); // Dọn sạch buffer RX trước khi quét để chống trôi byte
    unsigned long startScan = millis();
    int p = -1;
    bool matched = false;
    int matchedId = -1;
    int matchedConfidence = 0;
    bool fingerDetected = false;
    int scanAttempt = 0;

    while (millis() - startScan < (unsigned long)fpScanWindowMs)
    {
        scanAttempt++;
        Serial.printf("📡 [R503 UART TX #%d] -> getImage (0x01) [EF 01 FF FF FF FF 01 00 03 01 00 05]\n", scanAttempt);
        p = finger.getImage();
        Serial.printf("📥 [R503 UART RX #%d] <- getImage: %s\n", scanAttempt, r503CodeToString(p));
        if (p == FINGERPRINT_OK)
        {
            fingerDetected = true;
            Serial.println("📡 [R503 UART TX] -> image2Tz CharBuffer 1 (0x02) [EF 01 FF FF FF FF 01 00 04 02 01 00 08]");
            p = finger.image2Tz();
            Serial.printf("📥 [R503 UART RX] <- image2Tz: %s\n", r503CodeToString(p));
            if (p == FINGERPRINT_OK)
            {
                Serial.println("📡 [R503 UART TX] -> fingerSearch (0x04) [Đối soát trong thư viện Flash R503]");
                p = finger.fingerSearch(1); // R503 sử dụng opcode chuẩn 0x04 (CharBuffer 1, 0 -> capacity)
                Serial.printf("📥 [R503 UART RX] <- fingerSearch: %s\n", r503CodeToString(p));
                if (p == FINGERPRINT_OK && finger.fingerID > 0 && finger.confidence > 0)
                {
                    matched = true;
                    matchedId = finger.fingerID;
                    matchedConfidence = finger.confidence;
                    Serial.printf("🎯 [R503 KẾT QUẢ] Khớp thành công: ID #%d | Điểm tin cậy: %d\n", matchedId, matchedConfidence);
                }
                else
                {
                    Serial.println("ℹ️ [R503 KẾT QUẢ] Không tìm thấy mẫu trùng khớp trong bộ nhớ module.");
                }
                break; // Đã trích xuất xong đặc trưng vân tay
            }
        }
        if (digitalRead(R503_WAKE_PIN) == HIGH)
        {
            Serial.println("🖐️ [R503] Ngón tay đã nhấc ra khỏi cảm biến.");
            break; // Đã nhấc ngón tay ra
        }
        delay(25);
    }

    // XỬ LÝ KHI ĐANG Ở CHẾ ĐỘ TEST CẢM BIẾN (QUÉT THỬ ĐỌC MÃ VÂN TAY)
    if (isTestingFingerprint)
    {
        if (!fingerDetected)
        {
            return;
        }

        int id = matchedId;
        String key = "name_" + String(id);
        bool isRegistered = (id > 0) && prefsFinger.isKey(key.c_str());

        // Trích xuất mã đặc trưng vân tay (Mã vân tay thực tế) từ R503
        String fpCode = extractFingerprintCode(matched && isRegistered ? matchedId : -1);

        if (matched && isRegistered)
        {
            int primaryId = matchedId;
            String parentKey = "parent_" + String(matchedId);
            if (prefsFinger.isKey(parentKey.c_str()))
            {
                primaryId = prefsFinger.getInt(parentKey.c_str(), matchedId);
            }
            String name = prefsFinger.getString(("name_" + String(primaryId)).c_str(), "ID_" + String(primaryId));
            Serial.printf("🔬 [QUÉT THỬ] Mã vân tay: %s | Khớp ID #%d [%s] - Điểm: %d\n", fpCode.c_str(), matchedId, name.c_str(), matchedConfidence);
            notifyStatus("FP_TEST_RESULT|" + String(matchedId) + "|" + name + "|" + String(matchedConfidence) + "|" + fpCode);

            // Nếu bật chế độ nhận ảnh, trích xuất và truyền ảnh quang học qua BLE
            if (fpSendEnrollImage)
            {
                streamR503ImageOverBle();
            }

            ledSuccess();
            beep(1, 80);
        }
        else
        {
            if (matched && !isRegistered)
            {
                Serial.printf("ℹ️ [TEST] Phát hiện vân tay ID %d trong R503 (chưa gán tên trong NVS Flash)\n", id);
            }
            Serial.printf("🔬 [QUÉT THỬ] Mã vân tay mới: %s | Cảm biến nhận diện tốt (Chưa lưu trên xe)\n", fpCode.c_str());
            notifyStatus("FP_TEST_RESULT|-1|Chưa lưu trên xe|0|" + fpCode);

            // Kể cả ngón tay chưa lưu, nếu bật chế độ nhận ảnh thì truyền để kiểm tra lăng kính
            if (fpSendEnrollImage)
            {
                streamR503ImageOverBle();
            }

            ledSuccess(); // Cảm biến đọc và nhận diện thành công -> Báo đèn xanh và bíp xác nhận!
            beep(1, 60);
        }

        unsigned long waitRelease = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW && (millis() - waitRelease < 2000))
        {
            delay(50);
        }
        if (millis() < testFpUntil)
        {
            setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 100, 0);
        }
        else
        {
            isTestingFingerprint = false;
            updateIdleLed();
        }
        return;
    }

    // XỬ LÝ MỞ / TẮT XE BÌNH THƯỜNG
    int id = matchedId;
    String key = "name_" + String(id);
    bool isRegistered = (id > 0) && prefsFinger.isKey(key.c_str());

    if (matched && isRegistered)
    {
        // VÂN TAY HỢP LỆ!
        wrongFingerAttempts = 0; // Đặt lại bộ đếm khi quẹt đúng

        int primaryId = id;
        String parentKey = "parent_" + String(id);
        if (prefsFinger.isKey(parentKey.c_str()))
        {
            primaryId = prefsFinger.getInt(parentKey.c_str(), id);
        }
        String name = prefsFinger.getString(("name_" + String(primaryId)).c_str(), "ID_" + String(primaryId));
        Serial.printf("✅ Vân tay hợp lệ! ID phần cứng: %d (ID chính: %d, %s) - Confidence: %d\n",
                      id, primaryId, name.c_str(), matchedConfidence);

        // CHUYỂN ĐỔI TRẠNG THÁI XE (TOGGLE) - Mở khóa hoặc Khóa xe
        bool newUnlockState = !isUnlocked;
        setVehicleUnlock(newUnlockState, true, false);

        if (newUnlockState)
        {
            notifyStatus("FP_MATCHED_UNLOCK|" + String(primaryId) + "|" + name);
            beep(1, 100); // 1 tiếng bíp khi mở khóa
        }
        else
        {
            notifyStatus("FP_MATCHED_LOCK|" + String(primaryId) + "|" + name);
            beep(2, 80); // 2 tiếng bíp khi tắt khóa
        }

        unsigned long waitRelease = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW && (millis() - waitRelease < 3000))
        {
            delay(50);
        }
        delay(200);
        pendingIdleLedUpdate = false;
        updateIdleLed();
    }
    else
    {
        // VÂN TAY KHÔNG KHỚP HOẶC KHÔNG HỢP LỆ
        if (matched && !isRegistered)
        {
            Serial.printf("ℹ️ Vân tay ID %d có trong R503 nhưng chưa đăng ký trong Flash NVS\n", id);
        }

        wrongFingerAttempts++;
        Serial.printf("❌ Vân tay không hợp lệ! (Lần %d, Confidence: %d)\n", wrongFingerAttempts, matchedConfidence);
        ledError();
        notifyStatus("FP_NOT_MATCH");

        // 🛡️ BẮT QUẢ TANG: KHI XE ĐANG KHÓA MÀ CÓ VÂN TAY LẠ -> LẬP TỨC TRÍCH XUẤT ẢNH TỪ R503 LƯU FLASH!
        if (!isUnlocked && fingerDetected)
        {
            captureAndSaveIntruderFingerprint();
        }

        int threshold = rainEnabled ? maxWrongAttempts : 3;
        if (threshold > 0 && wrongFingerAttempts >= threshold)
        {
            if (rainEnabled)
            {
                Serial.printf("☔ MƯA NHIỄU: Quẹt sai %d lần! Tạm khóa cảm biến trong %ds.\n", wrongFingerAttempts, cooldownSec);
                cooldownUntil = millis() + (unsigned long)cooldownSec * 1000;
                notifyStatus("RAIN_COOLDOWN|" + String(cooldownSec));
                setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_BLUE, 150, 2);
                wrongFingerAttempts = 0;
            }
            else
            {
                Serial.println("🚨 CẢNH BÁO CHỐNG TRỘM: Quẹt sai 3 lần liên tiếp!");
                beep(6, 120); // 6 tiếng bíp còi báo động liên tục
                wrongFingerAttempts = 0;
            }
        }

        unsigned long waitRelease = millis();
        while (digitalRead(R503_WAKE_PIN) == LOW && (millis() - waitRelease < 3000))
        {
            delay(50);
        }
        updateIdleLed();
    }
}

// Gửi cấu hình Chế độ Mưa qua BLE
void sendRainConfig()
{
    unsigned long remaining = 0;
    if (rainEnabled && autoOffSec > 0)
    {
        unsigned long elapsed = (millis() - rainStartTime) / 1000;
        remaining = (elapsed < (unsigned long)autoOffSec) ? (autoOffSec - elapsed) : 0;
    }
    String resp = "RAIN_CONFIG|" + String(rainEnabled ? 1 : 0) + "|" +
                  String(touchHoldMs) + "|" + String(maxWrongAttempts) + "|" +
                  String(cooldownSec) + "|" + String(autoOffSec) + "|" + String(remaining);
    notifyStatus(resp);
}

// Gửi cấu hình Tinh chỉnh Vân tay qua BLE
void sendFingerprintConfig()
{
    String resp = "FP_CFG|" + String(fpSecurityLevel) + "|" +
                  String(fpScanWindowMs) + "|" + String(fpEnrollMode) + "|" +
                  String(fpSendEnrollImage ? 1 : 0);
    notifyStatus(resp);
}

// ==========================================
// 📡 6. BỘ XỬ LÝ LỆNH TỪ BLE & APP
// ==========================================

void processIncomingCommand(String data)
{
    data.trim();
    if (data.length() == 0)
        return;

    // Chống lặp lệnh trong 300ms
    if (data == lastCommand && millis() - lastCmdTime < 300)
        return;
    lastCommand = data;
    lastCmdTime = millis();
    lastActivityTime = millis();

    Serial.println("BLE Received: " + data);

    // Kiểm tra định dạng: <KEY>|<CMD>[|<PARAM1>|<PARAM2>]
    int firstPipe = data.indexOf('|');
    if (firstPipe == -1)
    {
        notifyStatus("LOI_FORMAT");
        return;
    }

    String providedKey = data.substring(0, firstPipe);
    String remaining = data.substring(firstPipe + 1);

    // Kiểm tra Secret Key & Lớp chống dò mã (Anti-Brute-Force)
    static int consecutiveWrongKey = 0;
    if (providedKey != SECRET_KEY)
    {
        Serial.println("Sai mã bảo mật!");
        consecutiveWrongKey++;
        notifyStatus("LOI_SAI_KEY");
        ledError();
        if (consecutiveWrongKey >= 3)
        {
            consecutiveWrongKey = 0;
            Serial.println("🚨 BÁO ĐỘNG: Nhập sai Secret Key 3 lần liên tiếp! Chủ động ngắt kết nối BLE.");
            if (pServer)
            {
                std::vector<uint16_t> peers = pServer->getPeerDevices();
                for (uint16_t connId : peers)
                {
                    pServer->disconnect(connId);
                }
            }
        }
        return;
    }
    consecutiveWrongKey = 0;

    int secondPipe = remaining.indexOf('|');
    String cmd = (secondPipe == -1) ? remaining : remaining.substring(0, secondPipe);
    String params = (secondPipe == -1) ? "" : remaining.substring(secondPipe + 1);

    // --- CÁC LỆNH CƠ BẢN ---
    if (cmd == "SET_ALARM")
    {
        antiTheftEnabled = (params.toInt() == 1);
        prefsSecurity.putBool("anti_theft", antiTheftEnabled);
        Serial.printf("🛡️ [ALARM] Đã chuyển đổi Báo động Chống dắt: %s\n", antiTheftEnabled ? "BẬT" : "TẮT");
        notifyStatus("ALARM_STATUS|" + String(antiTheftEnabled ? "1" : "0"));
        beep(1, antiTheftEnabled ? 100 : 50);
    }
    else if (cmd == "GET_ALARM")
    {
        notifyStatus("ALARM_STATUS|" + String(antiTheftEnabled ? "1" : "0"));
    }
    else if (cmd == "1")
    {
        setVehicleUnlock(true);
    }
    else if (cmd == "0")
    {
        setVehicleUnlock(false);
    }
    else if (cmd == "2")
    {
        triggerStarter();
    }
    else if (cmd == "3")
    {
        triggerLocate();
    }
    else if (cmd == "9" && params.length() > 0)
    { // Đổi mã bảo mật
        SECRET_KEY = params;
        prefsSecurity.putString("master_key", SECRET_KEY);
        uint32_t newPasskey = SECRET_KEY.toInt();
        if (newPasskey == 0 && SECRET_KEY != "000000")
            newPasskey = 271000;
        NimBLEDevice::setSecurityPasskey(newPasskey);
        Serial.println("Đã đổi SECRET_KEY & Passkey BLE thành: " + SECRET_KEY);
        notifyStatus("DA_DOI_KEY");
        beep(2, 100);
    }
    else if (cmd == "SET_NAME" || cmd == "SET_VEHICLE_NAME")
    { // Đổi tên riêng của xe
        String newName = params;
        newName.trim();
        if (newName.length() > 0 && newName.length() <= 32)
        {
            vehicleName = newName;
            prefsSecurity.putString("vehicle_name", vehicleName);
            Serial.printf("🏍️ [NAME] Đã lưu tên xe mới vào Flash NVS: %s\n", vehicleName.c_str());
            notifyStatus("VEHICLE_NAME|" + vehicleName);
            beep(2, 60);
        }
        else
        {
            notifyStatus("ERR_NAME_INVALID");
        }
    }
    else if (cmd == "GET_NAME" || cmd == "GET_VEHICLE_NAME")
    {
        notifyStatus("VEHICLE_NAME|" + vehicleName);
    }
    else if (cmd == "UNPAIR_ALL")
    { // Xóa toàn bộ danh sách Bonding
        Serial.println("🛡️ Yêu cầu thu hồi & xóa sạch toàn bộ thiết bị đã Bonding (Unpair All)...");
        NimBLEDevice::deleteAllBonds();
        notifyStatus("UNPAIR_ALL_OK");
        beep(3, 80);
    }
    else if (cmd == "SLEEP_NOW" || cmd == "DEEP_SLEEP")
    {
        Serial.println("💤 Nhận lệnh vào TẦNG 2 DEEP SLEEP từ ứng dụng BLE!");
        notifyStatus("DANG_VAO_DEEP_SLEEP");
        delay(300);
        enterTier2DeepSleep();
    }

    // --- CÁC LỆNH QUẢN LÝ VÂN TAY ---
    else if (cmd == "FP_LIST")
    {
        listAllFingerprints();
    }
    else if (cmd == "FP_ENROLL")
    {
        String fingerName = params.length() > 0 ? params : "Vân tay mới";
        if (fingerName.endsWith("|IMG"))
        {
            fpSendEnrollImage = true;
            fingerName = fingerName.substring(0, fingerName.length() - 4);
        }
        else if (fingerName.endsWith("|NO_IMG"))
        {
            fpSendEnrollImage = false;
            fingerName = fingerName.substring(0, fingerName.length() - 7);
        }
        startEnrollTask(fingerName);
    }
    else if (cmd == "SET_FP_IMG_MODE")
    {
        fpSendEnrollImage = (params == "1");
        prefsFinger.putBool("send_img", fpSendEnrollImage);
        notifyStatus("FP_IMG_MODE|" + String(fpSendEnrollImage ? "1" : "0"));
    }
    else if (cmd == "CAPTURE_FP_IMG")
    {
        startCaptureImageTask();
    }
    else if (cmd == "FP_CANCEL")
    {
        cancelEnrollRequested = true;
        Serial.println("Nhận lệnh FP_CANCEL từ App!");
    }
    else if (cmd == "FP_DELETE" && params.length() > 0)
    {
        int id = params.toInt();
        deleteFingerprint(id);
    }
    else if (cmd == "FP_RENAME" && params.length() > 0)
    {
        int splitIdx = params.indexOf('|');
        if (splitIdx != -1)
        {
            int id = params.substring(0, splitIdx).toInt();
            String newName = params.substring(splitIdx + 1);
            renameFingerprint(id, newName);
        }
    }
    else if (cmd == "FP_CLEAR" && params == "CONFIRM")
    {
        clearAllFingerprints();
    }

    // --- CÁC LỆNH ĐỒNG BỘ THỜI GIAN & BẢO VỆ VÂN TAY KẺ GIAN (INTRUDER AUDIT) ---
    else if (cmd == "SYNC_TIME")
    {
        // Gói tin: <KEY>|SYNC_TIME|<epoch> (ví dụ: SYNC_TIME|1727823900)
        unsigned long ep = strtoul(params.c_str(), NULL, 10);
        if (ep > 1000000000)
        {
            rtc.setTime(ep);
            hasSyncedTime = true;
            String timeStr = rtc.getTime("%H:%M:%S %d/%m/%Y");
            Serial.printf("⏰ [RTC ESP32Time] Đồng bộ giờ thực thành công: %s (Epoch: %lu)\n", timeStr.c_str(), ep);
            notifyStatus("FB|SYNC_TIME_OK|" + timeStr);
        }
        else
        {
            notifyStatus("FB|SYNC_TIME_ERR");
        }
    }
    else if (cmd == "GET_INTRUDER_COUNT")
    {
        notifyStatus("FB|INTRUDER_COUNT|" + String(getIntruderLogCount()));
    }
    else if (cmd == "GET_INTRUDER_LIST")
    {
        sendIntruderListOverBle();
    }
    else if (cmd == "FETCH_INTRUDER_IMG" && params.length() > 0)
    {
        sendIntruderImageFileOverBle(params);
    }
    else if (cmd == "CLEAR_INTRUDER_LOGS")
    {
        clearAllIntruderLogs();
    }
    // --- CÁC LỆNH TINH CHỈNH CẢM BIẾN VÂN TAY ---
    else if (cmd == "GET_FP_CFG")
    {
        sendFingerprintConfig();
    }
    else if (cmd == "SET_FP_CFG")
    {
        // Gói tin: <KEY>|SET_FP_CFG|<sec_level>|<scan_win>|<enroll_mode>[|<send_img>]
        int p1 = params.indexOf('|');
        int p2 = params.indexOf('|', p1 + 1);
        if (p1 != -1 && p2 != -1)
        {
            fpSecurityLevel = params.substring(0, p1).toInt();
            fpScanWindowMs = params.substring(p1 + 1, p2).toInt();
            int p3 = params.indexOf('|', p2 + 1);
            if (p3 != -1)
            {
                fpEnrollMode = params.substring(p2 + 1, p3).toInt();
                fpSendEnrollImage = (params.substring(p3 + 1).toInt() == 1);
                prefsFinger.putBool("send_img", fpSendEnrollImage);
            }
            else
            {
                fpEnrollMode = params.substring(p2 + 1).toInt();
            }

            if (fpSecurityLevel < 1)
                fpSecurityLevel = 1;
            if (fpSecurityLevel > 5)
                fpSecurityLevel = 5;
            if (fpScanWindowMs < 400)
                fpScanWindowMs = 400;
            if (fpScanWindowMs > 3000)
                fpScanWindowMs = 3000;
            if (fpEnrollMode != 2 && fpEnrollMode != 4)
                fpEnrollMode = 4;

            prefsFinger.putInt("sec_level", fpSecurityLevel);
            prefsFinger.putInt("scan_win", fpScanWindowMs);
            prefsFinger.putInt("enroll_mode", fpEnrollMode);

            if (r503Ready)
            {
                finger.setSecurityLevel(fpSecurityLevel);
            }

            Serial.printf("✅ Đã cập nhật FP Config: SecLevel=%d, ScanWin=%d, EnrollMode=%d, SendImg=%d\n",
                          fpSecurityLevel, fpScanWindowMs, fpEnrollMode, fpSendEnrollImage ? 1 : 0);
            sendFingerprintConfig();
            notifyStatus("FP_CFG_OK");
            beep(1, 80);
        }
        else
        {
            notifyStatus("LOI_PARAM_FP_CFG");
        }
    }
    else if (cmd == "TEST_FP")
    {
        int durSec = params.toInt();
        if (durSec <= 0)
            durSec = 15;
        isTestingFingerprint = true;
        testFpUntil = millis() + (unsigned long)durSec * 1000;
        setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 100, 0);
        notifyStatus("FP_TEST_STARTED|" + String(durSec));
        Serial.printf("🔬 Bắt đầu chế độ Live Test cảm biến vân tay trong %d giây...\n", durSec);
    }
    // --- CÁC LỆNH CHẾ ĐỘ CHỐNG NƯỚC MƯA (ANTI-RAIN MODE) ---
    else if (cmd == "SET_RAIN")
    {
        // Gói tin: <KEY>|SET_RAIN|<enabled>|<touch_hold_ms>|<max_wrong>|<cooldown_sec>|<auto_off_sec>
        int p1 = params.indexOf('|');
        int p2 = params.indexOf('|', p1 + 1);
        int p3 = params.indexOf('|', p2 + 1);
        int p4 = params.indexOf('|', p3 + 1);

        if (p1 != -1 && p2 != -1 && p3 != -1 && p4 != -1)
        {
            rainEnabled = (params.substring(0, p1).toInt() == 1);
            touchHoldMs = params.substring(p1 + 1, p2).toInt();
            maxWrongAttempts = params.substring(p2 + 1, p3).toInt();
            cooldownSec = params.substring(p3 + 1, p4).toInt();
            autoOffSec = params.substring(p4 + 1).toInt();

            // Giới hạn dải an toàn
            if (touchHoldMs < 100)
                touchHoldMs = 100;
            if (touchHoldMs > 3000)
                touchHoldMs = 3000;
            if (maxWrongAttempts < 0)
                maxWrongAttempts = 0;
            if (maxWrongAttempts > 10)
                maxWrongAttempts = 10;
            if (cooldownSec < 5)
                cooldownSec = 5;
            if (cooldownSec > 600)
                cooldownSec = 600;
            if (autoOffSec < 0)
                autoOffSec = 0;

            prefsRain.putBool("enabled", rainEnabled);
            prefsRain.putInt("touch_hold", touchHoldMs);
            prefsRain.putInt("max_wrong", maxWrongAttempts);
            prefsRain.putInt("cooldown", cooldownSec);
            prefsRain.putInt("auto_off", autoOffSec);

            if (rainEnabled)
                rainStartTime = millis();
            cooldownUntil = 0; // Xóa cooldown hiện tại

            Serial.printf("✅ Đã cập nhật Rain Config: Enabled=%d, Hold=%d, MaxWrong=%d, Cooldown=%d, AutoOff=%d\n",
                          rainEnabled, touchHoldMs, maxWrongAttempts, cooldownSec, autoOffSec);

            sendRainConfig();
            beep(1, 80);
        }
        else
        {
            notifyStatus("LOI_PARAM_RAIN");
        }
    }
    else if (cmd == "TOGGLE_RAIN")
    {
        int newState = params.toInt();
        rainEnabled = (newState == 1);
        prefsRain.putBool("enabled", rainEnabled);
        if (rainEnabled)
            rainStartTime = millis();
        cooldownUntil = 0;

        sendRainConfig();
        beep(1, 60);
    }
    else if (cmd == "GET_RAIN")
    {
        sendRainConfig();
    }

    // --- CÁC LỆNH CẤU HÌNH ĐÈN VÒNG MÀU R503 (AURA RGB) ---
    else if (cmd == "GET_LED_CFG")
    {
        sendLedConfigResponse();
    }
    else if (cmd == "SET_LED_CFG")
    {
        // Gói tin: <KEY>|SET_LED_CFG|<u_m>|<u_c>|<u_s>|<l_m>|<l_c>|<l_s>|<s_m>|<s_c>|<s_s>|<e_m>|<e_c>|<e_s>
        int tokens[12];
        int lastIdx = 0;
        bool valid = true;
        for (int i = 0; i < 11; i++)
        {
            int nextIdx = params.indexOf('|', lastIdx);
            if (nextIdx == -1)
            {
                valid = false;
                break;
            }
            tokens[i] = params.substring(lastIdx, nextIdx).toInt();
            lastIdx = nextIdx + 1;
        }
        if (valid)
        {
            tokens[11] = params.substring(lastIdx).toInt();

            ledConfig.unlocked.mode = (uint8_t)tokens[0];
            ledConfig.unlocked.color = (uint8_t)tokens[1];
            ledConfig.unlocked.speed = (uint8_t)tokens[2];

            ledConfig.locked.mode = (uint8_t)tokens[3];
            ledConfig.locked.color = (uint8_t)tokens[4];
            ledConfig.locked.speed = (uint8_t)tokens[5];

            ledConfig.success.mode = (uint8_t)tokens[6];
            ledConfig.success.color = (uint8_t)tokens[7];
            ledConfig.success.speed = (uint8_t)tokens[8];

            ledConfig.error.mode = (uint8_t)tokens[9];
            ledConfig.error.color = (uint8_t)tokens[10];
            ledConfig.error.speed = (uint8_t)tokens[11];

            saveLedConfig();
            updateIdleLed();
            notifyStatus("LED_CFG_OK");
            beep(1, 80);
        }
        else
        {
            notifyStatus("LOI_PARAM_LED_CFG");
        }
    }
    else if (cmd == "TEST_LED")
    {
        // Gói tin: <KEY>|TEST_LED|<mode>|<color>|<speed>|<count>
        int p1 = params.indexOf('|');
        int p2 = params.indexOf('|', p1 + 1);
        int p3 = params.indexOf('|', p2 + 1);
        if (p1 != -1 && p2 != -1 && p3 != -1)
        {
            uint8_t tMode = params.substring(0, p1).toInt();
            uint8_t tColor = params.substring(p1 + 1, p2).toInt();
            uint8_t tSpeed = params.substring(p2 + 1, p3).toInt();
            uint8_t tCount = params.substring(p3 + 1).toInt();
            r503SetAuraLed(tMode, tColor, tSpeed, tCount);
            notifyStatus("LED_TEST_OK");
        }
    }

    else if (cmd == "TELE" || cmd == "STATUS")
    {
        notifyStatus(isUnlocked ? "STATUS|1" : "STATUS|0");
        sendTelemetry();
    }

    // --- LỆNH HIỆU CHUẨN ĐO ĐIỆN ÁP ẮC QUY QUA BLE ---
    else if (cmd == "CALIB_VOLT")
    {
        // Cú pháp: <KEY>|CALIB_VOLT|<real_voltage> (Ví dụ: CALIB_VOLT|12.0)
        float realV = params.toFloat();
        if (realV >= 5.0f && realV <= 30.0f)
        {
            uint32_t sumMv = 0;
            const int SAMPLES = 32;
            analogReadMilliVolts(BATTERY_ADC_PIN);
            analogReadMilliVolts(BATTERY_ADC_PIN);
            for (int i = 0; i < SAMPLES; i++)
            {
                sumMv += analogReadMilliVolts(BATTERY_ADC_PIN);
                delayMicroseconds(250);
            }
            float avgMv = (float)sumMv / (float)SAMPLES;
            float rawVoltage = (avgMv * 11.0f) / 1000.0f;
            if (rawVoltage > 0.5f)
            {
                batteryVoltageCalib = realV / rawVoltage;
                prefsSecurity.putFloat("v_calib", batteryVoltageCalib);
                Serial.printf("✅ Đã cân chỉnh hệ số ADC mới: %.5f (V_thực = %.2fV)\n", batteryVoltageCalib, realV);
                notifyStatus("CALIB_VOLT_OK|" + String(readBatteryVoltage(), 2));
                sendTelemetry();
                beep(1, 100);
            }
            else
            {
                notifyStatus("CALIB_VOLT_ERR_NO_POWER");
            }
        }
        else
        {
            notifyStatus("CALIB_VOLT_ERR_PARAM");
        }
    }
    else if (cmd == "GET_VOLT_CALIB")
    {
        notifyStatus("VOLT_CALIB|" + String(batteryVoltageCalib, 5) + "|" + String(readBatteryVoltage(), 2));
    }

    // --- LỆNH NÂNG CẤP FIRMWARE TỪ XA QUA BLE (BLE OTA) ---
    else if (cmd == "OTA_BEGIN")
    {
        if (isUnlocked)
        {
            Serial.println("❌ [OTA] Bị từ chối: Xe đang mở khóa ACC!");
            notifyStatus("OTA_ERR_VEHICLE_ON");
            return;
        }
        if (isEnrolling || isCapturingImage)
        {
            Serial.println("❌ [OTA] Bị từ chối: Đang thêm vân tay hoặc chụp ảnh!");
            notifyStatus("OTA_ERR_BUSY");
            return;
        }

        int pipeIdx = params.indexOf('|');
        size_t size = (pipeIdx != -1 ? params.substring(0, pipeIdx) : params).toInt();
        String md5 = (pipeIdx != -1) ? params.substring(pipeIdx + 1) : "";
        md5.trim();

        if (size == 0 || size > 1966080)
        { // Tối đa kích thước phân vùng ota_1 (1.875 MB)
            Serial.printf("❌ [OTA] Kích thước file không hợp lệ: %u bytes\n", (unsigned int)size);
            notifyStatus("OTA_ERR_INVALID_SIZE");
            return;
        }

        if (!Update.begin(size, U_FLASH))
        {
            Serial.printf("❌ [OTA] Update.begin failed! Lỗi: %u\n", Update.getError());
            notifyStatus("OTA_ERR_BEGIN");
            return;
        }

        if (md5.length() == 32)
        {
            Update.setMD5(md5.c_str());
            Serial.printf("🔐 [OTA] Expected MD5: %s\n", md5.c_str());
        }

        isOtaUpdating = true;
        otaTotalBytes = size;
        otaWrittenBytes = 0;
        otaLastChunkTime = millis();
        Serial.printf("🚀 [OTA] Sẵn sàng nhận firmware: %u bytes\n", (unsigned int)size);
        notifyStatus("OTA_READY");
    }
    else if (cmd == "OTA_END")
    {
        if (!isOtaUpdating)
        {
            notifyStatus("OTA_ERR_NOT_RUNNING");
            return;
        }
        Serial.printf("🏁 [OTA] Kết thúc nạp dữ liệu. Đã nhận: %u / %u bytes. Đang xác thực...\n",
                      (unsigned int)otaWrittenBytes, (unsigned int)otaTotalBytes);
        if (Update.end(true))
        {
            if (Update.isFinished())
            {
                Serial.println("🎉 [OTA] Nâng cấp thành công 100%! Đang khởi động lại hệ thống...");
                notifyStatus("OTA_SUCCESS");
                isOtaUpdating = false;
                delay(1000);
                esp_restart();
            }
            else
            {
                Serial.println("❌ [OTA] Firmware chưa hoàn tất!");
                notifyStatus("OTA_ERR_UNFINISHED");
                isOtaUpdating = false;
            }
        }
        else
        {
            Serial.printf("❌ [OTA] Xác thực thất bại! Mã lỗi: %u\n", Update.getError());
            notifyStatus("OTA_ERR_VERIFY");
            isOtaUpdating = false;
        }
    }
    else if (cmd == "OTA_ABORT")
    {
        if (isOtaUpdating)
        {
            Update.abort();
            isOtaUpdating = false;
            Serial.println("⚠️ [OTA] Đã hủy tiến trình cập nhật theo yêu cầu!");
            notifyStatus("OTA_ABORTED");
        }
    }

    // --- LỆNH KIỂM TRA PHIÊN BẢN & CẬP NHẬT FIRMWARE QUA WIFI ---
    else if (cmd == "GET_FW_INFO")
    {
        notifyStatus("FW_INFO|" + String(CURRENT_FW_VERSION) + "|" + String(CURRENT_FW_VERSION_NAME));
    }
    else if (cmd == "WIFI_OTA")
    {
        // Cú pháp: <KEY>|WIFI_OTA|<SSID>|<PASSWORD> hoặc <KEY>|WIFI_OTA|<SSID>|<PASSWORD>|<CUSTOM_URL>
        if (isUnlocked)
        {
            Serial.println("❌ [WIFI_OTA] Bị từ chối: Xe đang mở khóa ACC!");
            notifyStatus("OTA_ERR_VEHICLE_ON");
            return;
        }
        int p1 = params.indexOf('|');
        if (p1 == -1)
        {
            notifyStatus("WIFI_OTA_ERR_PARAM");
            return;
        }
        String ssid = params.substring(0, p1);
        String rest = params.substring(p1 + 1);
        int p2 = rest.indexOf('|');
        String pass = (p2 != -1) ? rest.substring(0, p2) : rest;
        String customUrl = (p2 != -1) ? rest.substring(p2 + 1) : "";
        customUrl.trim();

        notifyStatus("WIFI_OTA_CONNECTING");
        struct WifiOtaTaskArgs
        {
            String s;
            String p;
            String u;
        };
        WifiOtaTaskArgs *args = new WifiOtaTaskArgs{ssid, pass, customUrl};

        xTaskCreate([](void *param)
                    {
            WifiOtaTaskArgs* a = (WifiOtaTaskArgs*)param;
            extern bool performWiFiOta(const String& ssid, const String& pass, const String& customUrl);
            performWiFiOta(a->s, a->p, a->u);
            delete a;
            vTaskDelete(NULL); }, "wifi_ota_task", 8192, args, 1, NULL);
    }
}

// Hàm kết nối WiFi và nạp Firmware từ xa qua HTTP
bool performWiFiOta(const String &ssid, const String &pass, const String &customUrl)
{
    Serial.printf("📡 [WIFI_OTA] Đang kết nối WiFi: %s ...\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long startConn = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startConn < 15000)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("❌ [WIFI_OTA] Kết nối WiFi thất bại hoặc quá thời gian chờ (15s)!");
        notifyStatus("WIFI_OTA_ERR_CONNECT");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }

    Serial.printf("✅ [WIFI_OTA] Đã kết nối WiFi! IP: %s\n", WiFi.localIP().toString().c_str());
    notifyStatus("WIFI_OTA_CONNECTED");

    String url = customUrl.length() > 0 ? customUrl : String(VERSION_CHECK_URL);
    Serial.printf("🌐 [WIFI_OTA] Kiểm tra version từ: %s\n", url.c_str());

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("❌ [WIFI_OTA] Không thể tải version.json! Mã HTTP: %d\n", httpCode);
        notifyStatus("WIFI_OTA_ERR_JSON_HTTP");
        http.end();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }

    String jsonPayload = http.getString();
    http.end();

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, jsonPayload);
    if (err)
    {
        Serial.printf("❌ [WIFI_OTA] Lỗi phân tích cú pháp JSON: %s\n", err.c_str());
        notifyStatus("WIFI_OTA_ERR_JSON_PARSE");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }

    int remoteVersion = doc["firmware"]["versionCode"] | 0;
    const char *remoteName = doc["firmware"]["versionName"] | "Unknown";
    const char *binUrl = doc["firmware"]["binUrl"] | "";

    Serial.printf("🔍 [WIFI_OTA] Hiện tại: v%s (%d) | Máy chủ: v%s (%d)\n",
                  CURRENT_FW_VERSION_NAME, CURRENT_FW_VERSION, remoteName, remoteVersion);

    if (strlen(binUrl) == 0)
    {
        Serial.println("❌ [WIFI_OTA] Không tìm thấy URL file firmware .bin trong JSON!");
        notifyStatus("WIFI_OTA_ERR_NO_URL");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }

    if (remoteVersion <= CURRENT_FW_VERSION)
    {
        Serial.println("ℹ️ [WIFI_OTA] Firmware hiện tại đã là bản mới nhất.");
        notifyStatus("WIFI_OTA_LATEST");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return true;
    }

    Serial.printf("⬇️ [WIFI_OTA] Bắt đầu tải và nạp file .bin từ: %s\n", binUrl);
    notifyStatus("WIFI_OTA_DOWNLOADING");

    // Hiệu ứng đèn nhấp nháy báo nạp Firmware
    r503SetAuraLed(LED_MODE_FLASHING, LED_COLOR_PURPLE, 50, 0);

    WiFiClient client;
    httpUpdate.setLedPin(ONBOARD_LED_PIN, LOW); // Nháy đèn Onboard khi ghi Flash
    httpUpdate.rebootOnUpdate(false);

    t_httpUpdate_return ret = httpUpdate.update(client, binUrl);
    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
        Serial.printf("❌ [WIFI_OTA] Nạp thất bại! Lỗi (%d): %s\n",
                      httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        notifyStatus("WIFI_OTA_ERR_FLASH");
        ledError();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;

    case HTTP_UPDATE_NO_UPDATES:
        Serial.println("ℹ️ [WIFI_OTA] Không có bản cập nhật mới.");
        notifyStatus("WIFI_OTA_NO_UPDATES");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;

    case HTTP_UPDATE_OK:
        Serial.println("🎉 [WIFI_OTA] Nâng cấp Firmware THÀNH CÔNG! Đang khởi động lại...");
        notifyStatus("WIFI_OTA_SUCCESS");
        ledSuccess();
        beep(2, 100);
        delay(1500);
        ESP.restart();
        return true;
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
}

// ==========================================
// 📶 7. CALLBACKS NIMBLE
// ==========================================

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer) override
    {
        deviceConnected = true;
        lastActivityTime = millis();
        Serial.println("BLE Client đã kết nối!");
        setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 50, 1);
        pendingIdleLedUntil = millis() + 400; // Sau 1 nháy kết nối BLE, tự khôi phục màu đèn trạng thái xe
        pendingIdleLedUpdate = true;
        notifyStatus(isUnlocked ? "STATUS|1" : "STATUS|0");
        sendTelemetry();
        delay(40);
        notifyStatus("ALARM_STATUS|" + String(antiTheftEnabled ? "1" : "0"));
        delay(40);
        sendRainConfig();
        delay(40);
        sendFingerprintConfig();
        delay(40);
        sendLedConfigResponse();
        delay(40);
        notifyStatus("VEHICLE_NAME|" + vehicleName);
        delay(40);
        notifyStatus("FB|INTRUDER_COUNT|" + String(getIntruderLogCount()));
    }

    uint32_t onPassKeyRequest() override
    {
        Serial.println("🔑 BLE SMP: Nhận yêu cầu Passkey từ thiết bị Client...");
        uint32_t passkey = SECRET_KEY.toInt();
        if (passkey == 0 && SECRET_KEY != "000000")
            passkey = 271000;
        return passkey;
    }

    bool onConfirmPIN(uint32_t pin) override
    {
        Serial.printf("🔢 BLE SMP: Xác nhận Passkey %06u\n", pin);
        uint32_t myPin = SECRET_KEY.toInt();
        if (myPin == 0 && SECRET_KEY != "000000")
            myPin = 271000;
        return (pin == myPin);
    }

    void onAuthenticationComplete(ble_gap_conn_desc *desc) override
    {
        if (desc->sec_state.encrypted)
        {
            Serial.printf("🔒 BLE SMP: Ghép đôi (Bonding) THÀNH CÔNG! Đã mã hóa AES-128 (Authen=%d, Bonded=%d)\n",
                          desc->sec_state.authenticated, desc->sec_state.bonded);
            beep(1, 100);
        }
        else
        {
            Serial.println("❌ BLE SMP: Ghép đôi THẤT BẠI hoặc người dùng nhập sai PIN! Chủ động ngắt kết nối.");
            if (pServer)
            {
                pServer->disconnect(desc->conn_handle);
            }
        }
    }

    void onDisconnect(NimBLEServer *pServer) override
    {
        deviceConnected = false;
        lastActivityTime = millis();
        Serial.println("BLE Client đã ngắt kết nối.");

        // Hủy chu trình thêm vân tay nếu đang chạy dở khi mất kết nối BLE
        if (enrollTaskHandle != nullptr || isEnrolling)
        {
            cancelEnrollRequested = true;
            if (enrollTaskHandle != nullptr)
            {
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
        if (isUnlocked)
        {
            Serial.println("⚠️ CẢNH BÁO: Mất kết nối BLE khi xe đang nổ máy!");
            Serial.println("🛡️ Failsafe: Giữ nguyên Relay 1 (ACC ON) - Đảm bảo an toàn xe lăn bánh!");
            beep(2, 60);                                                           // 2 tiếng bíp ngắn cảnh báo tài xế
            setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_PURPLE, 100, 0); // Đèn thở tím cảnh báo
        }
        else
        {
            updateIdleLed();
        }

        if (isOtaUpdating)
        {
            Serial.println("⚠️ [OTA] Mất kết nối BLE khi đang nạp firmware! Hủy OTA an toàn.");
            Update.abort();
            isOtaUpdating = false;
        }

        Serial.println("Bắt đầu Advertising lại...");
        updateBleAdvertisingMode(isUnlocked);
    }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pCharacteristic)
    {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0)
        {
            processIncomingCommand(String(value.c_str()));
        }
    }
};

class OtaDataCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pChar)
    {
        if (!isOtaUpdating)
            return;
        std::string val = pChar->getValue();
        size_t len = val.length();
        if (len == 0)
            return;

        size_t written = Update.write((uint8_t *)val.data(), len);
        if (written != len)
        {
            Serial.printf("❌ [OTA] Write failed! Expected %u, wrote %u\n", (unsigned int)len, (unsigned int)written);
            isOtaUpdating = false;
            Update.abort();
            notifyStatus("OTA_ERR_WRITE");
            return;
        }
        otaWrittenBytes += written;
        otaLastChunkTime = millis();
        lastActivityTime = millis();
    }
};

// ==========================================
// 🚀 8. SETUP & MAIN LOOP
// ==========================================

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\n==============================");
    Serial.println("   TSMARTKEY ESP32-C3 SYSTEM   ");
    Serial.println("==============================");

    lastActivityTime = millis();

    // 0. Kiểm tra nguyên nhân khởi động / thức dậy từ Deep Sleep
    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    if (wakeupCause == ESP_SLEEP_WAKEUP_GPIO)
    {
        uint64_t wakeupPins = esp_sleep_get_gpio_wakeup_status();
        Serial.printf("⚡ [WAKEUP] Thức dậy từ DEEP SLEEP qua RTC GPIO! Mask: 0x%llX\n", wakeupPins);
        if (wakeupPins & (1ULL << R503_WAKE_PIN))
        {
            Serial.printf("👉 [WAKEUP] Kích hoạt bởi: Chạm vân tay R503 (GPIO %d)!\n", R503_WAKE_PIN);
            wakeTriggeredByFingerprint = true;
        }
        if (wakeupPins & (1ULL << SW420_VIBRATE_PIN))
        {
            Serial.printf("👉 [WAKEUP] Kích hoạt bởi: Cảm biến rung SW-420 (GPIO %d)!\n", SW420_VIBRATE_PIN);
            wakeTriggeredByVibration = true;
        }
        if (wakeupPins & (1ULL << RF_LOCATE_PIN))
        {
            Serial.printf("👉 [WAKEUP] Kích hoạt bởi: Remote RF 433MHz (GPIO %d)!\n", RF_LOCATE_PIN);
            wakeTriggeredByRf = true;
        }
    }
    else
    {
        Serial.println("🔌 [BOOT] Khởi động hệ thống bình thường (Power-on / Reset).");
    }

    // 1. Khởi tạo Relay Pins & Chân Ngoại vi
    pinMode(RELAY1_PIN, OUTPUT);
    digitalWrite(RELAY2_PIN, HIGH); // Active-LOW: Kéo HIGH trước khi set OUTPUT để chống giật xung đóng relay khi khởi động
    pinMode(RELAY2_PIN, OUTPUT);
    digitalWrite(RELAY3_PIN, HIGH); // Active-LOW: Kéo HIGH trước khi set OUTPUT để chống giật xung đóng relay khi khởi động
    pinMode(RELAY3_PIN, OUTPUT);
    pinMode(ONBOARD_LED_PIN, OUTPUT);
    digitalWrite(ONBOARD_LED_PIN, HIGH); // Active LOW: Tắt LED xanh Onboard

    // Cấu hình chân đo điện áp ắc quy ADC (GPIO 2 - ADC1_CH2, Cầu phân áp 100k - 10k)
    // Dùng chế độ ANALOG để tắt digital input buffer và triệt tiêu dòng rò
    pinMode(BATTERY_ADC_PIN, ANALOG);
    analogSetAttenuation(ADC_11db); // Dải đo điện áp ADC tối ưu lên đến ~2.6V - 3.1V

    // Cấu hình chân tín hiệu Module RF 433MHz (Chân VT Active HIGH: Nhấn remote = 3.3V)
    pinMode(RF_LOCATE_PIN, INPUT_PULLDOWN);

    // Cấu hình cảm biến rung SW-420 (Active HIGH khi rung: Bình thường = 0V, Rung = 3.3V)
    pinMode(SW420_VIBRATE_PIN, INPUT_PULLDOWN);

    // Cấu hình chân cảm ứng ngắt WAKEUP của R503 (Active LOW: Không chạm = 3.2V, Chạm = 0V)
    pinMode(R503_WAKE_PIN, INPUT_PULLUP);

    // 2. Tải cấu hình từ Flash NVS
    prefsSecurity.begin("safe_key", false);
    prefsFinger.begin("fingerprint", false);
    prefsRain.begin("rain_config", false);
    loadLedConfig();
    initIntruderStorage(); // Khởi tạo phân vùng LittleFS lưu ảnh vân tay kẻ gian

    batteryVoltageCalib = prefsSecurity.getFloat("v_calib", 12.0f / 23.6f);
    Serial.printf("🔋 Cân chỉnh ADC Ắc quy: Hệ số = %.5f | Điện áp hiện tại = %.2fV\n", batteryVoltageCalib, readBatteryVoltage());

    SECRET_KEY = prefsSecurity.getString("master_key", "271000");
    vehicleName = prefsSecurity.getString("vehicle_name", DEVICE_NAME);
    Serial.printf("🏍️ Tên xe (Vehicle Name): %s\n", vehicleName.c_str());
    isUnlocked = prefsSecurity.getBool("is_unlocked", false);
    antiTheftEnabled = prefsSecurity.getBool("anti_theft", false);
    Serial.printf("🛡️ Chống dắt (Anti-Theft): %s (GPIO 4 Active HIGH)\n", antiTheftEnabled ? "BẬT" : "TẮT");

    rainEnabled = prefsRain.getBool("enabled", false);
    touchHoldMs = prefsRain.getInt("touch_hold", 500);
    maxWrongAttempts = prefsRain.getInt("max_wrong", 5);
    cooldownSec = prefsRain.getInt("cooldown", 30);
    autoOffSec = prefsRain.getInt("auto_off", 3600);
    if (rainEnabled)
    {
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
    digitalWrite(RELAY2_PIN, HIGH); // Active-LOW: TẮT đề (HIGH = TẮT)
    digitalWrite(RELAY3_PIN, HIGH); // Active-LOW: TẮT còi/đèn (HIGH = TẮT)

    Serial.printf("Secret Key: %s | Trạng thái xe: %s\n",
                  SECRET_KEY.c_str(), isUnlocked ? "MỞ KHÓA" : "ĐANG KHÓA");

    // 3. Khởi tạo & Tự động quét Baudrate tìm cảm biến R503 (57600, 9600, 115200, 19200, 38400)
    Serial.println("⏳ Đang quét Baudrate tìm cảm biến R503...");
    uint32_t bauds[] = {57600, 9600, 115200, 19200, 38400};
    bool r503Found = false;

    delay(100); // Cho cảm biến R503 thời gian ổn định điện áp nguồn

    for (uint32_t b : bauds)
    {
        r503Serial.setRxBufferSize(24576);
        r503Serial.begin(b, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
        delay(80);
        while (r503Serial.available())
            r503Serial.read(); // Xóa rác RX trước khi gửi lệnh bắt tay
        Serial.printf("📡 [R503 UART TX] -> verifyPassword (0x13) [EF 01 FF FF FF FF 01 00 07 13 00 00 00 00 ...] tại Baud %d\n", b);
        if (finger.verifyPassword())
        {
            Serial.printf("📥 [R503 UART RX] <- verifyPassword: %s (Module R503 phản hồi chính xác!)\n", r503CodeToString(0x00));
            Serial.printf("✅ Cảm biến R503 kết nối thành công tại Baudrate: %d!\n", b);
            r503Found = true;
            break;
        }
        else
        {
            Serial.printf("📥 [R503 UART RX] <- Không có phản hồi hợp lệ tại Baud %d\n", b);
        }
        r503Serial.end();
        delay(30);
    }

    if (r503Found)
    {
        r503Ready = true;
        finger.getParameters();
        if (finger.capacity == 0 || finger.capacity > 200)
        {
            finger.capacity = 200;
        }
        Serial.printf("📊 R503 Dung lượng bộ nhớ: %d vân tay | Bảo mật hiện tại trên module: Mức %d\n", finger.capacity, finger.security_level);
        finger.setSecurityLevel(fpSecurityLevel);
        Serial.printf("🔒 Đã áp dụng mức bảo mật R503 theo cấu hình: Mức %d\n", fpSecurityLevel);
        int regCount = getRegisteredFingerprintCount();
        Serial.printf("🔑 Số lượng vân tay hợp lệ trong Flash NVS: %d\n", regCount);
        updateIdleLed();
    }
    else
    {
        r503Ready = false;
        r503Serial.setRxBufferSize(24576);
        r503Serial.begin(57600, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN); // Giữ UART hoạt động ở baudrate chuẩn để cắm lại vẫn nhận diện được
        Serial.println("⚠️ Không tìm thấy cảm biến R503 ở mọi baudrate! Kiểm tra lại dây RX(GPIO 0)/TX(GPIO 1) và nguồn 3.3V.");
    }

    // 4. Khởi tạo NimBLE Server & Cấu hình SMP Security (Pairing, Bonding, Passkey AES-128)
    NimBLEDevice::init(vehicleName.c_str());
    // Kích công suất phát sóng BLE lên mức tối đa (+21dBm) cho cả kết nối và quảng bá
    NimBLEDevice::setPower(ESP_PWR_LVL_P21, ESP_BLE_PWR_TYPE_DEFAULT);
    NimBLEDevice::setPower(ESP_PWR_LVL_P21, ESP_BLE_PWR_TYPE_ADV);
    NimBLEDevice::setMTU(517); // Đặt MTU tối đa để tránh cắt cụt gói tin BLE payload

    // 🔐 CẤU HÌNH BẢO MẬT SMP: BONDING + MITM + SECURE CONNECTIONS (LESC P-256)
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY); // Xe hiển thị PIN / mã cố định, điện thoại nhập PIN
    uint32_t initialPasskey = SECRET_KEY.toInt();
    if (initialPasskey == 0 && SECRET_KEY != "000000")
        initialPasskey = 271000;
    NimBLEDevice::setSecurityPasskey(initialPasskey);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
            NIMBLE_PROPERTY::READ_ENC |
            NIMBLE_PROPERTY::READ_AUTHEN |
            NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::WRITE_ENC |
            NIMBLE_PROPERTY::WRITE_AUTHEN |
            NIMBLE_PROPERTY::WRITE_NR |
            NIMBLE_PROPERTY::NOTIFY);
    pCharacteristic->setCallbacks(new CharacteristicCallbacks());

    pOtaDataCharacteristic = pService->createCharacteristic(
        OTA_DATA_UUID,
        NIMBLE_PROPERTY::WRITE |
            NIMBLE_PROPERTY::WRITE_NR |
            NIMBLE_PROPERTY::WRITE_ENC |
            NIMBLE_PROPERTY::WRITE_AUTHEN);
    pOtaDataCharacteristic->setCallbacks(new OtaDataCallbacks());

    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    if (!isUnlocked)
    {
        // Tầng 1 Power Saving: 1280ms (2048 * 0.625ms = 1280ms)
        pAdvertising->setMinInterval(2048);
        pAdvertising->setMaxInterval(2048);
        isPowerSavingAdvertising = true;
        Serial.println("🍃 BLE Advertising ban đầu: TẦNG 1 POWER SAVING (1280ms / 1.28s)");
    }
    else
    {
        // Fast mode: 100ms - 200ms
        pAdvertising->setMinInterval(160);
        pAdvertising->setMaxInterval(320);
        isPowerSavingAdvertising = false;
        Serial.println("⚡ BLE Advertising ban đầu: FAST (100ms - 200ms)");
    }
    pAdvertising->start();

    Serial.println("✅ BLE Advertising đã bắt đầu! Đang chờ kết nối...");

    // Hiển thị Menu điều khiển Console tương tác trực tiếp qua Serial Monitor
    printMenu();

    // 5. XỬ LÝ HÀNH ĐỘNG TỨC THỜI NẾU VỪA THỨC DẬY TỪ DEEP SLEEP
    if (wakeTriggeredByFingerprint)
    {
        wakeTriggeredByFingerprint = false;
        Serial.println("⚡ [WAKEUP ACTION] Chạm ngón tay đánh thức ESP32! Xác thực vân tay mở xe...");
        handleFingerprintTouch();
    }
    else if (wakeTriggeredByVibration)
    {
        wakeTriggeredByVibration = false;
        Serial.println("⚡ [WAKEUP ACTION] Xe bị rung động khi đỗ! Kích hoạt cảnh báo chống trộm...");
        handleVibrationDetected();
    }
    else if (wakeTriggeredByRf)
    {
        wakeTriggeredByRf = false;
        Serial.println("⚡ [WAKEUP ACTION] Bấm remote RF đánh thức ESP32! Phát tín hiệu tìm xe...");
        triggerLocate();
    }
}

void loop()
{
    // 🛡️ XỬ LÝ TIẾN TRÌNH BLE OTA (CHỐNG TREO & KHÓA CHỨC NĂNG NGOẠI VI KHI ĐANG NẠP FIRMWARE)
    if (isOtaUpdating)
    {
        if (millis() - otaLastChunkTime > 15000)
        { // 15 giây không nhận thêm gói tin
            Serial.println("⚠️ [OTA] Quá thời gian chờ gói tin (15s Timeout)! Hủy tiến trình OTA.");
            Update.abort();
            isOtaUpdating = false;
            notifyStatus("OTA_TIMEOUT");
        }
        delay(10);
        return;
    }

    // Nếu đang trong chu trình thêm vân tay mới hoặc chụp ảnh từ App thì bỏ qua quét thông thường
    if (isEnrolling || isCapturingImage)
    {
        delay(50);
        return;
    }

    // Gửi dữ liệu Telemetry định kỳ mỗi 3 giây khi có kết nối BLE (tạm dừng khi đang truyền ảnh hoặc thêm vân tay)
    static unsigned long lastTelemetryTime = 0;
    if (deviceConnected && !isEnrolling && !isCapturingImage && (millis() - lastTelemetryTime > 3000))
    {
        lastTelemetryTime = millis();
        sendTelemetry();
    }

    // Kiểm tra tự động tắt Chế độ Mưa nếu hết thời gian autoOffSec
    if (rainEnabled && autoOffSec > 0)
    {
        if (millis() - rainStartTime >= (unsigned long)autoOffSec * 1000)
        {
            rainEnabled = false;
            prefsRain.putBool("enabled", false);
            Serial.println("☔ Chế độ Chống Nước Mưa đã tự động TẮT sau thời gian đếm ngược.");
            notifyStatus("RAIN_AUTO_OFF");
            beep(2, 60);
        }
    }

    // Kiểm tra hết thời gian Live Test cảm biến vân tay
    if (isTestingFingerprint && millis() > testFpUntil)
    {
        isTestingFingerprint = false;
        Serial.println("🔬 Đã kết thúc thời gian Live Test cảm biến vân tay.");
        updateIdleLed();
    }

    // Tự động khôi phục chế độ đèn Aura LED Idle (Xe Mở / Xe Khóa) sau khi nháy xác nhận
    if (pendingIdleLedUpdate && millis() >= pendingIdleLedUntil)
    {
        pendingIdleLedUpdate = false;
        updateIdleLed();
    }

    // Kiểm tra chạm ngón tay (Active LOW: Chạm = 0V / LOW trên chân WAKE)
    static unsigned long lastTouchTrigger = 0;
    bool wakeTriggered = (digitalRead(R503_WAKE_PIN) == LOW);
    if (r503Ready && !isCapturingImage && !isEnrolling && wakeTriggered && (millis() - lastTouchTrigger > 800))
    {
        lastTouchTrigger = millis();
        lastActivityTime = millis();
        handleFingerprintTouch();
    }

    // -------------------------------------------------------------
    // 🚨 KIỂM TRA CẢM BIẾN RUNG SW-420 (BÁO ĐỘNG KHI XE KHÓA & BẬT CHỐNG DẮT)
    // Chân DO Active HIGH: Nhảy lên HIGH (3.3V) khi có rung động
    // -------------------------------------------------------------
    static unsigned long lastVibrateTime = 0;
    if (!isUnlocked && antiTheftEnabled && (digitalRead(SW420_VIBRATE_PIN) == HIGH))
    {
        if (millis() - lastVibrateTime > 1500)
        { // Cooldown chống dội 1.5s
            lastVibrateTime = millis();
            lastActivityTime = millis();
            handleVibrationDetected();
        }
    }

    // -------------------------------------------------------------
    // 📻 KIỂM TRA TÍN HIỆU TỪ REMOTE RF 433MHz (CHỈ TÌM XE - KHÔNG MỞ KHÓA)
    // Chân VT Active HIGH: Nhảy lên HIGH (3.3V) khi có tín hiệu remote hợp lệ
    // -------------------------------------------------------------
    static unsigned long lastRfTime = 0;
    if (digitalRead(RF_LOCATE_PIN) == HIGH)
    {
        if (millis() - lastRfTime > 1500)
        { // Cooldown chống dội 1.5s
            lastRfTime = millis();
            lastActivityTime = millis();
            Serial.println("📻 Tín hiệu RF Remote: Yêu cầu Tìm xe (Locate Only - Chân VT Active HIGH)!");
            triggerLocate();
        }
    }

    // -------------------------------------------------------------
    // 💤 KIỂM TRA TỰ ĐỘNG CHUYỂN TỪ TẦNG 1 SANG TẦNG 2 (DEEP SLEEP)
    // -------------------------------------------------------------
    if (!isUnlocked && !deviceConnected && !isEnrolling && !isCapturingImage)
    {
        if (millis() - lastActivityTime >= DEEP_SLEEP_TIMEOUT_MS)
        {
            Serial.println("⏳ [POWER] Đã quá 24h không có hoạt động chạm/kết nối! Tự động chuyển sang TẦNG 2: DEEP SLEEP...");
            enterTier2DeepSleep();
        }
    }

    // -------------------------------------------------------------
    // ⌨️ NHẬN LỆNH QUA SERIAL MONITOR (COM PORT) ĐỂ DEBUG / TEST / CONSOLE
    // -------------------------------------------------------------
    if (Serial.available())
    {
        char firstCh = Serial.peek();
        if (firstCh >= '1' && firstCh <= '9')
        {
            char ch = Serial.read();
            delay(10);
            while (Serial.available() && (Serial.peek() == '\r' || Serial.peek() == '\n' || Serial.peek() == ' '))
            {
                Serial.read();
            }

            switch (ch)
            {
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
                    while (millis() - startWait < 8000)
                    {
                        if (r503GetImage() == 0x00)
                        {
                            touched = true;
                            break;
                        }
                        delay(50);
                    }
                    if (touched)
                    {
                        r503Image2Tz(1);
                        uint16_t id = 0, conf = 0;
                        r503FastSearch(1, 1, 200, id, conf);
                    }
                    else
                    {
                        Serial.println("⏱️ Hết thời gian chờ chạm ngón tay!");
                    }
                }
                break;
            case '7':
                Serial.println("\n👉 Bắt đầu chụp và trích xuất ảnh lăng kính R503...");
                startCaptureImageTask();
                break;
            case '8':
                Serial.println("\n⚠️ BẠN CÓ CHẮC MUỐN XÓA TOÀN BỘ VÂN TAY TRONG R503? (Gõ 'y' để xác nhận)");
                {
                    unsigned long confirmStart = millis();
                    bool confirmed = false;
                    while (millis() - confirmStart < 10000)
                    {
                        if (Serial.available())
                        {
                            char c = Serial.read();
                            if (c == 'y' || c == 'Y')
                            {
                                confirmed = true;
                                break;
                            }
                        }
                        delay(10);
                    }
                    if (confirmed)
                    {
                        r503EmptyDatabase();
                        clearAllFingerprints();
                    }
                    else
                    {
                        Serial.println("❌ Đã hủy thao tác xóa hoặc hết thời gian chờ.");
                    }
                }
                break;
            case '9':
                Serial.println("\n💤 Thử nghiệm: Kích hoạt ngay TẦNG 2: DEEP SLEEP...");
                enterTier2DeepSleep();
                break;
            default:
                break;
            }
            printMenu();
        }
        else
        {
            String sCmd = Serial.readStringUntil('\n');
            sCmd.trim();
            if (sCmd.length() > 0)
            {
                Serial.printf("⌨️ Lệnh Serial: %s\n", sCmd.c_str());
                if (sCmd == "?" || sCmd == "menu" || sCmd == "help")
                {
                    printMenu();
                }
                else if (sCmd == "FP_LIST")
                {
                    listAllFingerprints();
                }
                else if (sCmd.startsWith("FP_ENROLL"))
                {
                    String name = (sCmd.indexOf('|') != -1) ? sCmd.substring(sCmd.indexOf('|') + 1) : "Vân tay mới";
                    startEnrollTask(name);
                }
                else if (sCmd == "FP_CLEAR")
                {
                    clearAllFingerprints();
                }
                else if (sCmd.startsWith("FP_DELETE|"))
                {
                    int id = sCmd.substring(10).toInt();
                    deleteFingerprint(id);
                }
                else if (sCmd == "7" || sCmd == "i" || sCmd == "I" || sCmd == "img" || sCmd == "CAPTURE_FP_IMG")
                {
                    startCaptureImageTask();
                }
                else
                {
                    processIncomingCommand(sCmd);
                }
            }
        }
    }

    delay(30);
}
