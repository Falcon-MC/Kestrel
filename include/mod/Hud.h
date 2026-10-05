#pragma once

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
};

}
