/**
 * @file TZM1026.cpp
 * @brief Hiện thực lớp điều khiển cảm biến vân tay bán dẫn BIOSEC TM1026M / TZM1026_V1.0
 * @author Tysmartkey Engineering Team
 * @copyright MIT License
 */

#include "TZM1026.h"

TZM1026::TZM1026(Stream* stream) : _stream(stream), _debugHex(false) {}

void TZM1026::setStream(Stream* stream) {
    _stream = stream;
}

void TZM1026::setDebugHex(bool enable) {
    _debugHex = enable;
}

uint8_t TZM1026::calcChecksum(const uint8_t* buf) {
    return buf[1] ^ buf[2] ^ buf[3] ^ buf[4] ^ buf[5];
}

void TZM1026::sendPacket(uint8_t type, uint8_t p1, uint8_t p2, uint8_t p3) {
    if (!_stream) return;
    uint8_t pkt[8] = { TZM_PACKET_HEADER, type, p1, p2, p3, 0x00, 0x00, TZM_PACKET_TAIL };
    pkt[6] = calcChecksum(pkt);

    if (_debugHex) {
        Serial.print("   [TZM TX >>] ");
        for (int i = 0; i < 8; i++) {
            if (pkt[i] < 0x10) Serial.print("0");
            Serial.print(pkt[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
    }
    _stream->write(pkt, 8);
}

bool TZM1026::receivePacket(uint8_t* resp, uint32_t timeoutMs) {
    if (!_stream) return false;
    uint32_t start = millis();
    uint8_t idx = 0;

    while (millis() - start < timeoutMs) {
        while (_stream->available()) {
            uint8_t c = _stream->read();
            if (idx == 0 && c != TZM_PACKET_HEADER) continue; // Bỏ qua dữ liệu rác đến khi thấy 0xF5
            resp[idx++] = c;
            if (idx == 8) {
                if (_debugHex) {
                    Serial.print("   [TZM RX <<] ");
                    for (int i = 0; i < 8; i++) {
                        if (resp[i] < 0x10) Serial.print("0");
                        Serial.print(resp[i], HEX);
                        Serial.print(" ");
                    }
                    Serial.println();
                }
                // Xác thực Byte kết thúc 0xF5 và mã kiểm tra Checksum
                return (resp[7] == TZM_PACKET_TAIL && resp[6] == calcChecksum(resp));
            }
        }
        delay(1);
    }
    return false;
}

void TZM1026::sfmBreak() {
    if (!_stream) return;
    sendPacket(TZM_CMD_BREAK);
    delay(50);
    while (_stream->available()) _stream->read();
}

bool TZM1026::handshake(uint32_t timeoutMs) {
    if (!_stream) return false;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_USER_COUNT);
    uint8_t ack[8];
    if (receivePacket(ack, timeoutMs)) {
        return (ack[1] == TZM_CMD_USER_COUNT && ack[4] == TZM_ACK_SUCCESS);
    }
    return false;
}

int16_t TZM1026::getUserCount() {
    if (!_stream) return -1;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_USER_COUNT);

    uint8_t ack[8];
    if (receivePacket(ack, 1500)) {
        if (ack[1] == TZM_CMD_USER_COUNT && ack[4] == TZM_ACK_SUCCESS) {
            return ((uint16_t)ack[2] << 8) | ack[3];
        }
    }
    return -1;
}

int16_t TZM1026::getNextFreeId() {
    if (!_stream) return -1;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_GET_FREE_ID);

    uint8_t ack[8];
    if (receivePacket(ack, 1500) && ack[4] == TZM_ACK_SUCCESS) {
        return ((uint16_t)ack[2] << 8) | ack[3];
    }
    return -1;
}

