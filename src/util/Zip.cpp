#include "util/Zip.h"

#include <zlib.h>

#include <limits>

namespace kestrel::util {

namespace {

constexpr uint32_t LocalHeader = 0x04034b50u;
constexpr uint32_t CentralHeader = 0x02014b50u;
constexpr uint32_t EndOfDirectory = 0x06054b50u;
constexpr uint16_t Stored = 0;
constexpr uint16_t Deflated = 8;
constexpr uint16_t Utf8Names = 0x0800;
constexpr uint16_t Version = 20;

uint16_t readLe16(const std::string& bytes, size_t at)
{
    return static_cast<uint16_t>(uint8_t(bytes[at]) | uint8_t(bytes[at + 1]) << 8);
}

uint32_t readLe32(const std::string& bytes, size_t at)
{
    return uint32_t(readLe16(bytes, at)) | uint32_t(readLe16(bytes, at + 2)) << 16;
}

void writeLe16(std::string& out, uint16_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>(value >> 8));
}

void writeLe32(std::string& out, uint32_t value)
{
    writeLe16(out, static_cast<uint16_t>(value & 0xFFFF));
    writeLe16(out, static_cast<uint16_t>(value >> 16));
}

/**
 * The entry's path with forward slashes, or empty when it is a folder or
 * would escape the extraction folder.
 */
std::string safePath(std::string path)
{
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (path.empty() || path.front() == '/' || path.back() == '/' || path.find(':') != std::string::npos) {
        return {};
    }
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        std::string_view part(path.data() + start, (slash == std::string::npos ? path.size() : slash) - start);
        if (part.empty() || part == "." || part == "..") {
            return {};
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return path;
}

bool inflateRaw(const char* data, size_t size, size_t expected, std::string& out)
{
    out.assign(expected, '\0');
    z_stream stream {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    stream.avail_in = static_cast<uInt>(size);
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(expected);
    int result = inflate(&stream, Z_FINISH);
    bool complete = result == Z_STREAM_END && stream.total_out == expected;
    inflateEnd(&stream);
    return complete;
}

bool deflateRaw(const std::string& data, std::string& out)
{
    z_stream stream {};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }
    out.assign(deflateBound(&stream, static_cast<uLong>(data.size())), '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    int result = deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END;
}

}

bool readZip(const std::string& archive, uint64_t maxExpanded, std::vector<ZipEntry>& entries, std::string& error)
{
    entries.clear();
    auto fail = [&](const char* message) {
        error = message;
        entries.clear();
        return false;
    };
    if (archive.size() < 22) {
        return fail("not a ZIP archive");
    }
    size_t end = std::string::npos;
    const size_t first = archive.size() > 65557 ? archive.size() - 65557 : 0;
    for (size_t at = archive.size() - 22 + 1; at-- > first;) {
        if (readLe32(archive, at) == EndOfDirectory && at + 22 + readLe16(archive, at + 20) == archive.size()) {
            end = at;
            break;
        }
    }
    if (end == std::string::npos || readLe16(archive, end + 4) != 0 || readLe16(archive, end + 6) != 0) {
        return fail("unsupported ZIP archive");
    }
    const size_t directoryStart = readLe32(archive, end + 16);
    const size_t directoryEnd = directoryStart + readLe32(archive, end + 12);
    const uint16_t count = readLe16(archive, end + 10);
    if (directoryEnd != end) {
        return fail("invalid ZIP directory");
    }
    uint64_t expanded = 0;
    size_t cursor = directoryStart;
    for (uint16_t i = 0; i < count; ++i) {
        if (directoryEnd - cursor < 46 || readLe32(archive, cursor) != CentralHeader) {
            return fail("corrupt ZIP directory");
        }
        const uint16_t flags = readLe16(archive, cursor + 8);
        const uint16_t method = readLe16(archive, cursor + 10);
        const uint32_t crc = readLe32(archive, cursor + 16);
        const uint32_t compressed = readLe32(archive, cursor + 20);
        const uint32_t size = readLe32(archive, cursor + 24);
        const uint16_t nameSize = readLe16(archive, cursor + 28);
        const size_t recordSize = 46ull + nameSize + readLe16(archive, cursor + 30) + readLe16(archive, cursor + 32);
        const size_t local = readLe32(archive, cursor + 42);
        if (recordSize > directoryEnd - cursor) {
            return fail("truncated ZIP directory");
        }
        std::string rawName = archive.substr(cursor + 46, nameSize);
        cursor += recordSize;
        if (rawName.empty() || rawName.back() == '/' || rawName.back() == '\\') {
            continue;
        }
        std::string path = safePath(rawName);
        if (path.empty()) {
            return fail("ZIP entry with an unsafe path");
        }
        if ((flags & 1) || (method != Stored && method != Deflated) || (method == Stored && size != compressed)) {
            return fail("unsupported ZIP entry");
        }
        if (size > maxExpanded - expanded) {
            return fail("ZIP archive expands too much");
        }
        expanded += size;
        if (local > directoryStart || directoryStart - local < 30 || readLe32(archive, local) != LocalHeader) {
            return fail("invalid ZIP entry");
        }
        const size_t offset = local + 30ull + readLe16(archive, local + 26) + readLe16(archive, local + 28);
        if (offset > directoryStart || compressed > directoryStart - offset) {
            return fail("invalid ZIP entry");
        }
        ZipEntry entry;
        entry.path = std::move(path);
        if (method == Stored) {
            entry.data = archive.substr(offset, size);
        } else if (!inflateRaw(archive.data() + offset, compressed, size, entry.data)) {
            return fail("corrupt ZIP entry");
        }
        if (crc32(0, reinterpret_cast<const Bytef*>(entry.data.data()), static_cast<uInt>(entry.data.size())) != crc) {
            return fail("ZIP entry fails its checksum");
        }
        entries.push_back(std::move(entry));
    }
    return true;
}

std::string writeZip(const std::vector<ZipEntry>& entries)
{
    if (entries.size() > std::numeric_limits<uint16_t>::max()) {
        return {};
    }
    std::string out;
    std::string directory;
    for (const ZipEntry& entry : entries) {
        if (entry.data.size() > std::numeric_limits<uint32_t>::max() || out.size() > std::numeric_limits<uint32_t>::max()) {
            return {};
        }
        std::string compressed;
        uint16_t method = Deflated;
        if (!deflateRaw(entry.data, compressed)) {
            return {};
        }
        if (compressed.size() >= entry.data.size()) {
            compressed = entry.data;
            method = Stored;
        }
        const uint32_t crc = crc32(0, reinterpret_cast<const Bytef*>(entry.data.data()), static_cast<uInt>(entry.data.size()));
        const uint32_t offset = static_cast<uint32_t>(out.size());
        writeLe32(out, LocalHeader);
        writeLe16(out, Version);
        writeLe16(out, Utf8Names);
        writeLe16(out, method);
        writeLe32(out, 0);
        writeLe32(out, crc);
        writeLe32(out, static_cast<uint32_t>(compressed.size()));
        writeLe32(out, static_cast<uint32_t>(entry.data.size()));
        writeLe16(out, static_cast<uint16_t>(entry.path.size()));
        writeLe16(out, 0);
        out += entry.path;
        out += compressed;

        writeLe32(directory, CentralHeader);
        writeLe16(directory, Version);
        writeLe16(directory, Version);
        writeLe16(directory, Utf8Names);
        writeLe16(directory, method);
        writeLe32(directory, 0);
        writeLe32(directory, crc);
        writeLe32(directory, static_cast<uint32_t>(compressed.size()));
        writeLe32(directory, static_cast<uint32_t>(entry.data.size()));
        writeLe16(directory, static_cast<uint16_t>(entry.path.size()));
        writeLe16(directory, 0);
        writeLe16(directory, 0);
        writeLe16(directory, 0);
        writeLe16(directory, 0);
        writeLe32(directory, 0);
        writeLe32(directory, offset);
        directory += entry.path;
    }
    if (out.size() > std::numeric_limits<uint32_t>::max()) {
        return {};
    }
    const uint32_t directoryOffset = static_cast<uint32_t>(out.size());
    out += directory;
    writeLe32(out, EndOfDirectory);
    writeLe16(out, 0);
    writeLe16(out, 0);
    writeLe16(out, static_cast<uint16_t>(entries.size()));
    writeLe16(out, static_cast<uint16_t>(entries.size()));
    writeLe32(out, static_cast<uint32_t>(directory.size()));
    writeLe32(out, directoryOffset);
    writeLe16(out, 0);
    return out;
}

}
