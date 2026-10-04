#include "client/Client.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr float StickDeadzone = 0.24f;
constexpr float TriggerThreshold = 0.5f;
constexpr float CursorSpeed = 900.0f;
constexpr float ScrollNotchesPerSecond = 8.0f;

/**
 * A stick past its round deadzone, rescaled so the edge of the deadzone
 * reads as zero and a full push as one.
 */
std::array<float, 2> deadzoned(float x, float y)
{
    float magnitude = std::hypot(x, y);
    if (magnitude <= StickDeadzone) {
        return { 0.0f, 0.0f };
    }
    float scaled = (std::min(magnitude, 1.0f) - StickDeadzone) / (1.0f - StickDeadzone);
    return { x / magnitude * scaled, y / magnitude * scaled };
}

}

/**
 * Reads the gamepad and plays it through the keyboard and mouse input the
 * rest of the client already handles. In game the buttons hold the bound
 * keys (A jump, B sneak, left stick press sprint, Y inventory, the d-pad
 * perspective, drop and chat), the triggers attack and use, the bumpers
 * cycle the hotbar, Start pauses and the right stick turns the camera. In
 * menus the left stick moves a cursor A clicks with, B goes back, the right
 * stick scrolls and the d-pad moves the focus.
 */
void Client::driveGamepad()
{
    InputState& in = window->input();
    GamepadState& pad = in.gamepad;
    pollGamepad(pad);
    double now = secondsNow();
    float dt = static_cast<float>(std::clamp(now - padClock, 0.0, 0.1));
    padClock = now;

    bool playing = menu.capturesMouse();
    auto sync = [&](PadButton button, Key key, bool active) {
        size_t index = static_cast<size_t>(button);
        bool down = active && pad.connected && pad.isHeld(button);
        Key current = padKeys[index];
        if (down && current != key) {
            if (current != Key::None) {
                in.setKey(current, false);
            }
            in.setKey(key, true);
            padKeys[index] = key;
        } else if (!down && current != Key::None) {
            in.setKey(current, false);
            padKeys[index] = Key::None;
        }
    };
    auto trigger = [&](bool down, bool& held, bool& inputDown, bool& inputPressed, bool& inputReleased) {
        if (down && !held) {
            inputDown = true;
            inputPressed = true;
        } else if (!down && held) {
            inputDown = false;
            inputReleased = true;
        } else if (down) {
            inputDown = true;
        }
        held = down;
    };

    if (!pad.connected) {
        for (size_t i = 0; i < PadButtonCount; ++i) {
            sync(static_cast<PadButton>(i), Key::None, false);
        }
        if (padAttack || padUse || padClick) {
            bool off = false;
            trigger(off, padAttack, in.mouseDown, in.mousePressed, in.mouseReleased);
            trigger(off, padUse, in.rightMouseDown, in.rightMousePressed, in.rightMouseReleased);
            trigger(off, padClick, in.mouseDown, in.mousePressed, in.mouseReleased);
        }
        padCursorShown = false;
        return;
    }

    std::array<float, 2> left = deadzoned(pad.axis(PadAxis::LeftX), pad.axis(PadAxis::LeftY));
    std::array<float, 2> right = deadzoned(pad.axis(PadAxis::RightX), pad.axis(PadAxis::RightY));
    bool touched = left[0] != 0.0f || left[1] != 0.0f || right[0] != 0.0f || right[1] != 0.0f
        || pad.axis(PadAxis::LeftTrigger) > TriggerThreshold || pad.axis(PadAxis::RightTrigger) > TriggerThreshold
        || std::any_of(pad.held.begin(), pad.held.end(), [](bool held) { return held; });
    if (in.mouseDeltaX != 0.0f || in.mouseDeltaY != 0.0f) {
        padCursorShown = false;
    }
    if (touched) {
        padCursorShown = true;
    }

    const KeyBindings& keys = menu.keyBindings();
    sync(PadButton::A, keys.up(), playing);
    sync(PadButton::B, keys.down(), playing);
    sync(PadButton::LeftStick, Key::Control, playing);
    sync(PadButton::Y, keys.inventory(), playing || menu.inventoryOpen());
    sync(PadButton::DpadUp, playing ? keys.perspective() : Key::Up, true);
    sync(PadButton::DpadDown, playing ? keys.drop() : Key::Down, true);
    sync(PadButton::DpadRight, playing ? keys.chat() : Key::Right, true);
    sync(PadButton::DpadLeft, Key::Left, !playing);

    if (pad.wasPressed(PadButton::Start) || (!playing && pad.wasPressed(PadButton::B))) {
        in.escape = true;
    }

    if (playing) {
        trigger(pad.axis(PadAxis::RightTrigger) > TriggerThreshold, padAttack, in.mouseDown, in.mousePressed, in.mouseReleased);
        trigger(pad.axis(PadAxis::LeftTrigger) > TriggerThreshold, padUse, in.rightMouseDown, in.rightMousePressed, in.rightMouseReleased);
        if (pad.wasPressed(PadButton::LeftBumper)) {
            in.wheel += 1.0f;
        }
        if (pad.wasPressed(PadButton::RightBumper)) {
            in.wheel -= 1.0f;
        }
        float sensitivity = static_cast<float>(menu.option("controller_sensitivity", 50)) / 100.0f;
        float radiansPerSecond = 1.0f + 4.0f * sensitivity;
        float invert = menu.option("controller_invert_y_axis", 0) != 0 ? -1.0f : 1.0f;
        auto curve = [](float value) {
            return value * std::abs(value);
        };
        in.mouseDeltaX += curve(right[0]) * radiansPerSecond * dt / FreeCamera::LookSensitivity;
        in.mouseDeltaY -= invert * curve(right[1]) * radiansPerSecond * dt / FreeCamera::LookSensitivity;
        padMove = left;
        return;
    }

    padMove = { 0.0f, 0.0f };
    bool off = false;
    if (padAttack) {
        trigger(off, padAttack, in.mouseDown, in.mousePressed, in.mouseReleased);
    }
    if (padUse) {
        trigger(off, padUse, in.rightMouseDown, in.rightMousePressed, in.rightMouseReleased);
    }
    float width = static_cast<float>(window->width());
    float height = static_cast<float>(window->height());
    if (in.mouseX < 0.0f || in.mouseY < 0.0f) {
        in.mouseX = width * 0.5f;
        in.mouseY = height * 0.5f;
    }
    if (left[0] != 0.0f || left[1] != 0.0f) {
        float speed = CursorSpeed * window->contentScale();
        in.mouseX = std::clamp(in.mouseX + left[0] * speed * dt, 0.0f, width - 1.0f);
        in.mouseY = std::clamp(in.mouseY - left[1] * speed * dt, 0.0f, height - 1.0f);
    }
    trigger(pad.isHeld(PadButton::A), padClick, in.mouseDown, in.mousePressed, in.mouseReleased);
    padScroll += right[1] * ScrollNotchesPerSecond * dt;
    if (std::abs(padScroll) >= 1.0f) {
        float notches = std::trunc(padScroll);
        in.wheel += notches;
        padScroll -= notches;
    }
}

}
