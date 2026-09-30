/**
 * @file TZM1026.h
 * @brief Driver độc lập cho cảm biến vân tay bán dẫn điện dung BIOSEC TM1026M / TZM1026_V1.0
 * @details Giao thức BIOSEC SFM-V1.7 (Khung 8-byte F5). Hỗ trợ ESP32, STM32, Arduino, AVR.
 * @author Tysmartkey Engineering Team
 * @version 1.0.0
 * @date 2026-09-30
 * @copyright MIT License
 */

#ifndef TZM1026_H
#define TZM1026_H

#include <Arduino.h>

// ============================================================================
// ĐỊNH NGHĨA KHUNG GÓI TIN GIAO THỨC BIOSEC SFM-V1.7
// ============================================================================
#define TZM_PACKET_HEADER       0xF5
#define TZM_PACKET_TAIL         0xF5

// Bảng mã lệnh Opcode (CMD)
#define TZM_CMD_ENROLL_1        0x01  ///< Đăng ký lần 1 (Chỉ định UserID & Role)
#define TZM_CMD_ENROLL_2        0x02  ///< Đăng ký lần 2
#define TZM_CMD_ENROLL_3        0x03  ///< Đăng ký lần 3 (Tổng hợp template & Lưu)
#define TZM_CMD_DEL_USER        0x04  ///< Xóa một User ID
#define TZM_CMD_CLEAR_ALL       0x05  ///< Xóa sạch toàn bộ bộ nhớ
#define TZM_CMD_USER_COUNT      0x09  ///< Đọc tổng số lượng vân tay đang lưu
#define TZM_CMD_CHECK_USER      0x0A  ///< Kiểm tra User ID đã tồn tại hay chưa
#define TZM_CMD_MATCH_1_1       0x0B  ///< So khớp 1:1 với ID chỉ định
#define TZM_CMD_MATCH_1_N       0x0C  ///< So khớp 1:N với toàn bộ thư viện
#define TZM_CMD_GET_FREE_ID     0x0D  ///< Lấy User ID trống nhỏ nhất (chưa sử dụng)
#define TZM_CMD_SET_BAUDRATE    0x21  ///< Cài đặt tốc độ Baudrate (Flag: 0=Tạm, 1=Vĩnh viễn EEPROM)
#define TZM_CMD_GET_IMAGE       0x24  ///< Chụp & truyền ảnh đồ họa thô (Raw Grayscale Bitmap)
#define TZM_CMD_GET_VERSION     0x26  ///< Đọc chuỗi thông tin firmware & cảm biến
#define TZM_CMD_SECURITY_LEVEL  0x28  ///< Cài đặt / Đọc mức độ bảo mật so khớp (0, 1, 2)
#define TZM_CMD_LIST_USERS      0x2B  ///< Đọc danh bạ tất cả User ID & Vai trò (Role)
#define TZM_CMD_SLEEP           0x2C  ///< Đưa chip vào chế độ ngủ tiết kiệm điện
#define TZM_CMD_FINGER_DETECT   0x30  ///< Kiểm tra trạng thái ngón tay đang chạm
#define TZM_CMD_CONFIG_FEATURE  0x3F  ///< Cấu hình tham số chuyên sâu (NCNR, Tự học, Đồng nguyên)
#define TZM_CMD_BREAK           0xFE  ///< Lệnh ngắt khẩn cấp / Hủy chu trình đang chờ

// Bảng mã phản hồi trạng thái (ACK / Confirmation Code)
#define TZM_ACK_SUCCESS         0x00  ///< Hoàn thành thành công
#define TZM_ACK_FAIL            0x01  ///< Thất bại / Không khớp
#define TZM_ACK_FULL            0x04  ///< Bộ nhớ cảm biến đã đầy (100 mẫu)
#define TZM_ACK_NOUSER          0x05  ///< User ID không tồn tại
#define TZM_ACK_USER_EXIST      0x07  ///< User ID này đã có người đăng ký
#define TZM_ACK_TIMEOUT         0x08  ///< Quá thời gian chờ đặt ngón tay
#define TZM_ACK_HARDWARE_ERR    0x0A  ///< Lỗi phần cứng hoặc mắt đọc
#define TZM_ACK_IMAGE_ERR       0x10  ///< Lỗi chất lượng ảnh (quá mờ hoặc méo)
#define TZM_ACK_ALGORITHM_FAIL  0x11  ///< Phát hiện ngón tay giả / màng silicon
#define TZM_ACK_HOMOLOGY_FAIL   0x12  ///< Lỗi đồng nguyên (ấn nhầm ngón tay khác)
#define TZM_ACK_BREAK           0x18  ///< Quá trình bị ngắt bởi lệnh hủy

