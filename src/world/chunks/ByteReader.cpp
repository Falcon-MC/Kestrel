#include "world/ByteReader.h"

namespace kestrel::world {

bool ByteReader::readByte(uint8_t& out, std::string& error, const char* context)
{
    if (remaining() < 1) {
        error = std::string("unexpected end of data reading ") + context;
        return false;
    }
    out = data[offset++];
    return true;
}

bool ByteReader::readWords(uint32_t* out, size_t count, std::string& error, const char* context)
{
    if (remaining() < count * 4) {
        error = std::string("unexpected end of data reading ") + context;
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* bytes = data + offset + i * 4;
        out[i] = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
    }
    offset += count * 4;
    return true;
}

bool ByteReader::readVarInt(int32_t& out, std::string& error, const char* context)
{
    uint32_t encoded = 0;
    for (int index = 0; index < 5; ++index) {
        uint8_t byte = 0;
        if (!readByte(byte, error, context)) {
            return false;
        }
        if (index == 4 && (byte & 0xF0) != 0) {
            error = std::string("varint overflow reading ") + context;
            return false;
        }
        encoded |= uint32_t(byte & 0x7F) << (index * 7);
        if ((byte & 0x80) == 0) {
            int32_t magnitude = static_cast<int32_t>(encoded >> 1);
            out = (encoded & 1) ? ~magnitude : magnitude;
            return true;
        }
    }
    error = std::string("varint too long reading ") + context;
    return false;
}

}
