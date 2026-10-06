#include "menu/Hud.h"

#include "ui/Skin.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kestrel::menu {

namespace {

constexpr int32_t MaxHeartRows = 10;
constexpr ui::Color White { 255, 255, 255, 255 };
constexpr ui::Color TextShadow { 63, 63, 63, 255 };
constexpr float IconSize = 9.0f;
constexpr float IconStep = 8.0f;
constexpr std::array<int32_t, 11> HarmfulEffects { 2, 4, 7, 9, 15, 17, 18, 19, 20, 25, 30 };

constexpr double GameTipFadeIn = 0.5;
constexpr const char* ItemNameTextOffset = "0,-11";

const char* effectSprite(int32_t id)
{
    switch (id) {
    case 1:
        return "ui/speed_effect";
    case 2:
        return "ui/slowness_effect";
    case 3:
        return "ui/haste_effect";
    case 4:
        return "ui/mining_fatigue_effect";
    case 5:
        return "ui/strength_effect";
    case 8:
        return "ui/jump_boost_effect";
    case 9:
        return "ui/nausea_effect";
    case 10:
        return "ui/regeneration_effect";
    case 11:
        return "ui/resistance_effect";
    case 12:
        return "ui/fire_resistance_effect";
    case 13:
        return "ui/water_breathing_effect";
    case 14:
        return "ui/invisibility_effect";
    case 15:
        return "ui/blindness_effect";
    case 16:
        return "ui/night_vision_effect";
    case 17:
        return "ui/hunger_effect";
    case 18:
        return "ui/weakness_effect";
    case 19:
        return "ui/poison_effect";
    case 20:
        return "ui/wither_effect";
    case 21:
        return "ui/health_boost_effect";
    case 22:
        return "ui/absorption_effect";
    case 24:
        return "ui/levitation_effect";
    case 26:
        return "ui/conduit_power_effect";
    case 27:
        return "ui/slow_falling_effect";
    case 28:
        return "ui/bad_omen_effect";
    case 29:
        return "ui/village_hero_effect";
    case 30:
        return "ui/darkness_effect";
    default:
        return nullptr;
    }
}

std::string heartSprite(HeartKind kind, bool flash, bool half)
{
    std::string base = "ui/";
    switch (kind) {
    case HeartKind::Poison:
        base += "poison_heart";
        break;
    case HeartKind::Wither:
        base += "wither_heart";
        break;
    case HeartKind::Freeze:
        base += "freeze_heart";
        break;
    case HeartKind::Normal:
        base += "heart";
        break;
    }
    if (flash) {
        base += "_flash";
    }
    return half ? base + "_half" : base;
}

int32_t heartRows(const HudView& view, int32_t& healthHearts, int32_t& absorptionHearts)
{
    int32_t maximum = static_cast<int32_t>(std::ceil(view.maxHealth));
    int32_t absorption = static_cast<int32_t>(std::ceil(view.absorption));
    healthHearts = std::min((maximum + 1) / 2, MaxHeartRows * 10);
    absorptionHearts = std::min((absorption + 1) / 2, 20);
    int32_t total = std::max(healthHearts + absorptionHearts, 1);
    return std::max((total + 9) / 10, 1);
}

ui::Color faded(float alpha)
{
    return { 255, 255, 255, static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f) };
}

ui::UiValue text(std::string value)
{
    return ui::UiValue::of(std::move(value));
}

ui::UiValue flag(bool value)
{
    return ui::UiValue::of(value);
}

ui::UiValue number(double value)
{
    return ui::UiValue::of(value);
}

/**
 * Hearts from their first row's top left corner, further rows stacking
 * upward and closer together the more there are.
 */
