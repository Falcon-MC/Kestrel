#pragma once

#include "mod/Types.h"

#include <optional>
#include <string_view>
#include <utility>

namespace kestrel::mod {

/**
 * Parts of the game's own HUD a mod can take off the screen, usually to draw
 * its own version in their place.
 */
enum class HudElement {
    Crosshair,
    Sidebar,
    Hotbar,
    Health,
    Hunger,
    Armor,
    AirBubbles,
    ExperienceBar,
    StatusEffects,
    PaperDoll,
    ItemText,
    BossBars,
    // the list Tab holds open
    PlayerList,
};

class Hud {
public:
    virtual ~Hud() = default;

    /**
     * Hides one of the game's HUD elements, or shows it again. It stays
     * hidden while any mod asks for it, and comes back when the mod unloads.
     * Elements the server hides stay hidden either way.
     */
    virtual void setHidden(HudElement element, bool hidden) = 0;
    // Whether this mod asked for it to be hidden.
    virtual bool hidden(HudElement element) const = 0;

    // Added in API 4 and kept last so older mods still find everything above.
    /**
     * Where a world position lands on the Canvas, in interface units from
     * the top left, as the last drawn frame saw it; empty when it is behind
     * the camera or no world was drawn yet. It may be off screen.
     */
    virtual std::optional<std::pair<float, float>> project(const Vec3& world) const = 0;

    /**
     * Shows text in a small notification over the HUD for seconds, at most a
     * minute. Notifications stack in the top right corner, the newest last,
     * fade out at the end and go away when the mod unloads.
     */
    virtual void notify(std::string_view text, double seconds) = 0;
};

}
