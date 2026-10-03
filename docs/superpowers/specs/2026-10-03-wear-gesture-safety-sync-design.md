# Thiết kế Bộ lọc an toàn Cử chỉ Búng tay Wear OS & Đồng bộ Cài đặt 2 Chiều Phone - Watch

- **Tài liệu**: Đặc tả Thiết kế Kỹ thuật (Design Specification)
- **Ngày lập**: 03/10/2026
- **Trạng thái**: Đã phê duyệt (Approved)
- **Hệ thống liên quan**: Ứng dụng Android Handheld (`APP Controlesp/app`) & Ứng dụng Wear OS (`APP Controlesp/wear`)

---

## 1. Bối cảnh & Vấn đề Cần giải quyết

### 1.1 Vấn đề hiện tại
- Trên đồng hồ Wear OS ([MainActivity.kt](file:///c:/Users/phamn/Documents/PlatformIO/Tysmartkey/APP%20Controlesp/wear/src/main/java/com/example/control_esp/wear/MainActivity.kt)), cử chỉ búng tay (`Pinch / Snap Gesture`) để đề xe đã hoạt động nhưng gặp hiện tượng **kích hoạt nhầm nghiêm trọng ngay khi mở ứng dụng**:
  - Người dùng mở app trên đồng hồ $\rightarrow$ Động tác đưa tay, xoay cổ tay, chạm màn hình tạo gia tốc vượt ngưỡng tức thời $\rightarrow$ Xe tự động bật điện (`Lệnh 1`), và ngay sau đó tiếp tục tự đề nổ máy (`Lệnh 2`).
- Ứng dụng điện thoại chưa có giao diện cài đặt hay tùy biến các thông số của cử chỉ Wear OS; cấu hình chưa được đồng bộ 2 chiều qua Google Wearable Data Layer API.

### 1.2 Nguyên nhân kỹ thuật
1. **Thiếu thời gian làm ấm (Grace Period):** Cảm biến gia tốc bắt đầu phân tích ngay khi `onResume()` được gọi.
2. **`lastGestureTime` khởi tạo là `0L`:** Khiến điều kiện cooldown `currentTime - lastGestureTime > 1500` luôn đúng ở mẫu đo đầu tiên.
3. **Ngưỡng gia tốc quá nhạy:** $12.0\text{ m/s}^2$ ($\approx 1.22\text{ g}$) quá thấp, vung tay nhẹ cũng vượt ngưỡng.
4. **Hành vi tự kích hoạt liên hoàn:** Nếu xe đang tắt thì búng tay bật xe; sau đó một cử động nhẹ tiếp theo hoặc tính năng `AUTO_START` sẽ lập tức kích hoạt đề xe.

---

## 2. Giải pháp Kiến trúc & Thiết kế Chi tiết

### 2.1 Bộ lọc an toàn 4 lớp trên Đồng hồ Wear OS

#### Lớp 1: Khóa an toàn khi mở ứng dụng (Warm-up Grace Period)
- Khi `onResume()` được gọi trên Wear OS:
  - Ghi nhận `appResumeTime = System.currentTimeMillis()`.
  - Gán `lastGestureTime = System.currentTimeMillis()` để triệt tiêu việc thỏa mãn cooldown ban đầu.
- Trong `onSensorChanged()`:
  - Nếu `System.currentTimeMillis() - appResumeTime < 2000L` (2 giây đầu tiên sau khi app mở / màn hình bật): **Bỏ qua toàn bộ dữ liệu cảm biến**.

#### Lớp 2: Bộ lọc đặc trưng xung búng tay (Snap Impulse Filter)
- Tùy biến ngưỡng theo 3 cấp độ nhạy trong SharedPreferences (`WEAR_PREF`):
  - **Mức Thấp (Low - An toàn tối đa):** Ngưỡng $26.0\text{ m/s}^2$ ($\approx 2.65\text{ g}$).
  - **Mức Vừa (Medium - Mặc định khuyên dùng):** Ngưỡng $21.0\text{ m/s}^2$ ($\approx 2.14\text{ g}$).
  - **Mức Cao (High - Nhạy):** Ngưỡng $16.0\text{ m/s}^2$ ($\approx 1.63\text{ g}$).
- Tăng thời gian Cooldown giữa 2 lần búng tay liên tiếp từ `1500ms` lên `2500ms`.
- Rung xác nhận Haptic `150ms` để báo hiệu cử chỉ đã được nhận diện hợp lệ.

#### Lớp 3: Phân quyền chế độ hành vi cử chỉ (Gesture Action Mode)
- Lưu cấu hình `GESTURE_ACTION_MODE` (Int):
  - **Chế độ 0 (`MODE_START_ONLY` - Khuyên dùng & Mặc định):**
    - Khi xe đang **TẮT** (`isVehicleOn == false`): Không thực hiện hành động nào (Tránh 100% việc vô tình bật nguồn xe).
    - Khi xe đang **BẬT** (`isVehicleOn == true`): Búng tay kích hoạt Đề nổ (`triggerEngineStart()` $\rightarrow$ Lệnh `"2"`).
  - **Chế độ 1 (`MODE_TOGGLE_AND_START`):**
    - Khi xe Tắt: Bật xe (Lệnh `"1"`).
    - Khi xe Bật: Đề nổ xe (Lệnh `"2"`).

#### Lớp 4: Cơ chế đồng bộ cấu hình 2 chiều qua Wearable Data Layer

```
  [ Điện thoại Android ]                               [ Đồng hồ Wear OS ]
┌─────────────────────────┐                     ┌─────────────────────────┐
│ • SharedPreferences     │                     │ • SharedPreferences     │
│   "WEAR_SETTINGS_PREF"  │                     │   "WEAR_PREF"           │
│                         │                     │                         │
│ • Dialog Cài đặt        │ ── /settings_sync ─>│ • onMessageReceived     │
│   Cử chỉ Wear OS        │    (MessageClient)  │   Cập nhật UI Compose   │
│                         │                     │                         │
│ • WearMessageListener   │<─ /settings_watch ──│ • SettingsScreen        │
│   Cập nhật UI Phone     │    (MessageClient)  │   Đổi cấu hình tức thì  │
└─────────────────────────┘                     └─────────────────────────┘
```

1. **Giao thức truyền dữ liệu:**
   - Sử dụng định dạng chuỗi: `isGestureEnabled|gestureMode|gestureSensitivity`
     - Ví dụ: `"true|0|1"` (Bật cử chỉ, Chế độ Chỉ Đề, Độ nhạy Vừa).
   - **Từ Phone sang Watch:** Path `/settings_sync`.
   - **Từ Watch sang Phone:** Path `/settings_watch`.
   - Khi Watch gửi `/ping` lúc mở app, Phone sẽ phản hồi cả trạng thái xe lẫn cấu hình cài đặt mới nhất.

---

## 3. Thiết kế Giao diện Người dùng (UI/UX)

### 3.1 Trên Ứng dụng Điện thoại (`APP Controlesp/app`)
- Trong Thẻ "ĐỒNG HỒ THÔNG MINH WEAR OS" tại màn hình Cài đặt:
  - Thêm nút "Cài đặt cử chỉ" (`btnWatchGestureSettings`).
- Hiển thị Dialog sang trọng `dialog_wear_gesture_settings.xml`:
  - **Switch chính:** Bật/Tắt tính năng búng tay.
  - **Nhóm chọn Chế độ hành vi (RadioGroup):**
    + [x] Chỉ đề nổ khi xe đã bật điện (Khuyên dùng - An toàn).
    + [ ] Bật điện xe & Đề nổ máy.
  - **Nhóm chọn Độ nhạy (Segmented/Radio):**
    + Thấp (Chống nhầm cao) | Vừa (Khuyên dùng) | Cao (Nhạy).
  - **Nút "Lưu & Đồng bộ":** Lưu cục bộ và gửi tức thì sang Wear OS.

### 3.2 Trên Ứng dụng Đồng hồ (`APP Controlesp/wear`)
- Trong Tab 3 (`SettingsScreen`):
  - Switch: "Búng tay đề máy" (On/Off).
  - Tùy chọn: Chế độ ("Chỉ đề khi bật" / "Bật & Đề").
  - Tùy chọn: Độ nhạy ("Vừa", "Thấp", "Cao").
  - Khi người dùng chạm thay đổi trên đồng hồ, lưu `WEAR_PREF` và gửi bản tin `/settings_watch` về điện thoại.

---

## 4. Kế hoạch Kiểm thử & Xác minh (Verification Criteria)
1. **Kiểm tra Grace Period:** Mở app Wear OS, lập tức lắc mạnh/vung tay trong 2 giây đầu $\rightarrow$ Xác nhận không có lệnh nào được gửi đi (Log không có `⚡ BÚNG TAY THÀNH CÔNG`).
2. **Kiểm tra Ngưỡng nhạy & Búng tay:** Sau 2 giây, thực hiện búng tay thật sự $\rightarrow$ Đồng hồ rung Haptic 150ms và kích hoạt lệnh chính xác.
3. **Kiểm tra Chế độ `MODE_START_ONLY`:** Khi xe tắt, búng tay không gửi lệnh `"1"`. Chỉ khi xe đã bật điện, búng tay mới gửi lệnh `"2"`.
4. **Kiểm tra Đồng bộ 2 chiều:**
   - Thay đổi cài đặt trên điện thoại $\rightarrow$ Đồng hồ cập nhật ngay lập tức.
   - Thay đổi cài đặt trên đồng hồ $\rightarrow$ Điện thoại cập nhật ngay lập tức.
