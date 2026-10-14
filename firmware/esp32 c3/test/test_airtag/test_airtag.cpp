/**
 * @file test_airtag.cpp
 * @brief Testbench Firmware độc lập cho Apple AirTag (Find My Network) trên ESP32-C3 SuperMini.
 *
 * Tính năng kiểm thử:
 * 1. Lưu trữ và đọc 28 bytes Public Key từ Flash NVS (Preferences.h).
 * 2. Tự động sinh Static Random MAC và dựng khung gói tin quảng bá thô 31 bytes chuẩn Apple.
 * 3. Cơ chế phát sóng thông minh theo sự kiện (State-Driven BLE):
 *    - Trạng thái PARKED (Mặc định): Phát Apple AirTag Beacon (Interval 1500ms, Non-connectable).
 *    - Trạng thái CONNECTABLE (Khi nhấn nút BOOT GPIO 9 mô phỏng chạm WAKE):
 *      Ngắt AirTag, mở Custom GATT Service 0xFFE0 / 0xFFE1 để nhận nạp 28 bytes Public Key mới qua OTA.
 * 4. Tự động chuyển đổi mượt mà giữa 2 địa chỉ MAC không gây rò bộ nhớ.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <NimBLEDevice.h>

extern "C" int ble_hs_id_set_rnd(const uint8_t *rnd_addr);

#define BOOT_BUTTON_PIN 9 // Nút BOOT trên ESP32-C3 SuperMini (Active LOW)
#define LED_PIN 8         // LED tích hợp trên ESP32-C3 SuperMini (Active LOW trên một số bản)

// UUID cho Service & Characteristic nạp Key OTA
#define SERVICE_UUID_OTA "FFE0"
#define CHARACTERISTIC_UUID_KEY "FFE1"

// Public Key 28 bytes từ fm.hs2t.com (Base64: XHgLP2nlTeQ+f9rqEKKftlqwmk4ryx4j3ZF/bA==)
// Static Random MAC: DC:78:0B:3F:69:E5
const uint8_t DEFAULT_PUB_KEY[28] = {
    0x5C, 0x78, 0x0B, 0x3F, 0x69, 0xE5, 0x4D, 0xE4, 0x3E, 0x7F, 0xDA, 0xEA, 0x10, 0xA2,
    0x9F, 0xB6, 0x5A, 0xB0, 0x9A, 0x4E, 0x2B, 0xCB, 0x1E, 0x23, 0xDD, 0x91, 0x7F, 0x6C
};
#define AIRTAG_PUB_KEY DEFAULT_PUB_KEY
Preferences prefs;
uint8_t active_pub_key[28];
bool is_connectable_mode = false;
unsigned long connectable_start_time = 0;
const unsigned long CONNECTABLE_TIMEOUT_MS = 15000; // Tự động quay lại AirTag sau 15s nếu không kết nối

NimBLEServer *pServer = nullptr;
NimBLEAdvertising *pAdvertising = nullptr;

// Khai báo hàm
void load_or_init_keys();
void save_new_key(const uint8_t *key_bytes, size_t len);
void start_airtag_advertising();
void start_connectable_advertising();
void print_hex_array(const char *label, const uint8_t *data, size_t len);

// Callback khi nhận ghi Public Key qua BLE OTA
class OtaKeyCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pCharacteristic)
    {
        std::string value = pCharacteristic->getValue();
        if (value.length() == 28)
        {
            Serial.printf("\n[OTA] Da nhan thanh cong 28 bytes Public Key moi!\n");
            save_new_key((const uint8_t *)value.data(), 28);

            // Nháy LED báo nạp thành công
            for (int i = 0; i < 4; i++)
            {
                digitalWrite(LED_PIN, LOW);
                delay(100);
                digitalWrite(LED_PIN, HIGH);
                delay(100);
            }

            // Khởi động lại chế độ AirTag với Key mới
            is_connectable_mode = false;
            start_airtag_advertising();
        }
        else
        {
            Serial.printf("\n[OTA-ERROR] Do dai key khong hop le: %d bytes (Yeu cau dung 28 bytes)\n", value.length());
        }
    }
};

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n========================================================");
    Serial.println(" ESP32-C3 AIRTAG & FIND MY TESTBENCH (MULTI-USER V2.0)");
    Serial.println("========================================================");

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // Tắt LED

    load_or_init_keys();

    // Khởi tạo NimBLE Stack
    NimBLEDevice::init("Tsmartkey-OTA");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Công suất phát tối đa

    pServer = NimBLEDevice::createServer();
    NimBLEService *pService = pServer->createService(SERVICE_UUID_OTA);
    NimBLECharacteristic *pChar = pService->createCharacteristic(
        CHARACTERISTIC_UUID_KEY,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    pChar->setCallbacks(new OtaKeyCallbacks());
    pService->start();

    pAdvertising = NimBLEDevice::getAdvertising();

    // Khởi động chế độ AirTag mặc định
    start_airtag_advertising();
}

void loop()
{
    static bool last_btn_state = HIGH;
    bool btn_state = digitalRead(BOOT_BUTTON_PIN);

    // Phát hiện nhấn nút BOOT (mô phỏng ngắt WAKE chạm vân tay R503)
    if (btn_state == LOW && last_btn_state == HIGH)
    {
        delay(50); // Chống dội phím
        if (digitalRead(BOOT_BUTTON_PIN) == LOW)
        {
            Serial.println("\n[EVENT] Nhan tin hieu cham WAKE (Nut BOOT) -> Kich hoat che do Connectable 15s!");
            is_connectable_mode = true;
            connectable_start_time = millis();
            start_connectable_advertising();
        }
    }
    last_btn_state = btn_state;

    // Tự động timeout quay về AirTag Mode sau 15s nếu người dùng không thao tác
    if (is_connectable_mode && (millis() - connectable_start_time > CONNECTABLE_TIMEOUT_MS))
    {
        Serial.println("\n[TIMEOUT] Het 15s Connectable -> Tu dong quay lai che do Apple AirTag!");
        is_connectable_mode = false;
        start_airtag_advertising();
    }

    // Nhấp nháy LED nhẹ mỗi 1.5 giây báo hiệu đang phát gói tin Apple Find My
    static unsigned long last_blink = 0;
    if (!is_connectable_mode && millis() - last_blink > 1500)
    {
        last_blink = millis();
        digitalWrite(LED_PIN, LOW); // Bật LED
        delay(25);                  // Chớp ngắn 25ms
        digitalWrite(LED_PIN, HIGH); // Tắt LED
    }

    // In log định kỳ mỗi 10 giây
    static unsigned long last_heartbeat = 0;
    if (millis() - last_heartbeat > 10000)
    {
        last_heartbeat = millis();
        Serial.printf("[HEARTBEAT] Mode: %s | Free Heap: %d bytes\n",
                      is_connectable_mode ? "CONNECTABLE (SmartKey/OTA)" : "AIRTAG (Apple Find My)",
                      ESP.getFreeHeap());
    }

    delay(20);
}

void load_or_init_keys()
{
    prefs.begin("airtag_cfg", false);
    // Luôn ghi đè DEFAULT_PUB_KEY mới vào NVS để đảm bảo đồng bộ với code người dùng vừa nạp
    memcpy(active_pub_key, DEFAULT_PUB_KEY, 28);
    prefs.putBytes("pub_key", active_pub_key, 28);
    prefs.end();

    Serial.println("[NVS] Da dong bo 28 bytes Public Key moi vao bo nho Flash NVS!");
    print_hex_array("[KEY] Active Public Key X (28B): ", active_pub_key, 28);
}

void save_new_key(const uint8_t *key_bytes, size_t len)
{
    if (len != 28)
        return;
    prefs.begin("airtag_cfg", false);
    prefs.putBytes("pub_key", key_bytes, 28);
    prefs.end();
    memcpy(active_pub_key, key_bytes, 28);
    Serial.println("[NVS] Da luu Public Key moi vao Flash NVS thanh cong!");
    print_hex_array("[KEY] Updated Public Key X (28B): ", active_pub_key, 28);
}

void start_airtag_advertising()
{
    pAdvertising->stop();

    // 1. Tính toán Static Random MAC từ 6 bytes đầu của Public Key (Set 2 bit cao = 11)
    uint8_t mac[6];
    mac[0] = active_pub_key[0] | 0b11000000;
    memcpy(&mac[1], &active_pub_key[1], 5);

    // Dùng NimBLEAddress để chuẩn hóa Endianness tự động (loại địa chỉ: 1 = RANDOM)
    NimBLEAddress nimbleAddr(mac, 1);
    int rc_mac = ble_hs_id_set_rnd(const_cast<uint8_t*>(nimbleAddr.getNative()));
    NimBLEDevice::setOwnAddrType(BLE_ADDR_RANDOM);

    // 2. Dựng gói tin quảng bá thô 31 bytes CHUẨN 100% OpenHaystack & Kênh Táy Máy
    // Byte 0: 0x1E (Length: 30 bytes)
    // Byte 1: 0xFF (Type: Manufacturer Data)
    // Byte 2..3: 0x4C, 0x00 (Apple Company ID)
    // Byte 4..5: 0x12, 0x19 (Offline Finding Type 0x12, Length 0x19 = 25 bytes)
    // Byte 6: 0x00 (State)
    // Byte 7..28: 22 bytes từ active_pub_key[6..27]
    // Byte 29: active_pub_key[0] >> 6 (2 bits cao)
    // Byte 30: 0x00 (Hint)
    uint8_t raw_adv[31];
    raw_adv[0] = 0x1E;
    raw_adv[1] = 0xFF;
    raw_adv[2] = 0x4C;
    raw_adv[3] = 0x00;
    raw_adv[4] = 0x12;
    raw_adv[5] = 0x19;
    raw_adv[6] = 0x00;
    memcpy(&raw_adv[7], &active_pub_key[6], 22);
    raw_adv[29] = active_pub_key[0] >> 6;
    raw_adv[30] = 0x00; // Hint byte (Bắt buộc theo chuẩn OpenHaystack)

    NimBLEAdvertisementData advData;
    advData.addData((char *)raw_adv, sizeof(raw_adv));
    pAdvertising->setAdvertisementData(advData);

    // 3. Cấu hình quảng bá Non-connectable với Interval 1280ms - 1500ms
    pAdvertising->setMinInterval(0x0640); // 1000ms
    pAdvertising->setMaxInterval(0x0C80); // 2000ms
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    pAdvertising->setScanResponse(false);

    bool adv_ok = pAdvertising->start();

    digitalWrite(LED_PIN, HIGH); // Tắt LED
    Serial.println("\n--------------------------------------------------------");
    Serial.println("[AIRTAG-MODE] Da kich hoat Apple Find My Beacon (Chuan OpenHaystack)!");
    Serial.printf("  -> Static Random MAC: %s\n", nimbleAddr.toString().c_str());
    Serial.printf("  -> Ket qua ble_hs_id_set_rnd: %d (0 = Thanh cong)\n", rc_mac);
    Serial.printf("  -> Trang thai phat BLE: %s\n", adv_ok ? "DANG PHAT SONG OKE" : "LOI KHONG PHAT DUOC");
    Serial.printf("  -> Raw Advertisement Payload Length: %d bytes (Exact 31B)\n", advData.getPayload().length());
    Serial.println("--------------------------------------------------------");
}

void start_connectable_advertising()
{
    pAdvertising->stop();

    // 1. Dùng MAC mặc định của ESP32 (Public Address)
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
    NimBLEAdvertisementData advData;
    advData.setName("Tsmartkey-OTA");
    advData.setCompleteServices(NimBLEUUID(SERVICE_UUID_OTA));
    pAdvertising->setAdvertisementData(advData);

    // 2. Chế độ Connectable với tần suất cao (40ms) để điện thoại kết nối tức thì
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
    pAdvertising->setMinInterval(64); // 64 * 0.625ms = 40ms
    pAdvertising->setMaxInterval(64);

    pAdvertising->start();

    digitalWrite(LED_PIN, LOW); // Bật LED báo hiệu đang mở kết nối
    Serial.println("\n--------------------------------------------------------");
    Serial.println("[CONNECTABLE-MODE] Da mo ket noi BLE OTA (Tsmartkey-OTA)!");
    Serial.println("  -> UUID Service: 0xFFE0 | UUID Characteristic: 0xFFE1");
    Serial.println("  -> Interval: 40 ms (Ket noi sieu toc 0.2s)");
    Serial.println("--------------------------------------------------------");
}

void print_hex_array(const char *label, const uint8_t *data, size_t len)
{
    Serial.print(label);
    for (size_t i = 0; i < len; i++)
    {
        if (data[i] < 0x10)
            Serial.print("0");
        Serial.print(data[i], HEX);
    }
    Serial.println();
}
