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
    int32_t overlay = 0;
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

/**
 * Text one of the HUD's factories shows: the item name or a popup over the
 * hotbar, the tip above it or the action bar. hud_screen.json fades it in
 * and out; a new serial makes the control again, starting that over, and a
 * zero serial shows nothing. Hold is how long it stays before fading, the
 * $wait_duration of the item text.
 */
struct HudText {
    std::string text;
    uint64_t serial = 0;
    float hold = 1.0f;
};

/**
 * The title the server set, with the fade times in seconds of the title
 * packet that showed it. Every title shown gets a new serial, and a subtitle
 * arriving while it shows a new subtitleSerial, which plays the subtitle's
 * own fade in.
 */
struct HudTitle {
    std::string title;
    std::string subtitle;
    uint64_t serial = 0;
    uint64_t subtitleSerial = 0;
    bool subtitleWithTitle = false;
    float fadeIn = 0.5f;
    float stay = 3.5f;
    float fadeOut = 1.0f;
};

/**
 * The game tip over the hotbar: its text, the name of its animation under
 * textures/ui/game_tip_animations and a serial, zero when no tip shows.
 */
struct HudGameTip {
    std::string text;
    std::string animation;
    uint64_t serial = 0;
};

/**
 * A chat line the HUD shows until it fades; the serial tells lines apart so
 * each keeps its own fade.
 */
struct HudChatLine {
    std::string text;
    uint64_t serial = 0;
    // The line's $chat_text_color, as a JSON array of its channels.
    std::string color = "[1.0, 1.0, 1.0]";
};

/**
 * How the chat settings draw chat lines: the $chat_font_type, the
 * $chat_font_scale_factor and the $chat_line_spacing padding both the HUD
 * and the chat screen pass their message controls.
 */
struct ChatStyle {
    std::string fontType = "default";
    double fontScale = 1.0;
    double linePadding = 0.0;
    double lifetime = 10.0;
};

// The game's HudElement ids, as SetHud names them.
enum class HudElement : uint32_t {
    PaperDoll = 0,
    Armor = 1,
    ToolTips = 2,
    TouchControls = 3,
    Crosshair = 4,
    HotBar = 5,
    Health = 6,
    ProgressBar = 7,
    Hunger = 8,
    AirBubbles = 9,
    HorseHealth = 10,
    StatusEffects = 11,
    ItemText = 12,
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
    // The crosshair shows only through the player's own eyes.
    bool crosshair = true;
    std::array<HudSlot, 9> hotbar {};
    HudSlot offhand;
    int32_t selected = 0;
    // The item name and popups share one factory, so the newest replaces the other.
    HudText itemText;
    bool jukebox = false;
    HudText tip;
    HudText actionbar;
    HudTitle title;
    HudGameTip gameTip;
    std::vector<HudChatLine> chat;
    ChatStyle chatStyle;
    float health = 20.0f;
    int32_t previousHealth = 20;
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
    // Everyone on the server's player list, for the pause screen's players_collection.
    std::vector<std::string> players;
    bool sidebarVisible = false;
    ui::UiData sidebar;
    // The paper doll is up, which pushes the chat below it.
    bool paperDoll = false;
    uint32_t hiddenElements = 0;

    bool hidden(HudElement element) const
    {
        return (hiddenElements >> static_cast<uint32_t>(element)) & 1u;
    }
};

/**
 * What hud_screen.json binds to, the way the game's HUD screen controller
 * fills it in from the view: globals, the hotbar and chat collections and the
 * title, action bar and item text factories.
 */
ui::UiData hudData(const HudView& view);

/**
 * Draws one of the HUD's custom renderers at the place hud_screen.json laid
 * it out: slot backgrounds, item icons, durability bars, hearts, armor,
 * hunger, air bubbles and status effects.
 */
void drawHudRenderer(ui::Context& ui, const HudView& view, const std::string& renderer, const ui::Rect& rect, float alpha, const ui::UiLookup& lookup);

/**
 * Draws the name tags in the order given, each line centered on a
 * translucent strip, lines of one tag stacking upward.
 */
void drawNameTags(ui::Context& ui, const std::vector<NameTag>& tags);

}
