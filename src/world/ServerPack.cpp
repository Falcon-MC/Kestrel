#include "world/ServerPack.h"

#include "Core/Json/Json.h"

#include <openssl/evp.h>
#include <zlib.h>

#include <cstring>
#include <fstream>
#include <sstream>

namespace kestrel::world {

namespace {

constexpr uint32_t EncryptedMagic = 0x9BCFB9FCu;
constexpr size_t EncryptedHeaderSize = 0x100;

uint16_t readLe16(const std::string& data, size_t offset)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data() + offset);
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

uint32_t readLe32(const std::string& data, size_t offset)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data() + offset);
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

bool inflateRaw(const char* data, size_t size, size_t expected, std::string& out)
{
    z_stream stream {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return false;
    }
    out.assign(expected, '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    stream.avail_in = static_cast<uInt>(size);
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    int status = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return status == Z_STREAM_END && stream.total_out == expected;
}

bool unzip(const std::string& archive, std::unordered_map<std::string, std::string>& files, std::string& error)
{
    if (archive.size() < 22) {
        error = "archive too small";
        return false;
    }
    size_t end = std::string::npos;
    size_t searchStart = archive.size() >= 65557 ? archive.size() - 65557 : 0;
    for (size_t position = archive.size() - 22 + 1; position-- > searchStart;) {
        if (readLe32(archive, position) == 0x06054b50u) {
            end = position;
            break;
        }
    }
    if (end == std::string::npos) {
        error = "missing zip end record";
        return false;
    }
    uint16_t count = readLe16(archive, end + 10);
    size_t cursor = readLe32(archive, end + 16);
    for (uint16_t i = 0; i < count; ++i) {
        if (cursor + 46 > archive.size() || readLe32(archive, cursor) != 0x02014b50u) {
            error = "corrupt zip directory";
            return false;
        }
        uint16_t method = readLe16(archive, cursor + 10);
        uint32_t compressedSize = readLe32(archive, cursor + 20);
        uint32_t size = readLe32(archive, cursor + 24);
        uint16_t nameLength = readLe16(archive, cursor + 28);
        uint16_t extraLength = readLe16(archive, cursor + 30);
        uint16_t commentLength = readLe16(archive, cursor + 32);
        uint32_t local = readLe32(archive, cursor + 42);
        std::string name = archive.substr(cursor + 46, nameLength);
        cursor += 46 + size_t(nameLength) + extraLength + commentLength;

        if (name.empty() || name.back() == '/' || local + 30 > archive.size()) {
            continue;
        }
        size_t dataStart = local + 30 + readLe16(archive, local + 26) + readLe16(archive, local + 28);
        if (dataStart + compressedSize > archive.size()) {
            continue;
        }
        std::string content;
        if (method == 0) {
            content = archive.substr(dataStart, compressedSize);
        } else if (method != 8 || !inflateRaw(archive.data() + dataStart, compressedSize, size, content)) {
            continue;
        }
        for (char& c : name) {
            if (c == '\\') {
                c = '/';
            }
        }
        files.emplace(std::move(name), std::move(content));
    }
    return true;
}

bool decryptCfb8(const std::string& key, const char* data, size_t size, std::string& out)
{
    if (key.size() < 32) {
        return false;
    }
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (!context) {
        return false;
    }
    out.assign(size, '\0');
    int written = 0;
    bool ok = EVP_DecryptInit_ex(context, EVP_aes_256_cfb8(), nullptr, reinterpret_cast<const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(key.data()))
        && EVP_DecryptUpdate(context, reinterpret_cast<unsigned char*>(out.data()), &written, reinterpret_cast<const unsigned char*>(data), static_cast<int>(size));
    EVP_CIPHER_CTX_free(context);
    return ok && size_t(written) == size;
}

std::string rootPrefix(const std::unordered_map<std::string, std::string>& files)
{
    size_t best = std::string::npos;
    std::string prefix;
    for (const auto& [name, content] : files) {
        const std::string manifest = "manifest.json";
        if (name.size() >= manifest.size() && name.compare(name.size() - manifest.size(), manifest.size(), manifest) == 0) {
            std::string candidate = name.substr(0, name.size() - manifest.size());
            if (candidate.size() < best) {
                best = candidate.size();
                prefix = candidate;
            }
        }
    }
    return prefix;
}

}

std::shared_ptr<const PackFiles> loadServerPack(const std::filesystem::path& archivePath, const std::string& contentKey, std::string& error)
{
    std::ifstream file(archivePath, std::ios::binary);
    if (!file) {
        error = "cannot open " + archivePath.string();
        return nullptr;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    std::unordered_map<std::string, std::string> raw;
    if (!unzip(buffer.str(), raw, error)) {
        return nullptr;
    }

    std::string prefix = rootPrefix(raw);
    auto pack = std::make_shared<PackFiles>();
    for (auto& [name, content] : raw) {
        if (name.compare(0, prefix.size(), prefix) == 0) {
            pack->files.emplace(name.substr(prefix.size()), std::move(content));
        }
    }

    auto contents = pack->files.find("contents.json");
    if (contents == pack->files.end() || contents->second.size() < EncryptedHeaderSize || readLe32(contents->second, 4) != EncryptedMagic) {
        return pack;
    }
    std::string decrypted;
    if (!decryptCfb8(contentKey, contents->second.data() + EncryptedHeaderSize, contents->second.size() - EncryptedHeaderSize, decrypted)) {
        error = "missing or invalid content key";
        return nullptr;
    }
    std::unique_ptr<json::Value> document = json::parse(decrypted);
    const json::Value* entries = document ? document->get("content") : nullptr;
    if (!entries || !entries->isArray()) {
        error = "could not decrypt contents.json";
        return nullptr;
    }
    for (const auto& entry : entries->mArray) {
        const json::Value* path = entry->get("path");
        const json::Value* key = entry->get("key");
        if (!path || !key || !path->isString() || !key->isString() || key->string().empty()) {
            continue;
        }
        auto target = pack->files.find(path->string());
        if (target == pack->files.end()) {
            continue;
        }
        std::string plain;
        if (decryptCfb8(key->string(), target->second.data(), target->second.size(), plain)) {
            target->second = std::move(plain);
        }
    }
    pack->files["contents.json"] = std::move(decrypted);
    return pack;
}

}
