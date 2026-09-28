#pragma once

#include "ui/Context.h"
#include "ui/Image.h"
#include "ui/JsonUi.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kestrel::menu {

/**
 * One inventory cell: the skin sprite of its icon, stack count and remaining
 * durability as a fraction (negative when the item has no durability bar to
 * show).
 */
struct HudSlot {
    bool filled = false;
    std::string icon;
    int32_t count = 0;
    float durability = -1.0f;
};

struct HudBossBar {
    std::string title;
    float progress = 1.0f;
    int32_t color = 0;
};

struct HudEffectView {
    int32_t id = 0;
    bool ambient = false;
    float alpha = 1.0f;
};

/**
 * A name floating over an entity, already projected: x and y are where the
 * bottom center of its last line sits on screen, and magnify how many menu
 * units one font pixel takes at that distance. Depth is the projected world
 * depth for per-pixel occlusion against terrain and entities.
 */
struct NameTag {
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
    float magnify = 1.0f;
    float depth = 0.0f;
    bool sneaking = false;
};

enum class HeartKind {
    Normal,
    Poison,
    Wither,
    Freeze,
};

/**
 * Everything the gameplay HUD draws in one frame.
 */
struct HudView {
    bool visible = false;
    bool showHotbar = true;
    bool showStats = true;
    std::array<HudSlot, 9> hotbar {};
    HudSlot offhand;
    int32_t selected = 0;
    std::string selectedName;
    float labelAlpha = 0.0f;
    float health = 20.0f;
    float maxHealth = 20.0f;
    float absorption = 0.0f;
    float hunger = 20.0f;
    int32_t armor = 0;
    float experience = 0.0f;
    int32_t level = 0;
    int32_t air = 300;
    int32_t maxAir = 300;
    HeartKind heartKind = HeartKind::Normal;
    bool heartFlash = false;
    bool hungerEffect = false;
    std::vector<HudEffectView> effects;
    std::vector<HudBossBar> bossBars;
    std::vector<NameTag> nameTags;
    bool sidebarVisible = false;
    ui::UiData sidebar;
    // The paper doll is up, which pushes the chat below it.
    bool paperDoll = false;
};

/**
 * Draws the hotbar, hearts, armor, hunger, air bubbles, experience bar,
 * selected item name and status effects over a width by height area, laid
 * out in the same logical coordinates as the rest of the interface.
 */
void drawHud(ui::Context& ui, const HudView& view, float x, float y, float width, float height);

/**
 * Draws the name tags in the order given, each line centered on a
 * translucent strip, lines of one tag stacking upward.
 */
void drawNameTags(ui::Context& ui, const std::vector<NameTag>& tags);

}
