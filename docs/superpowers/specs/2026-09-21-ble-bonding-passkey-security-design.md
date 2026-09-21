# Đặc Tả Thiết Kế: Bảo Mật BLE Smartkey - Pairing, Bonding & Passkey AES-128

**Ngày lập**: 2026-09-21  
**Trạng thái**: Đã phê duyệt (Approved)  
**Tác giả**: Antigravity & User  

---

## 1. Tổng Quan & Mục Tiêu

Hệ thống khóa thông minh Smartkey ESP32-C3 điều khiển relay mở khóa xe máy/ô tô qua sóng Bluetooth Low Energy (BLE). Để bảo vệ xe an toàn tuyệt đối trước các nguy cơ tấn công không dây, thiết kế này nâng cấp bảo mật từ tầng Ứng Dụng (Plaintext) lên tầng **Phần Cứng Bluetooth Security Manager Protocol (SMP)**:

1. **Chống kết nối trái phép (Unauthorized Access)**: Chỉ những thiết bị đã ghép đôi (Bonded) với mã Passkey 6 số hợp lệ mới được cấp quyền đọc/ghi Characteristic điều khiển xe.
2. **Mã hóa phần cứng AES-128 (Hardware Encryption)**: Toàn bộ gói tin truyền qua không khí đều được mã hóa bằng AES-128 với khóa bảo mật ECDH (Elliptic Curve Diffie-Hellman), chống hoàn toàn nghe lén (Sniffing).
3. **Lưu trữ khóa dài hạn (Bonding / Multi-Device Support)**: Thiết bị sau khi ghép đôi thành công lần đầu sẽ lưu khóa LTK (Long Term Key). Những lần sau xe tự nhận dạng điện thoại mà không cần nhập lại mã PIN. Cho phép ghép đôi nhiều điện thoại hợp lệ (điện thoại phụ, người thân).
4. **Chống Replay Attack & Man-in-the-Middle (MITM)**: Bật cờ MITM Protection và Secure Connections (LESC) theo chuẩn Bluetooth 4.2+.
5. **Cơ chế quản trị an toàn**: Cho phép đổi mã Passkey và lệnh thu hồi/xóa sạch danh sách thiết bị đã ghép đôi khi mất điện thoại.

---

## 2. Kiến Trúc Bảo Mật NimBLE (ESP32-C3)

### 2.1. Cấu Hình Bảo Mật SMP
* **Security Authentication Flags**:
  * `bonding = true`: Cho phép lưu khóa mã hóa dài hạn vào Flash NVS.
  * `mitm = true`: Kích hoạt bảo vệ chống tấn công Man-in-the-Middle (bắt buộc nhập mã PIN Passkey).
  * `sc = true`: Bật chuẩn Secure Connections (LESC) dùng thuật toán mật mã đường cong elliptic P-256.
* **I/O Capabilities**:
  * `BLE_HS_IO_DISPLAY_ONLY`: Xe đóng vai trò thiết bị chỉ hiển thị mã hoặc có mã PIN cố định. Điện thoại (Keyboard/Display) sẽ chịu trách nhiệm hiển thị popup để người dùng nhập mã.
* **Mã Passkey 6 số**:
  * Đồng bộ trực tiếp với `SECRET_KEY` (mặc định: `271000`, hoặc giá trị lưu trong namespace `"safe_key"` key `"master_key"`).
  * Chuyển đổi: `uint32_t passkey = SECRET_KEY.toInt();` (giá trị từ `000000` đến `999999`).

### 2.2. Bảo Vệ Thuộc Tính Characteristic
Characteristic `0000ff02-0000-1000-8000-00805f9b34fb` được cấu hình bắt buộc mã hóa và xác thực:
```cpp
pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ |
    NIMBLE_PROPERTY::READ_ENC |
    NIMBLE_PROPERTY::READ_AUTHEN |
    NIMBLE_PROPERTY::WRITE |
    NIMBLE_PROPERTY::WRITE_ENC |
    NIMBLE_PROPERTY::WRITE_AUTHEN |
    NIMBLE_PROPERTY::WRITE_NR |
    NIMBLE_PROPERTY::NOTIFY
);
```
Khi một thiết bị lạ chưa ghép đôi cố tình đọc hoặc ghi vào Characteristic:
* NimBLE Core lập tức trả về mã lỗi `BLE_ATT_ERR_INSUFFICIENT_AUTHEN` hoặc `BLE_ATT_ERR_INSUFFICIENT_ENC`.
* Hệ điều hành Android/iOS của điện thoại lập tức chặn lệnh và bật popup yêu cầu người dùng nhập mã Passkey 6 số.

