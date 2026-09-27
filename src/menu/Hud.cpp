#include "menu/Hud.h"

#include "ui/Skin.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace kestrel::menu {

namespace {

constexpr float HotbarWidth = 182.0f;
constexpr uint8_t HotbarCapAlpha = 166;
constexpr int32_t MaxHeartRows = 10;
constexpr ui::Color White { 255, 255, 255, 255 };
constexpr ui::Color TextShadow { 63, 63, 63, 255 };
constexpr ui::Color LevelColor { 128, 255, 32, 255 };
constexpr std::array<int32_t, 11> HarmfulEffects { 2, 4, 7, 9, 15, 17, 18, 19, 20, 25, 30 };

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

/**
 * Maps GUI pixels to menu units and draws named skin sprites at their
 * classic GUI sizes.
 */
struct Layout {
    ui::Context& ui;
    const HudView& view;
    float originX = 0.0f;
    float originY = 0.0f;
    float unit = 1.0f;
    float guiWidth = 0.0f;
    float guiHeight = 0.0f;

    ui::Rect rect(float gx, float gy, float gw, float gh) const
    {
        return { originX + gx * unit, originY + gy * unit, gw * unit, gh * unit };
    }

    void sprite(const std::string& name, float gx, float gy, float gw, float gh, ui::Color tint = White) const
    {
        ui.sprite(rect(gx, gy, gw, gh), name, tint);
    }

    void region(const std::string& name, float gx, float gy, float gw, float gh, const ui::Rect& texels) const
    {
        ui.spriteRegion(rect(gx, gy, gw, gh), name, texels, White);
    }

    void solid(float gx, float gy, float gw, float gh, ui::Color color) const
    {
        ui.fill(rect(gx, gy, gw, gh), color);
    }

