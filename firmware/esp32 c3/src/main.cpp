#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <Adafruit_Fingerprint.h>

// ==========================================
// 📌 1. ĐỊNH NGHĨA CHÂN PHẦN CỨNG (ESP32-C3)
// ==========================================
#define RELAY1_PIN   4  // Relay 1: Khóa điện ACC (Đấu song song ổ khóa cơ)
#define RELAY2_PIN   5  // Relay 2: Đề xe (Starter)
#define RELAY3_PIN   6  // Relay 3: Đèn / Còi (Buzzer / Horn / Turn lights)

#define RF_LOCATE_PIN      7  // GPIO 7: Tín hiệu từ Module RF 433MHz (CHỈ DÙNG TÌM XE - TUYỆT ĐỐI KHÔNG MỞ KHÓA)

#define R503_RX_PIN        0  // GPIO 0 kết nối TXD (dây Vàng) của R503
#define R503_TX_PIN        1  // GPIO 1 kết nối RXD (dây Xanh lá) của R503
#define R503_WAKE_PIN      3  // GPIO 3 kết nối WAKEUP (dây Xanh dương) của R503 (Active LOW: chạm = 0V))

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

// Khởi tạo Preferences lưu trữ Flash NVS
Preferences prefsSecurity; // namespace "safe_key"
Preferences prefsFinger;   // namespace "fingerprint"
Preferences prefsRain;     // namespace "rain_config"

// Cấu hình Chế độ Chống Nước Mưa (Anti-Rain Mode)
bool rainEnabled = false;
int touchHoldMs = 500;         // Thời gian giữ ngón liên tục (ms) để lọc giọt nước chạm lướt
int maxWrongAttempts = 5;      // Ngưỡng quẹt sai trước khi khóa/báo động (0 = tắt còi)
int cooldownSec = 30;          // Thời gian tạm khóa khi chạm sai liên tục (giây)
int autoOffSec = 3600;         // Thời gian tự tắt chế độ mưa (giây, 0 = không tự tắt)
unsigned long rainStartTime = 0;   // Thời điểm kích hoạt chế độ mưa (millis)
unsigned long cooldownUntil = 0;   // Mốc thời gian kết thúc cooldown (millis)

// Khởi tạo UART cho R503
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
    Serial.print("Sending Feedback: " + fullMsg);
    
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

// Điều khiển LED RGB trên R503
void setR503Led(uint8_t mode, uint8_t color, uint8_t speed = 50, uint8_t count = 1) {
    finger.LEDcontrol(mode, speed, color, count);
}

void ledSuccess() {
    setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_BLUE, 40, 2); // Nháy xanh xác nhận
}

void ledError() {
    setR503Led(FINGERPRINT_LED_FLASHING, FINGERPRINT_LED_RED, 30, 3);  // Nháy đỏ cảnh báo
}

void ledBreathingIdle() {
    setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_BLUE, 150, 0); // Thở nhẹ
}

void ledOff() {
    setR503Led(FINGERPRINT_LED_OFF, 0, 0, 0);
}

