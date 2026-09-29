#pragma once

#include "ui/GameAssets.h"
#include "ui/Image.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kestrel::ui {

struct Sprite {
    ImageRef image;
    float width = 0.0f;
    float height = 0.0f;
    NineSlice slice;
    NineSlice texels;
    bool valid = false;
};

// A CSS border-image as the HTML menus declare it: slices in source texels, widths and
// outset already converted to menu units.
struct BorderImage {
    std::string sprite;
    NineSlice slice;
    NineSlice width;
    NineSlice outset;
    bool fill = false;
    bool valid = false;
};

/**
 * Owns every picture the menus draw and packs them into the bottom half of the UI atlas,
 * below the rows the font rasterizer uses. Sprites are named "ui/<file>" for classic
 * textures, "hbui/<file>" for the HTML menus and "font/<file>" for bitmap glyph pages.
 * Anything asked for that isn't loaded yet gets read on the spot and is added after the
 * pictures already placed, which keep their place so a frame drawn before the upload
 * still samples the right texels.
 */
class Skin {
public:
    static constexpr uint32_t AtlasSize = 4096;
    static constexpr uint32_t ImageTop = 2048;

    explicit Skin(GameAssets& assets);

    const Sprite& sprite(std::string_view name);
    const Bitmap* bitmap(std::string_view name);
    std::string_view themeValue(std::string_view name) const;
    float themeLength(std::string_view name, float fallback = 0.0f) const;
    const BorderImage& border(std::string_view component, std::string_view state);
    void setDynamic(const std::string& name, Bitmap bitmap, NineSlice slice = {});
    void clearDynamic(const std::string& name);

    bool dirty() const
    {
        return changed;
    }

    void pack(std::vector<uint8_t>& atlasRgba);

private:
    struct Entry {
        Bitmap bitmap;
        Sprite sprite;
        bool placed = false;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t slotWidth = 0;
        uint32_t slotHeight = 0;
        uint64_t lastUse = 0;
    };

    /**
     * A rectangle of the atlas a picture used to hold, free for another one
     * that fits inside it.
     */
    struct FreeSlot {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    Entry& load(std::string_view name);
    bool place(Entry& entry);
    bool placeInFreeSlot(Entry& entry);
    void release(Entry& entry);
    bool evictFor(Entry& entry);
    void repackAll();

    GameAssets& assets;
    std::unordered_map<std::string, Entry> entries;
    std::unordered_map<std::string, std::string> theme;
    std::unordered_map<std::string, BorderImage> borders;
    bool changed = false;
    uint32_t cursorX = 0;
    uint32_t cursorY = ImageTop;
    uint32_t shelfHeight = 0;
    std::vector<FreeSlot> freeSlots;
    uint64_t useClock = 1;
};

}