    void text(const std::string& value, float lx, float ly, ui::Color color) const
    {
        ui.textShadowed(value, ui::TextStyle::Pixel, lx, ly, color, { TextShadow.r, TextShadow.g, TextShadow.b, color.a });
    }
};

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

void drawSlotContents(const Layout& layout, const HudSlot& slot, float gx, float gy)
{
    if (!slot.filled) {
        return;
    }
    if (!slot.icon.empty()) {
        layout.sprite(slot.icon, gx, gy, 16.0f, 16.0f);
    }
    if (slot.durability >= 0.0f) {
        layout.solid(gx + 2.0f, gy + 13.0f, 13.0f, 2.0f, { 0, 0, 0, 255 });
        float width = std::round(std::clamp(slot.durability, 0.0f, 1.0f) * 13.0f);
        if (width > 0.0f) {
            float hue = slot.durability / 3.0f;
            float red = std::clamp(std::abs(hue * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
            float green = std::clamp(2.0f - std::abs(hue * 6.0f - 2.0f), 0.0f, 1.0f);
            layout.solid(gx + 2.0f, gy + 13.0f, width, 1.0f, { static_cast<uint8_t>(red * 255.0f), static_cast<uint8_t>(green * 255.0f), 0, 255 });
        }
    }
    if (slot.count > 1) {
        std::string value = std::to_string(slot.count);
        float width = layout.ui.measure(value, ui::TextStyle::Pixel);
        float height = layout.ui.lineHeight(ui::TextStyle::Pixel);
        ui::Rect cell = layout.rect(gx, gy, 17.0f, 17.0f);
        layout.text(value, cell.right() - width, cell.bottom() - height, White);
    }
}

void drawHotbar(const Layout& layout)
{
    const HudView& view = layout.view;
    float left = (layout.guiWidth - HotbarWidth) * 0.5f;
    float top = layout.guiHeight - 22.0f;
    layout.sprite("ui/hotbar_start_cap", left, top, 1.0f, 22.0f, { 255, 255, 255, HotbarCapAlpha });
    for (int slot = 0; slot < 9; ++slot) {
        layout.sprite("ui/hotbar_" + std::to_string(slot), left + 1.0f + slot * 20.0f, top, 20.0f, 22.0f);
    }
    layout.sprite("ui/hotbar_end_cap", left + HotbarWidth - 1.0f, top, 1.0f, 22.0f, { 255, 255, 255, HotbarCapAlpha });
    int32_t selected = std::clamp(view.selected, 0, 8);
    layout.sprite("ui/selected_hotbar_slot", left + selected * 20.0f - 1.0f, top - 1.0f, 24.0f, 24.0f);
    if (view.offhand.filled) {
        layout.sprite("ui/hotbar_0", left - 29.0f, top, 20.0f, 22.0f);
    }
    for (int slot = 0; slot < 9; ++slot) {
        drawSlotContents(layout, view.hotbar[size_t(slot)], left + 3.0f + slot * 20.0f, layout.guiHeight - 19.0f);
    }
    drawSlotContents(layout, view.offhand, left - 26.0f, layout.guiHeight - 19.0f);

    if (view.labelAlpha > 0.0f && !view.selectedName.empty()) {
        float width = layout.ui.measure(view.selectedName, ui::TextStyle::Pixel);
        float height = layout.ui.lineHeight(ui::TextStyle::Pixel);
        float above = view.showStats ? 49.0f : 25.0f;
        ui::Rect anchor = layout.rect(0.0f, layout.guiHeight - above, 0.0f, 0.0f);
        float x = std::floor(layout.originX + (layout.guiWidth * layout.unit - width) * 0.5f);
        layout.text(view.selectedName, x, std::floor(anchor.y - height), { 255, 255, 255, static_cast<uint8_t>(std::clamp(view.labelAlpha, 0.0f, 1.0f) * 255.0f) });
    }
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

void drawHealth(const Layout& layout)
{
    const HudView& view = layout.view;
    int32_t healthHearts = 0;
    int32_t absorptionHearts = 0;
    int32_t rows = heartRows(view, healthHearts, absorptionHearts);
    float rowHeight = static_cast<float>(std::max(10 - std::max(rows - 2, 0), 3));
    int32_t current = static_cast<int32_t>(std::ceil(std::max(view.health, 0.0f)));
    int32_t absorption = static_cast<int32_t>(std::ceil(std::max(view.absorption, 0.0f)));
    float left = (layout.guiWidth - HotbarWidth) * 0.5f;
    float base = layout.guiHeight - 39.0f;
    int32_t total = std::max(healthHearts + absorptionHearts, 1);
    for (int32_t index = 0; index < total; ++index) {
        float x = left + (index % 10) * 8.0f;
        float y = base - (index / 10) * rowHeight;
        layout.sprite("ui/heart_background", x, y, 9.0f, 9.0f);
        if (index < healthHearts) {
            int32_t filled = current - index * 2;
            if (filled >= 2) {
                layout.sprite(heartSprite(view.heartKind, view.heartFlash, false), x, y, 9.0f, 9.0f);
            } else if (filled == 1) {
                layout.sprite(heartSprite(view.heartKind, view.heartFlash, true), x, y, 9.0f, 9.0f);
            }
        } else {
            int32_t filled = absorption - (index - healthHearts) * 2;
            if (filled >= 2) {
                layout.sprite("ui/absorption_heart", x, y, 9.0f, 9.0f);
            } else if (filled == 1) {
                layout.sprite("ui/absorption_heart_half", x, y, 9.0f, 9.0f);
            }
        }
    }
    if (view.armor > 0) {
        float y = base - (rows - 1) * rowHeight - 10.0f;
        for (int32_t index = 0; index < 10; ++index) {
            int32_t remaining = view.armor - index * 2;
            const char* name = remaining >= 2 ? "ui/armor_full" : remaining == 1 ? "ui/armor_half" : "ui/armor_empty";
            layout.sprite(name, left + index * 8.0f, y, 9.0f, 9.0f);
        }
    }
}

void drawHunger(const Layout& layout)
{
    const HudView& view = layout.view;
    std::string background = view.hungerEffect ? "ui/hunger_effect_background" : "ui/hunger_background";
    std::string full = view.hungerEffect ? "ui/hunger_effect_full" : "ui/hunger_full";
    std::string half = view.hungerEffect ? "ui/hunger_effect_half" : "ui/hunger_half";
    int32_t current = static_cast<int32_t>(std::ceil(std::max(view.hunger, 0.0f)));
    float right = (layout.guiWidth + HotbarWidth) * 0.5f;
    for (int32_t index = 0; index < 10; ++index) {
        float x = right - index * 8.0f - 9.0f;
        float y = layout.guiHeight - 39.0f;
        layout.sprite(background, x, y, 9.0f, 9.0f);
        int32_t remaining = current - index * 2;
        if (remaining >= 2) {
            layout.sprite(full, x, y, 9.0f, 9.0f);
        } else if (remaining == 1) {
            layout.sprite(half, x, y, 9.0f, 9.0f);
        }
    }
    if (view.air < view.maxAir) {
        int32_t maximum = std::max(view.maxAir, 1);
        int32_t air = std::max(view.air, 0);
        int32_t fullBubbles = (std::max(air - 2, 0) * 10 + maximum - 1) / maximum;
        int32_t popping = std::max((air * 10 + maximum - 1) / maximum - fullBubbles, 0);
        for (int32_t index = 0; index < std::min(fullBubbles + popping, 10); ++index) {
            layout.sprite(index < fullBubbles ? "ui/bubble" : "ui/bubble_pop", right - index * 8.0f - 9.0f, layout.guiHeight - 49.0f, 9.0f, 9.0f);
        }
    }
}

void drawExperience(const Layout& layout)
{
    const HudView& view = layout.view;
    const ui::Sprite& icons = layout.ui.skin().sprite("textures/gui/icons");
    float texel = icons.valid && icons.width > 0.0f ? icons.width / 256.0f : 1.0f;
    float left = (layout.guiWidth - HotbarWidth) * 0.5f;
    float top = layout.guiHeight - 29.0f;
    layout.region("textures/gui/icons", left, top, 182.0f, 5.0f, { 0.0f, 64.0f * texel, 182.0f * texel, 5.0f * texel });
    float filled = std::min(std::floor(std::clamp(view.experience, 0.0f, 1.0f) * 183.0f), 182.0f);
    if (filled >= 1.0f) {
        layout.region("textures/gui/icons", left, top, filled, 5.0f, { 0.0f, 69.0f * texel, filled * texel, 5.0f * texel });
    }
    if (view.level > 0) {
        std::string value = std::to_string(view.level);
        float width = layout.ui.measure(value, ui::TextStyle::Pixel);
        float height = layout.ui.lineHeight(ui::TextStyle::Pixel);
        ui::Rect bar = layout.rect(0.0f, layout.guiHeight - 31.0f, 0.0f, 0.0f);
        float x = std::floor(layout.originX + (layout.guiWidth * layout.unit - width) * 0.5f);
        float y = std::floor(bar.y - height * 0.5f - 2.0f);
        for (const std::array<float, 2>& offset : { std::array<float, 2> { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f } }) {
            layout.ui.text(value, ui::TextStyle::Pixel, x + offset[0], y + offset[1], { 0, 0, 0, 255 });
        }
        layout.ui.text(value, ui::TextStyle::Pixel, x, y, LevelColor);
    }
}

void drawEffects(const Layout& layout)
{
    std::vector<const HudEffectView*> beneficial;
    std::vector<const HudEffectView*> harmful;
    for (const HudEffectView& effect : layout.view.effects) {
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
        float y = 1.0f + row * 25.0f;
        for (size_t column = 0; column < list->size(); ++column) {
            const HudEffectView& effect = *(*list)[column];
            float x = layout.guiWidth - 25.0f * (column + 1);
            if (x < 0.0f) {
                break;
            }
            uint8_t alpha = static_cast<uint8_t>(std::clamp(effect.alpha, 0.0f, 1.0f) * 255.0f);
            const char* background = effect.ambient ? "ui/hud_mob_ambient_effect_background" : "ui/hud_mob_effect_background";
            layout.sprite(background, x, y, 24.0f, 24.0f, { 255, 255, 255, alpha });
            layout.sprite(effectSprite(effect.id), x + 3.0f, y + 3.0f, 18.0f, 18.0f, { 255, 255, 255, alpha });
        }
        ++row;
    }
}

}

void drawNameTags(ui::Context& ui, const std::vector<NameTag>& tags)
{
    constexpr ui::Color TagBackground { 0, 0, 0, 64 };
    std::vector<std::string_view> lines;
    for (const NameTag& tag : tags) {
        lines.clear();
        std::string_view rest = tag.text;
        while (true) {
            size_t end = rest.find('\n');
            lines.push_back(rest.substr(0, end));
            if (end == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(end + 1);
        }
        float widest = 0.0f;
        for (std::string_view line : lines) {
            widest = std::max(widest, ui.measure(line, ui::TextStyle::Pixel));
        }
        float lineHeight = 9.0f * tag.magnify;
        float top = tag.y - static_cast<float>(lines.size()) * lineHeight;
        ui.fill({ tag.x - (widest * 0.5f + 1.0f) * tag.magnify, top - tag.magnify, (widest + 2.0f) * tag.magnify, static_cast<float>(lines.size()) * lineHeight + tag.magnify }, TagBackground);
        for (size_t i = 0; i < lines.size(); ++i) {
            float width = ui.measure(lines[i], ui::TextStyle::Pixel) * tag.magnify;
            ui.pixelTextScaled(lines[i], tag.x - width * 0.5f, top + static_cast<float>(i) * lineHeight, tag.magnify, White);
        }
    }
}

void drawHud(ui::Context& ui, const HudView& view, float x, float y, float width, float height)
{
    if (!view.visible || width <= 0.0f || height <= 0.0f) {
        return;
    }
    float pixelScale = std::max(ui.pixelScale(), 0.01f);
    float physicalHeight = height * pixelScale;
    float physicalWidth = width * pixelScale;
    float k = std::max(1.0f, std::min(std::floor(physicalWidth / 480.0f), std::floor(physicalHeight / 360.0f)));
    Layout layout { ui, view, x, y, k / pixelScale, 0.0f, 0.0f };
    layout.guiWidth = width / layout.unit;
    layout.guiHeight = height / layout.unit;
    if (layout.guiWidth < HotbarWidth || layout.guiHeight < 59.0f) {
        return;
    }
    if (view.showHotbar) {
        drawHotbar(layout);
    }
    if (view.showStats) {
        drawHealth(layout);
        drawHunger(layout);
        drawExperience(layout);
    }
    drawEffects(layout);
}

}