bool TZM1026::verify(uint16_t &matchedId, uint8_t &userRole, uint32_t timeoutMs) {
    if (!_stream) return false;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_MATCH_1_N);

    uint8_t ack[8];
    if (receivePacket(ack, timeoutMs)) {
        if (ack[1] == TZM_CMD_MATCH_1_N) {
            uint16_t id = ((uint16_t)ack[2] << 8) | ack[3];
            if (id > 0) {
                matchedId = id;
                userRole = ack[4];
                return true;
            }
        }
    }
    return false;
}

bool TZM1026::verify1to1(uint16_t id, uint32_t timeoutMs) {
    if (!_stream) return false;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_MATCH_1_1, (id >> 8) & 0xFF, id & 0xFF, 0x00);

    uint8_t ack[8];
    if (receivePacket(ack, timeoutMs)) {
        return (ack[1] == TZM_CMD_MATCH_1_1 && ack[4] == TZM_ACK_SUCCESS);
    }
    return false;
}

uint8_t TZM1026::enrollStep(uint8_t step, uint16_t id, uint8_t role) {
    if (!_stream) return 0xFF;
    while (_stream->available()) _stream->read();

    if (step == 1) {
        sendPacket(TZM_CMD_ENROLL_1, (id >> 8) & 0xFF, id & 0xFF, role);
    } else if (step == 2) {
        sendPacket(TZM_CMD_ENROLL_2);
    } else if (step == 3) {
        sendPacket(TZM_CMD_ENROLL_3);
    } else {
        return 0xFF;
    }

    uint8_t ack[8];
    if (receivePacket(ack, 4500)) {
        return ack[4];
    }
    return TZM_ACK_TIMEOUT;
}

bool TZM1026::enroll3C3R(uint16_t &id, uint8_t role, int wakePin, TZMEnrollCallback cb) {
    if (!_stream) return false;
    if (id == 0) {
        int16_t freeId = getNextFreeId();
        if (freeId <= 0) return false;
        id = (uint16_t)freeId;
    }

    const char* stepGuides[3] = {
        "ĐẶT ngón tay lần 1/3 (Chính giữa)",
        "ĐẶT ngón tay lần 2/3 (Nghiêng nhẹ mép)",
        "ĐẶT ngón tay lần 3/3 (Hoàn tất)"
    };

    for (uint8_t step = 1; step <= 3; step++) {
        if (cb) cb(step, 3, stepGuides[step - 1]);

        if (wakePin >= 0) {
            uint32_t waitTouchStart = millis();
            while (digitalRead(wakePin) == LOW) {
                if (millis() - waitTouchStart > 10000) {
                    sfmBreak();
                    return false;
                }
                delay(10);
            }
            delay(150); // Chờ tiếp xúc ổn định
        }

        uint8_t ackCode = enrollStep(step, id, role);
        if (ackCode != TZM_ACK_SUCCESS) {
            sfmBreak();
            return false;
        }

        if (step < 3) {
            if (cb) cb(step, 3, "Hãy NHẤC ngón tay ra...");
            if (wakePin >= 0) {
                while (digitalRead(wakePin) == HIGH) delay(30);
                delay(300);
            } else {
                delay(1000);
            }
        } else {
            if (cb) cb(3, 3, "Đăng ký thành công!");
            if (wakePin >= 0) {
                while (digitalRead(wakePin) == HIGH) delay(30);
            }
        }
    }
    return true;
}

