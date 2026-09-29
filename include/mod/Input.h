#pragma once

#include "mod/Event.h"
#include "mod/Types.h"

#include <functional>

namespace kestrel::mod {

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
     * rest of the client.
     */
    virtual Subscription bind(Key key, std::function<void()> action) = 0;
};

}
