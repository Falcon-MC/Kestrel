#pragma once

#include "mod/Canvas.h"
#include "mod/Event.h"

#include <string>
#include <vector>

namespace kestrel::mod {

/**
 * One line of a mod's generated settings page, bound to key in the mod's
 * Config. Toggle stores "true" or "false", Range a number from minimum to
 * maximum in steps of step (0 for any value), Choice one of choices, Key a
 * key name as key binds store it and Text any single line. defaultValue is
 * shown until the player changes the setting.
 */
struct SettingSpec {
    enum class Kind {
        Toggle,
        Range,
        Choice,
        Key,
        Text,
    };

    std::string key;
    std::string label;
    Kind kind = Kind::Toggle;
    double minimum = 0.0;
    double maximum = 1.0;
    double step = 0.0;
    std::vector<std::string> choices;
    std::string defaultValue;
};

// Main-thread only. Coordinates use Canvas units; IDs must be stable within a screen.
class Controls {
public:
    virtual ~Controls() = default;
    virtual bool button(std::string_view id, std::string_view label, Rect rect, bool enabled = true) = 0;
    virtual bool slider(std::string_view id, Rect rect, float& value, float minimum, float maximum, float step = 0.0f, bool enabled = true) = 0;
    // UTF-8, single line. Returns true when edited. maxBytes includes no terminator.
    virtual bool textField(std::string_view id, Rect rect, std::string& value, std::string_view placeholder = {}, size_t maxBytes = 1024, bool enabled = true) = 0;
    virtual void focus(std::string_view id) = 0;
    virtual bool focused(std::string_view id) const = 0;
};

// Delivered only to the owner of the topmost screen. References last for this callback.
struct UiRenderEvent : Event {
    KESTREL_EVENT("kestrel:ui_render/v1")
    UiRenderEvent(std::string id, Canvas& canvas, Controls& controls)
        : id(std::move(id)), canvas(canvas), controls(controls) { }
    std::string id;
    Canvas& canvas;
    Controls& controls;
};

// Screens are modal, Escape closes the top screen, and unloading closes the owner's screens.
class Ui {
public:
    virtual ~Ui() = default;

    virtual bool supported() const = 0;
    virtual bool open(std::string_view id) = 0;
    virtual bool close(std::string_view id) = 0;
    virtual bool isOpen(std::string_view id) const = 0;

    // Added in API 4 and kept last so older mods still find everything above.
    /**
     * Gives the mod a settings page built from settings, opened from its
     * entry in the Mods settings page or with ".mods settings <id>". Changes
     * are saved to the mod's Config right away and announced with a
     * ConfigReloadEvent for this mod. Calling it again replaces the page, an
     * empty list removes it. Throws std::invalid_argument for a setting
     * without a key, a key used twice, a Range whose bounds are not finite
     * and increasing, or a Choice without choices.
     */
    virtual void addSettings(std::vector<SettingSpec> settings) = 0;
};

}
