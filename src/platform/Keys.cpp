#include "platform/Keys.h"

#include <array>

namespace kestrel {

namespace {

constexpr std::array<const char*, KeyCount> Names = {
    "None",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "Space", "Shift", "Ctrl", "Alt", "Tab", "Enter", "Backspace", "Escape",
    "Up", "Down", "Left", "Right",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
};

}

const char* keyName(Key key)
{
    size_t index = static_cast<size_t>(key);
    return index < Names.size() ? Names[index] : "None";
}

Key keyFromName(const std::string& name)
{
    for (size_t i = 1; i < Names.size(); ++i) {
        if (name == Names[i]) {
            return static_cast<Key>(i);
        }
    }
    return Key::None;
}

const char* KeyBindings::label(size_t index)
{
    static constexpr const char* labels[Count] = { "Move forward", "Move back", "Strafe left", "Strafe right", "Fly up", "Fly down", "Toggle perspective", "Chat" };
    return index < Count ? labels[index] : "";
}

const char* KeyBindings::translationKey(size_t index)
{
    static constexpr const char* keys[Count] = { "key.forward", "key.back", "key.left", "key.right", "key.jump", "key.sneak", "key.togglePerspective", "key.chat" };
    return index < Count ? keys[index] : "";
}

const char* KeyBindings::id(size_t index)
{
    static constexpr const char* ids[Count] = { "forward", "back", "left", "right", "up", "down", "perspective", "chat" };
    return index < Count ? ids[index] : "";
}

}
