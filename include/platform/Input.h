#pragma once

#include "platform/Gamepad.h"
#include "platform/Keys.h"

#include <array>
#include <string>
#include <vector>

namespace kestrel {

struct TouchPoint {
    uint64_t id = 0;
    float x = 0, y = 0, dx = 0, dy = 0;
    bool down = true, pressed = false, released = false, cancelled = false;
};

struct InputState {
    std::vector<TouchPoint> touches;
    float touchForward = 0, touchSideways = 0;
    float touchAimX = -1, touchAimY = -1;
    float mouseX = -1.0f;
    float mouseY = -1.0f;
    bool mouseDown = false;
    bool mousePressed = false;
    bool mouseReleased = false;
    bool rightMouseDown = false;
    bool rightMousePressed = false;
    bool rightMouseReleased = false;
    bool middleMousePressed = false;
    bool middleMouseReleased = false;
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
    Key releasedKey = Key::None;
    GamepadState gamepad;

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
        if (!down && held[static_cast<size_t>(key)]) {
            releasedKey = key;
        }
        held[static_cast<size_t>(key)] = down;
    }

    void beginFrame()
    {
        std::erase_if(touches, [](const TouchPoint& touch) { return !touch.down; });
        for (auto& touch : touches) {
            touch.pressed = touch.released = touch.cancelled = false;
            touch.dx = touch.dy = 0;
        }
        touchForward = touchSideways = 0;
        touchAimX = touchAimY = -1;
        mousePressed = false;
        rightMousePressed = false;
        middleMousePressed = false;
        rightMouseReleased = false;
        middleMouseReleased = false;
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
        releasedKey = Key::None;
    }

    void releaseKeys()
    {
        held.fill(false);
    }
};

}
