#include "client/WorldStore.h"

#include "util/Text.h"
#include "util/Zip.h"

#include "Core/NBT/NbtIo.h"
#include "Core/Utility/BinaryStream.h"
#include "Core/Utility/ReadOnlyBinaryStream.h"
#include "Level/Generator/Overworld/OverworldGenerator.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <random>

namespace kestrel {

namespace {

constexpr int32_t StorageVersion = 10;
constexpr size_t LevelDatHeader = 8;
constexpr size_t IdLength = 12;
constexpr uint64_t MaxImportBytes = 4ull << 30;
const char* const NameFile = "levelname.txt";
const char* const LevelDat = "level.dat";
const char* const IconFile = "world_icon.jpeg";

std::optional<std::string> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool writeFile(const std::filesystem::path& path, const std::string& data)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(file);
}

std::optional<Tag> readLevelDat(const std::filesystem::path& folder)
{
    std::optional<std::string> bytes = readFile(folder / LevelDat);
    if (!bytes || bytes->size() <= LevelDatHeader) {
        return std::nullopt;
    }
    try {
        ReadOnlyBinaryStream stream(bytes->substr(LevelDatHeader));
        Tag tag = NbtIo::readTag(stream, NbtVariant::LittleEndian);
        if (!tag.isCompound()) {
            return std::nullopt;
        }
        return tag;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool writeLevelDat(const std::filesystem::path& folder, const Tag& tag)
{
    BinaryStream body;
    NbtIo::writeTag(body, tag, NbtVariant::LittleEndian);
    const std::string& payload = body.getBuffer();
    BinaryStream file;
    file.putLInt(static_cast<uint32_t>(tag.getInt("StorageVersion", StorageVersion)));
    file.putLInt(static_cast<uint32_t>(payload.size()));
    file.put(payload);
    return writeFile(folder / LevelDat, file.getBuffer());
}

std::string trimmed(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

void applySettings(Tag& tag, const menu::WorldSettings& settings)
{
    tag.putString("LevelName", settings.name);
    tag.putInt("GameType", settings.gameType);
    tag.putInt("Difficulty", settings.difficulty);
    tag.putByte("commandsEnabled", settings.cheats ? 1 : 0);
}

uint64_t folderSize(const std::filesystem::path& folder)
{
    uint64_t total = 0;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error)) {
            total += it->file_size(error);
        }
    }
    return total;
}

}

WorldStore::WorldStore(std::filesystem::path directory)
    : root(std::move(directory))
{
}

std::filesystem::path WorldStore::folder(const std::string& id) const
{
    return root / id;
}

/**
 * Twelve letters and digits, like the folder names the game picks, and free
 * of every character the server refuses in a world name.
 */
std::string WorldStore::freeId() const
{
    static const char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device device;
    std::uniform_int_distribution<size_t> pick(0, sizeof(Alphabet) - 2);
    std::error_code error;
    for (;;) {
        std::string id;
        for (size_t i = 0; i < IdLength; ++i) {
            id.push_back(Alphabet[pick(device)]);
        }
        if (!std::filesystem::exists(folder(id), error)) {
            return id;
        }
    }
}

std::optional<menu::LocalWorld> WorldStore::find(const std::string& id) const
{
    std::filesystem::path path = folder(id);
    std::optional<Tag> tag = readLevelDat(path);
    if (!tag) {
        return std::nullopt;
    }
    menu::LocalWorld world;
    world.id = id;
    std::optional<std::string> name = readFile(path / NameFile);
    world.settings.name = name ? trimmed(*name) : tag->getString("LevelName");
    if (world.settings.name.empty()) {
        world.settings.name = tag->getString("LevelName", id);
    }
    world.settings.gameType = tag->getInt("GameType", 0);
    world.settings.difficulty = tag->getInt("Difficulty", 2);
    world.settings.cheats = tag->getByte("commandsEnabled", 0) != 0;
    world.settings.bonusChest = tag->getByte("bonusChestEnabled", 0) != 0;
    world.settings.seed = std::to_string(tag->getLong("RandomSeed", 0));
    world.lastPlayed = tag->getLong("LastPlayed", 0);
    world.size = folderSize(path);
    return world;
}

std::vector<menu::LocalWorld> WorldStore::list() const
{
    std::vector<menu::LocalWorld> worlds;
    std::error_code error;
    for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_directory(error)) {
            continue;
        }
        if (std::optional<menu::LocalWorld> world = find(it->path().filename().string())) {
            worlds.push_back(std::move(*world));
        }
    }
    std::sort(worlds.begin(), worlds.end(), [](const menu::LocalWorld& a, const menu::LocalWorld& b) {
        return a.lastPlayed != b.lastPlayed ? a.lastPlayed > b.lastPlayed : a.settings.name < b.settings.name;
    });
    return worlds;
}