bool TZM1026::enroll5C5R(uint16_t &id, uint8_t role, int wakePin, TZMEnrollCallback cb) {
    if (!_stream) return false;
    uint8_t ack[8];

    // 1. Chuyển sang NCNR Mode (CMD 0x3F PID 0x0000 Val 0x06)
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_CONFIG_FEATURE, 0x00, 0x00, 0x06);
    if (!receivePacket(ack, 1500) || ack[4] != TZM_ACK_SUCCESS) return false;

    // 2. Mở rộng Homology = 0 (Cho phép lăn xoay ngón tay lấy góc rộng)
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_CONFIG_FEATURE, 0x00, 0x01, 0x00);
    receivePacket(ack, 1500);

    // 3. Đặt số lần lấy mẫu N = 5
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_CONFIG_FEATURE, 0x00, 0x03, 5);
    if (!receivePacket(ack, 1500) || ack[4] != TZM_ACK_SUCCESS) return false;

    if (id == 0) {
        int16_t freeId = getNextFreeId();
        if (freeId <= 0) return false;
        id = (uint16_t)freeId;
    }

    const char* touchGuides[5] = {
        "ĐẶT THẲNG CHÍNH GIỮA ngón tay (Lần 1/5)",
        "ĐẶT NGHIÊNG MÉP TRÁI ngón tay (Lần 2/5)",
        "ĐẶT NGHIÊNG MÉP PHẢI ngón tay (Lần 3/5)",
        "ĐẶT CHÓP / ĐẦU NGÓN TAY (Lần 4/5)",
        "ĐẶT ĐỐT DƯỚI NGÓN TAY (Lần 5/5)"
    };

    for (uint8_t step = 1; step <= 5; step++) {
        if (cb) cb(step, 5, touchGuides[step - 1]);

        if (wakePin >= 0) {
            uint32_t waitTouchStart = millis();
            while (digitalRead(wakePin) == LOW) {
                if (millis() - waitTouchStart > 10000) {
                    sfmBreak();
                    return false;
                }
                delay(10);
            }
            delay(150);
        }

        while (_stream->available()) _stream->read();
        if (step == 1) {
            sendPacket(TZM_CMD_ENROLL_1, (id >> 8) & 0xFF, id & 0xFF, role);
        } else {
            sendPacket(TZM_CMD_ENROLL_1, 0x00, 0x00, 0x00);
        }

        if (!receivePacket(ack, 8000)) {
            sfmBreak();
            return false;
        }

        if (step < 5) {
            if (cb) cb(step, 5, "Hãy NHẤC ngón tay ra...");
            if (wakePin >= 0) {
                while (digitalRead(wakePin) == HIGH) delay(30);
                delay(300);
            } else {
                delay(1000);
            }
        } else {
            if (ack[4] == TZM_ACK_SUCCESS) {
                if (cb) cb(5, 5, "Đăng ký 5C5R thành công hoàn hảo!");
                if (wakePin >= 0) {
                    while (digitalRead(wakePin) == HIGH) delay(30);
                }
                return true;
            } else {
                sfmBreak();
                return false;
            }
        }
    }
    return true;
}

bool TZM1026::deleteUser(uint16_t id) {
    if (!_stream) return false;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_DEL_USER, (id >> 8) & 0xFF, id & 0xFF, 0x00);

    uint8_t ack[8];
    if (receivePacket(ack, 2000)) {
        return (ack[4] == TZM_ACK_SUCCESS);
    }
    return false;
}

bool TZM1026::clearAll() {
    if (!_stream) return false;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_CLEAR_ALL);

    uint8_t ack[8];
    if (receivePacket(ack, 3500)) {
        return (ack[4] == TZM_ACK_SUCCESS);
    }
    return false;
}

bool TZM1026::getAllUsers(TZMUser* userList, uint16_t maxUsers, uint16_t &totalUsers) {
    totalUsers = 0;
    if (!_stream || !userList || maxUsers == 0) return false;

    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_LIST_USERS);

    uint8_t ack[8];
    if (!receivePacket(ack, 2000) || ack[4] != TZM_ACK_SUCCESS) {
        sfmBreak();
        return false;
    }

    uint16_t len = ((uint16_t)ack[2] << 8) | ack[3];
    if (len < 2) return true; // Bộ nhớ trống

    uint8_t* buf = (uint8_t*)malloc(len + 4);
    if (!buf) return false;

    uint32_t start = millis();
    while (millis() - start < 1500 && !_stream->available()) delay(1);
    if (_stream->available() && _stream->peek() == TZM_PACKET_HEADER) {
        _stream->read(); // Bỏ qua 0xF5 Header
    }

    size_t readCount = 0;
    start = millis();
    while (readCount < len && millis() - start < 2000) {
        while (_stream->available() && readCount < len) {
            buf[readCount++] = (uint8_t)_stream->read();
        }
    }
    delay(20);
    while (_stream->available()) _stream->read(); // Xả Checksum và Tail

    if (readCount < len) {
        free(buf);
        return false;
    }

    uint16_t count = ((uint16_t)buf[0] << 8) | buf[1];
    for (uint16_t i = 0; i < count && i < maxUsers; i++) {
        uint16_t offset = 2 + i * 3;
        if (offset + 2 >= len) break;
        userList[i].id = ((uint16_t)buf[offset] << 8) | buf[offset + 1];
        userList[i].role = buf[offset + 2];
        totalUsers++;
    }
    free(buf);
    return true;
}

