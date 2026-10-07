#include "ui/Skin.h"

#include <cstdlib>
#include <iostream>

using namespace kestrel;

void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

ui::Bitmap solid(uint8_t value)
{
    return { 1024, 1024, std::vector<uint8_t>(1024 * 1024 * 4, value) };
}

int main()
{
    ui::GameAssets assets;
    ui::Skin skin(assets);
    for (int i = 0; i < 4; ++i) skin.setDynamic("mod-image:" + std::to_string(i), solid(static_cast<uint8_t>(50 + i)));
    std::vector<uint8_t> pixels(size_t(ui::Skin::AtlasSize) * ui::Skin::AtlasSize * 4, 0);
    skin.pack(pixels);
    const auto image = skin.sprite("mod-image:0").image;
    float width = (image.u1 - image.u0) * ui::Skin::AtlasSize;
    check(image.valid && width < 1024, "synthetic textures force atlas downsampling");
    skin.setDynamic("mod-image:0", solid(200));
    auto regions = skin.pack(pixels);
    check(regions.size() == 1 && regions[0].width == static_cast<uint32_t>(width) + 2, "update preserves downsampled slot bounds");
    const auto updated = skin.sprite("mod-image:0").image;
    check(updated.valid && updated.u0 == image.u0 && updated.u1 == image.u1, "same-size update keeps UVs stable");
    for (int i = 0; i < 4; ++i) {
        auto ref = skin.sprite("mod-image:" + std::to_string(i)).image;
        auto x = static_cast<uint32_t>(ref.u0 * ui::Skin::AtlasSize);
        auto y = static_cast<uint32_t>(ref.v0 * ui::Skin::AtlasSize);
        check(pixels[(size_t(y) * ui::Skin::AtlasSize + x) * 4] == (i ? 50 + i : 200), "update preserves neighbouring textures");
    }
    skin.clearDynamic("mod-image:0");
    check(!skin.sprite("mod-image:0").valid, "destroyed sprite cannot be drawn");
}