// Cấp bậc phân quyền người dùng (User Role)
enum TZMRole : uint8_t {
    TZM_ROLE_ADMIN  = 1,  ///< Chủ xe (Toàn quyền quản trị, thêm/xóa)
    TZM_ROLE_NORMAL = 2,  ///< Người nhà (Người dùng thông thường)
    TZM_ROLE_GUEST  = 3   ///< Khách / Người mượn xe (Có thể đặt lịch tự hủy)
};

// Cấp độ bảo mật / Độ nhạy so khớp (Security Level)
enum TZMSecurityLevel : uint8_t {
    TZM_SECURITY_HIGH_SENSITIVE = 0,  ///< FAR 1/100,000 - Cực nhạy, nhận diện nhanh nhất
    TZM_SECURITY_BALANCED       = 1,  ///< FAR 1/500,000 - Cân bằng giữa độ nhạy và bảo mật
    TZM_SECURITY_STRICT         = 2   ///< FAR 1/1,000,000 - Nghiêm ngặt nhất, chống nhận nhầm tuyệt đối
};

// Mã Baudrate theo chuẩn SFM V1.7
enum TZMBaudrateId : uint8_t {
    TZM_BAUD_9600   = 1,
    TZM_BAUD_19200  = 2,
    TZM_BAUD_38400  = 3,
    TZM_BAUD_57600  = 4,
    TZM_BAUD_115200 = 5   ///< Mặc định của module TM1026M
};

// Cấu trúc thông tin một người dùng đã lưu
struct TZMUser {
    uint16_t id;
    uint8_t role;
};

// Callback tiến trình đăng ký vân tay
typedef void (*TZMEnrollCallback)(uint8_t currentStep, uint8_t totalSteps, const char* message);

/**
 * @class TZM1026
 * @brief Lớp điều khiển cảm biến vân tay bán dẫn TZM1026 / TM1026M
 */
class TZM1026 {
private:
    Stream* _stream;
    bool _debugHex;

    uint8_t calcChecksum(const uint8_t* buf);
    void sendPacket(uint8_t type, uint8_t p1 = 0, uint8_t p2 = 0, uint8_t p3 = 0);
    bool receivePacket(uint8_t* resp, uint32_t timeoutMs = 2500);

public:
    /**
     * @brief Khởi tạo đối tượng TZM1026
     * @param stream Con trỏ tới Stream giao tiếp UART (HardwareSerial, SoftwareSerial)
     */
    TZM1026(Stream* stream = nullptr);

    /**
     * @brief Gán Stream giao tiếp cho cảm biến
     * @param stream Con trỏ Stream
     */
    void setStream(Stream* stream);

    /**
     * @brief Bật hoặc tắt in mã Hex các gói tin truyền thông (Debug)
     */
    void setDebugHex(bool enable);

    /**
     * @brief Kiểm tra bắt tay (Handshake) với cảm biến
     * @param timeoutMs Thời gian chờ phản hồi (ms)
     * @return true nếu cảm biến phản hồi đúng chuẩn SFM-V1.7
     */
    bool handshake(uint32_t timeoutMs = 1000);

    /**
     * @brief Lấy tổng số lượng vân tay đang lưu trong cảm biến
     * @return Số lượng vân tay (0-100), hoặc -1 nếu lỗi/timeout
     */
    int16_t getUserCount();

