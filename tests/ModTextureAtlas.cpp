#include "ui/Skin.h"
#include "world/PackSource.h"

#include <chrono>
#include <filesystem>
#include <fstream>

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
    auto temporary = std::filesystem::temp_directory_path() / ("kestrel-loose-texture-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary / "vanilla/textures/ui");
    std::ofstream(temporary / "vanilla/textures/ui/friendsicon.png") << "synthetic texture";
    world::PackSource pack(temporary / "vanilla");
    pack.indexInBackground();
    std::string encoded;
    check(pack.readTexture("textures/ui/FriendsIcon", encoded) && encoded == "synthetic texture", "downloaded lowercase texture names resolve mixed-case UI references");
    std::filesystem::remove_all(temporary);
    ui::GameAssets assets;
    ui::Skin skin(assets);
    check(skin.border("tabBarNeutral", "Pressed").valid, "missing stylesheet gets a procedural tab border");
    const auto* tab = skin.bitmap("hbui/tabBar_neutral_pressed");
    check(tab && tab->width == 5 && tab->height == 5, "missing native tab image gets a procedural bitmap");
    const auto* back = skin.bitmap("hbui/arrowBack");
    check(back && back->width == 8 && back->height == 16, "missing native icon gets procedural art");
    skin.setDynamic("hbui/arrowBack", {2, 2, std::vector<uint8_t>(16, 123)});
    check(skin.bitmap("hbui/arrowBack")->width == 2, "provided art wins over a fallback");
    skin.clearDynamic("hbui/arrowBack");
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
