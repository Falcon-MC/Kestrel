#include "platform/Gamepad.h"

#import <GameController/GameController.h>

namespace kestrel {

void pollGamepad(GamepadState& state)
{
    @autoreleasepool {
        for (GCController* controller in [GCController controllers]) {
            GCExtendedGamepad* pad = controller.extendedGamepad;
            if (!pad) {
                continue;
            }
            std::array<bool, PadButtonCount> buttons {};
            buttons[static_cast<size_t>(PadButton::A)] = pad.buttonA.pressed;
            buttons[static_cast<size_t>(PadButton::B)] = pad.buttonB.pressed;
            buttons[static_cast<size_t>(PadButton::X)] = pad.buttonX.pressed;
            buttons[static_cast<size_t>(PadButton::Y)] = pad.buttonY.pressed;
            buttons[static_cast<size_t>(PadButton::LeftBumper)] = pad.leftShoulder.pressed;
            buttons[static_cast<size_t>(PadButton::RightBumper)] = pad.rightShoulder.pressed;
            buttons[static_cast<size_t>(PadButton::Back)] = pad.buttonOptions ? pad.buttonOptions.pressed : false;
            buttons[static_cast<size_t>(PadButton::Start)] = pad.buttonMenu.pressed;
            buttons[static_cast<size_t>(PadButton::LeftStick)] = pad.leftThumbstickButton ? pad.leftThumbstickButton.pressed : false;
            buttons[static_cast<size_t>(PadButton::RightStick)] = pad.rightThumbstickButton ? pad.rightThumbstickButton.pressed : false;
            buttons[static_cast<size_t>(PadButton::DpadUp)] = pad.dpad.up.pressed;
            buttons[static_cast<size_t>(PadButton::DpadDown)] = pad.dpad.down.pressed;
            buttons[static_cast<size_t>(PadButton::DpadLeft)] = pad.dpad.left.pressed;
            buttons[static_cast<size_t>(PadButton::DpadRight)] = pad.dpad.right.pressed;
            buttons[static_cast<size_t>(PadButton::Guide)] = pad.buttonHome ? pad.buttonHome.pressed : false;
            std::array<float, PadAxisCount> axes {};
            axes[static_cast<size_t>(PadAxis::LeftX)] = pad.leftThumbstick.xAxis.value;
            axes[static_cast<size_t>(PadAxis::LeftY)] = pad.leftThumbstick.yAxis.value;
            axes[static_cast<size_t>(PadAxis::RightX)] = pad.rightThumbstick.xAxis.value;
            axes[static_cast<size_t>(PadAxis::RightY)] = pad.rightThumbstick.yAxis.value;
            axes[static_cast<size_t>(PadAxis::LeftTrigger)] = pad.leftTrigger.value;
            axes[static_cast<size_t>(PadAxis::RightTrigger)] = pad.rightTrigger.value;
            state.connected = true;
            state.name = controller.vendorName ? std::string(controller.vendorName.UTF8String) : std::string("Gamepad");
            state.update(buttons, axes);
            return;
        }
    }
    state.disconnect();
}

}
