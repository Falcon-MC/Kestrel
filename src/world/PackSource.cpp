#include "world/PackSource.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace kestrel::world {

namespace {

namespace fs = std::filesystem;

constexpr uint64_t ArchiveMagic = 0x267052A0B125277Dull;
constexpr size_t ArchiveHeaderSize = 16;
constexpr size_t ArchiveEntrySize = 256;
constexpr size_t ArchiveNameCapacity = 247;

bool readFile(const fs::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

uint32_t readLe32(const std::string& data, size_t offset)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data() + offset);
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

/**
 * The numbers of a version folder name such as 1.26.51.1, so versions sort
 * by value: 1.26 is newer than 1.9.
 */
std::vector<uint64_t> versionNumbers(const std::string& name)
{
    std::vector<uint64_t> numbers;
    uint64_t current = 0;
    bool inNumber = false;
    for (char c : name) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + uint64_t(c - '0');
            inNumber = true;
        } else if (inNumber) {
            numbers.push_back(current);
            current = 0;
            inNumber = false;
        }
    }
    if (inNumber) {
        numbers.push_back(current);
    }
    return numbers;
}

}

fs::path PackSource::locateVanilla()
{
    if (const char* overridePath = std::getenv("KESTREL_VANILLA_PACK"); overridePath && *overridePath) {
        return overridePath;
    }
    std::error_code error;
#if defined(_WIN32)
    for (char drive = 'C'; drive <= 'Z'; ++drive) {
        fs::path candidate = std::string(1, drive) + ":/XboxGames/Minecraft for Windows/Content/data/resource_packs/vanilla";
        if (fs::exists(candidate / "blocks.json", error)) {
            return candidate;
        }
    }
#else
    if (const char* home = std::getenv("HOME")) {
        // The Flatpak launcher keeps its data under ~/.var/app and extracts the APK one level deeper.
        const fs::path launcherRoots[] = {
            fs::path(home) / ".local/share/mcpelauncher/versions",
            fs::path(home) / ".var/app/io.mrarm.mcpelauncher/data/mcpelauncher/versions",
        };
        fs::path newest;
        fs::path newestVersion;
        for (const fs::path& root : launcherRoots) {
            if (!fs::is_directory(root, error)) {
                continue;
            }
            for (const fs::directory_entry& version : fs::directory_iterator(root, error)) {
                for (const char* layout : { "assets/resource_packs/vanilla", "assets/assets/resource_packs/vanilla" }) {
                    fs::path pack = version.path() / layout;
                    if (fs::exists(pack / "blocks.json", error) && (newest.empty() || versionNumbers(version.path().filename().string()) > versionNumbers(newestVersion.string()))) {
                        newest = pack;
                        newestVersion = version.path().filename();
                    }
                }
            }
        }
        if (!newest.empty()) {
            return newest;
        }
    }
#endif
    return {};
}

PackSource::PackSource(fs::path root)
    : base(std::move(root))
{
    std::vector<std::pair<std::vector<int>, fs::path>> versioned;
    std::error_code error;
    std::string baseName = base.filename().string();
    for (const fs::directory_entry& entry : fs::directory_iterator(base.parent_path(), error)) {
        std::string name = entry.path().filename().string();
        if (!entry.is_directory(error) || name.size() <= baseName.size() + 1 || name.compare(0, baseName.size() + 1, baseName + "_") != 0) {
            continue;
        }
        std::vector<int> version;
        std::string part;
        bool numeric = true;
        for (size_t i = baseName.size() + 1; i <= name.size(); ++i) {
            if (i == name.size() || name[i] == '.') {
                if (part.empty()) {
                    numeric = false;
                    break;
                }
                version.push_back(std::stoi(part));
                part.clear();
            } else if (name[i] >= '0' && name[i] <= '9') {
                part += name[i];
            } else {
                numeric = false;
                break;
            }
        }
        if (numeric) {
            versioned.emplace_back(std::move(version), entry.path());
        }
    }
    std::sort(versioned.begin(), versioned.end(), [](const auto& left, const auto& right) {
        return left.first > right.first;
    });
    for (auto& [version, path] : versioned) {
        stack.push_back(std::move(path));
    }
    stack.push_back(base);
}

bool PackSource::readText(const std::string& relative, std::string& out) const
{
    return readFile(base / relative, out);
}

