#include "client/Client.h"

#include "client/DebugLog.h"
#include "platform/Window.h"
#include "world/ItemInfo.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace kestrel {

menu::HudSlot Client::inventoryIcon(const HudItem& item)
{
    menu::HudSlot slot;
    if (item.empty() || !blockAssets) return slot;
    slot.filled = true;
    slot.count = item.count;
    std::string name = "item/" + item.identifier + "#" + std::to_string(item.aux) + "#" + item.icon;
    auto known = itemIcons.find(name);
    if (known == itemIcons.end()) {
        auto pixels = blockAssets->itemIcon(item.identifier, item.aux, item.icon);
        bool rendered = pixels.size() == size_t(world::ItemIconSize) * world::ItemIconSize * 4;
        if (rendered) skin.setDynamic(name, { world::ItemIconSize, world::ItemIconSize, std::move(pixels) });
        known = itemIcons.emplace(name, rendered).first;
    }
    if (known->second) slot.icon = name;
    int maximum = world::itemMaxDurability(item.identifier);
    if (maximum > 0 && item.damage > 0) slot.durability = std::clamp(float(maximum - item.damage) / maximum, 0.0f, 1.0f);
    return slot;
}

/**
 * Changes the held hotbar slot with the number keys and the mouse wheel while
 * the game has the mouse, and passes clicks on to the session: left hits,
 * right uses the held item.
 */
void Client::handleHotbarInput()
{
    if (!menu.capturesMouse() || !worldShown) {
        return;
    }
    const InputState& input = window->input();
    if (input.mousePressed) {
        session.requestInteraction(false);
    }
    if (input.rightMousePressed) {
        session.requestInteraction(true);
    }
    if (input.pressedKey == menu.keyBindings().drop()) {
        session.requestInventory({ InventoryAction::Drop, hudState.selectedSlot, 0, input.isHeld(Key::Control), {} });
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
    view.nameTags = buildNameTags();
    view.sidebarVisible = sidebarView.visible;
    if (sidebarView.visible) {
        view.sidebar = sidebarData();
    }
    view.showHotbar = state.gameType != 6;
    view.showStats = state.gameType == 0 || state.gameType == 2;
    view.selected = std::clamp(state.selectedSlot, 0, 8);

    for (size_t index = 0; index < 9; ++index) view.hotbar[index] = inventoryIcon(state.inventory[index]);
    view.offhand = inventoryIcon(state.offhand);
    menu.inventoryPanel().itemIcon = [this](const HudItem& item) { return inventoryIcon(item); };

    const HudItem& held = state.inventory[size_t(view.selected)];
    if (!held.empty()) {
        view.selectedName = held.customName.empty() ? world::itemDisplayName(held.identifier) : held.customName;
        double elapsed = now - state.selectedChanged;
        view.labelAlpha = elapsed < 1.5 ? 1.0f : elapsed < 2.0 ? static_cast<float>((2.0 - elapsed) / 0.5) : 0.0f;
    }

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
    double sinceDrop = now - state.lastHealthDrop;
    view.heartFlash = state.lastHealthDrop > 0.0 && sinceDrop < 1.0 && static_cast<int>(sinceDrop / 0.15) % 2 == 0;
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
