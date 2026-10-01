#include "world/ServerPack.h"
#include "Core/Json/Json.h"
#include "util/Bytes.h"
#include "util/Text.h"
#include "util/JsonText.h"
#include <openssl/evp.h>
#include <zlib.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <list>
#include <mutex>
#include <unordered_map>

namespace kestrel::world {
namespace {
constexpr size_t MaxArchive = 512ull * 1024 * 1024;
constexpr size_t MaxFile = 64ull * 1024 * 1024;
constexpr uint64_t MaxExpanded = 1024ull * 1024 * 1024;
constexpr size_t CacheBudget = 8ull * 1024 * 1024;
using util::readLe16;
using util::readLe32;
std::string canonical(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos || path.find('\0') != std::string::npos) return {};
    size_t start = 0;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        auto component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") return {};
        start = end + 1;
    }
    return path;
}
bool decrypt(const std::string& key, const std::string& data, std::string& out)
{
    if (key.size() != 32 || data.size() > MaxFile) return false;
    auto* context = EVP_CIPHER_CTX_new();
    if (!context) return false;
    out.resize(data.size());
    int written = 0;
    bool ok = EVP_DecryptInit_ex(context, EVP_aes_256_cfb8(), nullptr,
        reinterpret_cast<const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(key.data()))
        && EVP_DecryptUpdate(context, reinterpret_cast<unsigned char*>(out.data()), &written,
            reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()));
    EVP_CIPHER_CTX_free(context);
    return ok && static_cast<size_t>(written) == data.size();
}
}
struct PackFiles::Storage {
    struct Entry { size_t offset; uint32_t compressed, size, crc; uint16_t method; std::string key; };
    struct Cached { std::shared_ptr<const std::string> data; std::list<std::string>::iterator lru; };
    std::string archive;
    std::unordered_map<std::string, Entry> entries;
    std::unordered_map<std::string, std::string> folded;
    uint64_t expanded = 0;
    mutable std::mutex mutex;
    mutable std::unordered_map<std::string, Cached> cache;
    mutable std::list<std::string> lru;
    mutable size_t cachedBytes = 0;
    std::shared_ptr<const std::string> read(const std::string& path) const
    {
        auto entry = entries.find(path);
        if (entry == entries.end()) {
            auto fold = folded.find(util::lowercase(path));
            if (fold == folded.end()) return nullptr;
            entry = entries.find(fold->second);
        }
        const std::string& exact = entry->first;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (auto found = cache.find(exact); found != cache.end()) {
                lru.splice(lru.begin(), lru, found->second.lru);
                return found->second.data;
            }
        }
        const auto& e = entry->second;
        std::string data(e.size, '\0');
        if (e.method == 0) data.assign(archive, e.offset, e.compressed);
        else {
            z_stream stream {};
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return nullptr;
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(archive.data() + e.offset));
            stream.avail_in = e.compressed;
            char empty = 0;
            stream.next_out = reinterpret_cast<Bytef*>(data.empty() ? &empty : data.data());
            stream.avail_out = data.empty() ? 1 : e.size;
            int result = inflate(&stream, Z_FINISH);
            bool ok = result == Z_STREAM_END && stream.total_out == e.size && stream.total_in == e.compressed;
            inflateEnd(&stream);
            if (!ok) return nullptr;
        }
        if (data.size() != e.size || crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size())) != e.crc) return nullptr;
        if (!e.key.empty()) {
            std::string plain;
            if (!decrypt(e.key, data, plain)) return nullptr;
            const bool isJson = util::lowercase(exact).ends_with(".json");
            if (!isJson || json::parse(util::stripJsonComments(plain)) || !json::parse(util::stripJsonComments(data))) data = std::move(plain);
        }
        auto value = std::make_shared<const std::string>(std::move(data));
        if (value->size() <= CacheBudget) {
            std::lock_guard<std::mutex> guard(mutex);
            if (auto found = cache.find(exact); found != cache.end()) {
                lru.splice(lru.begin(), lru, found->second.lru);
                return found->second.data;
            }
            while (!lru.empty() && cachedBytes > CacheBudget - value->size()) {
                auto old = cache.find(lru.back());
                cachedBytes -= old->second.data->size();
                cache.erase(old);
                lru.pop_back();
            }
            lru.push_front(exact);
            cachedBytes += value->size();
            cache.emplace(exact, Cached { value, lru.begin() });
        }
        return value;
    }
};
PackFiles::PackFiles(std::shared_ptr<Storage> value)
    : storage(std::move(value))
{
}
std::shared_ptr<const std::string> PackFiles::find(const std::string& input) const
{
    auto path = canonical(input);
    if (path.empty()) return nullptr;
    const auto folded = util::lowercase(path);
    if (folded.starts_with("subpacks/")) return nullptr;
    if (!subPack.empty() && folded != "manifest.json" && folded != "pack_manifest.json" && folded != "contents.json") {
        if (auto data = storage->read("subpacks/" + subPack + "/" + path)) return data;
    }
    return storage->read(path);
}
std::vector<std::string> PackFiles::paths() const
{
    std::unordered_map<std::string, std::string> logical;
    const auto prefix = "subpacks/" + subPack + "/";
    for (const auto& [path, entry] : storage->entries) {
        if (!util::lowercase(path).starts_with("subpacks/")) logical[util::lowercase(path)] = path;
    }
    if (!subPack.empty()) for (const auto& [path, entry] : storage->entries) {
        if (util::lowercase(path).starts_with(util::lowercase(prefix))) {
            auto name = path.substr(prefix.size());
            if (util::lowercase(name) != "manifest.json" && util::lowercase(name) != "pack_manifest.json") logical[util::lowercase(name)] = name;
        }
    }
    std::vector<std::string> result;
    result.reserve(logical.size());
    for (const auto& [folded, name] : logical) result.push_back(folded);
    std::sort(result.begin(), result.end());
    return result;
}
std::shared_ptr<const PackFiles> PackFiles::withSubPack(const std::string& name) const
{
    auto pack = std::make_shared<PackFiles>(storage);
    if (!name.empty() && name.find('/') == std::string::npos && name.find('\\') == std::string::npos && canonical(name) == name) pack->subPack = name;
    return pack;
}
size_t PackFiles::archiveBytes() const
{
    return storage->archive.size();
}
uint64_t PackFiles::expandedBytes() const
{
    return storage->expanded;
}
std::shared_ptr<const PackFiles> loadServerPack(const std::filesystem::path& path, const std::string& key, std::string& error)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > MaxArchive) { error = "missing or oversized resource pack archive"; return nullptr; }
    std::ifstream file(path, std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!file || !file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) { error = "cannot read resource pack archive"; return nullptr; }
    return loadServerPackData(std::move(bytes), key, error);
}
std::shared_ptr<const PackFiles> loadServerPackData(std::string bytes, const std::string& key, std::string& error)
{
    auto reject = [&](const char* message) -> std::shared_ptr<const PackFiles> { error = message; return nullptr; };
    if (bytes.size() < 22 || bytes.size() > MaxArchive) return reject("invalid resource pack archive size");
    size_t end = std::string::npos;
    const size_t first = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
    for (size_t pos = bytes.size() - 22 + 1; pos-- > first;) {
        if (readLe32(bytes, pos) == 0x06054b50u && pos + 22 + readLe16(bytes, pos + 20) == bytes.size()) { end = pos; break; }
    }
    if (end == std::string::npos || readLe16(bytes, end + 4) || readLe16(bytes, end + 6)) return reject("unsupported ZIP framing");
    auto storage = std::make_shared<PackFiles::Storage>();
    size_t cursor = readLe32(bytes, end + 16);
    const size_t directoryStart = cursor;
    const size_t directoryEnd = cursor + readLe32(bytes, end + 12);
    const auto count = readLe16(bytes, end + 10);
    if (directoryEnd != end || readLe16(bytes, end + 8) != count) return reject("invalid ZIP directory bounds");
    std::unordered_map<std::string, PackFiles::Storage::Entry> physical;
    std::string prefix;
    size_t best = std::string::npos;
    for (uint32_t i = 0; i < count; ++i) {
        if (cursor > directoryEnd || directoryEnd - cursor < 46 || readLe32(bytes, cursor) != 0x02014b50u) return reject("corrupt ZIP directory");
        const auto method = readLe16(bytes, cursor + 10);
        const auto compressed = readLe32(bytes, cursor + 20), size = readLe32(bytes, cursor + 24), crc = readLe32(bytes, cursor + 16);
        const auto nameSize = readLe16(bytes, cursor + 28);
        const size_t recordSize = 46ull + nameSize + readLe16(bytes, cursor + 30) + readLe16(bytes, cursor + 32);
        const size_t local = readLe32(bytes, cursor + 42);
        const auto flags = readLe16(bytes, cursor + 8);
        if (recordSize > directoryEnd - cursor) return reject("truncated ZIP directory");
        auto name = canonical(bytes.substr(cursor + 46, nameSize));
        cursor += recordSize;
        if (name.empty() || name.back() == '/') continue;
        if (size > MaxFile || storage->expanded > MaxExpanded - size) return reject("resource pack expansion exceeds limit");
        storage->expanded += size;
        if (local > bytes.size() || bytes.size() - local < 30 || readLe32(bytes, local) != 0x04034b50u) return reject("invalid ZIP local entry");
        const auto localNameSize = readLe16(bytes, local + 26);
        const size_t offset = local + 30ull + localNameSize + readLe16(bytes, local + 28);
        if (offset > bytes.size() || readLe16(bytes, local + 8) != method || readLe16(bytes, local + 6) != flags
            || canonical(bytes.substr(local + 30, localNameSize)) != name) return reject("ZIP local and directory entries disagree");
        if (offset > directoryStart || compressed > directoryStart - offset || (flags & 1) || (method != 0 && method != 8) || (method == 0 && size != compressed)) return reject("invalid or unsupported ZIP entry");
        const auto folded = util::lowercase(name);
        if (folded.ends_with("manifest.json") || folded.ends_with("pack_manifest.json")) {
            const auto slash = name.find_last_of('/');
            const auto root = slash == std::string::npos ? std::string() : name.substr(0, slash + 1);
            const auto filename = slash == std::string::npos ? folded : folded.substr(slash + 1);
            if ((filename == "manifest.json" || filename == "pack_manifest.json") && root.size() < best) { prefix = root; best = root.size(); }
        }
        if (!physical.emplace(name, PackFiles::Storage::Entry { offset, compressed, size, crc, method, {} }).second) return reject("duplicate ZIP path");
    }
    if (cursor != directoryEnd) return reject("invalid ZIP directory length");
    storage->archive = std::move(bytes);
    for (auto& [name, entry] : physical) if (name.starts_with(prefix)) {
        const auto relative = name.substr(prefix.size());
        auto [fold, inserted] = storage->folded.emplace(util::lowercase(relative), relative);
        if (!inserted && relative < fold->second) fold->second = relative;
        storage->entries.emplace(relative, std::move(entry));
    }
    auto pack = std::make_shared<PackFiles>(storage);
    auto manifest = pack->find("manifest.json");
    if (!manifest) manifest = pack->find("pack_manifest.json");
    const auto document = manifest ? json::parse(*manifest) : nullptr;
    if (!document || !document->get("header")) return reject("missing or invalid pack manifest");
    const auto contents = pack->find("contents.json");
    if (!contents || contents->size() < 0x100 || readLe32(*contents, 4) != 0x9BCFB9FCu) return pack;
    std::string decrypted;
    if (!decrypt(key, contents->substr(0x100), decrypted)) return reject("missing or invalid content key");
    auto index = json::parse(decrypted);
    const auto* entries = index ? index->get("content") : nullptr;
    if (!entries || !entries->isArray()) return reject("could not decrypt contents.json");
    for (const auto& item : entries->mArray) {
        const auto* path = item->get("path");
        const auto* entryKey = item->get("key");
        if (!path || !entryKey || !path->isString() || !entryKey->isString() || entryKey->string().empty()) continue;
        auto fold = storage->folded.find(util::lowercase(canonical(path->string())));
        if (fold == storage->folded.end() || entryKey->string().size() != 32) return reject("invalid encrypted pack entry");
        storage->entries.at(fold->second).key = entryKey->string();
    }
    storage->cache.clear();
    storage->lru.clear();
    storage->cachedBytes = 0;
    return pack;
}
}
