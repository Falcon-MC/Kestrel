#pragma once

#include "mod/Event.h"
#include "mod/Types.h"

#include <functional>
#include <string>

namespace kestrel::mod {

/**
 * A key the player can remap in Keyboard & Mouse. It only appears there
 * while this mod is loaded. The chosen key is stored as bind.<id> in the
 * mod's config.txt.
 */
struct KeyBindSpec {
    std::string id;
    std::string label;
    Key defaultKey = Key::None;
};

class Input {
public:
    virtual ~Input() = default;

    virtual bool isHeld(Key key) const = 0;

    /**
     * True while playing: the mouse steers the camera and no screen, chat or
     * inventory is open.
     */
    virtual bool inGame() const = 0;
    virtual float mouseX() const = 0;
    virtual float mouseY() const = 0;

    /**
     * Runs action when key is pressed in game. The press is kept from the
     * rest of the client. The key is not listed in settings.
     */
    virtual Subscription bind(Key key, std::function<void()> action) = 0;

    /**
     * Same, and the player can change the key in Keyboard & Mouse.
     */
    virtual Subscription bind(KeyBindSpec spec, std::function<void()> action) = 0;
};

}
