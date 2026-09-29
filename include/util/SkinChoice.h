#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kestrel::util {

/**
 * The classic skin the player wears: Steve, Alex or the image they imported,
 * and whether it uses the slim arms.
 */
struct SkinChoice {
    std::string kind = "steve";
    bool slim = false;
};

SkinChoice loadSkinChoice();
void saveSkinChoice(const SkinChoice& choice);
std::filesystem::path customSkinPath();

/**
 * Copies a PNG the player picked as their custom skin once it decodes to one
 * of the sizes the game takes, 64x32, 64x64 or 128x128.
 */
bool importCustomSkin(const std::string& source, std::string& error);

/**
 * The imported skin's pixels, empty when there is none or it cannot be read.
 */
bool readCustomSkin(uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba);

/**
 * One of the game's default characters: its skin file in the vanilla skin
 * pack, its name and whether it has slim arms.
 */
struct DefaultCharacter {
    std::string file;
    std::string name;
    bool slim = false;
};

/**
 * The default characters the game ships, in its order: Steve, Alex, Ari,
 * Efe, Kai, Makena, Noor, Sunny and Zuri.
 */
const std::vector<DefaultCharacter>& defaultCharacters();

/**
 * A default character's classic skin read from the game's vanilla skin pack.
 */
bool readDefaultSkin(const std::string& file, uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgba);

}
