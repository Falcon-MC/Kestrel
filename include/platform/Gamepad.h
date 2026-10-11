#pragma once

#include <array>
#include <cstddef>
#include <chrono>
#include <string>

namespace kestrel {

/**
 * The buttons of a standard gamepad, named after an Xbox controller.
 */
enum class PadButton {
    A,
    B,
    X,
    Y,
    LeftBumper,
    RightBumper,
    Back,
    Start,
    LeftStick,
    RightStick,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Guide,
    Count,
};

/**
 * Its axes: the sticks from -1 to 1 with up positive, the triggers from 0 to 1.
 */
enum class PadAxis {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

inline constexpr size_t PadButtonCount = static_cast<size_t>(PadButton::Count);
inline constexpr size_t PadAxisCount = static_cast<size_t>(PadAxis::Count);

/**
 * The first connected gamepad, read once a frame: what is held, what went
 * down or up since the last read, and where its sticks and triggers are.
 */
struct GamepadState {
    bool connected = false;
    double receiptTime = 0.0;
    std::string name;
    std::array<float, PadAxisCount> axes {};
    std::array<bool, PadButtonCount> held {};
    std::array<bool, PadButtonCount> pressed {};
    std::array<bool, PadButtonCount> released {};

    bool isHeld(PadButton button) const
    {
        return held[static_cast<size_t>(button)];
    }

    bool wasPressed(PadButton button) const
    {
        return pressed[static_cast<size_t>(button)];
    }

    bool wasReleased(PadButton button) const
    {
        return released[static_cast<size_t>(button)];
    }

    float axis(PadAxis which) const
    {
        return axes[static_cast<size_t>(which)];
    }

    /**
     * Takes this frame's held buttons and axes, working out which buttons
     * went down or up since the last frame.
     */
    void update(const std::array<bool, PadButtonCount>& now, const std::array<float, PadAxisCount>& values)
    {
        receiptTime = 0.0;
        if (now != held || values != axes) {
            receiptTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        for (size_t i = 0; i < PadButtonCount; ++i) {
            pressed[i] = now[i] && !held[i];
            released[i] = !now[i] && held[i];
        }
        held = now;
        axes = values;
    }

    void disconnect()
    {
        std::array<bool, PadButtonCount> none {};
        update(none, {});
        connected = false;
        name.clear();
    }
};

/**
 * Reads the first connected gamepad into state, through the platform's own
 * controller API.
 */
void pollGamepad(GamepadState& state);

}
