# TÀI LIỆU ĐẶC TẢ KỸ THUẬT & HƯỚNG DẪN AI AGENT (SPECIFICATION & PROMPT MASTER)
**Dự án:** Custom Apple AirTag trên ESP32-C3 SuperMini tích hợp Self-Hosted NAS Backend & GraphHopper Routing  
**Phiên bản:** v1.0-Engineering  
**Mục tiêu:** Cung cấp tài liệu này cho AI Agent để sinh mã nguồn kiểm thử (Testbench) độc lập cho Firmware ESP32-C3 và Dịch vụ Backend trên NAS.
ssh nas152@192.168.1.114
password:271000
---

## 1. TỔNG QUAN KIẾN TRÚC HỆ THỐNG (SYSTEM ARCHITECTURE)

```text
 ┌────────────────────────────────────────────────────────────────────────┐
 │ 1. XE MÁY (EDGE DEVICE): ESP32-C3 SuperMini                            │
 │  - NVS Flash: Lưu trữ 28 bytes Public Key                              │
 │  - BLE Controller: Phát xen kẽ (Time-Multiplexed):                     │
 │      + Mode A (3s): Apple AirTag Beacon (Non-Connectable)              │
 │      + Mode B (1s): SmartKey / BLE OTA Service (Connectable)           │
 └───────────────────────────────────┬────────────────────────────────────┘
                                     │ (Sóng BLE 2.4 GHz)
                                     ▼
 ┌────────────────────────────────────────────────────────────────────────┐
 │ 2. MẠNG LƯỚI TOÀN CẦU: Apple Find My Network                           │
 │  - Thiết bị iOS (iPhone/iPad của người đi đường) thu thập Beacon       │
 │  - Đóng gói GPS + Timestamp, mã hóa bằng Public Key (P-224)             │
 │  - Đẩy lên máy chủ Apple iCloud (/acsnservice/fetch)                   │
 └───────────────────────────────────┬────────────────────────────────────┘
                                     │ (Truy vấn định kỳ qua Internet)
                                     ▼
 ┌────────────────────────────────────────────────────────────────────────┐
 │ 3. MÁY CHỦ RIÊNG (NAS HOME LAB BACKEND)                                │
 │  ├─ Container 1: anisette-v3-server (Cung cấp Apple Auth Headers)      │
 │  ├─ Service 2: KeyGen & Cryptography (NIST P-224 Curve Manager)        │
 │  ├─ Service 3: Report Fetcher & Decryptor (Lấy & giải mã tọa độ xe)   │
 │  ├─ Service 4: RESTful API & OTA Dispatcher (Nạp Key xuống ESP32)      │
 │  └─ Service 5: GraphHopper Routing Engine (:8989) (Dẫn đường tới xe)   │
 └────────────────────────────────────────────────────────────────────────┘
```

---

## 2. ĐẶC TẢ MÃ HÓA & KHUNG GÓI TIN BLE (CRYPTOGRAPHY & BLE PROTOCOL)

### 2.1. Cấu trúc cặp khóa NIST P-224 (secp224r1)
* **Thuật toán:** Elliptic Curve Cryptography (ECC) trên đường cong chuẩn $NIST\text{ }P\text{-}224$ ($secp224r1$).
* **Private Key:** Độ dài $28\text{ bytes}$ ($224\text{ bits}$), lưu trữ bảo mật duy nhất trên Database của NAS Server.
* **Public Key:** Tọa độ điểm $X$ trên đường cong, độ dài đúng $28\text{ bytes}$. Đây là dữ liệu duy nhất nạp vào ESP32-C3.
* **Định dạng địa chỉ MAC:** Apple yêu cầu địa chỉ Static Random MAC được suy ra từ $6\text{ bytes}$ đầu của Public Key:
  $$\text{MAC}[0] = \text{PublicKey}[0] \mid 0\text{b}11000000\text{ (Set 2 bit cao nhất)}$$
  $$\text{MAC}[1..5] = \text{PublicKey}[1..5]$$

### 2.2. Khung gói tin quảng bá Apple AirTag (31 Bytes Raw Advertising Data)
ESP32-C3 phải phát chính xác mảng $31\text{ bytes}$ chuẩn giao thức Offline Finding của Apple:

