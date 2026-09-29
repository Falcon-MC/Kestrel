#include "util/SkinChoice.h"

#include "platform/Paths.h"
#include "ui/Image.h"
#include "world/PackSource.h"

#include <fstream>
#include <iterator>

namespace kestrel::util {

namespace {

std::filesystem::path choiceFile()
{
    return platform::dataDirectory() / "skin.txt";
}

bool readBytes(const std::filesystem::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool validSize(uint32_t width, uint32_t height)
{
    return (width == 64 && (height == 32 || height == 64)) || (width == 128 && height == 128);
}

}

std::filesystem::path customSkinPath()
{
    return platform::dataDirectory() / "custom_skin.png";
}

SkinChoice loadSkinChoice()
{
    SkinChoice choice;
    std::ifstream file(choiceFile());
    std::string kind;
    std::string arms;
    if (std::getline(file, kind) && (kind == "steve" || kind == "alex" || kind == "custom" || kind.rfind("default:", 0) == 0)) {
        choice.kind = kind;
    }
    if (std::getline(file, arms)) {
        choice.slim = arms == "slim";
    }
    if (choice.kind == "alex") {
        choice.slim = true;
    }
    return choice;
}

void saveSkinChoice(const SkinChoice& choice)
{
    std::ofstream file(choiceFile(), std::ios::trunc);
    file << choice.kind << '\n' << (choice.slim ? "slim" : "wide") << '\n';
}

bool importCustomSkin(const std::string& source, std::string& error)
{
    std::string encoded;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
    if (!readBytes(std::filesystem::u8path(source), encoded) || !ui::decodeImage(encoded, width, height, rgba) || !validSize(width, height)) {
        error = "invalid";
        return false;
    }
    std::ofstream file(customSkinPath(), std::ios::binary | std::ios::trunc);
    file.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    return static_cast<bool>(file);
}

bool readCustomSkin(uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba)
{
    std::string encoded;
    return readBytes(customSkinPath(), encoded) && ui::decodeImage(encoded, width, height, rgba) && validSize(width, height);
}

const std::vector<DefaultCharacter>& defaultCharacters()
{
    static const std::vector<DefaultCharacter> characters {
        { "steve", "Steve", false },
        { "alex", "Alex", true },
        { "ari", "Ari", false },
        { "efe", "Efe", true },
        { "kai", "Kai", false },
        { "makena", "Makena", true },
        { "noor", "Noor", true },
        { "sunny", "Sunny", true },
        { "zuri", "Zuri", false },
    };
    return characters;
}

bool readDefaultSkin(const std::string& file, uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba)
{
    std::filesystem::path vanilla = world::PackSource::locateVanilla();
    if (vanilla.empty()) {
        return false;
    }
    std::string encoded;
    std::filesystem::path path = vanilla.parent_path().parent_path() / "skin_packs" / "vanilla" / (file + ".png");
    return readBytes(path, encoded) && ui::decodeImage(encoded, width, height, rgba) && validSize(width, height);
}

}
