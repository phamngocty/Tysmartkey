# Tsmartkey - Smartkey Xe Thông Minh qua BLE & Wear OS

Dự án điều khiển mở khóa xe, đề nổ và tìm xe từ xa bằng App Android và Đồng hồ thông minh Wear OS qua Bluetooth / BLE kết nối ESP32.

## 📖 Tài liệu dự án
Toàn bộ kiến trúc hệ thống, sơ đồ phần cứng, bảng mã lệnh giao tiếp và hướng dẫn phát triển được tổng hợp đầy đủ tại:
👉 **[PROJECT_MASTER_DOCUMENT.md](file:///d:/Documents/PlatformIO/Tysmartkey/PROJECT_MASTER_DOCUMENT.md)**

## 📂 Cấu trúc dự án
- `firmware/esp32 c3/`: Mã nguồn firmware PlatformIO cho ESP32 điều khiển Relay.
- `APP Controlesp/app/`: Ứng dụng điện thoại Android (Bật/Tắt, Đề nổ, Tìm xe, Đổi mã bảo mật).
- `APP Controlesp/wear/`: Ứng dụng đồng hồ Wear OS (Giao diện Jetpack Compose, cử chỉ búng tay đề máy).