void drawHearts(ui::Context& ui, const HudView& view, float left, float top, ui::Color tint)
{
    int32_t healthHearts = 0;
    int32_t absorptionHearts = 0;
    int32_t rows = heartRows(view, healthHearts, absorptionHearts);
    float rowHeight = static_cast<float>(std::max(10 - std::max(rows - 2, 0), 3));
    int32_t current = static_cast<int32_t>(std::floor(std::max(view.health, 0.0f)));
    int32_t absorption = static_cast<int32_t>(std::ceil(std::max(view.absorption, 0.0f)));
    int32_t total = std::max(healthHearts + absorptionHearts, 1);
    for (int32_t index = 0; index < total; ++index) {
        ui::Rect cell { left + static_cast<float>(index % 10) * IconStep, top - static_cast<float>(index / 10) * rowHeight, IconSize, IconSize };
        ui.sprite(cell, view.heartFlash ? "ui/heart_blink" : "ui/heart_background", tint);
        if (index < healthHearts) {
            if (view.heartFlash) {
                int32_t previous = view.previousHealth - index * 2;
                if (previous >= 2) ui.sprite(cell, heartSprite(view.heartKind, true, false), tint);
                else if (previous == 1) ui.sprite(cell, heartSprite(view.heartKind, true, true), tint);
            }
            int32_t filled = current - index * 2;
            if (filled >= 2) {
                ui.sprite(cell, heartSprite(view.heartKind, false, false), tint);
            } else if (filled == 1) {
                ui.sprite(cell, heartSprite(view.heartKind, false, true), tint);
            }
        } else {
            int32_t filled = absorption - (index - healthHearts) * 2;
            if (filled >= 2) {
                ui.sprite(cell, "ui/absorption_heart", tint);
            } else if (filled == 1) {
                ui.sprite(cell, "ui/absorption_heart_half", tint);
            }
        }
    }
}

void drawArmor(ui::Context& ui, const HudView& view, float left, float top, ui::Color tint)
{
    int32_t healthHearts = 0;
    int32_t absorptionHearts = 0;
    int32_t rows = heartRows(view, healthHearts, absorptionHearts);
    float rowHeight = static_cast<float>(std::max(10 - std::max(rows - 2, 0), 3));
    float y = top - static_cast<float>(rows - 1) * rowHeight - 10.0f;
    for (int32_t index = 0; index < 10; ++index) {
        int32_t remaining = view.armor - index * 2;
        const char* name = remaining >= 2 ? "ui/armor_full" : remaining == 1 ? "ui/armor_half" : "ui/armor_empty";
        ui.sprite({ left + static_cast<float>(index) * IconStep, y, IconSize, IconSize }, name, tint);
    }
}

/**
 * Hunger from its right edge leftward, the way the game mirrors the hearts.
 */
void drawHunger(ui::Context& ui, const HudView& view, float right, float top, ui::Color tint)
{
    std::string background = view.hungerEffect ? "ui/hunger_effect_background" : "ui/hunger_background";
    std::string full = view.hungerEffect ? "ui/hunger_effect_full" : "ui/hunger_full";
    std::string half = view.hungerEffect ? "ui/hunger_effect_half" : "ui/hunger_half";
    int32_t current = static_cast<int32_t>(std::ceil(std::max(view.hunger, 0.0f)));
    for (int32_t index = 0; index < 10; ++index) {
        ui::Rect cell { right - static_cast<float>(index) * IconStep - IconSize, top, IconSize, IconSize };
        ui.sprite(cell, background, tint);
        int32_t remaining = current - index * 2;
        if (remaining >= 2) {
            ui.sprite(cell, full, tint);
        } else if (remaining == 1) {
            ui.sprite(cell, half, tint);
        }
    }
}

void drawBubbles(ui::Context& ui, const HudView& view, float right, float top, ui::Color tint)
{
    if (view.air >= view.maxAir) {
        return;
    }
    int32_t maximum = std::max(view.maxAir, 1);
    int32_t air = std::max(view.air, 0);
    int32_t fullBubbles = (std::max(air - 2, 0) * 10 + maximum - 1) / maximum;
    int32_t popping = std::max((air * 10 + maximum - 1) / maximum - fullBubbles, 0);
    for (int32_t index = 0; index < std::min(fullBubbles + popping, 10); ++index) {
        ui.sprite({ right - static_cast<float>(index) * IconStep - IconSize, top, IconSize, IconSize }, index < fullBubbles ? "ui/bubble" : "ui/bubble_pop", tint);
    }
}

