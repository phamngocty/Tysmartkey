# 📑 BÁO CÁO BÀN GIAO PHIÊN LÀM VIỆC (SESSION HANDOFF)
**Dự án:** Xe Máy Thông Minh Tsmartkey (ESP32-C3 SuperMini)  
**Chủ đề:** Tích hợp Apple AirTag (Find My Network), Web NAS TYMAP Control Center & Khóa Kênh Táy Máy  
**Thời gian:** 11/10/2026  
**NotebookLM Target ID:** `c7bfc20f-400d-4a5b-9846-e06f583af905`  

---

## 1. TỔNG QUAN TIẾN ĐỘ ĐÃ HOÀN THÀNH 100%

### A. Hạ Tầng Web & Backend NAS TYMAP (`192.168.1.114`)
1. **Web TYMAP Control Center (`port 8085`):**
   - Đã thay thế lớp bản đồ nền mặc định thành **OpenStreetMap (OSM Chuẩn)** giúp tải nhanh, tiết kiệm băng thông và hiển thị rõ ràng đường xá Việt Nam.
   - Đã tích hợp widget **"Giám Sát Xe Máy Smartkey (Apple Find My AirTag)"** với marker xe máy, vòng tròn sai số GPS, đo khoảng cách và nút dẫn đường qua GraphHopper scooter profile.
   - Nâng cấp **Modal Tạo & Quản Lý Khóa P-224** chuẩn 100% theo giao diện của **Kênh Táy Máy (`fm.hs2t.com`)**:
     - Ô nhập `Device Name` (VD: `ty1`).
     - Ô hiển thị `Advertisement Key (Base64)` kèm nút sao chép 1 chạm.
     - Tự động hiển thị địa chỉ `Static Random MAC`.
     - Cho phép dán khóa Base64 từ bên ngoài vào và lưu lại.
     - Hiển thị danh sách tất cả các xe/thiết bị đã lưu trên NAS.
   - Tích hợp **Modal Đăng Nhập Apple ID (iCloud Find My)**:
     - Form đăng nhập tài khoản Apple ID + Mật khẩu.
     - Tự động bắt sự kiện yêu cầu xác thực 2 lớp (2FA) và mở form nhập 6 số gửi về iPhone.
     - Nút quét và kéo vị trí thật từ Apple về ngay lập tức.

2. **Backend FastAPI Testbench (`port 8090` / Docker `findmy_backend_service`):**
   - Đã cài đặt thư viện **`findmy`** kết nối trực tiếp với **`anisette_v3_service` (`port 6969`)**.
   - Cung cấp đầy đủ các API:
     - `/api/apple/status`: Trạng thái đăng nhập iCloud.
     - `/api/apple/login`: Đăng nhập Apple ID qua Anisette headers.
     - `/api/apple/2fa`: Nhập mã xác thực 2 lớp.
     - `/api/apple/sync-now`: Quét và giải mã tọa độ thật từ Apple Find My.
     - `/api/key/list`: Liệt kê tất cả thiết bị trên NAS.
     - `/api/key/import-b64`: Nhập khóa Base64 từ `fm.hs2t.com`.
     - `/api/key/generate`: Sinh khóa P-224 mới.
     - Background Task tự động đồng bộ vị trí mỗi 45 giây và xuất ra `\\192.168.1.114\graphhopper-data\bike_live.json`.

---

### B. Firmware ESP32-C3 SuperMini (`test_airtag.cpp`)
1. **Chuẩn hóa gói tin 31 bytes chuẩn OpenHaystack & Kênh Táy Máy:**
   - Byte 0: `0x1E` (Độ dài 30B)
   - Byte 1: `0xFF` (Manufacturer Specific Data)
   - Byte 2..3: `0x4C 0x00` (Apple Company ID)
   - Byte 4..5: `0x12 0x19` (Offline Finding type & length 25B)
   - Byte 6: `0x00` (State)
   - Byte 7..28: 22 bytes từ `public_key[6..27]`
   - Byte 29: 2 bits cao từ `public_key[0] >> 6`
   - Byte 30: `0x00` (Hint byte)
   *Gói tin chiếm trọn vẹn đúng 31 bytes, không chứa cờ Flags thừa làm tràn BLE frame.*

2. **Tích hợp khóa từ Kênh Táy Máy (`fm.hs2t.com`):**
   - **Device Name:** `ty1`
   - **Advertisement Key (Base64):** `XHgLP2nlTeQ+f9rqEKKftlqwmk4ryx4j3ZF/bA==`
   - **Static Random MAC:** `DC:78:0B:3F:69:E5`
   - **Mảng C++:**
     ```cpp
     const uint8_t DEFAULT_PUB_KEY[28] = {
         0x5C, 0x78, 0x0B, 0x3F, 0x69, 0xE5, 0x4D, 0xE4, 0x3E, 0x7F, 0xDA, 0xEA, 0x10, 0xA2,
         0x9F, 0xB6, 0x5A, 0xB0, 0x9A, 0x4E, 0x2B, 0xCB, 0x1E, 0x23, 0xDD, 0x91, 0x7F, 0x6C
     };
     ```
   - Đã biên dịch sạch 100% trong môi trường `[env:test-airtag]`.

3. **Tính năng độc quyền trên phần cứng:**
   - **LED Heartbeat (GPIO 8):** Chớp nháy nhẹ 25ms mỗi 1.5s xác nhận sóng beacon đang phát.
   - **Nút WAKE / BOOT (GPIO 9):** Nhấn 1 lần chuyển sang chế độ `Tsmartkey-OTA` (Connectable, đèn sáng liên tục 15s) để điện thoại quét thấy tên hoặc nạp key không dây.

---

### C. Bộ Kỹ Năng Kỹ Sư (Matt Pocock Skills)
Đã cài đặt đầy đủ **27 skills** vào `.agents/skills/` và `~/.gemini/config/skills/`, bao gồm:
- `tdd`, `implement`, `diagnosing-bugs`, `code-review`, `prototype`
- `codebase-design`, `improve-codebase-architecture`, `domain-modeling`, `research`
- `to-spec`, `to-tickets`, `triage`, `pr`, `retro`, `setup-matt-pocock-skills`
- `grill-me`, `grilling`, `teach`, `handoff`, `writing-for-agents`
- Đã bật cơ chế **Auto-Skill Activation Protocol**: Tự động nhận diện ngữ cảnh và kích hoạt skill phù hợp mà người dùng không cần nhớ tên skill.

---

## 2. HƯỚNG DẪN CÁC BƯỚC CHO PHIÊN LÀM VIỆC TIẾP THEO

1. **Nạp Firmware Khóa `ty1` Vào ESP32-C3:**
   - Cắm cáp USB mạch ESP32-C3 vào máy tính.
   - Chạy lệnh:
     ```powershell
     & "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e test-airtag -t upload
     ```
   - Mạch sẽ phát sóng với MAC `DC:78:0B:3F:69:E5`.

2. **Kéo Tọa Độ Thật Về Bản Đồ:**
   - Đặt 1 chiếc iPhone có bật Bluetooth + Mạng cạnh ESP32-C3 khoảng 1 phút.
   - Mở `http://192.168.1.114:8085/` $\rightarrow$ Bấm nút ` Đăng Nhập Apple ID` để liên kết tài khoản (nhập mã 2FA gửi về iPhone).
   - Bấm `Quét & Kéo Tọa Độ Thật Từ Apple Ngay` để marker xe nhảy về vị trí GPS thật!
   - Có thể kiểm tra chéo vị trí trên cả trang `https://fm.hs2t.com` (thiết bị `ty1`).
