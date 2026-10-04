#include "platform/Gamepad.h"

#include <GLFW/glfw3.h>

namespace kestrel {

void pollGamepad(GamepadState& state)
{
    for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick) {
        GLFWgamepadstate reading;
        if (!glfwJoystickIsGamepad(joystick) || !glfwGetGamepadState(joystick, &reading)) {
            continue;
        }
        auto down = [&](int button) {
            return reading.buttons[button] == GLFW_PRESS;
        };
        std::array<bool, PadButtonCount> buttons {};
        buttons[static_cast<size_t>(PadButton::A)] = down(GLFW_GAMEPAD_BUTTON_A);
        buttons[static_cast<size_t>(PadButton::B)] = down(GLFW_GAMEPAD_BUTTON_B);
        buttons[static_cast<size_t>(PadButton::X)] = down(GLFW_GAMEPAD_BUTTON_X);
        buttons[static_cast<size_t>(PadButton::Y)] = down(GLFW_GAMEPAD_BUTTON_Y);
        buttons[static_cast<size_t>(PadButton::LeftBumper)] = down(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
        buttons[static_cast<size_t>(PadButton::RightBumper)] = down(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
        buttons[static_cast<size_t>(PadButton::Back)] = down(GLFW_GAMEPAD_BUTTON_BACK);
        buttons[static_cast<size_t>(PadButton::Start)] = down(GLFW_GAMEPAD_BUTTON_START);
        buttons[static_cast<size_t>(PadButton::LeftStick)] = down(GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
        buttons[static_cast<size_t>(PadButton::RightStick)] = down(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
        buttons[static_cast<size_t>(PadButton::DpadUp)] = down(GLFW_GAMEPAD_BUTTON_DPAD_UP);
        buttons[static_cast<size_t>(PadButton::DpadDown)] = down(GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
        buttons[static_cast<size_t>(PadButton::DpadLeft)] = down(GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
        buttons[static_cast<size_t>(PadButton::DpadRight)] = down(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
        buttons[static_cast<size_t>(PadButton::Guide)] = down(GLFW_GAMEPAD_BUTTON_GUIDE);
        std::array<float, PadAxisCount> axes {};
        axes[static_cast<size_t>(PadAxis::LeftX)] = reading.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
        axes[static_cast<size_t>(PadAxis::LeftY)] = -reading.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
        axes[static_cast<size_t>(PadAxis::RightX)] = reading.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
        axes[static_cast<size_t>(PadAxis::RightY)] = -reading.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
        axes[static_cast<size_t>(PadAxis::LeftTrigger)] = (reading.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
        axes[static_cast<size_t>(PadAxis::RightTrigger)] = (reading.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;
        state.connected = true;
        const char* name = glfwGetGamepadName(joystick);
        state.name = name ? name : "Gamepad";
        state.update(buttons, axes);
        return;
    }
    state.disconnect();
}

}
