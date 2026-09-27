#pragma once

#include "ui/Context.h"
#include "ui/Image.h"

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

struct HudEffectView {
    int32_t id = 0;
    bool ambient = false;
    float alpha = 1.0f;
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
};

/**
 * Draws the hotbar, hearts, armor, hunger, air bubbles, experience bar,
 * selected item name and status effects over a width by height area, laid
 * out in GUI pixels scaled by the classic GUI scale rule.
 */
void drawHud(ui::Context& ui, const HudView& view, float x, float y, float width, float height);

}
