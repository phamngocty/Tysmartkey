# KẾ HOẠCH TRIỂN KHAI TESTBENCH ĐỘC LẬP: AIRTAG ESP32-C3 & NAS BACKEND
> **Mục tiêu:** Xây dựng bộ mã nguồn kiểm thử (Testbench) độc lập 100% để kiểm chứng tính năng giả lập Apple AirTag trên ESP32-C3 SuperMini kết hợp máy chủ NAS cá nhân (Anisette v3, giải mã P-224, GraphHopper).  
> **Nguyên tắc an toàn (Zero Touch):** Tuyệt đối KHÔNG sửa đổi bất kỳ dòng mã nào trong `firmware/esp32 c3/src/main.cpp` hoặc Android App chính.  
> **Trạng thái:** Chờ người dùng nghiệm thu testbench thành công mới tiến hành lập kế hoạch Big Update.

---

## 1. CẤU TRÚC FILE HỆ THỐNG TESTBENCH

```text
Tysmartkey/
├── docs/
│   ├── airtag_esp32_c3_server.md          # Tài liệu đặc tả kỹ thuật gốc
│   └── airtag_testbench_plan.md           # Kế hoạch triển khai testbench này
│
├── firmware/esp32 c3/
│   ├── platformio.ini                     # Thêm target [env:test-airtag] (Không đổi env chính)
│   ├── src/main.cpp                       # [GIỮ NGUYÊN 100%]
│   └── test/
│       ├── test_r503/                     # Test sensor hiện hữu
│       ├── test_tzm1026/                  # Test NFC hiện hữu
│       └── test_airtag/
│           └── test_airtag.cpp            # Mã nguồn test BLE AirTag + Time-Multiplexing + OTA
│
└── tools/
    └── airtag_testbench/                  # Môi trường Backend & Kiểm thử trên PC / NAS
        ├── requirements.txt               # Thư viện Python: cryptography, fastapi, httpx, uvicorn
        ├── docker-compose.yml             # Cấu hình Anisette v3 + Backend FastAPI
        ├── crypto_engine.py               # Thuật toán P-224 (secp224r1), sinh key & giải mã Find My
        ├── backend_testbench.py           # FastAPI Server (4 endpoints theo đặc tả)
        └── test_pipeline.py               # Kịch bản E2E kiểm thử tự động toàn diện
```

---

## 2. CHI TIẾT CÁC GIAI ĐOẠN TRIỂN KHAI (TASKS)

### Task 1: Thiết lập môi trường Python & Thuật toán mã hóa P-224
* **Thư mục:** `tools/airtag_testbench/`
* **Nội dung:**
  1. Tạo `requirements.txt` với các thư viện: `cryptography>=41.0.0`, `fastapi>=0.104.0`, `uvicorn>=0.23.0`, `httpx>=0.25.0`, `requests>=2.31.0`.
  2. Tạo `crypto_engine.py`:
     - Triển khai sinh cặp khóa trên đường cong chuẩn $NIST\text{ }P\text{-}224$ ($secp224r1$).
     - Trích xuất Private Key ($28\text{ bytes}$) và Public Key ($28\text{ bytes}$ tọa độ điểm $X$).
     - Hàm tính toán Static Random MAC từ Public Key:
       $$\text{MAC}[0] = \text{PublicKey}[0] \mid 0\text{b}11000000,\quad \text{MAC}[1..5] = \text{PublicKey}[1..5]$$
     - Hàm dựng khung gói tin quảng bá Apple AirTag $31\text{ bytes}$ chuẩn:
       `0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x00` + `PublicKey[6..27]` (22 bytes) + `((PublicKey[0] >> 6) & 0x03)` + `0x00`.
     - Hàm giải mã báo cáo vị trí Find My từ Apple (ECDH P-224 chia sẻ bí mật + KDF SHA256 + AES-GCM-128 decrypt) để trích xuất Latitude, Longitude, Accuracy, Timestamp.
* **Tiêu chí nghiệm thu (Verification):**
  - Chạy test unit mã hóa: Khóa sinh ra đúng 28 bytes, địa chỉ MAC đúng 2 bit cao nhất là `11`, khung 31 bytes đúng độ dài, hàm giải mã giải đúng dữ liệu mẫu.

---

### Task 2: Xây dựng dịch vụ NAS Backend FastAPI & Docker Compose
* **Thư mục:** `tools/airtag_testbench/`
* **Nội dung:**
  1. Tạo `docker-compose.yml`:
     - Container 1: `anisette` (`dadoum/anisette-v3-server:latest`) tại port `6969`.
     - Container 2: `findmy-backend` tại port `8000`.
  2. Tạo `backend_testbench.py` triển khai 4 endpoint REST API:
     - `POST /api/key/generate`: Sinh cặp khóa mới, lưu vào file json/sqlite nội bộ, trả về `device_id`, `public_key_hex`, và mã C-array mẫu để copy vào test ESP32.
     - `GET /api/key/export-ota/{device_id}`: Trả về 28 bytes binary của Public Key để nạp qua BLE.
     - `POST /api/reports/fetch/{device_id}`: Lấy Anisette headers từ `:6969`, gửi request đến `https://gateway.icloud.com/acsnservice/fetch` (sử dụng Apple ID cấu hình), giải mã payload bằng Private Key và trả về JSON tọa độ.
     - `GET /api/route/to-bike`: Nhận `user_lat`, `user_lon`, `device_id`, lấy vị trí mới nhất của xe, gọi GraphHopper engine nội bộ (`http://192.168.1.114:8989`), trả về Polyline đường đi, cự ly và thời gian di chuyển.
