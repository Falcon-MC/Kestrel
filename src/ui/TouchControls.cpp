#include "ui/TouchControls.h"
#include "ui/Context.h"
#include "ui/GameAssets.h"
#include "menu/Menu.h"
#include <algorithm>
#include <cmath>

namespace kestrel::ui {
void TouchControls::update(InputState& input, const KeyBindings& keys, const menu::Menu& menu,
    float scale, float width, float height, bool playing, double now, float lookScale)
{
    held.clear();
    stickX = stickY = 0;
    std::array<bool, KeyCount> down {};
    auto key = [&](Key code) { if (code != Key::None) down[size_t(code)] = true; };
    bool attack = false, use = false;
    int mode = menu.option("touch_control_mode", 1);
    bool tapping = mode != 1 && !menu.option("split_controls", 0);
    if (!playing) {
        contacts.clear();
        sneaking = sprinting = false;
    } else for (const TouchPoint& point : input.touches) {
        float x = point.x / scale, y = point.y / scale;
        if (point.pressed) {
            Contact contact { "look", x, y, now, false };
            for (const auto& [action, area] : areas) if (area.contains(x, y)) { contact.action = action; break; }
            if (contact.action == "sneak" && menu.option("sneak", 0) == 0) sneaking = !sneaking;
            if (contact.action == "sprint") sprinting = !sprinting;
            contacts[point.id] = std::move(contact);
        }
        auto found = contacts.find(point.id);
        if (found == contacts.end()) continue;
        Contact& contact = found->second;
        const std::string& action = contact.action;
        if (point.cancelled) { contacts.erase(found); continue; }
        if (point.down || point.pressed) held.insert(action);
        if (action == "move" && point.down) {
            const Rect& area = areas.at(action);
            float radius = std::min(area.w, area.h) * 0.4f;
            stickX = (x - (area.x + area.w / 2)) / radius;
            stickY = (y - (area.y + area.h / 2)) / radius;
            float length = std::hypot(stickX, stickY);
            if (length > 1) { stickX /= length; stickY /= length; }
            if (length > 0.15f) { input.touchForward = -stickY; input.touchSideways = -stickX; }
        } else if (action == "look") {
            contact.dragged |= std::hypot(x - contact.x, y - contact.y) > 6;
            if (contact.dragged) {
                float sensitivity = (0.25f + menu.option("touch_sensitivity", 50) / 50.0f) * lookScale;
                input.mouseDeltaX += point.dx / scale * sensitivity;
                input.mouseDeltaY += point.dy / scale * sensitivity * (menu.option("touch_invert_y_axis", 0) ? -1 : 1);
            } else if (tapping) {
                input.touchAimX = point.x / width;
                input.touchAimY = point.y / height;
                if (point.down && now - contact.began >= 0.35) attack = true;
                if (point.released && now - contact.began < 0.35) use = true;
            }
        } else if (point.down || point.pressed) {
            if (action == "forward") key(keys.forward());
            else if (action == "back") key(keys.back());
            else if (action == "left") key(keys.left());
            else if (action == "right") key(keys.right());
            else if (action == "jump") key(keys.up());
            else if (action == "sneak" && menu.option("sneak", 0) == 1) key(keys.down());
            else if (action == "attack") attack = true;
            else if (action == "use") use = true;
            if (point.pressed) {
                if (action == "inventory") key(keys.inventory());
                else if (action == "chat") key(keys.chat());
                else if (action == "pause") { key(Key::Escape); input.escape = true; }
                else if (action == "perspective") key(keys.perspective());
                else if (action == "pick") input.middleMousePressed = true;
                else if (action.starts_with("slot")) key(digitKey(uint32_t(action.back() - '0')));
            }
        }
        if (!point.down) contacts.erase(found);
    }
    if (playing && sneaking) key(keys.down());
    if (playing && (sprinting || (menu.option("sprint_on_movement", 0) && input.touchForward > 0.85f))) key(Key::Control);
    for (size_t i = 1; i < KeyCount; ++i) {
        if (down[i] && !previousKeys[i]) input.pressedKey = Key(i);
        if (!down[i] && previousKeys[i] && !input.held[i]) input.releasedKey = Key(i);
        input.held[i] |= down[i];
    }
    previousKeys = down;
    auto click = [](bool down, bool& previous, bool& value, bool& pressed, bool& released) {
        pressed |= down && !previous;
        released |= !down && previous;
        value |= down;
        previous = down;
    };
    click(attack, attackHeld, input.mouseDown, input.mousePressed, input.mouseReleased);
    click(use, useHeld, input.rightMouseDown, input.rightMousePressed, input.rightMouseReleased);
}

void TouchControls::draw(Context& ui, GameAssets& assets, const menu::Menu& menu, const Rect& area)
{
    int signature = menu.option("touch_button_size", 100) * 10 + menu.option("left_handed", 0) * 3 + menu.option("top_button_scale", 1);
    if (!screen || signature != layoutSignature) {
        layoutSignature = signature;
        auto bytes = assets.readPackFile("ui/kestrel_touch_controls.json");
        definitions = std::make_shared<JsonUi>();
        definitions->addFile("ui/kestrel_touch_controls.json", std::string(bytes.begin(), bytes.end()));
        UiRow variables;
        double size = 36.0 * menu.option("touch_button_size", 100) / 100.0;
        bool left = menu.option("left_handed", 0) != 0;
        variables["$button_size"] = UiValue::of(size);
        variables["$stick_size"] = UiValue::of(size * 2.5);
        variables["$top_size"] = UiValue::of(24.0 + menu.option("top_button_scale", 1) * 6);
        variables["$move_anchor"] = UiValue::of(left ? "bottom_right" : "bottom_left");
        variables["$action_anchor"] = UiValue::of(left ? "bottom_left" : "bottom_right");
        variables["$move_x"] = UiValue::of(left ? -12.0 : 12.0);
        variables["$action_x"] = UiValue::of(left ? 12.0 : -12.0);
        screen = std::make_unique<JsonUiScreen>(definitions, "kestrel_touch.hud", variables);
        screen->setRenderer([this, &menu](Context& context, const std::string& name, const Rect& rect, float alpha, const UiLookup&) {
            std::string action = name;
            if (menu.option("swap_jump_and_sneak", 0)) {
                if (action == "jump") action = "sneak";
                else if (action == "sneak") action = "jump";
            }
            areas[action] = rect;
            if (action.starts_with("slot")) return;
            bool pressed = held.contains(action) || (action == "sneak" && sneaking) || (action == "sprint" && sprinting);
            if (action == "move") {
                int visibility = menu.option("joystick_visibility", 0);
                if (visibility == 1 || (visibility == 2 && !pressed)) return;
            }
            uint8_t opacity = uint8_t(std::clamp(alpha * menu.option("touch_control_opacity", 70) / 100.0f, 0.0f, 1.0f) * 255);
            Color tint { 255, 255, 255, opacity };
            if (action == "move") {
                context.sprite(rect, "ui/joystick_frame", tint);
                Rect knob { rect.x + rect.w * (0.25f + stickX * 0.25f), rect.y + rect.h * (0.25f + stickY * 0.25f), rect.w * 0.5f, rect.h * 0.5f };
                context.sprite(knob, "ui/joystick_knob", tint);
                return;
            }
            static const std::map<std::string, std::string> Buttons {
                {"jump", "jump"}, {"sneak", "sneak"}, {"sprint", "sprint"}, {"attack", "attack"},
                {"use", "interact"}, {"pick", "pick_block"}
            };
            if (auto button = Buttons.find(action); button != Buttons.end()) {
                context.sprite(rect, "ui/" + button->second + (pressed ? "_pressed" : ""), tint);
                return;
            }
            context.fill(rect, { uint8_t(pressed ? 100 : 25), uint8_t(pressed ? 100 : 25), uint8_t(pressed ? 100 : 25), opacity });
            context.outline(rect, { 230, 230, 230, opacity }, 1);
            static const std::map<std::string, std::string> Icons {
                {"inventory", "inventory_icon"}, {"chat", "chat_send"}, {"pause", "pause_icon"},
                {"forward", "up_arrow"}, {"back", "down_arrow"}, {"left", "arrowLeft"}, {"right", "arrowRight"},
                {"perspective", "camera-small"}
            };
            if (auto icon = Icons.find(action); icon != Icons.end()) context.sprite(rect.inset(rect.w * 0.2f), "ui/" + icon->second, tint);
        });
    }
    areas.clear();
    UiData data;
    data.globals["#joystick"] = UiValue::of(menu.option("touch_control_mode", 1) != 2);
    data.globals["#dpad"] = UiValue::of(menu.option("touch_control_mode", 1) == 2);
    data.globals["#actions"] = UiValue::of(menu.option("touch_control_mode", 1) == 1 || menu.option("show_action_button", 1));
    data.globals["#perspective"] = UiValue::of(menu.option("show_toggle_camera_perspective_button", 0) != 0);
    data.globals["#pick"] = UiValue::of(menu.option("show_block_select_button", 0) != 0);
    data.globals["#controls"] = UiValue::of(menu.option("hotbar_only_touch", 0) == 0);
    Rect safe = area.inset(8);
    screen->draw(ui, safe, data);
}
}