/**
 * Status effects in the top right corner of the screen, beneficial ones in
 * the first row and harmful ones under them.
 */
void drawEffects(ui::Context& ui, const HudView& view, float screenRight, float alpha)
{
    std::vector<const HudEffectView*> beneficial;
    std::vector<const HudEffectView*> harmful;
    for (const HudEffectView& effect : view.effects) {
        if (!effectSprite(effect.id)) {
            continue;
        }
        bool bad = std::find(HarmfulEffects.begin(), HarmfulEffects.end(), effect.id) != HarmfulEffects.end();
        (bad ? harmful : beneficial).push_back(&effect);
    }
    auto byId = [](const HudEffectView* a, const HudEffectView* b) {
        return a->id < b->id;
    };
    std::sort(beneficial.begin(), beneficial.end(), byId);
    std::sort(harmful.begin(), harmful.end(), byId);
    int row = 0;
    for (const std::vector<const HudEffectView*>* list : { &beneficial, &harmful }) {
        float y = 1.0f + static_cast<float>(row) * 25.0f;
        for (size_t column = 0; column < list->size(); ++column) {
            const HudEffectView& effect = *(*list)[column];
            float x = screenRight - 25.0f * static_cast<float>(column + 1);
            if (x < 0.0f) {
                break;
            }
            ui::Color tint = faded(effect.alpha * alpha);
            ui.sprite({ x, y, 24.0f, 24.0f }, effect.ambient ? "ui/hud_mob_ambient_effect_background" : "ui/hud_mob_effect_background", tint);
            ui.sprite({ x + 3.0f, y + 3.0f, 18.0f, 18.0f }, effectSprite(effect.id), tint);
        }
        ++row;
    }
}

void drawItem(ui::Context& ui, const HudSlot& slot, const ui::Rect& rect, float alpha)
{
    if (slot.filled && !slot.icon.empty()) {
        ui.sprite(rect, slot.icon, faded(alpha));
    }
}

/**
 * The durability bar of progress_bar_renderer: a black strip one unit
 * wider and taller than the control, with the remaining part going from green
 * to red over it.
 */
