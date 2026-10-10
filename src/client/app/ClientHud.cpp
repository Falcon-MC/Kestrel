#include "client/Client.h"

#include "client/DebugLog.h"
#include "platform/Window.h"
#include "ui/Localization.h"
#include "world/ItemInfo.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <utility>

namespace kestrel {

namespace {

// A factory control is made again whenever its serial changes, so every message gets
// its own, taken from when it arrived.
uint64_t serialOf(double shown)
{
    return shown < 0.0 ? 0 : static_cast<uint64_t>(shown * 1000.0) + 1;
}

menu::HudText hudText(const HudMessage& message, float hold)
{
    return { message.text, serialOf(message.shown), hold };
}

}

menu::HudSlot Client::inventoryIcon(const HudItem& item)
{
    menu::HudSlot slot;
    if (item.empty() || !blockAssets) return slot;
    slot.filled = true;
    slot.count = item.count;
    std::string name = "item/" + item.identifier + "#" + std::to_string(item.aux) + "#" + item.icon;
    if (item.customColor) name += "#color" + std::to_string(*item.customColor);
    int shimmerFrame = 0;
    if (item.enchanted) {
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        shimmerFrame = int(std::fmod(std::fmod(now, 3600.0) * visuals.glintSpeed.value_or(menu.glintSpeed()) / 100.0 * 8.0, 32.0));
        name += "#enchanted" + std::to_string(visuals.glintStrength.value_or(menu.glintStrength()));
    }
    auto known = itemIcons.find(name);
    if (known == itemIcons.end() || (item.enchanted && itemIconFrames[name] != shimmerFrame)) {
        auto pixels = blockAssets->itemIcon(item.identifier, item.aux, item.icon, item.customColor);
        bool rendered = pixels.size() == size_t(world::ItemIconSize) * world::ItemIconSize * 4;
        if (rendered && item.enchanted) {
            const ui::Bitmap* glint = skin.bitmap("textures/misc/enchanted_item_glint");
            if (glint && glint->width > 0 && glint->height > 0) {
                for (uint32_t y = 0; y < world::ItemIconSize; ++y) {
                    for (uint32_t x = 0; x < world::ItemIconSize; ++x) {
                        size_t target = (size_t(y) * world::ItemIconSize + x) * 4;
                        if (pixels[target + 3] == 0) {
                            continue;
                        }
                        for (int pass = 0; pass < 2; ++pass) {
                            uint32_t u = uint32_t(x + (pass ? y : world::ItemIconSize - y) + shimmerFrame * (pass ? 3 : 5)) % glint->width;
                            uint32_t v = uint32_t(y + shimmerFrame * (pass ? 5 : 2)) % glint->height;
                            size_t source = (size_t(v) * glint->width + u) * 4;
                            float opacity = glint->rgba[source + 3] / 255.0f * 0.35f * visuals.glintStrength.value_or(menu.glintStrength()) / 100.0f;
                            for (size_t channel = 0; channel < 3; ++channel) {
                                pixels[target + channel] = uint8_t(std::min(255.0f, pixels[target + channel] + glint->rgba[source + channel] * opacity));
                            }
                        }
                    }
                }
            }
        }
        if (rendered) skin.setDynamic(name, { world::ItemIconSize, world::ItemIconSize, std::move(pixels) });
        itemIconFrames[name] = shimmerFrame;
        known = itemIcons.insert_or_assign(name, rendered).first;
    }
    if (known->second) slot.icon = name;
    int maximum = world::itemMaxDurability(item.identifier);
    if (maximum > 0 && item.damage > 0) slot.durability = std::clamp(float(maximum - item.damage) / maximum, 0.0f, 1.0f);
    return slot;
}

/**
 * Changes the held hotbar slot with the number keys and the mouse wheel while
 * the game has the mouse, and passes clicks on to the session: left hits and
 * keeps mining while held, right uses the held item.
 */
void Client::handleHotbarInput()
{
    bool playing = menu.capturesMouse() && worldShown && !mods->wantsCursor();
    session.setAttackHeld(playing && window->input().mouseDown);
    session.setUseHeld(playing && window->input().rightMouseDown);
    if (!playing) {
        return;
    }
    const InputState& input = window->input();
    if (input.mousePressed) {
        for (uint32_t count = std::max(input.mousePressCount, 1u); count > 0; --count) session.requestInteraction(false);
    }
    if (input.rightMousePressed) {
        session.requestInteraction(true);
    }
    if (input.middleMousePressed) {
        session.requestPickBlock(input.isHeld(Key::Control));
    }
    if (input.pressedKey == menu.keyBindings().drop()) {
        session.requestInventory({ InventoryAction::Drop, hudState.selectedSlot, 0, input.isHeld(Key::Control), {} });
        if (hudState.selectedSlot >= 0 && hudState.selectedSlot < 9 && !hudState.inventory[size_t(hudState.selectedSlot)].identifier.empty()) {
            SoundRequest sound;
            sound.name = "drop.slot";
            sound.position = { camera.x(), camera.y(), camera.z() };
            playSoundRequest(sound);
        }
    }
    int selected = hudState.selectedSlot;
    if (input.pressedKey >= Key::Num1 && input.pressedKey <= Key::Num9) {
        selected = static_cast<int>(input.pressedKey) - static_cast<int>(Key::Num1);
    } else if (input.wheel != 0.0f) {
        int steps = input.wheel > 0.0f ? -1 : 1;
        selected = ((selected + steps) % 9 + 9) % 9;
    }
    if (selected != hudState.selectedSlot) {
        session.selectHotbarSlot(selected);
        hudState.selectedSlot = selected;
        hudState.selectedChanged = secondsNow();
    }
}

/**
 * How strongly night vision lights the world: fully while it lasts, pulsing
 * during its last ten seconds, and not at all without it.
 */
float Client::nightVisionStrength() const
{
    constexpr int32_t NightVisionEffect = 16;
    double now = secondsNow();
    for (const HudEffect& effect : hudState.effects) {
        if (effect.id != NightVisionEffect) {
            continue;
        }
        if (effect.expires < 0.0) {
            return 1.0f;
        }
        double remaining = effect.expires - now;
        if (remaining <= 0.0) {
            return 0.0f;
        }
        if (remaining > 10.0) {
            return 1.0f;
        }
        return 0.7f + static_cast<float>(std::sin(remaining * 20.0 * 3.14159265 * 0.2)) * 0.3f;
    }
    return 0.0f;
}

/**
 * The game's HUD elements mods asked to hide, as SetHud bits so they join
 * the ones the server hid.
 */
uint32_t Client::modHiddenElements() const
{
    if (!mods) {
        return 0;
    }
    static constexpr std::pair<mod::HudElement, menu::HudElement> Elements[] = {
        { mod::HudElement::Crosshair, menu::HudElement::Crosshair },
        { mod::HudElement::Hotbar, menu::HudElement::HotBar },
        { mod::HudElement::Health, menu::HudElement::Health },
        { mod::HudElement::Hunger, menu::HudElement::Hunger },
        { mod::HudElement::Armor, menu::HudElement::Armor },
        { mod::HudElement::AirBubbles, menu::HudElement::AirBubbles },
        { mod::HudElement::ExperienceBar, menu::HudElement::ProgressBar },
        { mod::HudElement::StatusEffects, menu::HudElement::StatusEffects },
        { mod::HudElement::PaperDoll, menu::HudElement::PaperDoll },
        { mod::HudElement::ItemText, menu::HudElement::ItemText },
    };
    uint32_t mask = 0;
    for (const auto& [element, bit] : Elements) {
        if (mods->hidesHud(element)) {
            mask |= 1u << static_cast<uint32_t>(bit);
        }
    }
    return mask;
}

/**
 * The HUD for this frame from the latest session state: slot icons from the
 * icon cache (new icons are rendered and queued for the atlas), durability,
 * armor points, heart and hunger looks from active effects, the fading name
 * of a newly selected item and blinking effects about to expire.
 */
menu::HudView Client::buildHudView()
{
    menu::HudView view;
    if (!blockAssets || !worldShown || !terrainReleased) {
        return view;
    }
    double now = secondsNow();
    const HudState& state = hudState;
    view.visible = true;
    if (seenSessionSnapshot && seenSessionSnapshot->showCoordinates && playerView.active) {
        view.playerPosition = ui::trf("map.position", "Position: %s, %s, %s", {
            std::to_string(static_cast<int>(std::floor(playerView.current[0]))),
            std::to_string(static_cast<int>(std::floor(playerView.current[1]))),
            std::to_string(static_cast<int>(std::floor(playerView.current[2]))),
        });
    }
    view.nameTags = buildNameTags();
    view.sidebarVisible = sidebarView.visible && !(mods && mods->hidesHud(mod::HudElement::Sidebar));
    view.hiddenElements = state.hiddenElements | modHiddenElements();
    view.paperDoll = paperDollVisible() && !view.hidden(menu::HudElement::PaperDoll);
    if (view.sidebarVisible) {
        view.sidebar = sidebarData();
    }
    if (!(mods && mods->hidesHud(mod::HudElement::BossBars))) {
        for (const BossBarView& bar : state.bossBars) {
            view.bossBars.push_back({ bar.title, bar.progress, bar.color, bar.overlay });
        }
    }
    view.playerList = !(mods && mods->hidesHud(mod::HudElement::PlayerList));
    view.crosshair = perspective == PerspectiveFirst && !cameraDetached && !view.hidden(menu::HudElement::Crosshair);
    view.showHotbar = state.gameType != 6;
    view.showStats = state.gameType == 0 || state.gameType == 2;
    view.selected = std::clamp(state.selectedSlot, 0, 8);

    for (size_t index = 0; index < 9; ++index) view.hotbar[index] = inventoryIcon(state.inventory[index]);
    view.offhand = inventoryIcon(state.offhand);
    menu.inventoryPanel().itemIcon = [this](const HudItem& item) { return inventoryIcon(item); };
    menu.inventoryPanel().entityRenderer = [this](ui::Context& context, const std::string& identifier, const ui::Rect& rect, float alpha) {
        drawInventoryEntity(context, identifier, rect, alpha);
    };

    // The item name and popups come from one exclusive factory, so the newest replaces the other.
    const HudItem& held = state.inventory[size_t(view.selected)];
    if (popupMessage.shown > state.selectedChanged) {
        view.itemText = hudText(popupMessage, popupMessage.hold);
        view.jukebox = popupMessage.jukebox;
    } else if (!held.empty() && state.selectedChanged > 0.0) {
        HudMessage name { held.customName.empty() ? world::itemDisplayName(held.identifier) : held.customName, state.selectedChanged };
        view.itemText = hudText(name, 1.0f);
    }
    if (view.hidden(menu::HudElement::ItemText)) {
        view.itemText = {};
    }
    view.tip = hudText(tipMessage, 1.0f);
    view.actionbar = hudText(actionbarMessage, 0.0f);
    view.title = titleView;
    view.titleUpdates = std::move(titleUiUpdates);
    titleUiUpdates.clear();
    view.gameTip = gameTip.view;

    view.health = state.health;
    view.maxHealth = state.maxHealth;
    view.absorption = state.absorption;
    view.hunger = state.hunger;
    view.experience = state.experience;
    view.level = state.level;
    view.air = state.air;
    view.maxAir = state.maxAir;
    for (const HudItem& piece : state.armor) {
        if (!piece.empty()) {
            view.armor += world::itemArmorPoints(piece.identifier);
        }
    }
    view.previousHealth = state.healthFeedback.previousHealth;
    view.heartFlash = state.healthFeedback.flashing(now);
    for (const HudEffect& effect : state.effects) {
        double remaining = effect.expires < 0.0 ? 1.0e9 : effect.expires - now;
        if (remaining <= 0.0) {
            continue;
        }
        if (effect.id == 19) {
            view.heartKind = menu::HeartKind::Poison;
        } else if (effect.id == 20) {
            view.heartKind = menu::HeartKind::Wither;
        } else if (effect.id == 17) {
            view.hungerEffect = true;
        }
        menu::HudEffectView entry;
        entry.id = effect.id;
        entry.ambient = effect.ambient;
        if (remaining < 10.0) {
            double pulse = std::cos(remaining * 3.14159265 * 2.0) * 0.5 + 0.5;
            entry.alpha = static_cast<float>(std::clamp(remaining / 10.0 * 0.5 + pulse * 0.5, 0.0, 1.0));
        }
        view.effects.push_back(entry);
    }
    return view;
}

}