size_t TZM1026::getUsersJson(char* jsonBuffer, size_t maxLen) {
    if (!jsonBuffer || maxLen < 3) return 0;
    TZMUser users[100];
    uint16_t count = 0;
    if (!getAllUsers(users, 100, count)) {
        snprintf(jsonBuffer, maxLen, "[]");
        return 2;
    }

    size_t offset = 0;
    offset += snprintf(jsonBuffer + offset, maxLen - offset, "[");
    for (uint16_t i = 0; i < count; i++) {
        const char* roleStr = (users[i].role == TZM_ROLE_ADMIN) ? "Chủ xe" :
                              ((users[i].role == TZM_ROLE_NORMAL) ? "Người nhà" : "Khách");
        offset += snprintf(jsonBuffer + offset, maxLen - offset,
                           "{\"id\":%d,\"role\":%d,\"name\":\"%s\"}%s",
                           users[i].id, users[i].role, roleStr, (i < count - 1) ? "," : "");
        if (offset >= maxLen - 2) break;
    }
    offset += snprintf(jsonBuffer + offset, maxLen - offset, "]");
    return offset;
}

bool TZM1026::captureRawImage(uint8_t* pixelBuffer, uint16_t &width, uint16_t &height, size_t maxBufSize) {
    if (!_stream || !pixelBuffer) return false;
    while (_stream->available()) _stream->read();

    sendPacket(TZM_CMD_GET_IMAGE);
    uint8_t ack[8];
    if (!receivePacket(ack, 6000) || ack[4] != TZM_ACK_SUCCESS) {
        sfmBreak();
        return false;
    }

    width = (uint16_t)ack[2] << 2;
    height = (uint16_t)ack[3] << 2;
    uint32_t totalPixels = (uint32_t)width * height;
    if (totalPixels > maxBufSize) {
        sfmBreak();
        return false;
    }

    // Chờ 0xF5 Header của Data package
    uint32_t start = millis();
    while (millis() - start < 2000 && !_stream->available()) delay(1);
    if (_stream->available() && _stream->peek() == TZM_PACKET_HEADER) {
        _stream->read();
    }

    size_t readCount = 0;
    start = millis();
    while (readCount < totalPixels && millis() - start < 8000) {
        while (_stream->available() && readCount < totalPixels) {
            pixelBuffer[readCount++] = (uint8_t)_stream->read();
        }
    }
    delay(20);
    while (_stream->available()) _stream->read(); // Xả Checksum & Tail

    return (readCount == totalPixels);
}