* **Tiêu chí nghiệm thu (Verification):**
  - Khởi chạy local FastAPI server `uvicorn backend_testbench:app --port 8000`.
  - Swagger UI tại `http://localhost:8000/docs` hoạt động đầy đủ 4 endpoints.

---

### Task 3: Kịch bản kiểm thử E2E tự động (End-to-End Simulation)
* **File:** `tools/airtag_testbench/test_pipeline.py`
* **Nội dung:**
  - Viết kịch bản tự động kiểm thử toàn bộ luồng không cần phần cứng:
    1. Gọi `/api/key/generate` sinh khóa mới.
    2. Xác thực cấu trúc 31-byte BLE frame phát ra từ Public Key.
    3. Giả lập một gói tin GPS mã hóa từ một iPhone giả lập $\rightarrow$ Gửi vào hàm giải mã $\rightarrow$ Xác nhận giải mã đúng vĩ độ/kinh độ.
    4. Mock/Gọi GraphHopper để kiểm tra tuyến đường trả về.
* **Tiêu chí nghiệm thu (Verification):**
  - Chạy `python tools/airtag_testbench/test_pipeline.py` trả về `ALL TESTS PASSED: 100% SUCCESS`.

---

### Task 4: Cấu hình Firmware Testbench trên ESP32-C3
* **Files:**
  - Sửa `firmware/esp32 c3/platformio.ini`: Thêm mục `[env:test-airtag]` độc lập (giữ nguyên `[env:esp32-c3]`).
  - Tạo `firmware/esp32 c3/test/test_airtag/test_airtag.cpp`:
    1. Quản lý bộ nhớ NVS Flash qua `Preferences.h` (Namespace: `"airtag_cfg"`, Key: `"pub_key"`, kích thước: 28 bytes).
    2. Khởi tạo BLE stack (hỗ trợ chuyển đổi mượt mà giữa 2 mode):
       - **Mode A - AIRTAG_MODE (3000ms):**
         - Thiết lập Static Random MAC: `MAC[0] = PK[0] | 0xC0`, `MAC[1..5] = PK[1..5]`.
         - Phát khung quảng bá thô 31-byte chuẩn Apple (`ADV_TYPE_NONCONN_IND`).
         - TX Power tối đa (`+20dBm`).
       - **Mode B - CONNECTABLE_MODE (1000ms):**
         - Chuyển về Public MAC mặc định của ESP32.
         - Phát quảng bá kết nối (`ADV_TYPE_IND`).
         - Khởi tạo Custom GATT Service UUID `0xFFE0` với Characteristic `0xFFE1` (Write).
         - Khi nhận được 28 bytes Public Key từ App/Web: Lưu vào NVS Flash và reload lại chu kỳ phát.
    3. Log Serial chi tiết (115200 bps): Hiển thị trạng thái chuyển mode, địa chỉ MAC hiện tại, dung lượng RAM trống, và log nhận key OTA.
* **Tiêu chí nghiệm thu (Verification):**
  - Chạy lệnh PlatformIO: `pio run -e test-airtag` biên dịch thành công 100% không cảnh báo rò bộ nhớ.
  - Lệnh `pio run -e esp32-c3` (firmware chính) vẫn biên dịch bình thường, hoàn toàn không bị ảnh hưởng.

---

### Task 5: Hướng dẫn kiểm thử thực tế trên NAS & Thiết bị thật
* **Tài liệu:** Tạo file hướng dẫn vận hành test `tools/airtag_testbench/README_TESTING.md`.
* **Nội dung:**
  - Hướng dẫn deploy Docker Compose lên NAS (`192.168.1.114`).
  - Hướng dẫn cấu hình tài khoản Apple ID thử nghiệm.
  - Hướng dẫn dùng app nRF Connect trên điện thoại để kiểm tra:
    + Bắt gói tin quảng bá Apple Beacon (31 bytes).
    + Kết nối vào Service `0xFFE0` / Characteristic `0xFFE1` để nạp 28 bytes Public Key mới.
    + Kiểm tra chu kỳ 3s / 1s chuyển mode trên Serial Monitor.

---

## 3. CHECKLIST AN TOÀN TRƯỚC KHI THỰC HIỆN

- [x] Không sửa `firmware/esp32 c3/src/main.cpp`.
- [x] Không sửa mã nguồn Android App.
- [x] Không ghi đè các cấu hình `[env:esp32-c3]` trong `platformio.ini`.
- [x] Mọi file mới đều nằm trong thư mục test độc lập (`test/test_airtag/` và `tools/airtag_testbench/`).
- [x] Có pipeline test tự động xác thực toán học mã hóa trước khi nạp vào mạch thật.
