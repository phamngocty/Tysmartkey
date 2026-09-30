#pragma once
#include <Arduino.h>

/**
 * @file TM1026.h
 * @brief Driver giao tiếp UART chuẩn SFM-V1.7 (khung 8-byte F5) cho cảm biến vân tay BIOSEC TM1026M / TZM1026_V1.0
 * @chip BIOSEC TA0702 / TA0982 (Cảm biến bán dẫn điện dung TS1026M)
 */

class TM1026 {
private:
    HardwareSerial* _serial;

    // Tính checksum XOR của byte 1 đến byte 5 (theo chuẩn SFM V1.7)
    uint8_t calcChecksum(const uint8_t* buf) {
        return buf[1] ^ buf[2] ^ buf[3] ^ buf[4] ^ buf[5];
    }

    // Gửi gói tin chuẩn 8-byte
    void sendPacket(uint8_t type, uint8_t p1 = 0, uint8_t p2 = 0, uint8_t p3 = 0) {
        uint8_t pkt[8] = { 0xF5, type, p1, p2, p3, 0x00, 0x00, 0xF5 };
        pkt[6] = calcChecksum(pkt);
        _serial->write(pkt, 8);
    }

    // Đọc gói tin phản hồi chuẩn 8-byte (có timeout)
    bool receivePacket(uint8_t* resp, uint32_t timeoutMs = 2500) {
        uint32_t start = millis();
        uint8_t idx = 0;
        while (millis() - start < timeoutMs) {
            while (_serial->available()) {
                uint8_t c = _serial->read();
                if (idx == 0 && c != 0xF5) continue; // Bỏ qua rác đến khi thấy Header 0xF5
                resp[idx++] = c;
                if (idx == 8) {
                    // Kiểm tra Tail 0xF5 và Checksum
                    return (resp[7] == 0xF5 && resp[6] == calcChecksum(resp));
                }
            }
            delay(1);
        }
        return false;
    }

public:
    TM1026(HardwareSerial* s) : _serial(s) {}

    void begin(int rxPin, int txPin, uint32_t baud = 115200) {
        _serial->begin(baud, SERIAL_8N1, rxPin, txPin);
    }

    // Kiểm tra bắt tay (Handshake) với cảm biến
    bool handshake(uint32_t timeoutMs = 1000) {
        while (_serial->available()) _serial->read();
        // Gửi lệnh kiểm tra tổng số user (0x09) hoặc lệnh Ping
        sendPacket(0x09);
        uint8_t ack[8];
        if (receivePacket(ack, timeoutMs)) {
            return (ack[1] == 0x09 && ack[4] == 0x00);
        }
        return false;
    }

    // Lấy tổng số vân tay đang lưu trong cảm biến
    int16_t getUserCount() {
        while (_serial->available()) _serial->read();
        sendPacket(0x09);

        uint8_t ack[8];
        if (receivePacket(ack, 1500)) {
            if (ack[1] == 0x09 && ack[4] == 0x00) {
                return ((uint16_t)ack[2] << 8) | ack[3];
            }
        }
        return -1;
    }

    // Quét so khớp 1:N (Chờ đặt ngón tay và đối chiếu)
    // Trả về ID (>0) nếu khớp, -1 nếu không khớp hoặc lỗi/timeout
    int16_t verify(uint32_t timeoutMs = 3500) {
        while (_serial->available()) _serial->read();
        sendPacket(0x0C); // Lệnh so khớp 1:N

        uint8_t ack[8];
        if (receivePacket(ack, timeoutMs)) {
            if (ack[1] == 0x0C) {
                uint16_t id = ((uint16_t)ack[2] << 8) | ack[3];
                if (id > 0) {
                    return id; // Trùng khớp thành công (ack[4] là User Role)
                }
            }
        }
        return -1;
    }