    /**
     * @brief Tự động quét và tìm User ID trống nhỏ nhất chưa sử dụng (CMD 0x0D)
     * @return User ID khả dụng (1-100), hoặc -1 nếu bộ nhớ đầy hoặc lỗi
     */
    int16_t getNextFreeId();

    /**
     * @brief Quét so khớp 1:N (Chờ đặt ngón tay và đối chiếu toàn bộ thư viện)
     * @param[out] matchedId ID người dùng khớp (nếu thành công)
     * @param[out] userRole Vai trò người dùng (1: Admin, 2: Normal, 3: Guest)
     * @param timeoutMs Thời gian chờ tối đa
     * @return true nếu nhận diện đúng, false nếu sai hoặc hết giờ
     */
    bool verify(uint16_t &matchedId, uint8_t &userRole, uint32_t timeoutMs = 3500);

    /**
     * @brief So khớp 1:1 với một User ID xác định (CMD 0x0B)
     * @param id User ID cần kiểm tra
     * @param timeoutMs Thời gian chờ tối đa
     * @return true nếu khớp đúng, false nếu không khớp
     */
    bool verify1to1(uint16_t id, uint32_t timeoutMs = 3500);

    /**
     * @brief Thực hiện một bước đăng ký vân tay đơn lẻ
     * @param step Bước đăng ký (1, 2, hoặc 3)
     * @param id User ID (chỉ cần truyền ở bước 1)
     * @param role Vai trò người dùng (1: Admin, 2: Normal, 3: Guest)
     * @return Mã phản hồi ACK từ cảm biến (0x00: Thành công)
     */
    uint8_t enrollStep(uint8_t step, uint16_t id = 0, uint8_t role = TZM_ROLE_NORMAL);

    /**
     * @brief Quy trình đăng ký vân tay chuẩn 3 lần chạm (3C3R)
     * @param id User ID cần đăng ký (nếu = 0, hàm sẽ tự động tìm ID trống)
     * @param role Vai trò phân quyền
     * @param wakePin Chân GPIO đọc ngắt WAKEUP (-1 nếu không dùng chân ngắt)
     * @param cb Con trỏ hàm callback thông báo trạng thái từng bước cho giao diện
     * @return true nếu đăng ký thành công hoàn toàn
     */
    bool enroll3C3R(uint16_t &id, uint8_t role = TZM_ROLE_NORMAL, int wakePin = -1, TZMEnrollCallback cb = nullptr);

    /**
     * @brief Quy trình đăng ký NÂNG CAO 5 lần chạm đa góc độ (5C5R NCNR)
     * @details Tạo composite template mở rộng biên vân tay, giúp mở khóa cực nhạy mọi góc đặt
     * @param id User ID cần đăng ký (nếu = 0, tự động lấy ID trống)
     * @param role Vai trò phân quyền
     * @param wakePin Chân GPIO đọc ngắt WAKEUP
     * @param cb Callback tiến trình
     * @return true nếu thành công
     */
    bool enroll5C5R(uint16_t &id, uint8_t role = TZM_ROLE_NORMAL, int wakePin = -1, TZMEnrollCallback cb = nullptr);

    /**
     * @brief Xóa một vân tay theo ID
     * @param id User ID cần xóa
     * @return true nếu xóa thành công
     */
    bool deleteUser(uint16_t id);

    /**
     * @brief Xóa sạch toàn bộ cơ sở dữ liệu vân tay
     * @return true nếu xóa thành công
     */
    bool clearAll();

    /**
     * @brief Đọc danh bạ tất cả User ID và Vai trò (CMD 0x2B)
     * @param[out] userList Mảng chứa kết quả
     * @param maxUsers Kích thước tối đa của mảng
     * @param[out] totalUsers Số lượng người dùng đọc được
     * @return true nếu đọc thành công
     */
    bool getAllUsers(TZMUser* userList, uint16_t maxUsers, uint16_t &totalUsers);

    /**
     * @brief Lấy danh bạ người dùng định dạng chuỗi JSON sẵn sàng gửi lên Mobile App
     * @param[out] jsonBuffer Bộ nhớ đệm chứa chuỗi JSON
     * @param maxLen Kích thước tối đa của buffer
     * @return Số ký tự đã ghi vào buffer
     */
    size_t getUsersJson(char* jsonBuffer, size_t maxLen);