std::vector<std::string> PackSource::readTextLayers(const std::string& relative) const
{
    std::vector<std::string> result;
    for (const std::shared_ptr<const PackFiles>& overlay : overlays) {
        if (const std::string* text = overlay->find(relative)) {
            result.push_back(*text);
        }
    }
    for (const fs::path& layer : stack) {
        std::string text;
        if (readFile(layer / relative, text)) {
            result.push_back(std::move(text));
        }
    }
    return result;
}

bool PackSource::readTexture(const std::string& texturePath, std::string& out)
{
    fs::path path(texturePath);
    for (const std::shared_ptr<const PackFiles>& overlay : overlays) {
        for (const char* extension : { ".png", ".tga" }) {
            if (const std::string* data = overlay->find(texturePath + extension)) {
                out = *data;
                return true;
            }
        }
    }
    for (const fs::path& layer : stack) {
        for (const char* extension : { ".png", ".tga" }) {
            if (readFile(layer / (texturePath + extension), out)) {
                return true;
            }
        }
        const Archive* source = archive(layer / "__brarchive" / (path.parent_path().string() + ".brarchive"));
        if (!source) {
            continue;
        }
        for (const char* extension : { ".png", ".tga" }) {
            auto found = source->entries.find(path.filename().string() + extension);
            if (found != source->entries.end()) {
                out.assign(source->data, source->dataStart + found->second.first, found->second.second);
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> PackSource::archiveEntries(const std::string& archiveName)
{
    std::vector<std::string> names;
    std::string prefix = archiveName + "/";
    for (const std::shared_ptr<const PackFiles>& overlay : overlays) {
        for (const auto& [name, data] : overlay->files) {
            if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 && name.find('/', prefix.size()) == std::string::npos) {
                std::string entry = name.substr(prefix.size());
                if (std::find(names.begin(), names.end(), entry) == names.end()) {
                    names.push_back(entry);
                }
            }
        }
    }
    for (const fs::path& layer : stack) {
        const Archive* source = archive(layer / "__brarchive" / (archiveName + ".brarchive"));
        if (!source) {
            continue;
        }
        for (const auto& [name, location] : source->entries) {
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
        }
    }
    return names;
}

bool PackSource::readArchived(const std::string& archiveName, const std::string& name, std::string& out)
{
    for (const std::shared_ptr<const PackFiles>& overlay : overlays) {
        if (const std::string* data = overlay->find(archiveName + "/" + name)) {
            out = *data;
            return true;
        }
    }
    for (const fs::path& layer : stack) {
        const Archive* source = archive(layer / "__brarchive" / (archiveName + ".brarchive"));
        if (!source) {
            continue;
        }
        auto found = source->entries.find(name);
        if (found != source->entries.end()) {
            out.assign(source->data, source->dataStart + found->second.first, found->second.second);
            return true;
        }
    }
    return false;
}

const PackSource::Archive* PackSource::archive(const fs::path& file)
{
    auto cached = archives.find(file);
    if (cached != archives.end()) {
        return cached->second.get();
    }

    auto loaded = std::make_unique<Archive>();
    uint64_t magic = 0;
    bool valid = readFile(file, loaded->data) && loaded->data.size() >= ArchiveHeaderSize;
    if (valid) {
        std::memcpy(&magic, loaded->data.data(), sizeof(magic));
        valid = magic == ArchiveMagic;
    }
    if (valid) {
        uint32_t count = readLe32(loaded->data, 8);
        loaded->dataStart = ArchiveHeaderSize + size_t(count) * ArchiveEntrySize;
        valid = loaded->data.size() >= loaded->dataStart;
        for (uint32_t i = 0; valid && i < count; ++i) {
            size_t entry = ArchiveHeaderSize + size_t(i) * ArchiveEntrySize;
            size_t nameLength = static_cast<unsigned char>(loaded->data[entry]);
            if (nameLength > ArchiveNameCapacity) {
                valid = false;
                break;
            }
            std::string name = loaded->data.substr(entry + 1, nameLength);
            size_t offset = readLe32(loaded->data, entry + 1 + ArchiveNameCapacity);
            size_t size = readLe32(loaded->data, entry + 1 + ArchiveNameCapacity + 4);
            if (loaded->dataStart + offset + size > loaded->data.size()) {
                valid = false;
                break;
            }
            loaded->entries.emplace(std::move(name), std::make_pair(offset, size));
        }
    }

    Archive* result = valid ? loaded.get() : nullptr;
    archives.emplace(file, valid ? std::move(loaded) : nullptr);
    return result;
}

}