| Byte Index | Giá trị (Hex) | Ý nghĩa kỹ thuật |
| :---: | :---: | :--- |
| `0` | `0x1E` | Chiều dài khối dữ liệu tiếp theo ($30\text{ bytes}$) |
| `1` | `0xFF` | Data Type: Manufacturer Specific Data |
| `2` | `0x4C` | Apple Company Identifier (LSB) |
| `3` | `0x00` | Apple Company Identifier (MSB) $\rightarrow$ `0x004C` |
| `4` | `0x12` | Subtype: Offline Finding / Find My Beacon |
| `5` | `0x19` | Chiều dài dữ liệu định vị Find My ($25\text{ bytes}$) |
| `6` | `0x00` | Trạng thái nguồn / Pin (0x00 = Normal, 0x01 = Low Battery) |
| `7 .. 28` | `Data` | $22\text{ bytes}$ trích xuất từ Public Key: `PublicKey[6..27]` |
| `29` | `Dynamic` | `(PublicKey[0] >> 6) & 0x03` (Hint status byte) |
| `30` | `0x00` | Sequence counter / Hint index |

---

## 3. CƠ CHẾ PHÁT XEN KẼ ẢO TRÊN ESP32-C3 (TIME-MULTIPLEXED ADVERTISING)

Để vừa định vị qua Apple Find My, vừa giữ kết nối với SmartKey/OTA mà không gây xung đột tài nguyên BLE Controller trên nhân RISC-V:

* **Chu kỳ tổng:** $4000\text{ ms}$ ($4\text{ giây}$).
  * **Trạng thái 1: AIRTAG_MODE (Kéo dài $3000\text{ ms}$)**
    * Gán địa chỉ MAC: `Static Random MAC` (suy ra từ Public Key).
    * Kiểu quảng bá: `ADV_TYPE_NONCONN_IND` (Non-connectable).
    * Advertising Interval: $320\text{ ms}$ (`0x0200`).
    * TX Power: $+21\text{ dBm}$ (Tối đa).
  * **Trạng thái 2: CONNECTABLE_MODE (Kéo dài $1000\text{ ms}$)**
    * Gán địa chỉ MAC: `Public MAC` mặc định của ESP32.
    * Kiểu quảng bá: `ADV_TYPE_IND` (Cho phép kết nối / Quét thiết bị).
    * Cung cấp Custom GATT Service (UUID: `0xFFE0`) với Characteristic `0xFFE1` để nhận $28\text{ bytes}$ Public Key từ OTA.
* **Lưu trữ Key:** Sử dụng ESP32 NVS Flash (`Preferences.h`), namespace: `"airtag_cfg"`, key name: `"pub_key"`. Nếu Flash chưa có key, thiết bị chỉ chạy chế độ CONNECTABLE_MODE để chờ nạp.

---

## 4. ĐẶC TẢ DỊCH VỤ NAS BACKEND (FASTAPI + DOCKER + GRAPHHOPPER)

Hệ thống Backend trên NAS được đóng gói triển khai bằng Docker Compose, bao gồm:

### 4.1. Cấu hình Docker Compose (`docker-compose.yml`)
```yaml
version: '3.8'

services:
  anisette:
    image: dadoum/anisette-v3-server:latest
    container_name: anisette_service
    restart: unless-stopped
    ports:
      - "6969:6969"

  findmy-backend:
    build: .
    container_name: findmy_backend_service
    restart: unless-stopped
    ports:
      - "8000:8000"
    environment:
      - ANISETTE_URL=http://anisette:6969
      - GRAPHHOPPER_URL=http://<IP_NAS>:8989
    volumes:
      - ./data:/app/data
    depends_on:
      - anisette
```

### 4.2. Danh mục RESTful API Endpoint cần triển khai:
1. `POST /api/key/generate`:
   * Sinh cặp khóa $NIST\text{ }P\text{-}224$.
   * Lưu `private_key` vào cơ sở dữ liệu nội bộ trên NAS.
   * Trả về: `device_id`, `public_key_hex` ($56\text{ ký tự hex}$ = $28\text{ bytes}$), và mảng byte C-style.
