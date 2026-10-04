#include "platform/Gamepad.h"

#include <windows.h>
#include <xinput.h>

#include <algorithm>

namespace kestrel {

namespace {

using GetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

/**
 * XInputGetState from whichever XInput the system has, loaded once so a
 * machine without it still starts.
 */
GetStateFunction xinputGetState()
{
    static GetStateFunction function = [] {
        for (const wchar_t* library : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" }) {
            if (HMODULE module = LoadLibraryW(library)) {
                if (auto found = reinterpret_cast<GetStateFunction>(GetProcAddress(module, "XInputGetState"))) {
                    return found;
                }
            }
        }
        return GetStateFunction { nullptr };
    }();
    return function;
}

float stick(SHORT value)
{
    return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
}

}

void pollGamepad(GamepadState& state)
{
    GetStateFunction getState = xinputGetState();
    if (!getState) {
        state.disconnect();
        return;
    }
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        XINPUT_STATE reading {};
        if (getState(slot, &reading) != ERROR_SUCCESS) {
            continue;
        }
        const XINPUT_GAMEPAD& pad = reading.Gamepad;
        auto down = [&](WORD mask) {
            return (pad.wButtons & mask) != 0;
        };
        std::array<bool, PadButtonCount> buttons {};
        buttons[static_cast<size_t>(PadButton::A)] = down(XINPUT_GAMEPAD_A);
        buttons[static_cast<size_t>(PadButton::B)] = down(XINPUT_GAMEPAD_B);
        buttons[static_cast<size_t>(PadButton::X)] = down(XINPUT_GAMEPAD_X);
        buttons[static_cast<size_t>(PadButton::Y)] = down(XINPUT_GAMEPAD_Y);
        buttons[static_cast<size_t>(PadButton::LeftBumper)] = down(XINPUT_GAMEPAD_LEFT_SHOULDER);
        buttons[static_cast<size_t>(PadButton::RightBumper)] = down(XINPUT_GAMEPAD_RIGHT_SHOULDER);
        buttons[static_cast<size_t>(PadButton::Back)] = down(XINPUT_GAMEPAD_BACK);
        buttons[static_cast<size_t>(PadButton::Start)] = down(XINPUT_GAMEPAD_START);
        buttons[static_cast<size_t>(PadButton::LeftStick)] = down(XINPUT_GAMEPAD_LEFT_THUMB);
        buttons[static_cast<size_t>(PadButton::RightStick)] = down(XINPUT_GAMEPAD_RIGHT_THUMB);
        buttons[static_cast<size_t>(PadButton::DpadUp)] = down(XINPUT_GAMEPAD_DPAD_UP);
        buttons[static_cast<size_t>(PadButton::DpadDown)] = down(XINPUT_GAMEPAD_DPAD_DOWN);
        buttons[static_cast<size_t>(PadButton::DpadLeft)] = down(XINPUT_GAMEPAD_DPAD_LEFT);
        buttons[static_cast<size_t>(PadButton::DpadRight)] = down(XINPUT_GAMEPAD_DPAD_RIGHT);
        std::array<float, PadAxisCount> axes {};
        axes[static_cast<size_t>(PadAxis::LeftX)] = stick(pad.sThumbLX);
        axes[static_cast<size_t>(PadAxis::LeftY)] = stick(pad.sThumbLY);
        axes[static_cast<size_t>(PadAxis::RightX)] = stick(pad.sThumbRX);
        axes[static_cast<size_t>(PadAxis::RightY)] = stick(pad.sThumbRY);
        axes[static_cast<size_t>(PadAxis::LeftTrigger)] = static_cast<float>(pad.bLeftTrigger) / 255.0f;
        axes[static_cast<size_t>(PadAxis::RightTrigger)] = static_cast<float>(pad.bRightTrigger) / 255.0f;
        state.connected = true;
        state.name = "Xbox Controller";
        state.update(buttons, axes);
        return;
    }
    state.disconnect();
}

}