bool TZM1026::generateBmp(const uint8_t* rawPixels, uint16_t width, uint16_t height, uint8_t* bmpBuffer, size_t &totalBmpSize) {
    if (!rawPixels || !bmpBuffer || width == 0 || height == 0) return false;

    uint32_t totalPixels = (uint32_t)width * height;
    uint32_t bmpHeaderSize = 14 + 40 + 1024; // 1078 bytes
    totalBmpSize = bmpHeaderSize + totalPixels;

    // Bitmap File Header (14 bytes)
    bmpBuffer[0] = 'B'; bmpBuffer[1] = 'M';
    bmpBuffer[2] = totalBmpSize & 0xFF;
    bmpBuffer[3] = (totalBmpSize >> 8) & 0xFF;
    bmpBuffer[4] = (totalBmpSize >> 16) & 0xFF;
    bmpBuffer[5] = (totalBmpSize >> 24) & 0xFF;
    bmpBuffer[6] = 0; bmpBuffer[7] = 0; bmpBuffer[8] = 0; bmpBuffer[9] = 0;
    bmpBuffer[10] = bmpHeaderSize & 0xFF;
    bmpBuffer[11] = (bmpHeaderSize >> 8) & 0xFF;
    bmpBuffer[12] = (bmpHeaderSize >> 16) & 0xFF;
    bmpBuffer[13] = (bmpHeaderSize >> 24) & 0xFF;

    // Bitmap Info Header (40 bytes)
    bmpBuffer[14] = 40; bmpBuffer[15] = 0; bmpBuffer[16] = 0; bmpBuffer[17] = 0;
    bmpBuffer[18] = width & 0xFF; bmpBuffer[19] = (width >> 8) & 0xFF; bmpBuffer[20] = 0; bmpBuffer[21] = 0;
    bmpBuffer[22] = height & 0xFF; bmpBuffer[23] = (height >> 8) & 0xFF; bmpBuffer[24] = 0; bmpBuffer[25] = 0;
    bmpBuffer[26] = 1; bmpBuffer[27] = 0; // 1 plane
    bmpBuffer[28] = 8; bmpBuffer[29] = 0; // 8 bits per pixel (grayscale)
    bmpBuffer[30] = 0; bmpBuffer[31] = 0; bmpBuffer[32] = 0; bmpBuffer[33] = 0; // BI_RGB
    bmpBuffer[34] = totalPixels & 0xFF;
    bmpBuffer[35] = (totalPixels >> 8) & 0xFF;
    bmpBuffer[36] = (totalPixels >> 16) & 0xFF;
    bmpBuffer[37] = (totalPixels >> 24) & 0xFF;
    bmpBuffer[38] = 0x13; bmpBuffer[39] = 0x0B; bmpBuffer[40] = 0; bmpBuffer[41] = 0; // 2835 ppm (~508 DPI)
    bmpBuffer[42] = 0x13; bmpBuffer[43] = 0x0B; bmpBuffer[44] = 0; bmpBuffer[45] = 0;
    bmpBuffer[46] = 0; bmpBuffer[47] = 1; bmpBuffer[48] = 0; bmpBuffer[49] = 0; // 256 colors
    bmpBuffer[50] = 0; bmpBuffer[51] = 0; bmpBuffer[52] = 0; bmpBuffer[53] = 0;

    // Palette Grayscale (256 * 4 = 1024 bytes)
    for (int i = 0; i < 256; i++) {
        bmpBuffer[54 + i * 4 + 0] = (uint8_t)i; // Blue
        bmpBuffer[54 + i * 4 + 1] = (uint8_t)i; // Green
        bmpBuffer[54 + i * 4 + 2] = (uint8_t)i; // Red
        bmpBuffer[54 + i * 4 + 3] = 0;
    }

    // Đảo thứ tự dòng từ dưới lên trên (Bottom-Up)
    for (int y = 0; y < height; y++) {
        int srcRow = (height - 1 - y) * width;
        int dstOffset = bmpHeaderSize + y * width;
        memcpy(&bmpBuffer[dstOffset], &rawPixels[srcRow], width);
    }
    return true;
}

bool TZM1026::setSecurityLevel(TZMSecurityLevel level) {
    if (!_stream) return false;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_SECURITY_LEVEL, 0x00, (uint8_t)level, 0x00);

    uint8_t ack[8];
    if (receivePacket(ack, 1500)) {
        return (ack[4] == TZM_ACK_SUCCESS);
    }
    return false;
}

