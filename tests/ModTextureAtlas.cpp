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

void vanillaPath(const std::string& path)
{
#if defined(_WIN32)
    _putenv_s("KESTREL_VANILLA_PACK", path.c_str());
#else
    setenv("KESTREL_VANILLA_PACK", path.c_str(), 1);
#endif
}

int main()
{
    auto temporary = std::filesystem::temp_directory_path() / ("kestrel-loose-texture-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary / "vanilla/textures/ui");
    std::filesystem::create_directories(temporary / "vanilla/textures/items/candles");
    std::filesystem::create_directories(temporary / "vanilla/ui");
    std::filesystem::create_directories(temporary / "vanilla/models/entity");
    std::filesystem::create_directories(temporary / "vanilla_1.2/ui");
    std::ofstream(temporary / "vanilla/textures/ui/friendsicon.png") << "synthetic texture";
    std::ofstream(temporary / "vanilla/textures/items/test_sword.png") << "synthetic item";
    std::ofstream(temporary / "vanilla/textures/items/candles/test.png") << "synthetic candle";
    std::ofstream(temporary / "vanilla/ui/hud_screen.json") << "base HUD";
    std::ofstream(temporary / "vanilla_1.2/ui/hud_screen.json") << "updated HUD";
    std::ofstream(temporary / "vanilla/models/entity/test.geo.json") << "synthetic model";
    std::ofstream(temporary / "vanilla/textures/item_texture.json") << "synthetic atlas";
    world::PackSource pack(temporary / "vanilla");
    pack.indexInBackground();
    std::string encoded;
    check(pack.readTexture("textures/ui/FriendsIcon", encoded) && encoded == "synthetic texture", "downloaded lowercase texture names resolve mixed-case UI references");
    auto items = pack.archiveEntries("textures/items");
    check(items.size() == 1 && items.front() == "test_sword.png", "downloaded item textures are enumerated without including subdirectories");
    check(pack.archiveEntries("textures/items/candles").size() == 1, "nested downloaded item folders are enumerated separately");
    check(pack.readArchived("models/entity", "test.geo.json", encoded) && encoded == "synthetic model", "downloaded entity models load through the same interface as installed archives");
    check(pack.readBaseArchived("ui", "HUD_SCREEN.json", encoded) && encoded == "updated HUD", "downloaded UI resolves lowercase names and prioritizes newer layers");
    auto hudLayers = pack.readArchivedLayers("ui", "hud_screen.json");
    check(hudLayers == std::vector<std::string>{"updated HUD", "base HUD"}, "downloaded UI inheritance sees every layer in priority order");
    check(pack.archiveEntries("ui").size() == 1, "shared definitions are enumerated once across loose layers");
    check(pack.readArchivedLayers("textures", "item_texture.json") == std::vector<std::string>{"synthetic atlas"}, "loose item atlases load once through the unified interface");

    std::string originalPack = std::getenv("KESTREL_VANILLA_PACK") ? std::getenv("KESTREL_VANILLA_PACK") : "";
    std::string png = ui::encodePng(std::vector<uint8_t>(4 * 4 * 4, 255), 4, 4);
    std::ofstream(temporary / "vanilla/textures/ui/white.png", std::ios::binary) << png;
    std::ofstream(temporary / "vanilla/textures/ui/generic.png", std::ios::binary) << png;
    vanillaPath((temporary / "vanilla").string());
    {
        ui::GameAssets downloaded;
        ui::Bitmap bitmap;
        ui::NineSlice slice;
        check(downloaded.readTexture("textures/ui/White", bitmap, &slice) && slice.declared && slice.left == 0, "missing White description stretches the scoreboard fill without adding an asset");
        check(downloaded.readTexture("textures/ui/generic", bitmap, &slice) && !slice.declared, "ordinary images retain aspect ratio without a description");
    }
    std::ofstream(temporary / "vanilla/textures/ui/white.json") << R"({ /* synthetic pack metadata */ "nineslice_size": 2, "base_size": [4, 4], "frames": [{ "frame": {"x": 0, "y": 0, "w": 2, "h": 2}, "duration": 200 }] })";
    {
        ui::GameAssets provided;
        ui::Bitmap bitmap;
        ui::NineSlice slice;
        std::vector<ui::SpriteFrame> frames;
        check(provided.readTexture("textures/ui/White", bitmap, &slice, nullptr, &frames) && slice.declared && slice.left == 2, "provided White metadata takes precedence over stretching fallback and accepts comments");
        check(frames.size() == 1 && frames.front().width == 2 && frames.front().duration == 0.2, "commented animation descriptions retain their frames");
    }
    vanillaPath(originalPack);
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
