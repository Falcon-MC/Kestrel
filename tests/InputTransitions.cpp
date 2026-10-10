#include "client/MotionInputBuffer.h"
#include "platform/Input.h"

int main()
{
    kestrel::InputState keys;
    keys.setKey(kestrel::Key::Space, true);
    const double receipt = keys.receiptTime;
    keys.setKey(kestrel::Key::Space, true);
    keys.setKey(kestrel::Key::Space, false);
    if (receipt == 0.0 || keys.receiptTime != receipt) return 1;
    keys.beginFrame();
    if (keys.receiptTime != 0.0) return 2;
    kestrel::InputState::KeyTransition transition;
    if (!keys.popKeyTransition(transition) || !transition.down || transition.key != kestrel::Key::Space) return 3;
    if (!keys.popKeyTransition(transition) || transition.down || transition.sequence != 2) return 4;
    if (keys.popKeyTransition(transition)) return 5;
    keys.setKey(kestrel::Key::Shift, true);
    keys.setKey(kestrel::Key::Control, true);
    keys.clearKeyTransitions();
    keys.releaseKeys();
    unsigned releases = 0;
    while (keys.popKeyTransition(transition)) {
        if (transition.down) return 6;
        ++releases;
    }
    if (releases != 2 || keys.isHeld(kestrel::Key::Shift)) return 7;

    kestrel::MotionInputBuffer buffer;
    kestrel::MotionInput input;
    input.jump = true;
    input.sprint = true;
    buffer.push(input);
    input.jump = false;
    input.sprint = false;
    input.yaw = 90.0f;
    input.forward = 1.0f;
    input.startFlying = true;
    buffer.push(input);
    input.startFlying = false;
    buffer.push(input);
    if (buffer.pending() != 2 || !buffer.preview().jump || buffer.pending() != 2) return 8;
    auto tick = buffer.consume();
    if (!tick.jump || !tick.sprint || !tick.startFlying || tick.forward != 1.0f || tick.yaw != 90.0f) return 9;
    tick = buffer.consume();
    if (tick.jump || tick.sprint || tick.startFlying || buffer.pending() != 0) return 10;
    if (buffer.consume().jump) return 11;

    for (size_t i = 0; i < kestrel::MotionInputBuffer::Capacity * 3; ++i) {
        input.jump = !input.jump;
        buffer.push(input);
    }
    if (buffer.pending() != kestrel::MotionInputBuffer::Capacity) return 12;
    while (buffer.pending() != 0) buffer.consume();
    if (buffer.consume().jump != input.jump) return 13;
    buffer.reset();
    if (buffer.consume().jump || buffer.pending() != 0) return 14;

    for (size_t i = 0; i < kestrel::InputState::TransitionCapacity * 3; ++i) {
        keys.setKey(kestrel::Key::Space, i % 2 == 0);
    }
    size_t transitions = 0;
    uint64_t last = 0;
    while (keys.popKeyTransition(transition)) {
        if (transition.sequence <= last) return 15;
        last = transition.sequence;
        ++transitions;
    }
    if (transitions != kestrel::InputState::TransitionCapacity || keys.isHeld(kestrel::Key::Space)) return 16;

    kestrel::GamepadState pad;
    std::array<bool, kestrel::PadButtonCount> buttons {};
    std::array<float, kestrel::PadAxisCount> axes {};
    pad.update(buttons, axes);
    if (pad.receiptTime != 0.0) return 17;
    buttons[static_cast<size_t>(kestrel::PadButton::A)] = true;
    pad.update(buttons, axes);
    if (pad.receiptTime == 0.0 || !pad.wasPressed(kestrel::PadButton::A)) return 18;
    pad.update(buttons, axes);
    if (pad.receiptTime != 0.0 || pad.wasPressed(kestrel::PadButton::A)) return 19;
    axes[static_cast<size_t>(kestrel::PadAxis::RightX)] = 0.5f;
    pad.update(buttons, axes);
    if (pad.receiptTime == 0.0) return 20;
    pad.disconnect();
    if (pad.receiptTime == 0.0 || !pad.wasReleased(kestrel::PadButton::A)) return 21;

    buffer.reset();
    input = {};
    input.trace = 101;
    input.jump = true;
    buffer.push(input);
    input.trace = 102;
    input.jump = false;
    buffer.push(input);
    input.trace = 103;
    buffer.push(input);
    if (buffer.preview().trace != 101 || buffer.consume().trace != 101) return 22;
    if (buffer.consume().trace != 102 || buffer.consume().trace != 103) return 23;
    input.trace = 104;
    input.startFlying = true;
    buffer.push(input);
    input.trace = 105;
    input.startFlying = false;
    buffer.push(input);
    tick = buffer.consume();
    if (!tick.startFlying || tick.trace != 104) return 24;
    if (buffer.consume().trace != 105 || buffer.consume().startFlying) return 25;

    keys.beginFrame();
    for (size_t i = 0; i < 100; ++i) {
        keys.recordMousePress(false);
        keys.recordMousePress(true);
    }
    if (keys.mousePressCount != 64 || keys.rightMousePressCount != 64 || keys.receiptTime == 0.0
        || !keys.mousePressed || !keys.rightMousePressed) return 26;
    keys.beginFrame();
    if (keys.mousePressCount != 0 || keys.rightMousePressCount != 0) return 27;

    keys.clearKeyTransitions();
    keys.setKey(kestrel::Key::Space, true);
    keys.setKey(kestrel::Key::Space, false);
    keys.setKey(kestrel::Key::Control, true);
    keys.setKey(kestrel::Key::Space, true);
    keys.setKey(kestrel::Key::Shift, true);
    keys.removeKeyTransitions(kestrel::Key::Space);
    if (!keys.popKeyTransition(transition) || transition.key != kestrel::Key::Control) return 28;
    if (!keys.popKeyTransition(transition) || transition.key != kestrel::Key::Shift) return 29;
    if (keys.popKeyTransition(transition)) return 30;
}
