#include "ui/FallbackArt.h"

#include <zlib.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void check(bool value, const std::string& message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

// Fingerprints of the installed 1.26.51 border images, compared without shipping any image fixture.
struct Reference { const char* name; uint32_t crc; };
const Reference references[] = {
        {"hbui/tabBar_neutral_default", 0xc4856817u},
        {"hbui/tabBar_neutral_hovered", 0xf8ae6087u},
        {"hbui/tabBar_neutral_pressed", 0xdc0a4a47u},
        {"hbui/tabBar_neutral_default_focused", 0x9807269fu},
        {"hbui/tabBar_neutral_pressed_focused", 0x2b389bcfu},
        {"hbui/pressable_elevated_primary_default", 0xcc050241u},
        {"hbui/pressable_elevated_primary_hovered", 0x89799c9eu},
        {"hbui/pressable_elevated_primary_focused", 0x5ea88464u},
        {"hbui/pressable_primary_pressed", 0x6826ecfeu},
        {"hbui/pressable_elevated_secondary_default", 0x3cbb9605u},
        {"hbui/pressable_elevated_secondary_hovered", 0xf6cc517eu},
        {"hbui/pressable_elevated_secondary_focused", 0x07cebd9bu},
        {"hbui/pressable_secondary_pressed", 0x55b51f0bu},
        {"hbui/pressable_elevated_destructive_default", 0x7c37fe5du},
        {"hbui/pressable_elevated_destructive_hovered", 0xe2cb97efu},
        {"hbui/pressable_elevated_destructive_focused", 0x41d73711u},
        {"hbui/pressable_destructive_pressed", 0x515f3ffdu},
        {"hbui/pressable_elevated_disabled", 0x0f2576f4u},
        {"hbui/baseTextField_neutral90", 0xeab6257bu},
        {"hbui/baseTextField_neutral80", 0x703184cau},
        {"hbui/baseTextField_disabled", 0x7ef9dc41u},
};
const char* icons[] = {
    "Close", "Plus", "arrowBack", "arrowUp", "arrowDown", "ArrowLeft", "ArrowRight", "ExternalLink", "search",
    "options@0.5x.icon", "gamerscore_small@0.5x.icon", "time@0.5x.icon", "pickaxe@0.5x.icon", "sword@0.5x.icon",
    "boots@0.5x.icon", "hanger@0.5x.icon", "image@0.5x.icon", "edit-image@0.5x.icon", "achievements@0.5x.icon",
    "friends", "party", "Realms", "World", "Settings", "cursor_gamepad_brackets", "accessibility", "account",
    "keyboard-mouse", "touch", "resource-packs-icon", "sound-block", "storage", "general-icon", "language", "server", "screenshot_3"
};
void dump(const std::filesystem::path& directory, std::string_view name, const kestrel::ui::Bitmap& bitmap)
{
    std::filesystem::create_directories(directory);
    std::string file(name.substr(5));
    std::ofstream output(directory / (file + ".rgba"), std::ios::binary);
    output.write(reinterpret_cast<const char*>(&bitmap.width), sizeof(bitmap.width));
    output.write(reinterpret_cast<const char*>(&bitmap.height), sizeof(bitmap.height));
    output.write(reinterpret_cast<const char*>(bitmap.rgba.data()), bitmap.rgba.size());
}
}
int main(int argc, char** argv)
{
    using namespace kestrel::ui;
    Bitmap bitmap;
    check(!drawFallbackArt("dynamic/avatar", bitmap), "dynamic artwork is not replaced");
    check(!drawFallbackArt("hbui/pressable_unknown", bitmap), "unknown button style remains missing");
    check(!drawFallbackArt("hbui/baseTextField_unknown", bitmap), "unknown text field remains missing");
    check(!drawFallbackArt("hbui/unknown", bitmap), "unknown artwork remains missing");
    for (const auto& reference : references) {
        check(drawFallbackArt(reference.name, bitmap), std::string(reference.name) + " must draw");
        check(crc32(0, bitmap.rgba.data(), bitmap.rgba.size()) == reference.crc, std::string(reference.name) + " differs from the vanilla border");
        if (argc == 2) dump(argv[1], reference.name, bitmap);
    }
    for (const auto* icon : icons) {
        std::string name = std::string("hbui/") + icon;
        check(drawFallbackArt(name, bitmap), name + " must draw");
        check(bitmap.width && bitmap.height && bitmap.rgba.size() == bitmap.width * bitmap.height * 4, name + " dimensions");
        bool visible = false;
        for (size_t i = 3; i < bitmap.rgba.size(); i += 4) visible |= bitmap.rgba[i] != 0;
        check(visible, name + " is empty");
        if (argc == 2) dump(argv[1], name, bitmap);
    }
    for (const auto* component : {"tabBarNeutral", "pressableElevatedPrimary", "pressableElevatedSecondary", "pressableElevatedDestructive", "baseTextField"}) {
        for (const auto* state : {"Default", "Hovered", "Pressed", "Focused", "Disabled"}) {
            auto border = fallbackBorder(component, state);
            check(border.valid && border.fill && drawFallbackArt(border.sprite, bitmap), std::string(component) + " has no fallback border");
            check(border.slice.left + border.slice.right < bitmap.width && border.slice.top + border.slice.bottom < bitmap.height,
                std::string(component) + " invalid nine-slice bounds");
        }
    }
    check(!fallbackBorder("unknown", "Default").valid, "unknown component stays missing");
}