std::optional<std::string> WorldStore::create(const menu::WorldSettings& settings)
{
    std::string id = freeId();
    std::filesystem::path path = folder(id);
    std::error_code error;
    if (!std::filesystem::create_directories(path, error)) {
        return std::nullopt;
    }
    std::random_device device;
    int64_t seed = settings.seed.empty() ? static_cast<int64_t>(uint64_t(device()) << 32 | device()) : OverworldGenerator::parseSeed(settings.seed);
    Tag tag = Tag::ofCompound();
    applySettings(tag, settings);
    tag.putLong("RandomSeed", seed);
    tag.putByte("bonusChestEnabled", settings.bonusChest ? 1 : 0);
    tag.putByte("bonusChestSpawned", 0);
    tag.putInt("StorageVersion", StorageVersion);
    tag.putLong("LastPlayed", static_cast<int64_t>(std::time(nullptr)));
    if (!writeLevelDat(path, tag) || !writeFile(path / NameFile, settings.name)) {
        std::filesystem::remove_all(path, error);
        return std::nullopt;
    }
    return id;
}

bool WorldStore::update(const std::string& id, const menu::WorldSettings& settings)
{
    std::filesystem::path path = folder(id);
    std::optional<Tag> tag = readLevelDat(path);
    if (!tag) {
        return false;
    }
    applySettings(*tag, settings);
    return writeLevelDat(path, *tag) && writeFile(path / NameFile, settings.name);
}

bool WorldStore::remove(const std::string& id)
{
    std::error_code error;
    std::filesystem::remove_all(folder(id), error);
    return !error;
}

std::optional<std::string> WorldStore::duplicate(const std::string& id)
{
    std::optional<menu::LocalWorld> source = find(id);
    if (!source) {
        return std::nullopt;
    }
    std::string copy = freeId();
    std::error_code error;
    std::filesystem::copy(folder(id), folder(copy), std::filesystem::copy_options::recursive, error);
    if (error) {
        std::filesystem::remove_all(folder(copy), error);
        return std::nullopt;
    }
    return copy;
}

/**
 * A .mcworld is a ZIP of the world folder, with level.dat at its root or, in
 * archives made by hand, one folder down.
 */
std::optional<std::string> WorldStore::importWorld(const std::filesystem::path& file, std::string& error)
{
    std::optional<std::string> archive = readFile(file);
    if (!archive) {
        error = "could not read the file";
        return std::nullopt;
    }
    std::vector<util::ZipEntry> entries;
    if (!util::readZip(*archive, MaxImportBytes, entries, error)) {
        return std::nullopt;
    }
    archive.reset();
    std::optional<std::string> prefix;
    for (const util::ZipEntry& entry : entries) {
        if (entry.path == LevelDat || entry.path.ends_with(std::string("/") + LevelDat)) {
            std::string candidate = entry.path.substr(0, entry.path.size() - std::string(LevelDat).size());
            if (!prefix || candidate.size() < prefix->size()) {
                prefix = candidate;
            }
        }
    }
    if (!prefix) {
        error = "the archive holds no level.dat";
        return std::nullopt;
    }
    std::string id = freeId();
    std::filesystem::path path = folder(id);
    std::error_code fileError;
    for (const util::ZipEntry& entry : entries) {
        if (!entry.path.starts_with(*prefix)) {
            continue;
        }
        std::filesystem::path target = path / util::utf8Path(entry.path.substr(prefix->size()));
        std::filesystem::create_directories(target.parent_path(), fileError);
        if (fileError || !writeFile(target, entry.data)) {
            std::filesystem::remove_all(path, fileError);
            error = "could not write the world";
            return std::nullopt;
        }
    }
    if (!find(id)) {
        std::filesystem::remove_all(path, fileError);
        error = "the world's level.dat is not readable";
        return std::nullopt;
    }
    return id;
}

bool WorldStore::exportWorld(const std::string& id, const std::filesystem::path& file, std::string& error) const
{
    std::filesystem::path path = folder(id);
    std::vector<util::ZipEntry> entries;
    std::error_code fileError;
    for (std::filesystem::recursive_directory_iterator it(path, fileError), end; !fileError && it != end; it.increment(fileError)) {
        if (!it->is_regular_file(fileError)) {
            continue;
        }
        std::optional<std::string> data = readFile(it->path());
        if (!data) {
            error = "could not read the world";
            return false;
        }
        std::u8string relative = std::filesystem::relative(it->path(), path, fileError).generic_u8string();
        entries.push_back(util::ZipEntry { std::string(relative.begin(), relative.end()), std::move(*data) });
    }
    if (fileError) {
        error = "could not read the world";
        return false;
    }
    std::string archive = util::writeZip(entries);
    if (archive.empty() || !writeFile(file, archive)) {
        error = "could not write the file";
        return false;
    }
    return true;
}

bool WorldStore::saveIcon(const std::string& id, const std::string& jpeg)
{
    std::error_code error;
    if (!std::filesystem::is_directory(folder(id), error)) {
        return false;
    }
    return writeFile(folder(id) / IconFile, jpeg);
}

std::optional<std::string> WorldStore::readIcon(const std::string& id) const
{
    return readFile(folder(id) / IconFile);
}

}