    // Đăng ký vân tay mới theo quy trình 3 lần chạm (3C3R)
    // step: 1 (lần đầu), 2 (lần 2), 3 (lần cuối)
    // Trả về 0: Thành công, >0: Mã lỗi
    uint8_t enrollStep(uint8_t step, uint16_t userId) {
        while (_serial->available()) _serial->read();
        if (step == 1) {
            sendPacket(0x01, (userId >> 8) & 0xFF, userId & 0xFF, 0x01); // Role = 1 (Normal user)
        } else if (step == 2) {
            sendPacket(0x02);
        } else if (step == 3) {
            sendPacket(0x03);
        } else {
            return 0xFF;
        }

        uint8_t ack[8];
        if (receivePacket(ack, 4000)) {
            return ack[4]; // 0x00 là thành công
        }
        return 0xEE; // Timeout
    }

    // Xóa một vân tay theo ID
    bool deleteUser(uint16_t userId) {
        while (_serial->available()) _serial->read();
        sendPacket(0x04, (userId >> 8) & 0xFF, userId & 0xFF, 0x00);

        uint8_t ack[8];
        if (receivePacket(ack, 2000)) {
            return (ack[4] == 0x00);
        }
        return false;
    }

    // Xóa toàn bộ vân tay trong bộ nhớ
    bool emptyDatabase() {
        while (_serial->available()) _serial->read();
        sendPacket(0x05);

        uint8_t ack[8];
        if (receivePacket(ack, 3000)) {
            return (ack[4] == 0x00);
        }
        return false;
    }

    // Cài đặt cấp độ bảo mật / độ nhạy so sánh:
    // level: 0 (FAR 1/100,000 - Cực nhạy, dễ nhận), 1 (FAR 1/500,000), 2 (FAR 1/1,000,000 - Mặc định)
    bool setSecurityLevel(uint8_t level) {
        while (_serial->available()) _serial->read();
        sendPacket(0x28, 0x00, level, 0x00);
        uint8_t ack[8];
        if (receivePacket(ack, 1500)) {
            return (ack[4] == 0x00);
        }
        return false;
    }

    // Bật tính năng AI Tự học thích ứng (Self-Learning Adaptation)
    // Tự động tinh chỉnh và mở rộng góc quét sau mỗi lần so khớp thành công
    bool enableSelfLearning() {
        while (_serial->available()) _serial->read();
        sendPacket(0x3F, 0x00, 0x40, 0x00);
        uint8_t ack[8];
        if (receivePacket(ack, 1500)) {
            return (ack[4] == 0x00);
        }
        return false;
    }

    // Cài đặt mức độ kiểm tra đồng nguyên khi đăng ký:
    // level: 0 (Cho phép xoay/lăn ngón tay tối đa để lấy mẫu rộng), 5 (Nghiêm ngặt nhất)
    bool setHomologyLevel(uint8_t level) {
        while (_serial->available()) _serial->read();
        sendPacket(0x3F, 0x00, 0x01, level);
        uint8_t ack[8];
        if (receivePacket(ack, 1500)) {
            return (ack[4] == 0x00);
        }
        return false;
    }

    // Cấu hình chế độ lấy mẫu N lần chạm (NCNR: N=4, 5, hoặc 6)
    bool configNCNR(uint8_t sampleCount = 5) {
        uint8_t ack[8];
        // 1. Chuyển sang NCNR mode
        while (_serial->available()) _serial->read();
        sendPacket(0x3F, 0x00, 0x00, 0x06);
        if (!receivePacket(ack, 1500) || ack[4] != 0x00) return false;

        // 2. Cho phép xoay góc lấy mẫu rộng (Homology Level = 0)
        while (_serial->available()) _serial->read();
        sendPacket(0x3F, 0x00, 0x01, 0x00);
        if (!receivePacket(ack, 1500) || ack[4] != 0x00) return false;

        // 3. Đặt số lần lấy mẫu N
        while (_serial->available()) _serial->read();
        sendPacket(0x3F, 0x00, 0x03, sampleCount);
        if (!receivePacket(ack, 1500) || ack[4] != 0x00) return false;

        return true;
    }
};
