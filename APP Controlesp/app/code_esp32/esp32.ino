#include "BluetoothSerial.h"
#include <Preferences.h>

BluetoothSerial SerialBT;
Preferences preferences;

#define RELAY1 23 // khóa
#define RELAY2 22 // đề
#define RELAY3 21 // đèn còi

bool isUnlocked = false;

// 🔐 BẢO MẬT
String DEVICE_NAME = "XE_tsmart";
String SECRET_KEY = "271000"; // Mã mặc định ban đầu
const char *BT_PIN = "1234";

// 🔥 CHỐNG LẶP
String lastCommand = "";
unsigned long lastTime = 0;

void setup()
{
  Serial.begin(115200);

  // Khởi tạo bộ nhớ Preferences để lưu Key và Trạng thái vĩnh viễn
  preferences.begin("safe_key", false);

  // 1. Tải SECRET_KEY
  SECRET_KEY = preferences.getString("master_key", "271000");

  // 2. Tải Trạng thái xe trước khi mất điện
  isUnlocked = preferences.getBool("is_unlocked", false);

  Serial.println("System Ready.");
  Serial.println("Loaded Key: " + SECRET_KEY);
  Serial.println("Last state: " + String(isUnlocked ? "UNLOCKED" : "LOCKED"));

  SerialBT.begin(DEVICE_NAME);
  SerialBT.setPin(BT_PIN);

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);

  // 3. KHÔI PHỤC TRẠNG THÁI NGAY LẬP TỨC (Cơ chế an toàn khi mất kết nối/lỗi ESP)
  digitalWrite(RELAY1, isUnlocked ? HIGH : LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, LOW);
}

void notifyStatus(String msg)
{
  SerialBT.println("FB|" + msg); // FB = Feedback
  Serial.println("Feedback: " + msg);
}

void loop()
{
  if (SerialBT.available())
  {
    String data = SerialBT.readStringUntil('\n');
    data.trim();

    if (data.length() == 0) return;

    // 🚫 CHỐNG LẶP (500ms)
    if (data == lastCommand && millis() - lastTime < 500) return;
    lastCommand = data;
    lastTime = millis();

    Serial.println("Received: " + data);

    // ===== CHECK FORMAT =====
    int firstPipe = data.indexOf('|');
    if (firstPipe == -1) return;

    String providedKey = data.substring(0, firstPipe);
    String remaining = data.substring(firstPipe + 1);

    // ===== CHECK KEY =====
    if (providedKey != SECRET_KEY) {
      Serial.println("Sai Key!");
      notifyStatus("LOI_SAI_KEY");
      return;
    }

    int secondPipe = remaining.indexOf('|');
    String cmd = (secondPipe == -1) ? remaining : remaining.substring(0, secondPipe);
    String param = (secondPipe == -1) ? "" : remaining.substring(secondPipe + 1);

    // ===== XỬ LÝ LỆNH =====

    // 🔓 MỞ KHÓA
    if (cmd == "1") {
      isUnlocked = true;
      digitalWrite(RELAY1, HIGH);
      preferences.putBool("is_unlocked", true); // Lưu vào Flash
      notifyStatus("DA_MO_KHOA");
    }

    // 🔒 KHÓA
    else if (cmd == "0") {
      isUnlocked = false;
      digitalWrite(RELAY1, LOW);
      preferences.putBool("is_unlocked", false); // Lưu vào Flash
      notifyStatus("DA_KHOA_XE");
    }

    // ⚡ ĐỀ XE
    else if (cmd == "2") {
      if (isUnlocked) {
        digitalWrite(RELAY2, HIGH);
        delay(1500);
        digitalWrite(RELAY2, LOW);
        notifyStatus("DA_DE_MAY");
      } else {
        notifyStatus("LOI_CHUA_MO_KHOA");
      }
    }

    // 🚀 TÌM XE
    else if (cmd == "3") {
      // Tìm xe không làm thay đổi trạng thái isUnlocked vĩnh viễn ở đây
      // trừ khi bạn muốn nó tự mở khóa khi tìm
      digitalWrite(RELAY3, HIGH);
      delay(3000);
      digitalWrite(RELAY3, LOW);
      notifyStatus("DA_TIM_XE");
    }

    // 🔑 ĐỔI MÃ BẢO MẬT (Lệnh 9)
    else if (cmd == "9" && param.length() > 0) {
      SECRET_KEY = param;
      preferences.putString("master_key", SECRET_KEY);
      Serial.println("Key changed to: " + SECRET_KEY);
      notifyStatus("DA_DOI_KEY");
    }
  }
}