    /**
     * @brief Chụp & Đọc ảnh vân tay đồ họa thô (Raw Grayscale 8-bit) từ cảm biến (CMD 0x24)
     * @param[out] pixelBuffer Vùng đệm chứa dữ liệu điểm ảnh (yêu cầu tối thiểu ~25,600 bytes)
     * @param[out] width Chiều rộng ảnh (pixels)
     * @param[out] height Chiều cao ảnh (pixels)
     * @param maxBufSize Dung lượng buffer cấp phát
     * @return true nếu đọc thành công trọn vẹn 100% dữ liệu ảnh
     */
    bool captureRawImage(uint8_t* pixelBuffer, uint16_t &width, uint16_t &height, size_t maxBufSize);

    /**
     * @brief Đóng gói dữ liệu ảnh thô thành file ảnh BMP 8-bit chuẩn Windows (1078-byte Header)
     * @param rawPixels Mảng điểm ảnh thô nhận từ captureRawImage
     * @param width Chiều rộng ảnh
     * @param height Chiều cao ảnh
     * @param[out] bmpBuffer Bộ nhớ đệm chứa file BMP hoàn chỉnh (yêu cầu size >= width*height + 1078)
     * @param[out] totalBmpSize Kích thước toàn bộ file BMP tạo ra
     * @return true nếu đóng gói thành công
     */
    static bool generateBmp(const uint8_t* rawPixels, uint16_t width, uint16_t height, uint8_t* bmpBuffer, size_t &totalBmpSize);

    /**
     * @brief Cài đặt cấp độ bảo mật / Độ nhạy so khớp (CMD 0x28)
     * @param level Cấp độ: 0 (Cực nhạy), 1 (Cân bằng), 2 (Nghiêm ngặt)
     * @return true nếu thành công
     */
    bool setSecurityLevel(TZMSecurityLevel level);

    /**
     * @brief Kích hoạt thuật toán AI Tự học thích ứng (Self-Learning Adaptation - CMD 0x3F PID 0x0040)
     * @details Sau mỗi lần quét đúng, cảm biến tự động cập nhật và mở rộng biên vân tay
     * @return true nếu kích hoạt thành công
     */
    bool enableSelfLearning();

    /**
     * @brief Đổi tốc độ Baudrate UART của module cảm biến (CMD 0x21)
     * @param baudId Mã tốc độ (1: 9600, 2: 19200, 3: 38400, 4: 57600, 5: 115200)
     * @param permanent true = Lưu vĩnh viễn vào EEPROM, false = Tạm thời cho đến khi reset nguồn
     * @return true nếu cảm biến xác nhận thành công
     */
    bool setBaudrate(TZMBaudrateId baudId, bool permanent = false);

    /**
     * @brief Đọc chuỗi thông tin phiên bản firmware và model mắt đọc (CMD 0x26)
     * @param[out] versionBuf Vùng đệm chứa chuỗi ký tự trả về
     * @param maxLen Độ dài tối đa vùng đệm (khuyên dùng >= 200 bytes)
     * @return true nếu đọc thành công
     */
    bool getFirmwareVersion(char* versionBuf, size_t maxLen);

    /**
     * @brief Đưa cảm biến vào chế độ ngủ sâu tiết kiệm điện (CMD 0x2C)
     * @return true nếu cảm biến đã nhận lệnh ngủ
     */
    bool sleep();

    /**
     * @brief Gửi lệnh ngắt khẩn cấp (Emergency Break) để hủy chu trình đang treo/chờ
     */
    void sfmBreak();

    /**
     * @brief Dịch mã trạng thái phản hồi ACK sang chuỗi văn bản tiếng Việt dễ đọc
     * @param code Mã trạng thái (ACK code)
     * @return Chuỗi mô tả ý nghĩa lỗi
     */
    static const char* getStatusString(uint8_t code);
};

#endif // TZM1026_H
