# HƯỚNG DẪN VẬN HÀNH AIRTAG TESTBENCH TRÊN MÁY TÍNH & NAS

Tài liệu này cung cấp các bước đơn giản để khởi chạy Backend và kiểm thử tính năng định vị Apple Find My (AirTag) kết hợp dẫn đường GraphHopper.

---

## 1. CHẠY KIỂM THỬ TRÊN MÁY TÍNH NỘI BỘ (LOCAL PC)

### Bước 1: Chạy kịch bản tự động E2E (Kiểm tra 5/5 bài test)
```bash
python tools/airtag_testbench/test_pipeline.py
```
*Kết quả:* Hệ thống sẽ tự động xác thực:
- Sinh cặp khóa $secp224r1$ 28 bytes.
- Tạo địa chỉ Static Random MAC (0xC0).
- Dựng khung $31\text{ bytes}$ chuẩn Apple Offline Finding & Tái tạo khóa ngược lại 100%.
- Giả lập iPhone mã hóa GPS $\rightarrow$ Server giải mã ra chính xác vĩ độ/kinh độ.
- Tính toán khoảng cách & dẫn đường tới xe.

### Bước 2: Khởi chạy FastAPI Server Localhost
```bash
cd tools/airtag_testbench
uvicorn backend_testbench:app --host 0.0.0.0 --port 8000 --reload
```
Truy cập tài liệu tương tác Swagger UI: `http://localhost:8000/docs`

---

## 2. TRIỂN KHAI LÊN MÁY CHỦ NAS (`192.168.1.114`)

### Bước 1: Kết nối SSH vào NAS
```bash
ssh nas152@192.168.1.114
# Nhập mật khẩu: 271000
```

### Bước 2: Tạo thư mục & sao chép code lên NAS
```bash
mkdir -p ~/airtag_testbench
```
Từ máy tính cá nhân, copy thư mục sang NAS (dùng scp hoặc git):
```bash
scp -r tools/airtag_testbench/* nas152@192.168.1.114:~/airtag_testbench/
```

### Bước 3: Khởi chạy Docker Compose trên NAS
```bash
cd ~/airtag_testbench
docker compose up -d --build
```

Kiểm tra trạng thái container:
```bash
docker compose ps
```
- `anisette_v3_service`: Cổng `6969` (Cung cấp Anisette headers cho Apple iCloud)
- `findmy_backend_service`: Cổng `8000` (FastAPI Server)

---

## 3. DANH MỤC API TESTBENCH SẴN SÀNG SỬ DỤNG

| Phương thức | Đường dẫn Endpoint | Chức năng |
| :---: | :--- | :--- |
| `POST` | `/api/key/generate` | Sinh cặp khóa mới cho xe (`device_id`), lưu vào DB, trả về MAC & Mảng byte C nạp ESP32 |
| `GET` | `/api/key/export-ota/{device_id}` | Tải file nhị phân đúng 28 bytes Public Key để nạp qua BLE OTA |
| `POST` | `/api/reports/fetch/{device_id}` | Lấy Anisette header, truy vấn vị trí từ Apple và giải mã ra tọa độ GPS của xe |
| `GET` | `/api/route/to-bike` | Truy vấn GraphHopper dẫn đường xe máy từ vị trí người tìm đến vị trí xe |
| `GET` | `/api/bike/live` | Đọc dữ liệu định vị mới nhất của xe máy |
| `POST` | `/api/bike/sync-to-graphhopper` | Đồng bộ file `bike_live.json` vào thư mục `\\192.168.1.114\graphhopper-data` |

---

## 4. TÍCH HỢP TRỰC TIẾP VÀO WEB CONTROL CENTER & SAMBA SHARE

### 📁 Thư mục chia sẻ mạng LAN:
- Đường dẫn mạng Windows: `\\192.168.1.114\graphhopper-data\bike_live.json`
- Đường dẫn Linux trên NAS: `/home/nas152/graphhopper-data/bike_live.json`

Mỗi khi giải mã được vị trí xe mới, Backend Find My (`port 8090`) sẽ tự động xuất file `bike_live.json` vào thư mục trên với định dạng:
```json
{
  "device_id": "smartkey-airtag-c3",
  "device_name": "Xe Máy Tsmartkey (AirTag Find My)",
  "latitude": 10.7952,
  "longitude": 106.7218,
  "accuracy_meters": 6,
  "timestamp_iso": "2026-10-10T17:10:47+00:00",
  "status": "online",
  "battery_level": "good",
  "provider": "Apple Find My Network (ECDH P-224 Decrypted)",
  "graphhopper_profile": "scooter"
}
```

### 🌐 Giao diện Web:
Truy cập: **[NAS TYMAP Control Center - Geo Multi-Route & Live Navigation Hub](http://192.168.1.114:8085/)**
- **Thẻ giám sát:** `Giám Sát Xe Máy Smartkey (Apple Find My AirTag)` hiển thị tọa độ, độ chính xác và trạng thái trực tuyến.
- **Icon xe máy:** Marker xe máy nổi bật với hiệu ứng radar pulse nhấp nháy và vòng tròn sai số (Accuracy Circle).
- **Tính năng Ghim xe (`FlyTo`):** Tự động đưa bản đồ về tâm vị trí xe máy.
- **Tính năng Dẫn đường (`Scooter Profile`):** Lấy vị trí người dùng, tự động vẽ lộ trình đường bộ xe máy đến xe qua GraphHopper trên NAS.
- **Đồng bộ trực tiếp (`Sync Find My`):** Bấm nút để gọi backend giải mã ngay lập tức.
- **Auto-polling:** Tự động tải lại file `bike_live.json` mỗi 5 giây khi xe di chuyển.