// Cập nhật trạng thái LED theo trạng thái xe (tiết kiệm bình ắc quy khi xe khóa)
void updateIdleLed() {
    if (isUnlocked) {
        setR503Led(FINGERPRINT_LED_BREATHING, FINGERPRINT_LED_BLUE, 120, 0); // Bật xe: thở xanh
    } else {
        setR503Led(FINGERPRINT_LED_OFF, 0, 0, 0); // Khóa xe: tắt LED tiết kiệm ắc quy
    }
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
    
    if (isUnlocked) {
        ledSuccess();
    } else {
        ledError();
    }
    delay(400);
    updateIdleLed();
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
                        p = finger.image2Tz(bufferSlot);
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
    int p = finger.createModel();
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
    p = finger.storeModel(id);
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
    p = finger.createModel();
    if (p == FINGERPRINT_OK) {
        int auxId = getNextFreeFingerIdExcluding(id);
        if (auxId > 0 && auxId <= maxCap) {
            // TUYỆT ĐỐI KHÔNG gọi finger.deleteModel(auxId) ở đây!
            p = finger.storeModel(auxId);
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

// Xóa vân tay theo ID (xóa cả ID chính và các slot phụ liên kết)
void deleteFingerprint(int id) {
    if (id < 1 || id > 200) {
        notifyStatus("FP_DELETE_FAILED|ID_KHONG_HOP_LE");
        return;
    }

    int p = finger.deleteModel(id);
    Serial.printf("🗑️ Xóa model vân tay ID %d trong R503: mã trả về = %d\n", id, p);
    
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
    int p = finger.emptyDatabase();
    delay(400); // Đợi Flash ROM của R503 xóa sạch hoàn toàn
    finger.getTemplateCount();
    Serial.printf("emptyDatabase R503 trả về mã: %d, còn lại: %d mẫu\n", p, finger.templateCount);
    
    prefsFinger.clear();
    notifyStatus("FP_CLEAR_OK");
    beep(3, 100);
    Serial.println("✅ Đã xóa sạch toàn bộ vân tay trong cả R503 và Flash NVS!");
}

// Kiểm tra quét vân tay thực tế khi người dùng chạm
void handleFingerprintTouch() {
    if (isEnrolling) return; // Bảo vệ: Không quét kích hoạt xe khi đang trong chu trình thêm vân tay!

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

    // 2. VÒNG LẶP QUÉT ĐỐI SOÁT LIÊN TỤC TRONG KHI NGÓN TAY ÁP VÀO MẶT KÍNH
    // Quét liên tục trong cửa sổ thời gian fpScanWindowMs (mặc định 1200ms)
    unsigned long startScan = millis();
    int p = -1;
    bool matched = false;
    int matchedId = -1;
    int matchedConfidence = 0;

    while (millis() - startScan < (unsigned long)fpScanWindowMs) {
        p = finger.getImage();
        if (p == FINGERPRINT_OK) {
            p = finger.image2Tz();
            if (p == FINGERPRINT_OK) {
                p = finger.fingerSearch();
                if (p != FINGERPRINT_OK) {
                    p = finger.fingerFastSearch();
                }
                if (p == FINGERPRINT_OK) {
                    matched = true;
                    matchedId = finger.fingerID;
                    matchedConfidence = finger.confidence;
                    break; // Đã tìm thấy vân tay khớp!
                }
            }
        }
        if (digitalRead(R503_WAKE_PIN) == HIGH) {
            break; // Đã nhấc ngón tay ra
        }
        delay(25);
    }

    // XỬ LÝ KHI ĐANG Ở CHẾ ĐỘ TEST CẢM BIẾN (LIVE TEST TỪ APP)
    if (isTestingFingerprint) {
        if (matched) {
            int primaryId = matchedId;
            String parentKey = "parent_" + String(matchedId);
            if (prefsFinger.isKey(parentKey.c_str())) {
                primaryId = prefsFinger.getInt(parentKey.c_str(), matchedId);
            }
            String name = prefsFinger.getString(("name_" + String(primaryId)).c_str(), "ID_" + String(primaryId));
            Serial.printf("🔬 TEST MATCH: ID %d [%s] - Confidence: %d\n", matchedId, name.c_str(), matchedConfidence);
            notifyStatus("FP_TEST_RESULT|" + String(matchedId) + "|" + name + "|" + String(matchedConfidence));
            ledSuccess();
            beep(1, 80);
        } else {
            Serial.println("🔬 TEST NOT MATCH: Không nhận diện được vân tay.");
            notifyStatus("FP_TEST_RESULT|-1|Không khớp|0");
            ledError();
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
            Serial.printf("⚠️ Phát hiện vân tay ID %d trong R503 nhưng KHÔNG có trong NVS Flash. Dọn dẹp model mồ côi...\n", id);
            finger.deleteModel(id);
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
                  String(fpScanWindowMs) + "|" + String(fpEnrollMode);
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
        startEnrollTask(fingerName);
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
        // Gói tin: <KEY>|SET_FP_CFG|<sec_level>|<scan_win>|<enroll_mode>
        int p1 = params.indexOf('|');
        int p2 = params.indexOf('|', p1 + 1);
        if (p1 != -1 && p2 != -1) {
            fpSecurityLevel = params.substring(0, p1).toInt();
            fpScanWindowMs = params.substring(p1 + 1, p2).toInt();
            fpEnrollMode = params.substring(p2 + 1).toInt();

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

            Serial.printf("✅ Đã cập nhật FP Config: SecLevel=%d, ScanWin=%d, EnrollMode=%d\n",
                          fpSecurityLevel, fpScanWindowMs, fpEnrollMode);
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

    // 2. Tải cấu hình từ Flash NVS
    prefsSecurity.begin("safe_key", false);
    prefsFinger.begin("fingerprint", false);
    prefsRain.begin("rain_config", false);

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
    Serial.printf("🔍 FP Config: SecLevel=%d | ScanWin=%dms | EnrollMode=%d\n",
                  fpSecurityLevel, fpScanWindowMs, fpEnrollMode);


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
        r503Serial.begin(b, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
        delay(80);
        while (r503Serial.available()) r503Serial.read(); // Xóa rác RX trước khi gửi lệnh bắt tay
        if (finger.verifyPassword()) {
            Serial.printf("✅ Cảm biến R503 kết nối thành công tại Baudrate: %d!\n", b);
            r503Found = true;
            break;
        }
        r503Serial.end();
        delay(30);
    }

    if (r503Found) {
        r503Ready = true;
        finger.getParameters();
        Serial.printf("📊 R503 Dung lượng bộ nhớ: %d vân tay | Bảo mật hiện tại trên module: Mức %d\n", finger.capacity, finger.security_level);
        finger.setSecurityLevel(fpSecurityLevel);
        Serial.printf("🔒 Đã áp dụng mức bảo mật R503 theo cấu hình: Mức %d\n", fpSecurityLevel);
        int regCount = getRegisteredFingerprintCount();
        Serial.printf("🔑 Số lượng vân tay hợp lệ trong Flash NVS: %d\n", regCount);
        updateIdleLed();
    } else {
        r503Ready = false;
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
}

void loop() {
    // Nếu đang trong chu trình thêm vân tay mới từ App thì bỏ qua quét thông thường
    if (isEnrolling) {
        delay(50);
        return;
    }

    // Gửi dữ liệu Telemetry định kỳ mỗi 3 giây khi có kết nối BLE
    static unsigned long lastTelemetryTime = 0;
    if (deviceConnected && (millis() - lastTelemetryTime > 3000)) {
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

    // Kiểm tra chạm ngón tay (Active LOW: Chạm = 0V / LOW)
    if (digitalRead(R503_WAKE_PIN) == LOW) {
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
    // ⌨️ NHẬN LỆNH QUA SERIAL MONITOR (COM PORT) ĐỂ DEBUG / TEST
    // -------------------------------------------------------------
    if (Serial.available()) {
        String sCmd = Serial.readStringUntil('\n');
        sCmd.trim();
        if (sCmd.length() > 0) {
            Serial.printf("⌨️ Lệnh Serial: %s\n", sCmd.c_str());
            if (sCmd == "FP_LIST") {
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

    delay(30);
}
