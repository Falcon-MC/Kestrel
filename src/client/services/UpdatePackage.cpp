#include "client/UpdatePackage.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <zlib.h>

namespace kestrel {
namespace {

constexpr size_t MaxBinary = 128 * 1024 * 1024;
constexpr size_t MaxExpanded = 256 * 1024 * 1024;

bool version(std::string_view text, std::array<unsigned, 3>& result)
{
    if (text.starts_with('v')) text.remove_prefix(1);
    text = text.substr(0, text.find('+'));
    for (size_t i = 0; i < result.size(); ++i) {
        size_t end = text.find('.');
        std::string_view part = text.substr(0, end);
        auto parsed = std::from_chars(part.data(), part.data() + part.size(), result[i]);
        if (part.empty() || parsed.ec != std::errc() || parsed.ptr != part.data() + part.size()) return false;
        if (i == 2) return end == std::string_view::npos;
        if (end == std::string_view::npos) return false;
        text.remove_prefix(end + 1);
    }
    return false;
}

uint32_t little(std::string_view data, size_t at, size_t bytes)
{
    uint32_t value = 0;
    for (size_t i = 0; i < bytes; ++i) value |= uint32_t(uint8_t(data[at + i])) << (8 * i);
    return value;
}

std::string expand(std::string_view input, int window, size_t maximum)
{
    if (input.size() > std::numeric_limits<uInt>::max()) return {};
    z_stream stream {};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (inflateInit2(&stream, window) != Z_OK) return {};
    std::string output;
    std::array<char, 32768> buffer;
    int status;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = buffer.size();
        status = inflate(&stream, Z_NO_FLUSH);
        size_t count = buffer.size() - stream.avail_out;
        if (count > maximum - output.size() || (status != Z_OK && status != Z_STREAM_END)) {
            inflateEnd(&stream);
            return {};
        }
        output.append(buffer.data(), count);
    } while (status == Z_OK);
    bool complete = stream.avail_in == 0;
    inflateEnd(&stream);
    return complete ? output : std::string();
}

}

bool newerRelease(std::string_view tag, std::string_view current)
{
    std::array<unsigned, 3> next {}, previous {};
    return version(tag, next) && version(current, previous) && next > previous;
}

std::string updateBinary(std::string_view archive, std::string_view path, bool zip)
{
    if (zip) {
        for (size_t at = 0; at + 46 <= archive.size(); ++at) {
            if (little(archive, at, 4) != 0x02014b50) continue;
            size_t name = little(archive, at + 28, 2);
            if (name > archive.size() - at - 46 || archive.substr(at + 46, name) != path) continue;
            uint32_t flags = little(archive, at + 8, 2), method = little(archive, at + 10, 2);
            size_t packed = little(archive, at + 20, 4), size = little(archive, at + 24, 4);
            size_t local = little(archive, at + 42, 4);
            if ((flags & 1) || size == 0 || size > MaxBinary || local > archive.size() || archive.size() - local < 30) return {};
            if (little(archive, local, 4) != 0x04034b50 || little(archive, local + 8, 2) != method) return {};
            size_t data = local + 30 + little(archive, local + 26, 2) + little(archive, local + 28, 2);
            if (data > archive.size() || packed > archive.size() - data) return {};
            std::string output;
            if (method == 0) output = archive.substr(data, packed);
            else if (method == 8) output = expand(archive.substr(data, packed), -MAX_WBITS, MaxBinary);
            if (output.size() != size || crc32(0, reinterpret_cast<const Bytef*>(output.data()), output.size()) != little(archive, at + 16, 4)) return {};
            return output;
        }
        return {};
    }
    std::string tar = expand(archive, MAX_WBITS + 16, MaxExpanded);
    for (size_t at = 0; at + 512 <= tar.size();) {
        std::string_view header(tar.data() + at, 512);
        size_t size = 0;
        std::string_view number = header.substr(124, 12);
        while (!number.empty() && (number.front() == ' ' || number.front() == '\0')) number.remove_prefix(1);
        number = number.substr(0, number.find_first_of(" \0", 0, 2));
        auto parsed = std::from_chars(number.data(), number.data() + number.size(), size, 8);
        if (number.empty() || parsed.ec != std::errc() || parsed.ptr != number.data() + number.size() || size > tar.size() - at - 512) return {};
        std::string name(header.substr(0, header.substr(0, 100).find('\0')));
        std::string prefix(header.substr(345, 155));
        prefix.resize(prefix.find('\0') == std::string::npos ? prefix.size() : prefix.find('\0'));
        if (!prefix.empty()) name = prefix + "/" + name;
        if (name == path) {
            if ((header[156] != '0' && header[156] != '\0') || size == 0 || size > MaxBinary) return {};
            return tar.substr(at + 512, size);
        }
        at += 512 + ((size + 511) / 512) * 512;
    }
    return {};
}

}
