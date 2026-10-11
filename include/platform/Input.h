#pragma once

#include "platform/Gamepad.h"
#include "platform/Keys.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace kestrel {

struct InputState {
    struct KeyTransition {
        Key key = Key::None;
        bool down = false;
        uint64_t sequence = 0;
    };

    static constexpr size_t TransitionCapacity = 256;
    double receiptTime = 0.0;
    float mouseX = -1.0f;
    float mouseY = -1.0f;
    bool mouseDown = false;
    bool mousePressed = false;
    uint32_t mousePressCount = 0;
    bool mouseReleased = false;
    bool rightMouseDown = false;
    bool rightMousePressed = false;
    uint32_t rightMousePressCount = 0;
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

    void recordReceipt()
    {
        if (receiptTime == 0.0) {
            receiptTime = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    void recordMousePress(bool right)
    {
        recordReceipt();
        if (right) rightMousePressed = true;
        else mousePressed = true;
        uint32_t& count = right ? rightMousePressCount : mousePressCount;
        if (count < 64) ++count;
    }

    bool popKeyTransition(KeyTransition& transition)
    {
        if (transitionCount == 0) return false;
        transition = transitions[transitionHead];
        transitionHead = (transitionHead + 1) % TransitionCapacity;
        --transitionCount;
        return true;
    }

    void clearKeyTransitions()
    {
        transitionHead = 0;
        transitionCount = 0;
    }

    void removeKeyTransitions(Key key)
    {
        size_t retained = 0;
        for (size_t i = 0; i < transitionCount; ++i) {
            const auto transition = transitions[(transitionHead + i) % TransitionCapacity];
            if (transition.key != key) {
                transitions[(transitionHead + retained) % TransitionCapacity] = transition;
                ++retained;
            }
        }
        transitionCount = retained;
    }

    bool isHeld(Key key) const
    {
        return key != Key::None && static_cast<size_t>(key) < KeyCount && held[static_cast<size_t>(key)];
    }

    void setKey(Key key, bool down)
    {
        if (key == Key::None || static_cast<size_t>(key) >= KeyCount) {
            return;
        }
        if (held[static_cast<size_t>(key)] != down) {
            recordReceipt();
            if (transitionCount == TransitionCapacity) {
                transitionHead = (transitionHead + 1) % TransitionCapacity;
                --transitionCount;
            }
            transitions[(transitionHead + transitionCount) % TransitionCapacity] = { key, down, ++transitionSequence };
            ++transitionCount;
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
        receiptTime = 0.0;
        mousePressed = false;
        mousePressCount = 0;
        rightMousePressed = false;
        rightMousePressCount = 0;
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
        for (size_t i = 1; i < KeyCount; ++i) {
            if (held[i]) setKey(static_cast<Key>(i), false);
        }
    }

private:
    std::array<KeyTransition, TransitionCapacity> transitions {};
    size_t transitionHead = 0;
    size_t transitionCount = 0;
    uint64_t transitionSequence = 0;
};

}
