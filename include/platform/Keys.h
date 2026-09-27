#pragma once

#include <cstdint>
#include <string>

namespace kestrel {

enum class Key : uint8_t {
    None,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Shift, Control, Alt, Tab, Enter, Backspace, Escape,
    Up, Down, Left, Right,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count,
};

inline constexpr size_t KeyCount = static_cast<size_t>(Key::Count);

const char* keyName(Key key);
Key keyFromName(const std::string& name);

inline Key letterKey(uint32_t offset)
{
    return static_cast<Key>(static_cast<uint8_t>(Key::A) + offset);
}

inline Key digitKey(uint32_t offset)
{
    return static_cast<Key>(static_cast<uint8_t>(Key::Num0) + offset);
}

inline Key functionKey(uint32_t offset)
{
    return static_cast<Key>(static_cast<uint8_t>(Key::F1) + offset);
}

struct KeyBindings {
    static constexpr size_t Count = 10;

    Key keys[Count] = { Key::W, Key::S, Key::A, Key::D, Key::Space, Key::Shift, Key::F5, Key::T, Key::E, Key::Q };

    static const char* label(size_t index);
    static const char* translationKey(size_t index);
    static const char* id(size_t index);

    Key forward() const
    {
        return keys[0];
    }

    Key back() const
    {
        return keys[1];
    }

    Key left() const
    {
        return keys[2];
    }

    Key right() const
    {
        return keys[3];
    }

    Key up() const
    {
        return keys[4];
    }

    Key down() const
    {
        return keys[5];
    }

    Key perspective() const
    {
        return keys[6];
    }

    Key chat() const
    {
        return keys[7];
    }

    Key inventory() const { return keys[8]; }
    Key drop() const { return keys[9]; }

    bool operator==(const KeyBindings& other) const
    {
        for (size_t i = 0; i < Count; ++i) {
            if (keys[i] != other.keys[i]) {
                return false;
            }
        }
        return true;
    }
};

}
