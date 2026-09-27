#include "ui/GameAssets.h"

#include "ui/Image.h"
#include "world/PackSource.h"

#include "Core/Json/Json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace kestrel::ui {

namespace {

namespace fs = std::filesystem;

std::vector<unsigned char> readFile(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool isHash(std::string_view text)
{
    return text.size() >= 8 && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isxdigit(static_cast<unsigned char>(c)) != 0;
    });
}

void readSlice(const std::string& text, NineSlice& slice)
{
    std::unique_ptr<json::Value> root = json::parse(text);
    const json::Value* size = root ? root->get("nineslice_size") : nullptr;
    if (!size) {
        return;
    }
    if (size->isNumber()) {
        float value = static_cast<float>(size->number());
        slice = { value, value, value, value };
    } else if (size->isArray() && size->mArray.size() == 4) {
        slice = {
            static_cast<float>(size->mArray[0]->number()),
            static_cast<float>(size->mArray[1]->number()),
            static_cast<float>(size->mArray[2]->number()),
            static_cast<float>(size->mArray[3]->number()),
        };
    }
}

}

void readNineSlice(const std::string& json, NineSlice& slice)
{
    readSlice(json, slice);
}

bool decodeBitmap(const std::string& encoded, Bitmap& out)
{
    return decodeImage(encoded, out.width, out.height, out.rgba);
}

GameAssets::GameAssets()
{
    fs::path vanilla = world::PackSource::locateVanilla();
    std::error_code error;
    if (vanilla.empty() || !fs::is_directory(vanilla, error)) {
        return;
    }
    pack = std::make_unique<world::PackSource>(vanilla);
    hbui = vanilla.parent_path().parent_path() / "gui" / "dist" / "hbui";
}

GameAssets::~GameAssets() = default;

bool GameAssets::readTexture(const std::string& path, Bitmap& out, NineSlice* slice)
{
    std::string encoded;
    if (!pack || !pack->readTexture(path, encoded) || !decodeBitmap(encoded, out)) {
        return false;
    }
    if (slice) {
        *slice = {};
        std::string text;
        fs::path source(path);
        if (pack->readText(path + ".json", text) || pack->readArchived(source.parent_path().generic_string(), source.filename().string() + ".json", text)) {
            readSlice(text, *slice);
        }
    }
    return true;
}

bool GameAssets::readArchived(const std::string& archive, const std::string& name, std::string& out)
{
    return pack && pack->readArchived(archive, name, out);
}

std::vector<unsigned char> GameAssets::readPackFile(const std::string& relative)
{
    if (!pack) {
        return {};
    }
    return readFile(pack->root() / relative);
}

fs::path GameAssets::findHashed(const fs::path& directory, std::string_view name) const
{
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        std::string stem = entry.path().stem().string();
        if (stem.size() > name.size() + 1 && stem.compare(0, name.size(), name) == 0 && stem[name.size()] == '-' && isHash(std::string_view(stem).substr(name.size() + 1))) {
            return entry.path();
        }
    }
    return {};
}

bool GameAssets::readHbuiImage(std::string_view name, Bitmap& out)
{
    fs::path file = findHashed(hbui / "assets", name);
    if (file.empty()) {
        return false;
    }
    std::vector<unsigned char> data = readFile(file);
    return !data.empty() && decodeBitmap(std::string(data.begin(), data.end()), out);
}

std::vector<unsigned char> GameAssets::readHbuiFont(std::string_view name)
{
    fs::path file = findHashed(hbui / "fonts", name);
    return file.empty() ? std::vector<unsigned char> {} : readFile(file);
}

std::string GameAssets::readHbuiText(std::string_view name)
{
    fs::path file = findHashed(hbui, name);
    std::vector<unsigned char> data = file.empty() ? std::vector<unsigned char> {} : readFile(file);
    return std::string(data.begin(), data.end());
}

}
