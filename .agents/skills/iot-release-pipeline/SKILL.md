---
name: iot-release-pipeline
description: Standardized end-to-end OTA release pipeline for IoT embedded systems (ESP32 / PlatformIO / Arduino C++) and companion Android apps (Kotlin). Use when the user asks to "tạo bản update", "phát hành firmware", "tạo release OTA", "build release cho dự án", "cập nhật version", or publish new firmware .bin files, APK, and version.json metadata to GitHub Releases or Gitea NAS for any project (Tysmartkey, TankMini, duck cop, quickshifter, etc.).
---

# IoT & Smartkey Universal Release Pipeline (Kỹ Năng Phát Hành Cập Nhật IoT Toàn Diện)

Kỹ năng chuẩn hóa và tự động hóa toàn bộ quy trình phát hành bản cập nhật OTA (Firmware OTA + Android APK) cho **mọi dự án IoT** trong hệ thống (Tysmartkey, TankMini, duck cop, quickshifter, v.v.).

---

## 1. Triết Lý Kiến Trúc Phân Phối Phiên Bản (`version.json`)

Mọi dự án IoT đều tuân thủ kiến trúc phân phối tập trung qua một file metadata duy nhất: `version.json`.
* Thiết bị nhúng (ESP32) và Ứng dụng di động (Android Kotlin) đều đọc cùng file `version.json` này để so sánh `remoteVersion > CURRENT_VERSION`.
* Nếu có bản mới:
  * ESP32 tự động tải file `.bin` qua `HTTPUpdate.h` hoặc qua Bluetooth BLE (`Update.h`).
  * Android hiển thị Changelog và tải file `.apk` cài đặt qua `FileProvider`.

---

## 2. Quy Trình Phát Hành Phổ Quát 6 Bước (Universal 6-Step Workflow)

Khi người dùng yêu cầu: *"Tạo bản update cho [Tên dự án]"*, AI tự động thực thi tuần tự theo chu trình sau:

### Giai Đoạn 1: Nhận Diện Cấu Trúc Dự Án (Project Discovery)
1. **Kiểm tra PlatformIO**:
   * Đọc `platformio.ini` để xác định các môi trường `[env:...]`, loại chip (ESP32-C3, ESP32-S3, ESP32-WROOM,...), và bảng phân vùng `partitions.csv`.
   * Đảm bảo cấu hình phân vùng có ít nhất 2 slot OTA (`ota_0`, `ota_1`) để nạp an toàn, không brick thiết bị.
2. **Kiểm tra Android App** (nếu có):
   * Định vị thư mục `app/build.gradle` hoặc `app/build.gradle.kts`.
   * Đọc `versionCode` và `versionName` hiện tại.
3. **Kiểm tra kênh phân phối**:
   * Kiểm tra xem dự án đang ưu tiên dùng **GitHub Releases Cloud** hay **Gitea NAS nội bộ** (`192.168.1.114:3002` / DuckDNS).

---

### Giai Đoạn 2: Nâng Cấp Phiên Bản Đồng Bộ (Semantic Version Bump)
1. **Tăng số phiên bản**:
   * `newVersionCode = currentVersionCode + 1`
   * `newVersionName = "1.0." + newVersionCode` (hoặc SemVer theo tính năng: Patch/Minor/Major).
   * `releaseTag = "v" + newVersionName`
2. **Cập nhật mã nguồn C++ (ESP32)**:
   * Tìm và cập nhật hằng số:
     ```cpp
     const int CURRENT_FW_VERSION = <newVersionCode>;
     const char* CURRENT_FW_VERSION_NAME = "<newVersionName>";
     ```
3. **Cập nhật Android `build.gradle(.kts)`** (nếu có):
   * Cập nhật `versionCode = <newVersionCode>` và `versionName = "<newVersionName>"`.
4. **Tổng hợp Changelog**:
   * Trích xuất tóm tắt các commit hoặc yêu cầu tính năng gần nhất thành danh sách ngắn gọn gàng (bullet points).

---

### Giai Đoạn 3: Biên Dịch Nhị Phân An Toàn (Safe Compilation)
1. **Biên dịch Firmware PlatformIO**:
   * Chạy lệnh build cho từng môi trường:
     ```powershell
     pio run -d "<thư-mục-dự-án>" -e <env_name>
     ```
   * Thu thập file binary tại `.pio\build\<env_name>\firmware.bin`.
   * Đổi tên chuẩn hóa rõ ràng: `firmware_<tên_biến_thể>.bin` (ví dụ: `firmware_oled.bin`, `firmware_c3.bin`).
   * **Kiểm tra an toàn Flash**: Kích thước file `.bin` bắt buộc phải nhỏ hơn kích thước phân vùng `ota_0` (thường là 1.2MB - 1.8MB tùy cấu hình).
2. **Biên dịch Android APK** (nếu có):
   * Thiết lập `JAVA_HOME="C:\Program Files\Android\Android Studio\jbr"`.
   * Chạy `./gradlew.bat :app:assembleDebug` (hoặc `assembleRelease`).
   * Thu thập file APK tại `app\build\outputs\apk\debug\app-debug.apk`.

---

### Giai Đoạn 4: Cập Nhật Metadata `version.json`
Tạo hoặc cập nhật file `version.json` chuẩn cấu trúc:
```json
{
  "app": {
    "versionCode": 4,
    "versionName": "1.0.4",
    "apkUrl": "https://github.com/<owner>/<repo>/releases/download/v1.0.4/app-debug.apk",
    "apkName": "app-debug.apk",
    "changelog": "• Cập nhật tính năng mới và cải thiện độ ổn định"
  },
  "firmware": {
    "versionCode": 4,
    "versionName": "1.0.4",
    "binUrl": "https://github.com/<owner>/<repo>/releases/download/v1.0.4/firmware.bin",
    "binName": "firmware.bin",
    "secondaryBinUrl": "",
    "oledBinUrl": "",
    "changelog": "• Cập nhật firmware tối ưu OTA"
  }
}
```

---

### Giai Đoạn 5: Xuất Bản Lên Release Hub (GitHub hoặc Gitea)
1. **Kênh GitHub Releases (Khuyên dùng - Hoạt động toàn cầu 4G/WiFi)**:
   * Tạo Release mới với tag `v<versionName>`.
   * Upload toàn bộ file `.bin` và `.apk` vào mục Assets.
   * Commit và Push file `version.json` lên nhánh `main`.
2. **Kênh Gitea / NAS (Nội bộ LAN / DuckDNS)**:
   * Đẩy file `.bin` và `.apk` vào kho lưu trữ Release của Gitea hoặc thư mục `web/` của máy chủ.
   * Cập nhật file `version.json` tại đường dẫn raw tương ứng.

---

### Giai Đoạn 6: Nghiệm Thu Hoàn Tất (Verification)
1. **Kiểm tra link tải**:
   * Gửi HTTP request thử nghiệm tải `version.json` và kiểm tra phản hồi `200 OK`.
   * Kiểm tra tính hợp lệ của cú pháp JSON (không bị lỗi dấu phẩy, chuỗi đóng/mở).
2. **Báo cáo bảng nghiệm thu**:
   * Cung cấp bảng tổng kết cho người dùng:
     * Phiên bản: `vX.X.X` (Code: `X`)
     * Danh sách file đính kèm & Dung lượng (Bytes/MB)
     * Đường link tải trực tiếp
     * Tóm tắt changelog đã ghi nhận.