void drawDurability(ui::Context& ui, const ui::Rect& rect, float fraction, float alpha)
{
    uint8_t a = static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    ui.fill({ rect.x, rect.y, rect.w + 1.0f, rect.h + 1.0f }, { 0, 0, 0, a });
    float width = std::round(std::clamp(fraction, 0.0f, 1.0f) * (rect.w + 1.0f));
    if (width <= 0.0f) {
        return;
    }
    float hue = fraction / 3.0f;
    float red = std::clamp(std::abs(hue * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
    float green = std::clamp(2.0f - std::abs(hue * 6.0f - 2.0f), 0.0f, 1.0f);
    ui.fill({ rect.x, rect.y, width, rect.h }, { static_cast<uint8_t>(red * 255.0f), static_cast<uint8_t>(green * 255.0f), 0, a });
}

}

ui::UiData hudData(const HudView& view)
{
    ui::UiData data = view.sidebar;
    ui::UiRow& g = data.globals;
    bool survival = view.showStats;
    bool hotbarShown = view.showHotbar && !view.hidden(HudElement::HotBar);
    bool experience = survival && !view.hidden(HudElement::ProgressBar);
    g["#hud_visible"] = flag(view.visible);
    // Packs draw their own tab list from these, the way the pause screen lists players.
    g["#players_grid_dimension"] = text("1," + std::to_string(view.players.size()));
    std::vector<ui::UiRow>& players = data.collections["players_collection"];
    players.reserve(view.players.size());
    for (const std::string& name : view.players) {
        players.push_back({ { "#gamertag", text(name) }, { "#gamerpic_visible", flag(false) }, { "#texture", text("") }, { "#texture_source", text("") } });
    }
    g["#hud_alpha"] = number(1.0);
    g["#hud_propagate_alpha"] = flag(false);
    g["#hud_visible_centered"] = flag(true);
    g["#hud_visible_centered_gui_elements"] = flag(true);
    g["#hud_visible_centered_touch"] = flag(false);
    g["#hud_visible_not_centered"] = flag(false);
    g["#hotbar_visible"] = flag(hotbarShown);
    g["#hotbar_visible_not_centered"] = flag(false);
    g["#hotbar_visible_not_centered_resizable"] = flag(false);
    g["#hotbar_with_xp_bar"] = flag(hotbarShown && experience);
    g["#hotbar_no_xp_bar"] = flag(hotbarShown && !experience);
    g["#hotbar_with_locator_bar"] = flag(false);
    g["#hotbar_elipses_left_visible"] = flag(false);
    g["#hotbar_elipses_right_visible"] = flag(false);
    g["#hotbar_grid_dimensions"] = text("9,1");
    g["#is_spectator_mode"] = flag(!view.showHotbar);
    g["#show_survival_ui"] = flag(survival);
    g["#is_armor_visible"] = flag(survival && view.armor > 0 && !view.hidden(HudElement::Armor));
    g["#is_not_riding_bubbles"] = flag(survival && view.air < view.maxAir && !view.hidden(HudElement::AirBubbles));
    g["#is_riding_bubbles"] = flag(false);
    g["#horse_hearts_touch"] = flag(false);
    g["#creative_horse_hearts"] = flag(false);
    g["#survival_horse_hearts"] = flag(false);
    g["#level_number"] = text(std::to_string(view.level));
    g["#level_number_visible"] = flag(experience && view.level > 0);
    // clip_ratio is the part cut away, so a quarter full bar clips three quarters.
    g["#exp_progress"] = number(1.0 - std::clamp(static_cast<double>(view.experience), 0.0, 1.0));
    g["#paper_doll_visible"] = flag(view.paperDoll);
    g["#status_effects_visible"] = flag(!view.hidden(HudElement::StatusEffects));
    g["#scoreboard_sidebar_visible"] = flag(view.sidebarVisible);
    g["#player_position_visible"] = flag(!view.playerPosition.empty());
    g["#player_position_text"] = text(view.playerPosition);
    g["#number_of_days_played_visible"] = flag(false);
    g["#hud_text_background_alpha"] = number(view.textBackgroundOpacity);
    static constexpr const char* BossBarColors[8] = {
        "#ff69b4", "#5555ff", "#ff5555", "#55ff55", "#ffff55", "#aa00aa", "#663399", "#ffffff",
    };
    static constexpr int BossBarNotches[5] = { 0, 6, 10, 12, 20 };
    g["#boss_grid_dimension"] = text("1," + std::to_string(view.bossBars.size()));
    std::vector<ui::UiRow>& bossBars = data.collections["boss_bars"];
    bossBars.reserve(view.bossBars.size());
    for (const HudBossBar& bar : view.bossBars) {
        bossBars.push_back({
            { "#bossName", text(bar.title) },
            { "#bar_visible", flag(true) },
            { "#progress_percentage", number(1.0 - std::clamp(static_cast<double>(bar.progress), 0.0, 1.0)) },
            { "#bar_color", text(BossBarColors[std::clamp(bar.color, 0, 7)]) },
            { "#bar_notches", number(BossBarNotches[std::clamp(bar.overlay, 0, 4)]) },
        });
    }
    g["#boss_hud_padding"] = flag(false);
    g["#boss_hud_touch_padding"] = flag(false);
    g["#on_new_death_screen"] = flag(false);
    g["#interact_visible"] = flag(false);
    g["#auto_save_animation_visible"] = flag(false);
    for (const char* hidden : { "#reset_modal_visible", "#close_without_saving_modal_visible", "#hint_drag_visible", "#hint_deselect_visible", "#hint_saved_visible", "#layout_customization_main_panel_visible", "#layout_customization_sub_panel_visible", "#left_tips_visible", "#emote_tips_visible", "#tooltip_visible" }) {
        g[hidden] = flag(false);
    }

    std::vector<ui::UiRow>& hotbar = data.collections["hotbar_items"];
    for (size_t index = 0; index < view.hotbar.size(); ++index) {
        const HudSlot& slot = view.hotbar[index];
        ui::UiRow row;
        row["#hotbar_slot"] = number(static_cast<double>(index));
        row["#slot_selected"] = flag(static_cast<int32_t>(index) == view.selected);
        row["#item_icon"] = text(slot.filled ? slot.icon : std::string());
        row["#inventory_stack_count"] = text(slot.count > 1 ? std::to_string(slot.count) : std::string());
        row["#stack_count_visible"] = flag(slot.filled && slot.count > 1);
        row["#item_durability_visible"] = flag(slot.filled && slot.durability >= 0.0f);
        row["#item_durability_total_amount"] = number(1.0);
        row["#item_durability_current_amount"] = number(std::max(0.0f, slot.durability));
        row["#item_storage_visible"] = flag(false);
        row["#item_lock_in_slot"] = flag(false);
        row["#item_lock_in_inventory"] = flag(false);
        hotbar.push_back(std::move(row));
    }

    auto item = [&](const char* factory, const char* control, const HudText& shown, ui::UiRow variables) {
        if (shown.serial == 0 || shown.text.empty()) {
            return;
        }
        variables["$wait_duration"] = number(shown.hold);
        data.factories[factory].push_back({ control, std::move(variables), shown.serial });
    };
    // The game makes the item name with its text background on, and lifts it clear of the
    // hearts and hunger the survival padding in hud_screen.json already steps over.
    ui::UiRow itemTextVariables {
        { "$show_text_background", flag(true) },
        { "$item_text_background_alpha", number(view.textBackgroundOpacity) },
    };
    g["#item_name_text_offset"] = text(ItemNameTextOffset);
    if (view.jukebox) {
        g["#jukebox_text"] = text(view.itemText.text);
        item("item_text_factory", "jukebox_text", view.itemText, std::move(itemTextVariables));
    } else {
        g["#item_text"] = text(view.itemText.text);
        item("item_text_factory", "item_text", view.itemText, std::move(itemTextVariables));
    }
    g["#tip_text"] = text(view.tip.text);
    item("hud_tip_text_factory", "hud_tip_text", view.tip, {});
    item("hud_actionbar_text_factory", "hud_actionbar_text", view.actionbar, {
        { "$actionbar_text", text(view.actionbar.text) },
        { "$actionbar_text_background_alpha", number(view.actionbarBackgroundOpacity) },
    });

    g["#hud_title_text_string"] = text(view.title.title);
    g["#hud_subtitle_text_string"] = text(view.title.subtitle);
    if (view.title.serial != 0) {
        data.factories["hud_title_text_factory"].push_back({ "hud_title_text", {
            { "$title_fade_in_time", number(view.title.fadeIn) },
            { "$title_stay_time", number(view.title.stay) },
            { "$title_fade_out_time", number(view.title.fadeOut) },
            { "$title_alpha", number(view.textBackgroundOpacity) },
            { "$subtitle_initially_visible", flag(view.title.subtitleWithTitle) },
            { "$title_shadow", flag(false) },
        }, view.title.serial });
    }

    g["#text"] = text(view.gameTip.text);
    g["#animation_name"] = text(view.gameTip.animation);
    if (view.gameTip.serial != 0 && !view.gameTip.text.empty()) {
        data.factories["game_tip_item_factory"].push_back({ "game_tip", {
            { "$anim_alpha_from", number(0.0) },
            { "$anim_alpha_to", number(1.0) },
            { "$anim_duration", number(GameTipFadeIn) },
            { "$ignore_arrow", flag(true) },
            { "$game_tip_offset", text("[-4, 26]") },
        }, view.gameTip.serial });
    }

    std::vector<ui::UiRow>& chat = data.collections["chat_text_grid"];
    for (const HudChatLine& line : view.chat) {
        chat.push_back({ { "#chat_text", text(line.text) } });
        data.factories["chat_item_factory"].push_back({ "chat_item", {
            { "$chat_item_lifetime", number(view.chatStyle.lifetime) },
            { "$chat_background_opacity", number(view.chatStyle.backgroundOpacity) },
            { "$chat_font_scale_factor", number(view.chatStyle.fontScale) },
            { "$chat_line_spacing", number(view.chatStyle.linePadding) },
            { "$chat_font_type", text(view.chatStyle.fontType) },
            { "$chat_text_color", text(line.color) },
        }, line.serial });
    }
    return data;
}

void drawHudRenderer(ui::Context& ui, const HudView& view, const std::string& renderer, const ui::Rect& rect, float alpha, const ui::UiLookup& lookup)
{
    ui::Color tint = faded(alpha);
    if (renderer == "hotbar_renderer") {
        int slot = static_cast<int>(lookup("#hotbar_slot").toNumber());
        ui.sprite(rect, "ui/hotbar_" + std::to_string(std::clamp(slot, 0, 8)), tint);
        // The off hand has no control of its own; the game draws it with the first slot.
        if (slot == 0 && view.offhand.filled) {
            ui.sprite({ rect.x - 30.0f, rect.y, rect.w, rect.h }, "ui/hotbar_0", tint);
            drawItem(ui, view.offhand, { rect.x - 27.0f, rect.y + 3.0f, 16.0f, 16.0f }, alpha);
            if (view.offhand.durability >= 0.0f) {
                drawDurability(ui, { rect.x - 25.0f, rect.y + 16.0f, 12.0f, 1.0f }, view.offhand.durability, alpha);
            }
        }
    } else if (renderer == "inventory_item_renderer") {
        std::string icon = lookup("#item_icon").toText();
        if (!icon.empty()) {
            ui.sprite(rect, icon, tint);
        }
    } else if (renderer == "progress_bar_renderer") {
        if (lookup("#touch_progress_bar_visible").truthy()) {
            double total = lookup("#progress_bar_total_amount").toNumber();
            double current = lookup("#progress_bar_current_amount").toNumber();
            drawDurability(ui, rect, total > 0.0 ? static_cast<float>(current / total) : 0.0f, alpha);
        }
    } else if (renderer == "heart_renderer") {
        if (!view.hidden(HudElement::Health)) drawHearts(ui, view, rect.x, rect.y, tint);
    } else if (renderer == "armor_renderer") {
        if (!view.hidden(HudElement::Armor)) drawArmor(ui, view, rect.x, rect.y, tint);
    } else if (renderer == "hunger_renderer") {
        if (!view.hidden(HudElement::Hunger)) drawHunger(ui, view, rect.right(), rect.y, tint);
    } else if (renderer == "bubbles_renderer") {
        if (!view.hidden(HudElement::AirBubbles)) drawBubbles(ui, view, rect.right(), rect.y, tint);
    } else if (renderer == "mob_effects_renderer") {
        if (!view.hidden(HudElement::StatusEffects)) drawEffects(ui, view, rect.right(), alpha);
    }
}

void drawNameTags(ui::Context& ui, const std::vector<NameTag>& tags)
{
    for (const NameTag& tag : tags) {
        ui.nameTag(tag.text, tag.x, tag.y, tag.magnify, tag.depth, tag.sneaking);
    }
}

}
