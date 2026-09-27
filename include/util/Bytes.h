#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace kestrel::util {

/**
 * Little endian reads from raw bytes, at any alignment.
 */
template <typename T>
T readLittle(const void* data)
{
    T value {};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

inline uint16_t readLe16(const std::string& data, size_t offset)
{
    return readLittle<uint16_t>(data.data() + offset);
}

inline uint32_t readLe32(const std::string& data, size_t offset)
{
    return readLittle<uint32_t>(data.data() + offset);
}

inline uint64_t readLe64(const std::string& data, size_t offset)
{
    return readLittle<uint64_t>(data.data() + offset);
}

}
