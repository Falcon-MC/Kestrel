#include "world/ByteReader.h"
#include "Core/NBT/NbtIo.h"

#include <exception>

namespace kestrel::world {

bool ByteReader::readTag(Tag& out, bool network, std::string& error)
{
    if (remaining() == 0) {
        error = "unexpected end of data reading block palette NBT";
        return false;
    }
    try {
        ReadOnlyBinaryStream stream(std::string(reinterpret_cast<const char*>(data + offset), remaining()));
        out = NbtIo::readTag(stream, network ? NbtVariant::Network : NbtVariant::LittleEndian);
        if (out.getType() != Tag::Type::Compound) { error = "block palette entry is not a compound"; return false; }
        offset += stream.getOffset();
        return true;
    } catch (const std::exception& exception) {
        error = std::string("block palette NBT: ") + exception.what();
        return false;
    }
}

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
