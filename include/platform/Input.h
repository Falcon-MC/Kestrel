#pragma once

#include "platform/Keys.h"

#include <array>
#include <string>

namespace kestrel {

struct InputState {
    float mouseX = -1.0f;
    float mouseY = -1.0f;
    bool mouseDown = false;
    bool mousePressed = false;
    bool mouseReleased = false;
    bool rightMouseDown = false;
    bool rightMousePressed = false;
    bool middleMousePressed = false;
    float wheel = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    std::u32string text;
    bool backspace = false;
    bool enter = false;
    bool escape = false;
    bool tab = false;
    std::array<bool, KeyCount> held {};
    Key pressedKey = Key::None;

    bool isHeld(Key key) const
    {
        return key != Key::None && held[static_cast<size_t>(key)];
    }

    void setKey(Key key, bool down)
    {
        if (key == Key::None) {
            return;
        }
        if (down && !held[static_cast<size_t>(key)]) {
            pressedKey = key;
        }
        held[static_cast<size_t>(key)] = down;
    }

    void beginFrame()
    {
        mousePressed = false;
        rightMousePressed = false;
        middleMousePressed = false;
        mouseReleased = false;
        wheel = 0.0f;
        mouseDeltaX = 0.0f;
        mouseDeltaY = 0.0f;
        text.clear();
        backspace = false;
        enter = false;
        escape = false;
        tab = false;
        pressedKey = Key::None;
    }

    void releaseKeys()
    {
        held.fill(false);
    }
};

}