2. `GET /api/key/export-ota/{device_id}`:
   * Trả về luồng nhị phân đúng $28\text{ bytes}$ Public Key để Web/App đọc và nạp vào ESP32 qua BLE.
3. `POST /api/reports/fetch/{device_id}`:
   * Kết nối `anisette-v3-server` lấy headers xác thực Apple (`X-Apple-I-MD`, `X-Apple-I-MD-M`, v.v.).
   * Gửi request lên Apple iCloud Endpoint: `https://gateway.icloud.com/acsnservice/fetch`.
   * Sử dụng `private_key` giải mã payload phản hồi.
   * Trích xuất: `latitude`, `longitude`, `horizontal_accuracy`, `timestamp`.
4. `GET /api/route/to-bike`:
   * Tham số: `user_lat`, `user_lon`, `device_id`.
   * Lấy tọa độ mới nhất của xe từ database.
   * Gửi truy vấn định tuyến sang GraphHopper Server nội bộ:
     `GET {GRAPHHOPPER_URL}/route?point={user_lat},{user_lon}&point={bike_lat},{bike_lon}&vehicle=motorcycle&locale=vi&points_encoded=false`
   * Trả về Polyline đường đi ngắn nhất, cự ly ($m$), thời gian dự kiến và danh sách điều hướng rẽ.

---

## 5. NHIỆM VỤ YÊU CẦU DÀNH CHO AI AGENT (PROMPT EXECUTION DIRECTIVES)

Khi nạp tài liệu này cho AI Agent, hãy yêu cầu sinh mã nguồn theo 3 Module độc lập sau:

### Nhiệm vụ 1: Mã nguồn Firmware ESP32-C3 (`esp32_c3_airtag_testbench.ino` hoặc `main.cpp`)
* **Yêu cầu:** 
  - Viết bằng C++ (Arduino-ESP32 Core).
  - Tích hợp `Preferences.h` để quản lý đọc/ghi $28\text{ bytes}$ Public Key.
  - Sử dụng trực tiếp `esp_gap_ble_api.h` và `esp_bt.h` để chuyển đổi mượt mà giữa `MODE_AIRTAG` ($3\text{s}$) và `MODE_CONNECTABLE` ($1\text{s}$) mà không gây rò bộ nhớ (Zero Memory Leak).
  - Triển khai một BLE Characteristic dạng Write để cho phép ghi đè $28\text{ bytes}$ Public Key mới vào Flash NVS, sau đó tự khởi động lại bộ phát Beacon.
  - Có log Serial chi tiết tốc độ $115200\text{ bps}$.

### Nhiệm vụ 2: Dịch vụ Backend Python FastAPI (`backend_testbench.py`)
* **Yêu cầu:**
  - Viết bằng Python 3.10+ sử dụng thư viện `cryptography`, `httpx`, `fastapi`, `uvicorn`.
  - Triển khai đầy đủ logic toán học đường cong $secp224r1$ để sinh khóa và giải mã gói tin Apple Find My.
  - Tích hợp hàm giải mã dữ liệu `Apple AES-GCM / P-224 Encrypted Reports` từ server Apple (dựa trên giải thuật của dự án OpenHaystack / biemster FindMy).
  - Tích hợp hàm gọi GraphHopper API để tính toán đường đi tìm xe.

### Nhiệm vụ 3: Script chạy Test E2E tự động (`test_pipeline.py`)
* **Yêu cầu:**
  - Tạo một kịch bản kiểm thử giả lập:
    1. Gọi API sinh khóa $\rightarrow$ Lưu Public Key ra file `key.bin`.
    2. Giả lập gói tin BLE phát ra từ ESP32 để kiểm tra độ khớp cấu trúc $31\text{ bytes}$.
    3. Giả lập một tọa độ GPS trả về từ Apple $\rightarrow$ Giải mã và xác thực tính toàn vẹn của tọa độ.
    4. Gửi tọa độ giải mã sang GraphHopper để xác nhận API dẫn đường hoạt động chính xác.