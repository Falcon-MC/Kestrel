#include "platform/Gamepad.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace kestrel {

void pollGamepad(GamepadState& state)
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_Gamepad* pad = nullptr;
    if (ids && count > 0) {
        // SDL keeps one handle per pad, so asking again each frame is cheap
        pad = SDL_GetGamepadFromID(ids[0]);
        if (!pad) {
            pad = SDL_OpenGamepad(ids[0]);
        }
    }
    SDL_free(ids);
    if (!pad) {
        state.disconnect();
        return;
    }
    auto down = [&](SDL_GamepadButton button) {
        return SDL_GetGamepadButton(pad, button);
    };
    auto axis = [&](SDL_GamepadAxis which) {
        return std::clamp(SDL_GetGamepadAxis(pad, which) / 32767.0f, -1.0f, 1.0f);
    };
    std::array<bool, PadButtonCount> buttons {};
    buttons[static_cast<size_t>(PadButton::A)] = down(SDL_GAMEPAD_BUTTON_SOUTH);
    buttons[static_cast<size_t>(PadButton::B)] = down(SDL_GAMEPAD_BUTTON_EAST);
    buttons[static_cast<size_t>(PadButton::X)] = down(SDL_GAMEPAD_BUTTON_WEST);
    buttons[static_cast<size_t>(PadButton::Y)] = down(SDL_GAMEPAD_BUTTON_NORTH);
    buttons[static_cast<size_t>(PadButton::LeftBumper)] = down(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    buttons[static_cast<size_t>(PadButton::RightBumper)] = down(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    buttons[static_cast<size_t>(PadButton::Back)] = down(SDL_GAMEPAD_BUTTON_BACK);
    buttons[static_cast<size_t>(PadButton::Start)] = down(SDL_GAMEPAD_BUTTON_START);
    buttons[static_cast<size_t>(PadButton::LeftStick)] = down(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    buttons[static_cast<size_t>(PadButton::RightStick)] = down(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    buttons[static_cast<size_t>(PadButton::DpadUp)] = down(SDL_GAMEPAD_BUTTON_DPAD_UP);
    buttons[static_cast<size_t>(PadButton::DpadDown)] = down(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    buttons[static_cast<size_t>(PadButton::DpadLeft)] = down(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    buttons[static_cast<size_t>(PadButton::DpadRight)] = down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    buttons[static_cast<size_t>(PadButton::Guide)] = down(SDL_GAMEPAD_BUTTON_GUIDE);
    std::array<float, PadAxisCount> axes {};
    axes[static_cast<size_t>(PadAxis::LeftX)] = axis(SDL_GAMEPAD_AXIS_LEFTX);
    axes[static_cast<size_t>(PadAxis::LeftY)] = -axis(SDL_GAMEPAD_AXIS_LEFTY);
    axes[static_cast<size_t>(PadAxis::RightX)] = axis(SDL_GAMEPAD_AXIS_RIGHTX);
    axes[static_cast<size_t>(PadAxis::RightY)] = -axis(SDL_GAMEPAD_AXIS_RIGHTY);
    // SDL already reports triggers from 0 up, unlike sticks
    axes[static_cast<size_t>(PadAxis::LeftTrigger)] = std::max(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f);
    axes[static_cast<size_t>(PadAxis::RightTrigger)] = std::max(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0f);
    state.connected = true;
    const char* name = SDL_GetGamepadName(pad);
    state.name = name ? name : "Gamepad";
    state.update(buttons, axes);
}

}
