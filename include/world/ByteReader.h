#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace kestrel::world {

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size)
        : data(data)
        , size(size)
    {
    }

    size_t position() const
    {
        return offset;
    }

    size_t remaining() const
    {
        return size - offset;
    }

    bool readByte(uint8_t& out, std::string& error, const char* context);
    bool readWords(uint32_t* out, size_t count, std::string& error, const char* context);
    bool readVarInt(int32_t& out, std::string& error, const char* context);

private:
    const uint8_t* data;
    size_t size;
    size_t offset = 0;
};

}
