#include "world/PackSource.h"

#include "util/Bytes.h"
#include "util/Text.h"

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

using util::readLe32;
using util::versionNumbers;

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
        if (auto text = overlay->find(relative)) {
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
    for (const char* extension : { ".png", ".tga", ".PNG", ".TGA" }) {
        if (texturePath.ends_with(extension)) {
            return readTexture(texturePath.substr(0, texturePath.size() - 4), out);
        }
    }
    fs::path path(texturePath);
    for (const std::shared_ptr<const PackFiles>& overlay : overlays) {
        for (const char* extension : { ".png", ".tga" }) {
            if (auto data = overlay->find(texturePath + extension)) {
                out = *data;
                return true;
            }
        }
    }
    // Paths the index would spell differently go straight to the disk.
    bool indexed = texturePath.find('\\') == std::string::npos && texturePath.find("//") == std::string::npos && texturePath.find("./") == std::string::npos;
    fs::path parent = path.parent_path();
    std::string folder = parent.string();
    std::string name = path.filename().string();
    const std::vector<size_t>* candidates = indexed ? &layersWith(parent.generic_string()) : nullptr;
    size_t count = candidates ? candidates->size() : stack.size();
    for (size_t i = 0; i < count; ++i) {
        size_t layer = candidates ? (*candidates)[i] : i;
        for (const char* extension : { ".png", ".tga" }) {
            std::string relative = texturePath + extension;
            if ((!indexed || looseFiles(layer).files.contains(relative)) && readFile(stack[layer] / relative, out)) {
                return true;
            }
        }
        const Archive* source = archive(layer, folder);
        if (!source) {
            continue;
        }
        for (const char* extension : { ".png", ".tga" }) {
            if (const std::pair<size_t, size_t>* found = source->find(name + extension)) {
                out.assign(source->data, source->dataStart + found->first, found->second);
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
        for (const auto& name : overlay->paths()) {
            if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 && name.find('/', prefix.size()) == std::string::npos) {
                std::string entry = name.substr(prefix.size());
                if (std::find(names.begin(), names.end(), entry) == names.end()) {
                    names.push_back(entry);
                }
            }
        }
    }
    for (size_t layer = 0; layer < stack.size(); ++layer) {
        const Archive* source = archive(layer, archiveName);
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
        if (auto data = overlay->find(archiveName + "/" + name)) {
            out = *data;
            return true;
        }
    }
    return readBaseArchived(archiveName, name, out);
}

bool PackSource::readBaseArchived(const std::string& archiveName, const std::string& name, std::string& out)
{
    for (size_t layer = 0; layer < stack.size(); ++layer) {
        const Archive* source = archive(layer, archiveName);
        if (!source) {
            continue;
        }
        if (const std::pair<size_t, size_t>* found = source->find(name)) {
            out.assign(source->data, source->dataStart + found->first, found->second);
            return true;
        }
    }
    return false;
}

const std::pair<size_t, size_t>* PackSource::Archive::find(const std::string& name) const
{
    auto exact = entries.find(name);
    if (exact != entries.end()) {
        return &exact->second;
    }
    auto folding = folded.find(util::lowercase(name));
    if (folding == folded.end()) {
        return nullptr;
    }
    auto original = entries.find(folding->second);
    return original != entries.end() ? &original->second : nullptr;
}

std::vector<std::string> PackSource::readArchivedLayers(const std::string& archiveName, const std::string& name)
{
    std::vector<std::string> result;
    for (const std::shared_ptr<const PackFiles>& overlay : overlays) {
        if (auto data = overlay->find(archiveName + "/" + name)) {
            result.push_back(*data);
        }
    }
    for (size_t layer = 0; layer < stack.size(); ++layer) {
        const Archive* source = archive(layer, archiveName);
        if (!source) {
            continue;
        }
        if (const std::pair<size_t, size_t>* found = source->find(name)) {
            result.emplace_back(source->data, source->dataStart + found->first, found->second);
        }
    }
    return result;
}

/**
 * The files of one layer outside its archives, relative to the layer. Probing
 * each of the dozens of versioned vanilla layers on disk for every texture
 * used to cost more than decoding the textures themselves.
 */
const PackSource::LooseFiles& PackSource::looseFiles(size_t layer)
{
    looseIndex.resize(stack.size());
    std::optional<LooseFiles>& index = looseIndex[layer];
    if (index) {
        return *index;
    }
    index.emplace();
    std::error_code error;
    const fs::path& root = stack[layer];
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (it->path().filename() == "__brarchive") {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code status;
        if (it->is_regular_file(status)) {
            fs::path relative = it->path().lexically_relative(root);
            index->files.insert(relative.generic_string());
            index->folders.insert(relative.parent_path().generic_string());
        }
    }
    return *index;
}

/**
 * The layers, in priority order, that hold loose files in the folder or an
 * archive of it; the rest cannot have any texture from there.
 */
const std::vector<size_t>& PackSource::layersWith(const std::string& folder)
{
    auto found = folderLayers.find(folder);
    if (found != folderLayers.end()) {
        return found->second;
    }
    std::vector<size_t> layers;
    for (size_t layer = 0; layer < stack.size(); ++layer) {
        if (looseFiles(layer).folders.contains(folder) || archive(layer, folder)) {
            layers.push_back(layer);
        }
    }
    return folderLayers.emplace(folder, std::move(layers)).first->second;
}

const PackSource::Archive* PackSource::archive(size_t layer, const std::string& name)
{
    std::string key = std::to_string(layer) + ':' + name;
    auto cached = archives.find(key);
    if (cached != archives.end()) {
        return cached->second.get();
    }

    auto loaded = std::make_unique<Archive>();
    uint64_t magic = 0;
    bool valid = readFile(stack[layer] / "__brarchive" / (name + ".brarchive"), loaded->data) && loaded->data.size() >= ArchiveHeaderSize;
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
            loaded->folded.emplace(util::lowercase(name), name);
            loaded->entries.emplace(std::move(name), std::make_pair(offset, size));
        }
    }

    Archive* result = valid ? loaded.get() : nullptr;
    archives.emplace(std::move(key), valid ? std::move(loaded) : nullptr);
    return result;
}

}