bool TZM1026::enableSelfLearning() {
    if (!_stream) return false;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_CONFIG_FEATURE, 0x00, 0x40, 0x00);

    uint8_t ack[8];
    if (receivePacket(ack, 1500)) {
        return (ack[4] == TZM_ACK_SUCCESS);
    }
    return false;
}

bool TZM1026::setBaudrate(TZMBaudrateId baudId, bool permanent) {
    if (!_stream) return false;
    while (_stream->available()) _stream->read();
    uint8_t flag = permanent ? 0x01 : 0x00;
    sendPacket(TZM_CMD_SET_BAUDRATE, 0x00, (uint8_t)baudId, flag);

    uint8_t ack[8];
    if (receivePacket(ack, 1500)) {
        return (ack[4] == TZM_ACK_SUCCESS);
    }
    return false;
}

bool TZM1026::getFirmwareVersion(char* versionBuf, size_t maxLen) {
    if (!_stream || !versionBuf || maxLen == 0) return false;
    while (_stream->available()) _stream->read();

    sendPacket(TZM_CMD_GET_VERSION);
    uint8_t ack[8];
    if (!receivePacket(ack, 2000) || ack[4] != TZM_ACK_SUCCESS) {
        sfmBreak();
        return false;
    }

    uint16_t strLen = ((uint16_t)ack[2] << 8) | ack[3];
    uint32_t start = millis();
    while (millis() - start < 1500 && !_stream->available()) delay(1);
    if (_stream->available() && _stream->peek() == TZM_PACKET_HEADER) {
        _stream->read(); // Bỏ qua Header 0xF5
    }

    size_t outIdx = 0;
    start = millis();
    while (outIdx < strLen && millis() - start < 2000) {
        while (_stream->available() && outIdx < strLen) {
            uint8_t b = (uint8_t)_stream->read();
            if (outIdx < maxLen - 1) {
                versionBuf[outIdx++] = (char)b;
            } else {
                outIdx++; // Vẫn đếm để xả hết serial
            }
        }
    }
    delay(20);
    while (_stream->available()) _stream->read(); // Xả Checksum và Tail

    if (maxLen > 0) {
        size_t termIdx = (outIdx < maxLen) ? outIdx : (maxLen - 1);
        versionBuf[termIdx] = '\0';
    }
    return true;
}

bool TZM1026::sleep() {
    if (!_stream) return false;
    while (_stream->available()) _stream->read();
    sendPacket(TZM_CMD_SLEEP);
    uint8_t ack[8];
    if (receivePacket(ack, 1000)) {
        return (ack[4] == TZM_ACK_SUCCESS);
    }
    return false;
}

const char* TZM1026::getStatusString(uint8_t code) {
    switch (code) {
        case TZM_ACK_SUCCESS:        return "0x00: Thành công";
        case TZM_ACK_FAIL:           return "0x01: Thất bại / Không khớp";
        case TZM_ACK_FULL:           return "0x04: Bộ nhớ đã đầy (100 mẫu)";
        case TZM_ACK_NOUSER:         return "0x05: User ID không tồn tại";
        case TZM_ACK_USER_EXIST:     return "0x07: User ID đã tồn tại";
        case TZM_ACK_TIMEOUT:        return "0x08: Hết thời gian chờ (Timeout)";
        case TZM_ACK_HARDWARE_ERR:   return "0x0A: Lỗi phần cứng cảm biến";
        case TZM_ACK_IMAGE_ERR:      return "0x10: Chất lượng ảnh kém / quá mờ";
        case TZM_ACK_ALGORITHM_FAIL: return "0x11: Phát hiện ngón tay giả";
        case TZM_ACK_HOMOLOGY_FAIL:  return "0x12: Lệch ngón tay khi lấy mẫu";
        case TZM_ACK_BREAK:          return "0x18: Quá trình bị ngắt bởi lệnh mới";
        default:                     return "Mã trạng thái chưa xác định";
    }
}
