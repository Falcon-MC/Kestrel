#include "world/ServerPack.h"
#include <openssl/evp.h>
#include <zlib.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
const std::string Key(32, 'k');
void check(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
void little(std::string& out, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i) out.push_back(static_cast<char>(value >> (i * 8)));
}
std::string encrypt(const std::string& data)
{
    auto* context = EVP_CIPHER_CTX_new();
    check(context != nullptr, "cipher context");
    std::string out(data.size() + 16, '\0');
    int written = 0;
    const auto* key = reinterpret_cast<const unsigned char*>(Key.data());
    check(EVP_EncryptInit_ex(context, EVP_aes_256_cfb8(), nullptr, key, key)
        && EVP_EncryptUpdate(context, reinterpret_cast<unsigned char*>(out.data()), &written,
            reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size())), "encrypt fixture");
    EVP_CIPHER_CTX_free(context);
    out.resize(written);
    return out;
}
struct Entry { std::string name, data; };
std::string archive(const std::vector<Entry>& entries)
{
    std::string out, directory;
    for (const auto& entry : entries) {
        const auto offset = static_cast<uint32_t>(out.size());
        const auto size = static_cast<uint32_t>(entry.data.size());
        const auto crc = static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(entry.data.data()), size));
        little(out, 0x04034b50, 4); little(out, 20, 2);
        out.append(8, '\0'); little(out, crc, 4); little(out, size, 4); little(out, size, 4);
        little(out, static_cast<uint32_t>(entry.name.size()), 2); little(out, 0, 2);
        out += entry.name; out += entry.data;
        little(directory, 0x02014b50, 4); little(directory, 20, 2); little(directory, 20, 2);
        directory.append(8, '\0'); little(directory, crc, 4); little(directory, size, 4); little(directory, size, 4);
        little(directory, static_cast<uint32_t>(entry.name.size()), 2); directory.append(12, '\0');
        little(directory, offset, 4); directory += entry.name;
    }
    const auto offset = static_cast<uint32_t>(out.size());
    out += directory;
    little(out, 0x06054b50, 4); out.append(4, '\0');
    little(out, static_cast<uint32_t>(entries.size()), 2); little(out, static_cast<uint32_t>(entries.size()), 2);
    little(out, static_cast<uint32_t>(directory.size()), 4); little(out, offset, 4); little(out, 0, 2);
    return out;
}
std::string fixture(const std::string& indexedPath, bool directory = true, const std::string& root = {},
    const std::string& entryKey = Key, const std::string& directoryData = {})
{
    std::string contents(4, '\0'); little(contents, 0x9BCFB9FC, 4); contents.resize(256, '\0');
    contents += encrypt("{\"content\":[{\"path\":\"" + indexedPath + "\",\"key\":\"" + entryKey
        + "\"},{\"path\":\"models/test.json\",\"key\":\"" + Key + "\"}]}");
    std::vector<Entry> entries {
        {root + "manifest.json", "{\"header\":{}}"},
        {root + "contents.json", contents},
        {root + "models/test.json", encrypt("{\"synthetic\":true}")}
    };
    if (directory) entries.push_back({root + "models/", directoryData});
    return archive(entries);
}
}
int main()
{
    std::string error;
    auto pack = kestrel::world::loadServerPackData(fixture("models/"), Key, error);
    check(pack != nullptr, "encrypted directory marker accepted");
    const auto file = pack->find("models/test.json");
    check(file && *file == "{\"synthetic\":true}", "file remains decrypted");
    check(!pack->find("models/"), "directory is not exposed as a file");
    check(kestrel::world::loadServerPackData(fixture("MODELS/", true, "root/"), Key, error) != nullptr,
        "root-relative and case-insensitive directory marker");
    check(!kestrel::world::loadServerPackData(fixture("missing/"), Key, error), "missing directory rejected");
    check(!kestrel::world::loadServerPackData(fixture("models/", false), Key, error), "unrecorded directory rejected");
    check(!kestrel::world::loadServerPackData(fixture("models/missing.json"), Key, error), "missing file rejected");
    check(!kestrel::world::loadServerPackData(fixture("../models/"), Key, error), "traversal rejected");
    check(!kestrel::world::loadServerPackData(fixture("/models/"), Key, error), "absolute path rejected");
    check(!kestrel::world::loadServerPackData(fixture("models/", true, {}, "short"), Key, error), "invalid key rejected");
    check(!kestrel::world::loadServerPackData(fixture("models/", true, {}, Key, "payload"), Key, error), "nonempty directory rejected");
    check(!kestrel::world::loadServerPackData(fixture("models/"), "short", error), "invalid master key rejected");
}
