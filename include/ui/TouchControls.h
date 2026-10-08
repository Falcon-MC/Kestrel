#pragma once
#include "platform/Input.h"
#include "ui/JsonUi.h"
#include <map>
#include <set>

namespace kestrel::menu { class Menu; }
namespace kestrel::ui {
class GameAssets;
class TouchControls {
public:
    void update(InputState& input, const KeyBindings& keys, const menu::Menu& menu, float scale,
        float width, float height, bool playing, double now, float lookScale = 1);
    void draw(Context& ui, GameAssets& assets, const menu::Menu& menu, const Rect& area);
private:
    struct Contact { std::string action; float x = 0, y = 0; double began = 0; bool dragged = false; };
    std::map<uint64_t, Contact> contacts;
    std::map<std::string, Rect> areas;
    std::set<std::string> held;
    std::array<bool, KeyCount> previousKeys {};
    std::shared_ptr<JsonUi> definitions;
    std::unique_ptr<JsonUiScreen> screen;
    bool attackHeld = false, useHeld = false, sneaking = false, sprinting = false;
    float stickX = 0, stickY = 0;
    int layoutSignature = -1;
};
}