### 2.3. Lớp Bảo Vệ Chiều Sâu (Defense-in-Depth)
Bên cạnh bảo mật phần cứng AES-128 của BLE SMP:
* Lớp ứng dụng vẫn duy trì kiểm tra `SECRET_KEY` trong chuỗi lệnh `<KEY>|<COMMAND>`.
* Bổ sung tính năng chống dò mã (Anti-Brute-Force): Nếu nhận 3 gói tin sai Secret Key liên tiếp qua BLE, ESP32 sẽ chủ động ngắt kết nối BLE Client và tạm dừng nhận kết nối trong 60 giây.

---

## 3. Kiến Trúc Ứng Dụng Android (APP Controlesp)

### 3.1. Theo Dõi Trạng Thái Ghép Đôi (Bond State Receiver)
* Đăng ký `BroadcastReceiver` theo dõi `BluetoothDevice.ACTION_BOND_STATE_CHANGED`:
  * `BluetoothDevice.BOND_BONDING`: Đang trong tiến trình người dùng nhập mã PIN.
  * `BluetoothDevice.BOND_BONDED`: Đã xác thực thành công, lưu khóa và hoàn tất mã hóa.
  * `BluetoothDevice.BOND_NONE`: Chưa ghép đôi hoặc người dùng hủy/nhập sai mã PIN.

### 3.2. Khởi Tạo Quá Trình Ghép Đôi Chủ Động
* Trong `BleManager.kt`:
  * Khi người dùng chọn kết nối tới xe, nếu `device.bondState != BluetoothDevice.BOND_BONDED`:
    * Gọi `device.createBond()` hoặc thực hiện đọc/ghi kiểm tra để kích hoạt popup hệ thống Android.
  * Hiển thị chỉ báo trực quan trên thanh trạng thái xe:
    * 🟢 *Đã kết nối & Mã hóa an toàn (Bonded AES-128)*.
    * 🟡 *Đang xác thực ghép đôi...*
    * 🔴 *Xác thực thất bại / Chưa được cấp quyền*.

### 3.3. Tính Năng Xóa Khóa Ghép Đôi (Unpair / Clear Bonds)
* Thêm nút tiện ích trong mục cài đặt bảo mật của App:
  * Cho phép người dùng gửi lệnh `UNPAIR_ALL` (yêu cầu Secret Key) để ESP32 xóa sạch toàn bộ danh sách Bonding trong Flash NVS khi cần thu hồi quyền của các máy cũ.

---

## 4. Kế Hoạch Xác Minh & Kiểm Thử (Verification Plan)

### 4.1. Tự Động Hóa (Build Verification)
* Biên dịch PlatformIO ESP32-C3:
  ```powershell
  & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d "firmware/esp32 c3"
  ```
  Xác nhận `[SUCCESS]`.
* Biên dịch Android Debug APK:
  ```powershell
  $env:JAVA_HOME="C:\Program Files\Android\Android Studio\jbr"; ./gradlew assembleDebug
  ```
  Xác nhận `BUILD SUCCESSFUL`.

### 4.2. Kiểm Thử Kịch Bản Thực Tế (Security Test Cases)
1. **Trường hợp điện thoại chính (Lần đầu)**:
   - Kết nối BLE $\rightarrow$ Android hiện popup nhập mã PIN $\rightarrow$ Nhập `271000` $\rightarrow$ Ghép đôi thành công $\rightarrow$ Mở khóa xe bình thường.
2. **Trường hợp điện thoại chính (Các lần sau)**:
   - Tắt/bật lại Bluetooth hoặc đi xa rồi lại gần $\rightarrow$ Tự động kết nối lại thành công, **không hỏi lại mã PIN**.
3. **Trường hợp điện thoại phụ (Máy khác của chủ xe)**:
   - Kết nối BLE $\rightarrow$ Hiện popup nhập PIN $\rightarrow$ Nhập `271000` $\rightarrow$ Ghép đôi thành công $\rightarrow$ Cả 2 máy cùng có thể điều khiển xe.
4. **Trường hợp kẻ gian / người lạ**:
   - Dùng điện thoại lạ bấm kết nối $\rightarrow$ Hiện popup nhập PIN $\rightarrow$ Không biết mã hoặc nhập sai $\rightarrow$ Bị từ chối kết nối, không thể gửi bất kỳ lệnh nào tới relay mở khóa.
