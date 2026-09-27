#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::world {
class PackSource;
}

namespace kestrel::ui {

struct Bitmap {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

struct NineSlice {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

/**
 * Reads menu art and fonts out of the installed game. Classic screens live in the vanilla
 * resource pack, the newer HTML based ones ship their files under gui/dist/hbui with a
 * content hash glued onto every name.
 */
class GameAssets {
public:
    GameAssets();
    ~GameAssets();

    bool available() const
    {
        return pack != nullptr;
    }

    bool readTexture(const std::string& path, Bitmap& out, NineSlice* slice = nullptr, NineSlice* texels = nullptr);
    bool readArchived(const std::string& archive, const std::string& name, std::string& out);
    bool readHbuiImage(std::string_view name, Bitmap& out);
    std::vector<unsigned char> readHbuiFont(std::string_view name);
    std::string readHbuiText(std::string_view name);
    std::vector<unsigned char> readPackFile(const std::string& relative);

private:
    std::filesystem::path findHashed(const std::filesystem::path& directory, std::string_view name) const;

    std::filesystem::path hbui;
    std::unique_ptr<world::PackSource> pack;
};

bool decodeBitmap(const std::string& encoded, Bitmap& out);

/**
 * Reads the nineslice_size of a texture's .json companion into slice.
 */
void readNineSlice(const std::string& json, NineSlice& slice);

/**
 * The bitmap box filtered down to width, keeping its aspect; unchanged when
 * it is already that narrow.
 */
Bitmap shrinkBitmap(const Bitmap& source, uint32_t width);

}
