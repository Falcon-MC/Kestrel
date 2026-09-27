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

void readSlice(const std::string& text, NineSlice& slice, float* baseWidth = nullptr, float* baseHeight = nullptr)
{
    std::unique_ptr<json::Value> root = json::parse(text);
    const json::Value* size = root ? root->get("nineslice_size") : nullptr;
    if (!size) {
        return;
    }
    const json::Value* base = root->get("base_size");
    if (base && base->isArray() && base->mArray.size() == 2 && baseWidth && baseHeight) {
        *baseWidth = static_cast<float>(base->mArray[0]->number());
        *baseHeight = static_cast<float>(base->mArray[1]->number());
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

bool GameAssets::readTexture(const std::string& path, Bitmap& out, NineSlice* slice, NineSlice* texels)
{
    std::string encoded;
    if (!pack || !pack->readTexture(path, encoded) || !decodeBitmap(encoded, out)) {
        return false;
    }
    if (slice) {
        *slice = {};
        float baseWidth = 0.0f;
        float baseHeight = 0.0f;
        std::string text;
        fs::path source(path);
        if (pack->readText(path + ".json", text) || pack->readArchived(source.parent_path().generic_string(), source.filename().string() + ".json", text)) {
            readSlice(text, *slice, &baseWidth, &baseHeight);
        }
        if (texels) {
            // nineslice_size counts base_size pixels, so a 2x texture has twice as many texels per slice.
            float sx = baseWidth > 0.0f ? static_cast<float>(out.width) / baseWidth : 1.0f;
            float sy = baseHeight > 0.0f ? static_cast<float>(out.height) / baseHeight : 1.0f;
            *texels = { slice->left * sx, slice->top * sy, slice->right * sx, slice->bottom * sy };
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
