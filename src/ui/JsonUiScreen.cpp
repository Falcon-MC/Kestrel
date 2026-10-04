#include "JsonUiInternal.h"

#include "platform/Input.h"
#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Utf8.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace kestrel::ui {

using namespace jsonui;

namespace {

/**
 * Whether this frame brings no input a screen reacts to: no click, drag,
 * wheel, key or typed text.
 */
bool quiet(const Context& ui)
{
    const InputState& in = ui.input();
    return !in.mousePressed && !in.mouseDown && !in.mouseReleased && !in.rightMousePressed && !in.rightMouseDown
        && in.wheel == 0.0f && in.text.empty() && in.pressedKey == Key::None && !in.backspace && !in.enter && !in.escape && !in.tab;
}

/**
 * Whether a control or any below it still has an animation running or a
 * control fading out, which change what is drawn from frame to frame.
 */
bool animating(const Node& node)
{
    if (!node.shown) {
        return false;
    }
    if (node.destroyed) {
        return true;
    }
    for (const AnimTrack& track : node.anims) {
        if (track.start >= 0.0 && !track.finished) {
            return true;
        }
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        if (animating(*child)) {
            return true;
        }
    }
    return false;
}

constexpr float CaretHeight = 7.0f;
constexpr size_t DefaultMaxLength = 256;
constexpr const char* ToggleStates[] = {
    "unchecked_control",
    "checked_control",
    "unchecked_hover_control",
    "checked_hover_control",
    "unchecked_locked_control",
    "checked_locked_control",
    "unchecked_locked_hover_control",
    "checked_locked_hover_control",
};

double secondsNow()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Rect intersect(const Rect& a, const Rect& b)
{
    float x = std::max(a.x, b.x);
    float y = std::max(a.y, b.y);
    float right = std::min(a.right(), b.right());
    float bottom = std::min(a.bottom(), b.bottom());
    return { x, y, std::max(0.0f, right - x), std::max(0.0f, bottom - y) };
}

std::string stripExtension(std::string texture)
{
    for (const char* extension : { ".png", ".jpg", ".jpeg", ".tga" }) {
        size_t length = std::char_traits<char>::length(extension);
        if (texture.size() > length && texture.compare(texture.size() - length, length, extension) == 0) {
            texture.erase(texture.size() - length);
            break;
        }
    }
    return texture;
}

}

bool JsonUiRuntime::isControl(const Node& node)
{
    return node.type == "button" || node.type == "toggle" || node.type == "dropdown" || node.type == "slider" || node.type == "slider_box" || node.type == "edit_box";
}

Node* JsonUiRuntime::find(Node& from, const std::string& name) const
{
    if (from.name == name) {
        return &from;
    }
    for (std::unique_ptr<Node>& child : from.children) {
        if (Node* found = find(*child, name)) {
            return found;
        }
    }
    return nullptr;
}

Node* JsonUiRuntime::named(const std::string& name)
{
    if (!root) {
        return nullptr;
    }
    if (namedCacheId != nextId || namedCacheDestroyed != jsonui::destroyedNodes) {
        namedCache.clear();
        namedCacheId = nextId;
        namedCacheDestroyed = jsonui::destroyedNodes;
    }
    if (auto found = namedCache.find(name); found != namedCache.end()) {
        return found->second;
    }
    Node* found = find(*root, name);
    namedCache.emplace(name, found);
    return found;
}

Node* JsonUiRuntime::nearest(Node& from, const std::string& name) const
{
    if (name.empty()) {
        return nullptr;
    }
    std::vector<Node*> queue { &from };
    for (size_t i = 0; i < queue.size(); ++i) {
        if (queue[i]->name == name) {
            return queue[i];
        }
        for (std::unique_ptr<Node>& child : queue[i]->children) {
            queue.push_back(child.get());
        }
    }
    return nullptr;
}

Node* JsonUiRuntime::ancestor(Node& node, const std::string& type) const
{
    for (Node* at = node.parent; at; at = at->parent) {
        if (at->type == type) {
            return at;
        }
    }
    return nullptr;
}

/**
 * Whether a key held down reaches the button through one of its global
 * button mappings.
 */
bool JsonUiRuntime::heldDown(const Node& node) const
{
    if (held.empty() || node.type != "button") {
        return false;
    }
    const json::Value* mappings = property(node, "button_mappings");
    if (!mappings || !mappings->isArray()) {
        return false;
    }
    for (const std::unique_ptr<json::Value>& mapping : mappings->mArray) {
        const json::Value* from = mapping->isObject() ? resolve(node, mapping->get("from_button_id")) : nullptr;
        const json::Value* type = mapping->isObject() ? resolve(node, mapping->get("mapping_type")) : nullptr;
        if (from && from->isString() && type && type->string() == "global" && std::find(held.begin(), held.end(), from->mString) != held.end()) {
            return true;
        }
    }
    return false;
}

/**
 * Picks which state children of a button, toggle, slider or edit box show,
 * from whether the mouse is over it, pressing it or it is locked, the way
 * default_control, hover_control and the rest name them.
 */
void JsonUiRuntime::chooseStates(Node& node)
{
    node.states.clear();
    bool hovered = hot == node.id || keyFocus == node.id || std::find(passedHover.begin(), passedHover.end(), node.id) != passedHover.end();
    bool pressed = (active == node.id && hovered && ui && ui->input().mouseDown) || heldDown(node);
    auto show = [&](const char* property, bool visible) {
        std::string name = text(node, property);
        if (name.empty()) {
            return;
        }
        for (std::unique_ptr<Node>& child : node.children) {
            if (child->name == name) {
                auto [entry, added] = node.states.emplace(child.get(), visible);
                if (!added) {
                    entry->second = entry->second || visible;
                }
            }
        }
    };
    if (node.type == "button" || node.type == "edit_box") {
        bool typing = node.type == "edit_box" && focused == node.id;
        std::string lockedName = text(node, "locked_control");
        bool locked = !node.enabled && !lockedName.empty();
        show("default_control", !locked && !hovered && !pressed && !typing);
        show("hover_control", !locked && hovered && !pressed && !typing);
        show("pressed_control", !locked && (pressed || typing));
        show("locked_control", locked);
        return;
    }
    if (node.type == "toggle" || node.type == "dropdown") {
        bool checked = node.dataToggle ? node.bound["#toggle_state"].truthy() : node.toggled;
        size_t chosen = (checked ? 1 : 0) + (hovered ? 2 : 0) + (node.enabled ? 0 : 4);
        for (size_t i = 0; i < std::size(ToggleStates); ++i) {
            show(ToggleStates[i], false);
        }
        show(ToggleStates[chosen], true);
        return;
    }
    if (node.type == "slider_box") {
        Node* slider = ancestor(node, "slider");
        bool lit = slider && (hot == slider->id || active == slider->id);
        bool locked = slider && !slider->enabled;
        show("default_control", !locked && !lit);
        show("hover_control", !locked && lit);
        show("indent_control", false);
        show("locked_control", locked);
        return;
    }
    if (node.type == "slider") {
        bool lit = hovered || active == node.id;
        show("default_control", !lit);
        show("hover_control", lit);
        if (active != node.id) {
            if (auto value = node.bound.find("#slider_value"); value != node.bound.end()) {
                node.value = static_cast<float>(std::clamp(value->second.toNumber(), 0.0, 1.0));
            }
        }
        int steps = static_cast<int>(node.bound.count("#slider_steps") ? node.bound.at("#slider_steps").toNumber() : 1.0);
        float width = node.w;
        if (Node* box = find(node, text(node, "slider_box_control")); box && box != &node) {
            box->offsetOverride = std::array<float, 2> { std::round(node.value * width - width * 0.5f), 0.0f };
        }
        // The step marks come from slider_step_factory, filled from code in the game.
        std::vector<UiFactoryItem> marks;
        if (steps > 1) {
            int selected = static_cast<int>(std::round(node.value * static_cast<float>(steps - 1)));
            for (int i = 0; i < steps; ++i) {
                UiFactoryItem mark;
                mark.control = std::string(i <= selected ? "slider_step_progress" : "slider_step") + (lit ? "_hover" : "");
                mark.serial = static_cast<uint64_t>(i);
                marks.push_back(std::move(mark));
            }
        }
        const json::Value* factory = property(node, "factory");
        syncItems(node, marks, factory ? resolve(node, factory->get("control_ids")) : nullptr, nullptr, MaxFactoryItems, 0);
        for (std::unique_ptr<Node>& child : node.children) {
            if (child->generated && steps > 1) {
                float x = width * static_cast<float>(child->serial) / static_cast<float>(steps - 1) - width * 0.5f;
                child->offsetOverride = std::array<float, 2> { std::round(x), 0.0f };
            }
        }
    }
}

/**
 * State a control pushes into the controls inside it: the text an edit box
 * is typing into its label, and how far a slider's progress image shows.
 */
void JsonUiRuntime::overrideState(Node& control, Node& node)
{
    if (control.type == "edit_box" && node.name == text(control, "text_control")) {
        bool typing = focused == control.id;
        bool listening = flag(control, "always_listening", false);
        node.selected = !typing && listeningSelected && listening;
        node.caret = !node.selected && (typing || (listeningCaret && listening));
        if (typing) {
            node.bound["#item_name"] = UiValue::of(control.edit);
        }
        return;
    }
    if (control.type == "slider") {
        bool lit = hot == control.id || active == control.id;
        std::string background = text(control, "background_control");
        std::string backgroundHover = text(control, "background_hover_control");
        std::string progress = text(control, "progress_control");
        std::string progressHover = text(control, "progress_hover_control");
        if (node.name == progress || node.name == progressHover) {
            node.bound["#clip_ratio"] = UiValue::of(1.0 - static_cast<double>(control.value));
            node.bound["#visible"] = UiValue::of(node.name == progress ? !lit : lit);
        } else if (node.name == background || node.name == backgroundHover) {
            node.bound["#visible"] = UiValue::of(node.name == background ? !lit : lit);
        }
    }
}

/**
 * Drops what a sizing pass measured, so the next pass measures again with
 * the sizes the last one gave.
 */
static void forgetMeasures(Node& node)
{
    node.measured = { false, false };
    for (std::unique_ptr<Node>& child : node.children) {
        forgetMeasures(*child);
    }
}

/**
 * Readies a shown control for layout: its text or texture, alpha and size
 * expressions, then its children.
 */
static void prepareTree(JsonUiRuntime& runtime, Node& node, bool shown)
{
    node.shown = shown && node.visible;
    node.measured = { false, false };
    if (!node.shown) {
        return;
    }
    if (node.type == "label") {
        UiValue value = runtime.valueOf(node, "text");
        if (auto bound = node.bound.find("#text"); bound != node.bound.end() && !node.props.count("text")) {
            value = bound->second;
        }
        node.text = value.toText();
        if (runtime.flag(node, "localize", true) && !node.text.empty()) {
            node.text = Localization::shared().label(node.text);
        }
    } else if (node.type == "image") {
        if (auto bound = node.bound.find("#texture"); bound != node.bound.end()) {
            node.texture = bound->second.toText();
        } else {
            node.texture = runtime.valueOf(node, "texture").toText();
        }
        node.texture = stripExtension(std::move(node.texture));
    }
    if (std::optional<float> alpha = runtime.animated(node, "alpha", 0, 0.0f)) {
        bool scaled = std::any_of(node.anims.begin(), node.anims.end(), [&](const AnimTrack& track) {
            if (track.target != "alpha") {
                return false;
            }
            Node probe;
            probe.vars = node.vars;
            probe.props = track.props;
            return runtime.flag(probe, "scale_from_starting_alpha", false);
        });
        const json::Value* starting = runtime.property(node, "alpha");
        float base = starting && starting->isNumber() ? static_cast<float>(starting->number(1.0)) : 1.0f;
        node.alpha = scaled ? *alpha * base : *alpha;
    } else if (auto bound = node.bound.find("#alpha"); bound != node.bound.end()) {
        node.alpha = static_cast<float>(bound->second.toNumber());
    } else if (const json::Value* alpha = runtime.property(node, "alpha"); alpha && alpha->isString() && !alpha->mString.empty() && alpha->mString.front() == '@') {
        node.alpha = 1.0f;
    } else {
        node.alpha = static_cast<float>(runtime.number(node, "alpha", 1.0));
    }
    if (auto bound = node.bound.find("#propagateAlpha"); bound != node.bound.end()) {
        node.propagate = bound->second.truthy();
    } else {
        node.propagate = runtime.flag(node, "propagate_alpha", false);
    }
    node.vertical = runtime.text(node, "orientation") != "horizontal";

    // Without a size a label or grid fits its content, a stack panel its children along
    // its orientation, and everything else fills its parent.
    const json::Value* size = runtime.property(node, "size");
    auto extent = [&](size_t axis) {
        bool naturalImage = node.type == "image" && runtime.flag(node, "default_size_scales_to_ratio", false);
        TermKind fallback = node.type == "label" || node.type == "grid" || naturalImage ? TermKind::Default : TermKind::Parent;
        if (node.type == "stack_panel" && (axis == 1) == node.vertical) {
            fallback = TermKind::Children;
        }
        const json::Value* value = size && size->isArray() && size->mArray.size() == 2 ? runtime.resolve(node, size->mArray[axis].get()) : nullptr;
        if (value && value->isString() && !value->mString.empty() && value->mString.front() == '#') {
            return Extent { { TermKind::Pixel, static_cast<float>(runtime.lookup(node, value->mString).toNumber()) } };
        }
        Extent parsed = parseExtent(value, fallback);
        // "default" is what a label, image or grid holds and what a stack panel stacks along
        // its orientation; any other control takes its parent's size.
        bool fits = node.type == "label" || naturalImage || node.type == "grid" || (node.type == "stack_panel" && (axis == 1) == node.vertical);
        for (Term& part : parsed) {
            if (part.kind == TermKind::Default && !fits) {
                part.kind = TermKind::Parent;
            }
        }
        // "fill" outside a stack panel takes what the parent has, like 100% does.
        if (!node.parent || node.parent->type != "stack_panel") {
            for (Term& part : parsed) {
                if (part.kind == TermKind::Fill) {
                    part.kind = TermKind::Parent;
                }
            }
        }
        return parsed;
    };
    node.width = extent(0);
    node.height = extent(1);

    if (node.type == "scroll_view") {
        if (Node* content = runtime.nearest(node, runtime.text(node, "scroll_content")); content && content != &node) {
            content->scroller = &node;
        }
    }
    for (std::unique_ptr<Node>& child : node.children) {
        prepareTree(runtime, *child, true);
    }
}

void JsonUiRuntime::gather(Node& node)
{
    if (!node.shown) {
        return;
    }
    order.push_back(&node);
    byId[node.id] = &node;
    for (std::unique_ptr<Node>& child : node.children) {
        gather(*child);
    }
}

/**
 * Sends an animation event to node and everything under it: anims waiting on
 * it as their play_event start, and chains with it as their reset_event, or
 * the animation_reset_name of their control, go back to their first anim.
 */
void JsonUiRuntime::fire(Node& node, const std::string& event)
{
    for (AnimTrack& track : node.anims) {
        if (!track.resetEvent.empty() && track.resetEvent == event) {
            track.props = track.first;
            track.holding = false;
            track.finished = false;
            track.start = -1.0;
            Node probe;
            probe.vars = node.vars;
            probe.props = track.props;
            track.playEvent = text(probe, "play_event");
            if (track.playEvent.empty()) {
                track.start = now;
            }
        }
        if (track.playEvent == event) {
            track.start = now;
            track.finished = false;
        }
    }
    for (std::unique_ptr<Node>& child : node.children) {
        fire(*child, event);
    }
}

void JsonUiRuntime::emit(UiEvent::Kind kind, const Node& node, std::string name)
{
    UiEvent event;
    event.kind = kind;
    event.name = std::move(name);
    bool namedCollection = false;
    for (const Node* at = &node; at; at = at->parent) {
        if (at->index >= 0 && !at->collection.empty()) {
            namedCollection = true;
            break;
        }
    }
    for (const Node* at = &node; at; at = at->parent) {
        if (at->index < 0 || (namedCollection && at->collection.empty())) {
            continue;
        }
        if (event.index < 0) {
            event.index = at->index;
            event.collection = at->collection;
        } else {
            event.outerIndex = at->index;
            break;
        }
    }
    events.push_back(std::move(event));
}

/**
 * Whether a button maps a click to a button id of its own, which a button
 * with an empty button_mappings, like a tooltip trigger, does not.
 */
bool JsonUiRuntime::pressMapped(const Node& node) const
{
    const json::Value* mappings = property(node, "button_mappings");
    if (!mappings || !mappings->isArray()) {
        return false;
    }
    for (const std::unique_ptr<json::Value>& mapping : mappings->mArray) {
        const json::Value* from = mapping->isObject() ? resolve(node, mapping->get("from_button_id")) : nullptr;
        const json::Value* type = mapping->isObject() ? resolve(node, mapping->get("mapping_type")) : nullptr;
        const json::Value* to = mapping->isObject() ? resolve(node, mapping->get("to_button_id")) : nullptr;
        if (from && from->string() == "button.menu_select" && type && type->string() == "pressed" && to && to->isString() && !to->mString.empty()) {
            return true;
        }
    }
    return false;
}

/**
 * Moves the keyboard focus: the first arrow key or Tab focuses the button
 * with the highest default_focus_precedence, then the arrows go to the
 * nearest focusable control that way and Tab through them in order. Enter
 * presses the focused control, and moving the mouse hands focus back to it.
 */
void JsonUiRuntime::navigateFocus(const InputState& in, float mouseX, float mouseY)
{
    if (laidOut && (mouseX != laidMouseX || mouseY != laidMouseY)) {
        keyFocus = 0;
    }
    std::vector<Node*> candidates;
    for (Node* node : order) {
        bool focusable = (node->type == "button" || node->type == "toggle") && node->enabled && node->w > 0.0f && node->h > 0.0f;
        if (focusable && (!node->clipped || (node->clip.w > 0.0f && node->clip.h > 0.0f)) && flag(*node, "focus_enabled", true)) {
            candidates.push_back(node);
        }
    }
    auto current = std::find_if(candidates.begin(), candidates.end(), [&](const Node* node) { return node->id == keyFocus; });
    if (current == candidates.end()) {
        keyFocus = 0;
    }
    if (in.enter && keyFocus) {
        click(**current);
        return;
    }
    Key key = in.pressedKey;
    bool arrow = key == Key::Up || key == Key::Down || key == Key::Left || key == Key::Right;
    if ((!arrow && !in.tab) || candidates.empty()) {
        return;
    }
    if (!keyFocus) {
        Node* first = candidates.front();
        for (Node* node : candidates) {
            if (number(*node, "default_focus_precedence", 0.0) > number(*first, "default_focus_precedence", 0.0)) {
                first = node;
            }
        }
        keyFocus = first->id;
        return;
    }
    if (in.tab) {
        size_t index = static_cast<size_t>(current - candidates.begin());
        size_t count = candidates.size();
        index = in.isHeld(Key::Shift) ? (index + count - 1) % count : (index + 1) % count;
        keyFocus = candidates[index]->id;
        return;
    }
    float dx = key == Key::Left ? -1.0f : key == Key::Right ? 1.0f : 0.0f;
    float dy = key == Key::Up ? -1.0f : key == Key::Down ? 1.0f : 0.0f;
    const Node& from = **current;
    float fromX = from.x + from.w * 0.5f;
    float fromY = from.y + from.h * 0.5f;
    Node* best = nullptr;
    float bestScore = std::numeric_limits<float>::infinity();
    for (Node* node : candidates) {
        if (node == &from) {
            continue;
        }
        float vx = node->x + node->w * 0.5f - fromX;
        float vy = node->y + node->h * 0.5f - fromY;
        float along = vx * dx + vy * dy;
        if (along <= 0.5f) {
            continue;
        }
        float across = std::abs(vx * dy - vy * dx);
        float score = along + across * 2.0f;
        if (score < bestScore) {
            bestScore = score;
            best = node;
        }
    }
    if (best) {
        keyFocus = best->id;
    }
}

bool JsonUiRuntime::mapButton(Node& node, const std::string& from, const std::string& mode, int depth)
{
    if (!node.enabled || depth > 8) {
        return false;
    }
    const json::Value* mappings = property(node, "button_mappings");
    if (!mappings || !mappings->isArray()) {
        return false;
    }
    for (const auto& mapping : mappings->mArray) {
        if (!mapping->isObject() || (mapping->get("ignored") && ignores(node, mapping->get("ignored")))) {
            continue;
        }
        const auto* source = resolve(node, mapping->get("from_button_id"));
        const auto* type = resolve(node, mapping->get("mapping_type"));
        const auto* destination = resolve(node, mapping->get("to_button_id"));
        if (!source || source->string() != from || !type || type->string() != mode
            || !destination || !destination->isString() || destination->mString.empty()) {
            continue;
        }
        if (destination->mString != from && mapButton(node, destination->mString, "pressed", depth + 1)) {
            return true;
        }
        emit(UiEvent::Kind::Button, node, destination->mString);
        return true;
    }
    return false;
}

void JsonUiRuntime::click(Node& node)
{
    if (!node.enabled) {
        return;
    }
    if (!text(node, "sound_name").empty()) {
        ui->countClick();
    }
    if (node.type == "button") {
        bool repeated = lastClickTarget == node.id && now - lastClickTime < 0.3;
        lastClickTarget = node.id;
        lastClickTime = now;
        if (repeated && mapButton(node, "button.menu_select", "double_pressed")) {
            return;
        }
        if (ui->input().enter && mapButton(node, "button.menu_ok", "focused")) {
            return;
        }
        if (ui->input().isHeld(Key::Shift) && mapButton(node, "button.menu_auto_place", "pressed")) {
            return;
        }
        mapButton(node, "button.menu_select", "pressed");
        return;
    }
    if (node.type == "toggle" || node.type == "dropdown") {
        bool checked = node.dataToggle ? node.bound["#toggle_state"].truthy() : node.toggled;
        bool next = flag(node, "radio_toggle_group", false) ? true : !checked;
        node.toggled = next;
        node.bound["#toggle_state"] = UiValue::of(next);
        std::string name = text(node, "toggle_name");
        if (!name.empty() && name.front() == '(') {
            name = evaluate(node, name).toText();
        }
        emit(UiEvent::Kind::Toggle, node, std::move(name));
        events.back().state = next;
        events.back().value = number(node, "toggle_group_forced_index", 0.0);
    }
}

/**
 * The mouse and keyboard for this frame: which control the mouse is over,
 * presses and clicks, slider and scroll bar drags, the wheel, typing into
 * the focused edit box and escape.
 */
void JsonUiRuntime::input()
{
    const InputState& in = ui->input();
    float mx = ui->mouseX();
    float my = ui->mouseY();
    bool blocked = ui->isBlocked();

    Node* modal = nullptr;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if ((*it)->type == "input_panel" && flag(**it, "modal", false)) {
            modal = *it;
            break;
        }
    }
    auto inside = [&](const Node* node, const Node* scope) {
        for (const Node* at = node; at; at = at->parent) {
            if (at == scope) {
                return true;
            }
        }
        return false;
    };
    auto under = [&](const Node& node) {
        Rect rect { node.x, node.y, node.w, node.h };
        return !blocked && rect.contains(mx, my) && (!node.clipped || node.clip.contains(mx, my)) && (!modal || inside(&node, modal));
    };

    for (Node* node : order) {
        node->forced = false;
    }
    std::vector<Node*> sorted(order);
    std::stable_sort(sorted.begin(), sorted.end(), [](const Node* a, const Node* b) { return a->z < b->z; });
    Node* target = nullptr;
    Node* passedPress = nullptr;
    passedHover.clear();
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        Node& node = **it;
        if (node.type == "tooltip_trigger" && under(node)) {
            std::string top = text(node, "tooltip_top_content_control");
            std::string bottom = text(node, "tooltip_bottom_content_control");
            std::string areaName = text(node, "tooltip_area");
            Node* bounds = nullptr;
            for (Node* at = node.parent; at && !areaName.empty(); at = at->parent) {
                if (at->name == areaName) {
                    bounds = at;
                    break;
                }
            }
            // The popup goes above the trigger unless that would leave the tooltip area.
            bool below = bounds && node.y - bounds->y < 40.0f;
            if (node.parent) {
                for (std::unique_ptr<Node>& sibling : node.parent->children) {
                    if (sibling->name == (below ? bottom : top)) {
                        sibling->forced = true;
                    }
                }
            }
            if (!flag(node, "consume_hover_events", false)) {
                continue;
            }
        }
        if ((isControl(node) && node.type != "slider_box") || node.type == "scrollbar_box" || node.type == "scroll_track") {
            if (node.enabled && under(node)) {
                if (node.type == "button" && !flag(node, "consume_hover_events", true)) {
                    passedHover.push_back(node.id);
                    if (!passedPress && pressMapped(node)) {
                        passedPress = &node;
                    }
                    continue;
                }
                target = &node;
                break;
            }
        }
    }
    if (passedPress) {
        target = passedPress;
    }
    hot = target ? target->id : 0;
    if (target && in.rightMousePressed) {
        mapButton(*target, "button.menu_secondary_select", "pressed");
    }
    if (navigation && !focused && !blocked) {
        navigateFocus(in, mx, my);
    }

    if (in.mousePressed) {
        active = hot;
        if (focused && focused != hot) {
            if (auto found = byId.find(focused); found != byId.end()) {
                emit(UiEvent::Kind::TextDone, *found->second, text(*found->second, "text_box_name"));
                events.back().text = found->second->edit;
            }
            focused = 0;
        }
        if (target && target->type == "edit_box" && focused != target->id) {
            focused = target->id;
            target->edit = lookup(*target, "#item_name").toText();
            if (Node* label = find(*target, text(*target, "text_control"))) {
                target->edit = lookup(*label, "#item_name").toText();
            }
        }
        if (target && target->type == "scrollbar_box") {
            grab = my - target->y;
            if (Node* view = ancestor(*target, "scroll_view")) {
                fire(*view, "scrollbar.active");
            }
        } else if (Node* view = target ? ancestor(*target, "scroll_view") : nullptr) {
            Node* track = nearest(*view, text(*view, "scrollbar_track"));
            std::string button = text(*view, "scrollbar_track_button");
            bool onTrack = track && (target == track || (!button.empty() && target->name == button));
            Node* content = nearest(*view, text(*view, "scroll_content"));
            Node* port = nearest(*view, text(*view, "scroll_view_port"));
            if (onTrack && content && port && track->h > 0.0f) {
                float fraction = std::clamp((my - track->y) / track->h, 0.0f, 1.0f);
                view->scroll = std::clamp(port->h * -0.5f + fraction * content->h, 0.0f, view->scrollRange);
            }
        }
    }

    auto dragging = byId.find(active);
    if (Node* dragged = active && dragging != byId.end() ? dragging->second : nullptr) {
        if (in.mouseDown && dragged->type == "slider" && dragged->w > 0.0f) {
            float fraction = std::clamp((mx - dragged->x) / dragged->w, 0.0f, 1.0f);
            int steps = static_cast<int>(dragged->bound.count("#slider_steps") ? dragged->bound.at("#slider_steps").toNumber() : 1.0);
            if (steps > 1) {
                fraction = std::round(fraction * static_cast<float>(steps - 1)) / static_cast<float>(steps - 1);
            }
            if (fraction != dragged->value || in.mousePressed) {
                dragged->value = fraction;
                emit(UiEvent::Kind::Slider, *dragged, text(*dragged, "slider_name"));
                events.back().value = fraction;
            }
        }
        if (in.mouseDown && dragged->type == "scrollbar_box") {
            if (Node* view = ancestor(*dragged, "scroll_view"); view && dragged->parent) {
                Node* content = nearest(*view, text(*view, "scroll_content"));
                Node* port = nearest(*view, text(*view, "scroll_view_port"));
                float travel = dragged->parent->h - dragged->h;
                if (content && port && travel > 0.0f) {
                    float range = std::max(0.0f, content->h - port->h);
                    view->scroll = std::clamp((my - grab - dragged->parent->y) / travel, 0.0f, 1.0f) * range;
                }
            }
        }
    }

    if (in.mouseReleased) {
        Node* open = nullptr;
        for (Node* node : order) {
            if (node->type == "dropdown" && (node->bound.count("#toggle_state") ? node->bound.at("#toggle_state").truthy() : node->toggled)) {
                open = node;
            }
        }
        if (auto held = byId.find(active); held != byId.end() && held->second->type == "scrollbar_box") {
            if (Node* view = ancestor(*held->second, "scroll_view")) {
                fire(*view, "scrollbar.released");
            }
        }
        Node* clicked = target && active == target->id ? target : nullptr;
        if (clicked) {
            click(*clicked);
        }
        if (open && clicked != open && (!clicked || !modal || inside(clicked, modal) || !inside(clicked, open->parent))) {
            open->toggled = false;
            open->bound["#toggle_state"] = UiValue::of(false);
        }
        active = 0;
    }

    if (in.wheel != 0.0f && !blocked) {
        auto inside = [&](const Node* control) {
            return control && control->shown && mx >= control->x && mx <= control->x + control->w && my >= control->y && my <= control->y + control->h;
        };
        for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
            Node& view = **it;
            if (view.type != "scroll_view") {
                continue;
            }
            bool takes = flag(view, "always_handle_scrolling", false)
                || inside(nearest(view, text(view, "scroll_view_port")))
                || inside(nearest(view, text(view, "scrollbar_track")));
            if (!takes) {
                continue;
            }
            if (!wheelSensitivity) {
                wheelSensitivity = static_cast<float>(number(view, "scroll_speed", 1.0));
            }
            float step = in.wheel > 0.0f ? 120.0f / 127.0f : 120.0f / 128.0f;
            view.scroll = std::clamp(view.scroll - *wheelSensitivity * in.wheel * step, 0.0f, view.scrollRange);
            fire(view, "scrollbar.active");
            fire(view, "scrollbar.released");
            break;
        }
    }

    auto typing = byId.find(focused);
    Node* editing = focused && typing != byId.end() ? typing->second : nullptr;
    if (!editing) {
        focused = 0;
    }
    if (editing) {
        std::u32string typed = in.text;
        bool multiline = flag(*editing, "enabled_newline", false);
        if (multiline && in.enter && std::find(typed.begin(), typed.end(), U'\n') == typed.end()) {
            typed.push_back(U'\n');
        }
        if (in.isHeld(Key::Control) && in.pressedKey == Key::V) {
            std::string pasted = platform::pasteText();
            size_t i = 0;
            while (i < pasted.size()) {
                typed.push_back(nextCodepoint(pasted, i));
            }
        }
        std::string before = editing->edit;
        if (in.backspace && !editing->edit.empty()) {
            popUtf8(editing->edit);
        }
        size_t limit = static_cast<size_t>(std::max(0.0, number(*editing, "max_length", static_cast<double>(DefaultMaxLength))));
        size_t length = 0;
        for (size_t i = 0; i < editing->edit.size(); ++length) {
            nextCodepoint(editing->edit, i);
        }
        for (char32_t cp : typed) {
            if ((cp < 32 && !(multiline && cp == U'\n')) || cp == 127 || length >= limit) {
                continue;
            }
            appendUtf8(editing->edit, cp);
            ++length;
        }
        if (editing->edit != before) {
            emit(UiEvent::Kind::Text, *editing, text(*editing, "text_box_name"));
            events.back().text = editing->edit;
        }
        if ((!multiline && in.enter) || in.escape) {
            emit(UiEvent::Kind::TextDone, *editing, text(*editing, "text_box_name"));
            events.back().text = editing->edit;
            focused = 0;
        }
        return;
    }

    if (in.escape) {
        for (Node* node : order) {
            if (node->type == "dropdown" && (node->bound.count("#toggle_state") ? node->bound.at("#toggle_state").truthy() : node->toggled)) {
                node->toggled = false;
                node->bound["#toggle_state"] = UiValue::of(false);
                return;
            }
        }
        // Escape is button.menu_cancel, which screens map to what closes them.
        for (Node* node : order) {
            const json::Value* mappings = property(*node, "button_mappings");
            if (!mappings || !mappings->isArray()) {
                continue;
            }
            for (const std::unique_ptr<json::Value>& mapping : mappings->mArray) {
                const json::Value* from = mapping->isObject() ? resolve(*node, mapping->get("from_button_id")) : nullptr;
                const json::Value* type = mapping->isObject() ? resolve(*node, mapping->get("mapping_type")) : nullptr;
                const json::Value* to = mapping->isObject() ? resolve(*node, mapping->get("to_button_id")) : nullptr;
                if (from && from->string() == "button.menu_cancel" && type && type->string() == "global" && to && to->isString()) {
                    emit(UiEvent::Kind::Button, *node, to->mString);
                    return;
                }
            }
        }
    }
}

void JsonUiRuntime::paint(Node& node)
{
    Rect rect { node.x, node.y, node.w, node.h };
    float alpha = std::clamp(node.paintAlpha, 0.0f, 1.0f);
    if (alpha <= 0.0f || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    if (node.clipped) {
        if (node.clip.w <= 0.0f || node.clip.h <= 0.0f) {
            return;
        }
        // Custom renderers may float outside their declared control rectangle.
        bool boundedCustom = node.type == "custom" && text(node, "renderer") == "gradient_renderer";
        bool progress = node.type == "custom" && text(node, "renderer") == "progress_bar_renderer";
        if (node.type == "image" || boundedCustom || progress) {
            Rect bounds = progress ? Rect { rect.x, rect.y, rect.w + 1.0f, rect.h + 1.0f } : rect;
            Rect visible = intersect(bounds, node.clip);
            if (visible.w <= 0.0f || visible.h <= 0.0f) {
                return;
            }
        }
        ui->setClip(node.clip);
    } else {
        ui->clearClip();
    }
    auto tint = [&]() {
        float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (const json::Value* color = property(node, "color"); color && color->isArray()) {
            for (size_t i = 0; i < std::min<size_t>(4, color->mArray.size()); ++i) {
                const json::Value* channel = resolve(node, color->mArray[i].get());
                rgba[i] = channel ? static_cast<float>(channel->number(1.0)) : 1.0f;
            }
        } else if (std::string key = color && color->isString() ? color->mString : std::string("#color"); !key.empty() && key.front() == '#') {
            if (auto bound = node.bound.find(key); bound != node.bound.end() && bound->second.kind == UiValue::Kind::String) {
                const std::string& channels = bound->second.text;
                size_t start = 0;
                if (channels.size() == 7 && channels.front() == '#') {
                    char* parsed = nullptr;
                    unsigned long rgb = std::strtoul(channels.c_str() + 1, &parsed, 16);
                    if (parsed == channels.c_str() + 7) {
                        rgba[0] = static_cast<float>((rgb >> 16) & 0xff) / 255.0f;
                        rgba[1] = static_cast<float>((rgb >> 8) & 0xff) / 255.0f;
                        rgba[2] = static_cast<float>(rgb & 0xff) / 255.0f;
                    }
                    start = channels.size() + 1;
                }
                for (size_t i = 0; i < 4 && start <= channels.size(); ++i) {
                    size_t end = channels.find(',', start);
                    rgba[i] = std::strtof(channels.substr(start, end == std::string::npos ? std::string::npos : end - start).c_str(), nullptr);
                    if (end == std::string::npos) {
                        break;
                    }
                    start = end + 1;
                }
            }
        }
        if (!node.enabled) {
            if (const json::Value* locked = node.type == "label" ? property(node, "locked_color") : nullptr; locked && locked->isArray()) {
                for (size_t i = 0; i < std::min<size_t>(4, locked->mArray.size()); ++i) {
                    const json::Value* channel = resolve(node, locked->mArray[i].get());
                    rgba[i] = channel ? static_cast<float>(channel->number(1.0)) : 1.0f;
                }
            }
            if (node.props.count("locked_alpha")) {
                rgba[3] *= static_cast<float>(number(node, "locked_alpha", 1.0));
            }
        }
        auto channel = [](float value) { return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return Color { channel(rgba[0]), channel(rgba[1]), channel(rgba[2]), channel(rgba[3] * alpha) };
    };

    if (node.type == "image") {
        if (node.texture.empty() || node.texture == "loading") {
            return;
        }
        if (node.texture == "textures/ui/title") {
            node.texture = ui->skin().sprite("dynamic/title").valid ? "dynamic/title" : "kestrel/title";
        }
        const Sprite& sprite = ui->skin().sprite(node.texture);
        Color color = tint();
        double ratio = 0.0;
        if (auto bound = node.bound.find("#clip_ratio"); bound != node.bound.end()) {
            ratio = bound->second.toNumber();
        } else if (std::optional<float> moving = animated(node, "clip_ratio", 0, 0.0f)) {
            ratio = *moving;
        } else {
            ratio = number(node, "clip_ratio", 0.0);
        }
        ratio = std::clamp(ratio, 0.0, 1.0);
        const json::Value* uv = property(node, "uv");
        const json::Value* uvSize = property(node, "uv_size");
        std::array<float, 2> sourceSize { sprite.width, sprite.height };
        if (uvSize && uvSize->isArray() && uvSize->mArray.size() == 2) {
            std::array<float, 2> given { term(node, uvSize->mArray[0].get(), 0.0f), term(node, uvSize->mArray[1].get(), 0.0f) };
            if (given[0] != 0.0f || given[1] != 0.0f) {
                sourceSize = given;
            }
        }
        std::string direction = text(node, "clip_direction");
        Rect clipVisible = rect;
        bool clipping = ratio > 0.0 && (direction == "left" || direction == "right" || direction == "up" || direction == "down" || direction == "center");
        if (clipping) {
            bool perfect = flag(node, "clip_pixelperfect", true);
            auto snap = [&](float pixels) {
                return perfect && pixels > 0.0f ? std::floor(static_cast<float>(ratio) * pixels) / pixels : static_cast<float>(ratio);
            };
            float cutX = direction == "up" || direction == "down" ? 0.0f : snap(sourceSize[0]);
            float cutY = direction == "left" || direction == "right" ? 0.0f : snap(sourceSize[1]);
            float w = rect.w * (1.0f - cutX);
            float h = rect.h * (1.0f - cutY);
            Rect visible { rect.x, rect.y, w, h };
            if (direction == "right" || direction == "down") {
                visible.x = rect.x + rect.w * cutX;
                visible.y = rect.y + rect.h * cutY;
            } else if (direction == "center") {
                visible.x = rect.x + rect.w * cutX * 0.5f;
                visible.y = rect.y + rect.h * cutY * 0.5f;
            }
            if (visible.w <= 0.0f || visible.h <= 0.0f) {
                return;
            }
            clipVisible = visible;
            ui->setClip(node.clipped ? intersect(node.clip, visible) : visible);
        }
        std::array<float, 2> origin { 0.0f, 0.0f };
        bool region = false;
        std::array<float, 2> frameSize { 0.0f, 0.0f };
        if (std::optional<float> u = animated(node, "uv", 0, 0.0f)) {
            origin = { *u, animated(node, "uv", 1, 0.0f).value_or(0.0f) };
            region = true;
        }
        for (const AnimTrack& track : node.anims) {
            if (track.target != "uv" || track.start < 0.0) {
                continue;
            }
            Node probe;
            probe.vars = node.vars;
            probe.props = track.props;
            std::string type = text(probe, "anim_type");
            if (type != "flip_book" && type != "aseprite_flip_book") {
                continue;
            }
            if (type == "aseprite_flip_book" && !sprite.frames.empty()) {
                double total = 0.0;
                for (const SpriteFrame& frame : sprite.frames) {
                    total += frame.duration;
                }
                double elapsed = std::max(0.0, now - track.start);
                elapsed = flag(probe, "looping", true) ? std::fmod(elapsed, total) : std::min(elapsed, total - 0.0001);
                const SpriteFrame* shown = &sprite.frames.back();
                for (const SpriteFrame& frame : sprite.frames) {
                    if (elapsed < frame.duration) {
                        shown = &frame;
                        break;
                    }
                    elapsed -= frame.duration;
                }
                origin = { shown->x, shown->y };
                frameSize = { shown->width, shown->height };
                region = true;
                continue;
            }
            const json::Value* initial = property(probe, "initial_uv");
            int frames = std::max(1, static_cast<int>(number(probe, "frame_count", 1.0)));
            float frameWidth = uvSize && uvSize->isArray() && uvSize->mArray.size() == 2 ? term(node, uvSize->mArray[0].get(), 0.0f) : 0.0f;
            float stepSize = static_cast<float>(number(probe, "frame_step", frameWidth));
            double fps = std::max(0.001, number(probe, "fps", 1.0));
            int frame = static_cast<int>((now - track.start) * fps);
            if (flag(probe, "reversible", false) && frames > 1) {
                int cycle = frames * 2 - 2;
                frame %= cycle;
                frame = frame < frames ? frame : cycle - frame;
            } else if (flag(probe, "looping", true)) {
                frame %= frames;
            } else {
                frame = std::min(frame, frames - 1);
            }
            float u = initial && initial->isArray() && !initial->mArray.empty() ? static_cast<float>(initial->mArray[0]->number()) : 0.0f;
            float v = initial && initial->isArray() && initial->mArray.size() > 1 ? static_cast<float>(initial->mArray[1]->number()) : 0.0f;
            origin = { u + stepSize * static_cast<float>(frame), v };
            region = true;
        }
        if (uv && uv->isArray() && uv->mArray.size() == 2 && !region) {
            origin = { term(node, uv->mArray[0].get(), 0.0f), term(node, uv->mArray[1].get(), 0.0f) };
            region = true;
        }
        if (frameSize[0] > 0.0f && frameSize[1] > 0.0f) {
            float scale = rect.h / frameSize[1];
            ui->spriteRegion({ rect.x, rect.y, frameSize[0] * scale, rect.h }, node.texture, { origin[0], origin[1], frameSize[0], frameSize[1] }, color);
            return;
        }
        Rect source { origin[0], origin[1], sourceSize[0], sourceSize[1] };
        bool whole = origin[0] == 0.0f && origin[1] == 0.0f && sourceSize[0] == sprite.width && sourceSize[1] == sprite.height;
        auto draw = [&](const Rect& dest, const Rect& texels) {
            if (whole && texels.x == source.x && texels.y == source.y && texels.w == source.w && texels.h == source.h) {
                ui->sprite(dest, node.texture, color);
            } else {
                ui->spriteRegion(dest, node.texture, texels, color);
            }
        };
        if (sprite.slice.left > 0.0f || sprite.slice.top > 0.0f || sprite.slice.right > 0.0f || sprite.slice.bottom > 0.0f) {
            ui->nineSlice(rect, node.texture, color);
            return;
        }
        const json::Value* tiled = resolve(node, property(node, "tiled"));
        std::string axes = tiled && tiled->isString() ? tiled->mString : std::string();
        bool both = (tiled && tiled->boolean(false)) || axes == "xy" || axes == "yx";
        bool tileX = both || axes == "x";
        bool tileY = both || axes == "y";
        if ((tileX || tileY) && sourceSize[0] > 0.0f && sourceSize[1] > 0.0f) {
            std::array<float, 2> scale { 1.0f, 1.0f };
            if (const json::Value* given = property(node, "tiled_scale"); given && given->isArray() && given->mArray.size() == 2) {
                float sx = term(node, given->mArray[0].get(), 0.0f);
                float sy = term(node, given->mArray[1].get(), 0.0f);
                if (sx > 1e-6f && sy > 1e-6f) {
                    scale = { sx, sy };
                }
            }
            float tw = tileX ? sourceSize[0] * scale[0] : rect.w;
            float th = tileY ? sourceSize[1] * scale[1] : rect.h;
            Rect bounds = node.clipped ? intersect(node.clip, rect) : rect;
            if (clipping) {
                bounds = intersect(bounds, clipVisible);
            }
            ui->setClip(bounds);
            size_t tiles = 0;
            for (float ty = rect.y; ty < rect.bottom() && th > 0.0f && tiles < 4096; ty += th) {
                for (float tx = rect.x; tx < rect.right() && tw > 0.0f && tiles < 4096; tx += tw, ++tiles) {
                    draw({ tx, ty, tw, th }, source);
                }
            }
            return;
        }
        if (clipping) {
            draw(rect, source);
            return;
        }
        if (flag(node, "fill", false) && rect.w > 0.0f && rect.h > 0.0f && source.h > 0.0f) {
            Rect cropped = source;
            if (source.w / source.h <= rect.w / rect.h) {
                cropped.h = rect.h / rect.w * source.w;
                cropped.y = source.y + (source.h - cropped.h) * 0.5f;
            } else {
                cropped.w = rect.w / rect.h * source.h;
                cropped.x = source.x + (source.w - cropped.w) * 0.5f;
            }
            draw(rect, cropped);
            return;
        }
        Rect dest = rect;
        if (flag(node, "keep_ratio", true) && sourceSize[0] > 0.0f && sourceSize[1] > 0.0f) {
            float sx = rect.w / sourceSize[0];
            float sy = rect.h / sourceSize[1];
            if (std::abs(sx - sy) > std::numeric_limits<float>::epsilon()) {
                float fit = std::min(sx, sy);
                float w = sourceSize[0] * fit;
                float h = sourceSize[1] * fit;
                dest = { rect.x + (rect.w - w) * 0.5f, rect.y + (rect.h - h) * 0.5f, w, h };
            }
        }
        draw(dest, source);
        return;
    }

    if (node.type == "label") {
        float scale = labelScale(node);
        bool caret = node.caret && static_cast<long long>(now * 2.0) % 2 == 0;
        if (node.text.empty()) {
            if (caret) {
                paintCaret(rect.x, rect.y, scale, alpha);
            }
            return;
        }
        float padding = static_cast<float>(number(node, "line_padding", 0.0));
        Color color = tint();
        bool shadow = flag(node, "shadow", false);
        TextStyle style = labelStyle(node);
        std::string alignment = text(node, "text_alignment");
        float bottom = property(node, "max_size") ? rect.bottom() + 0.5f : std::numeric_limits<float>::infinity();
        float lineY = rect.y;
        float caretX = rect.x;
        float caretY = rect.y;
        size_t room = 0;
        if (!node.caret && !node.selected) {
            float pitch = std::max(1e-3f, (LabelLineHeight + padding) * scale);
            room = static_cast<size_t>(std::max(1.0f, std::floor((rect.h + padding * scale) / pitch + 0.01f)));
        }
        const Font::TextLayout& layout = labelLayout(node, rect.w / scale + 0.01f, room);
        for (const Font::TextLine& line : layout.lines) {
            if (lineY + LabelLineHeight * scale > bottom) {
                return;
            }
            float width = line.width * scale;
            float lineX = alignment == "center" ? rect.x + std::floor((rect.w - width) * 0.5f) : alignment == "right" ? rect.right() - width : rect.x;
            bool visible = true;
            if (node.clipped && !line.obfuscated && !node.caret && ui->textFont().rasterScale() == ui->pixelScale() && scale > 0.0f) {
                float margin = 2.0f / ui->pixelScale() + (shadow ? scale : 0.0f);
                if (style != TextStyle::Pixel && style != TextStyle::Rune && !line.glyphs.empty()) {
                    float pixels = ui->pixelScale();
                    float extra = std::abs(std::max(1.0f, std::round(pixels * scale)) - scale * std::max(1.0f, std::round(pixels)));
                    margin += static_cast<float>(line.glyphs.back().boldBefore + 1) * extra / pixels;
                }
                Rect ink { lineX + line.ink.x * scale - margin, lineY + line.ink.y * scale - margin,
                    line.ink.w * scale + margin * 2.0f, line.ink.h * scale + margin * 2.0f };
                Rect clipped = intersect(ink, node.clip);
                visible = clipped.w > 0.0f && clipped.h > 0.0f;
            }
            if (visible && node.selected) {
                ui->fill({ lineX, lineY - scale, width + scale, (LabelLineHeight + 1.0f) * scale }, { 255, 255, 255, color.a });
                ui->textLayoutLine(line, style, lineX, lineY, scale, { 0, 0, 0, color.a });
                lineY += (LabelLineHeight + padding) * scale;
                continue;
            }
            if (visible && shadow) {
                ui->textLayoutLine(line, style, lineX + scale, lineY + scale, scale, color, true);
            }
            if (visible) {
                ui->textLayoutLine(line, style, lineX, lineY, scale, color);
            }
            caretX = lineX + std::max(0.0f, width - scale);
            caretY = lineY;
            lineY += (LabelLineHeight + padding) * scale;
        }
        if (caret) {
            paintCaret(caretX, caretY, scale, alpha);
        }
        return;
    }

    if (node.type == "custom" && text(node, "renderer") == "hover_text_renderer") {
        paintHoverText(node, alpha);
        return;
    }
    if (node.type == "custom" && text(node, "renderer") == "gradient_renderer") {
        paintGradient(node, rect, alpha);
        return;
    }
    if (node.type == "custom" && text(node, "renderer") == "progress_bar_renderer") {
        bool visible = lookup(node, "#progress_bar_visible").truthy() || lookup(node, "#touch_progress_bar_visible").truthy();
        double total = lookup(node, "#progress_bar_total_amount").toNumber();
        if (!visible || total <= 0.0) {
            return;
        }
        float fraction = float(std::clamp(lookup(node, "#progress_bar_current_amount").toNumber() / total, 0.0, 1.0));
        float width = std::round(rect.w * fraction);
        uint8_t opacity = uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
        if (lookup(node, "drop_shadow").truthy()) {
            ui->fill({ rect.x, rect.y, rect.w + 1.0f, rect.h + 1.0f }, { 0, 0, 0, opacity });
        }
        Color color { 102, 102, 255, opacity };
        if (lookup(node, "is_durability").truthy()) {
            color = { uint8_t(255.0f * std::min(1.0f, 2.0f * (1.0f - fraction))), uint8_t(255.0f * std::min(1.0f, 2.0f * fraction)), 0, opacity };
        }
        ui->fill({ rect.x, rect.y, width, rect.h }, color);
        return;
    }
    if (node.type == "custom" && renderer) {
        std::string name = text(node, "renderer");
        UiLookup find = [&](const std::string& key) { return lookup(node, key); };
        renderer(*ui, name, rect, alpha, find);
    }
}

/**
 * The text caret of an edit box, drawn by the game rather than the pack: a
 * green bar one pixel wide and as tall as a glyph, in the spacing column
 * right after the last character.
 */
void JsonUiRuntime::paintCaret(float x, float y, float scale, float alpha)
{
    ui->fill({ x, y, scale, CaretHeight * scale }, Color { 0, 255, 0, static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) });
}

/**
 * The game draws gradient_renderer itself: color1 at the top fading to
 * color2 at the bottom, or left to right when gradient_direction is
 * horizontal, drawn as thin bands.
 */
void JsonUiRuntime::paintGradient(const Node& node, const Rect& rect, float alpha)
{
    constexpr int Bands = 32;
    auto colorOf = [&](const char* name) {
        std::array<float, 4> rgba { 1.0f, 1.0f, 1.0f, 1.0f };
        if (const json::Value* color = property(node, name); color && color->isArray()) {
            for (size_t i = 0; i < std::min<size_t>(4, color->mArray.size()); ++i) {
                const json::Value* channel = resolve(node, color->mArray[i].get());
                rgba[i] = channel ? static_cast<float>(channel->number(1.0)) : 1.0f;
            }
        }
        return rgba;
    };
    std::array<float, 4> from = colorOf("color1");
    std::array<float, 4> to = colorOf("color2");
    bool horizontal = text(node, "gradient_direction") == "horizontal";
    float span = horizontal ? rect.w : rect.h;
    for (int band = 0; band < Bands; ++band) {
        float start = std::floor(span * static_cast<float>(band) / Bands);
        float end = std::floor(span * static_cast<float>(band + 1) / Bands);
        if (end <= start) {
            continue;
        }
        float t = (static_cast<float>(band) + 0.5f) / Bands;
        auto channel = [&](size_t i, float scale) {
            return static_cast<uint8_t>(std::clamp((from[i] + (to[i] - from[i]) * t) * scale, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        Color color { channel(0, 1.0f), channel(1, 1.0f), channel(2, 1.0f), channel(3, alpha) };
        Rect strip = horizontal ? Rect { rect.x + start, rect.y, end - start, rect.h } : Rect { rect.x, rect.y + start, rect.w, end - start };
        ui->fill(strip, color);
    }
}

/**
 * The game draws hover_text_renderer itself: the bound #hover_text next to
 * the mouse on the tooltip frame, kept on screen.
 */
void JsonUiRuntime::paintHoverText(const Node& node, float alpha)
{
    constexpr float Padding = 4.0f;
    constexpr float MouseGap = 12.0f;
    std::string value = lookup(node, "#hover_text").toText();
    if (value.empty() || !root || viewport.w <= 0.0f || viewport.h <= 0.0f) {
        return;
    }
    if (Localization::shared().has(value)) {
        value = tr(value, value);
    }
    float limit = static_cast<float>(number(node, "hover_text_max_width", 0.0));
    float padding = std::min(Padding, std::min(viewport.w, viewport.h) * 0.25f);
    float available = std::max(0.01f, viewport.w - padding * 2.0f - 1.0f);
    limit = limit > 0.0f && std::isfinite(limit) ? std::min(limit, available) : available;
    const Font& font = ui->textFont();
    if (hoverLayout.font != &font || hoverLayout.revision != font.revision() || hoverLayout.source != value || hoverLayout.width != limit) {
        hoverLayout.layout = font.layout(value, TextStyle::Pixel, limit);
        hoverLayout.font = &font;
        hoverLayout.revision = font.revision();
        hoverLayout.source = std::move(value);
        hoverLayout.width = limit;
    }
    const Font::TextLayout& layout = hoverLayout.layout;
    float height = static_cast<float>(layout.lines.size()) * LabelLineHeight;
    Rect box { ui->mouseX() + MouseGap, ui->mouseY() - MouseGap,
        std::min(viewport.w, layout.width + padding * 2.0f + 1.0f), std::min(viewport.h, height + padding * 2.0f + 1.0f) };
    box.x = std::clamp(box.x, viewport.x, viewport.right() - box.w);
    box.y = std::clamp(box.y, viewport.y, viewport.bottom() - box.h);
    Color tint { 255, 255, 255, static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) };
    ui->setClip(viewport);
    ui->nineSlice(box, "textures/ui/purpleBorder", tint);
    ui->setClip({ box.x + padding, box.y + padding, std::max(0.0f, box.w - padding * 2.0f), std::max(0.0f, box.h - padding * 2.0f) });
    float y = box.y + padding;
    for (const Font::TextLine& line : layout.lines) {
        if (y >= box.bottom() - padding) {
            break;
        }
        ui->textLayoutLine(line, TextStyle::Pixel, box.x + padding + 1.0f, y + 1.0f, 1.0f, tint, true);
        ui->textLayoutLine(line, TextStyle::Pixel, box.x + padding, y, 1.0f, tint);
        y += LabelLineHeight;
    }
}

JsonUiScreen::JsonUiScreen(std::shared_ptr<const JsonUi> definitions, std::string root, const UiRow& variables)
    : runtime(std::make_unique<JsonUiRuntime>())
{
    runtime->defs = std::move(definitions);
    runtime->rootReference = std::move(root);
    runtime->now = secondsNow();
    if (!runtime->defs) {
        return;
    }
    runtime->root = runtime->make(nullptr, "root@" + runtime->rootReference, nullptr, nullptr, nullptr, &variables, 0);
    fire("screen.entrance_push");
}

JsonUiScreen::~JsonUiScreen() = default;

UiEvent JsonUiScreen::pointerTarget() const
{
    UiEvent event;
    auto found = runtime->byId.find(runtime->hot);
    if (found == runtime->byId.end()) {
        return event;
    }
    const Node& target = *found->second;
    const auto* mappings = runtime->property(target, "button_mappings");
    std::string pressed = runtime->ui && runtime->ui->input().isHeld(Key::Shift) ? "button.menu_auto_place"
        : runtime->ui && runtime->ui->input().rightMousePressed ? "button.menu_secondary_select" : "button.menu_select";
    if (mappings && mappings->isArray()) {
        for (const auto& mapping : mappings->mArray) {
            if (!mapping->isObject() || (mapping->get("ignored") && runtime->ignores(target, mapping->get("ignored")))) {
                continue;
            }
            const auto* from = runtime->resolve(target, mapping->get("from_button_id"));
            const auto* mode = runtime->resolve(target, mapping->get("mapping_type"));
            const auto* to = runtime->resolve(target, mapping->get("to_button_id"));
            if (from && from->string() == pressed && mode && mode->string() == "pressed" && to && to->isString()) {
                event.name = to->mString;
                break;
            }
        }
    }
    for (const Node* node = found->second; node; node = node->parent) {
        if (node->index >= 0 && !node->collection.empty()) {
            event.index = node->index;
            event.collection = node->collection;
            break;
        }
    }
    return event;
}

bool JsonUiScreen::pointerInsideContent(float x, float y) const
{
    if (!runtime->root) {
        return false;
    }
    for (const Node* node : runtime->order) {
        if (!node->shown || node->type != "image" || node->w <= 0.0f || node->h <= 0.0f) {
            continue;
        }
        if (node->w >= runtime->root->w && node->h >= runtime->root->h) {
            continue;
        }
        if (Rect { node->x, node->y, node->w, node->h }.contains(x, y)
            && (!node->clipped || node->clip.contains(x, y))) {
            return true;
        }
    }
    return false;
}

const std::shared_ptr<const JsonUi>& JsonUiScreen::definitions() const
{
    return runtime->defs;
}

bool JsonUiScreen::valid() const
{
    return runtime->root != nullptr;
}

void JsonUiScreen::setRenderer(UiRenderer renderer)
{
    runtime->renderer = std::move(renderer);
}

void JsonUiScreen::setKeyboardNavigation(bool enabled)
{
    runtime->navigation = enabled;
    if (!enabled) {
        runtime->keyFocus = 0;
    }
}

void JsonUiScreen::fire(const std::string& event)
{
    if (runtime->root) {
        runtime->fire(*runtime->root, event);
    }
}

void JsonUiScreen::holdButton(const std::string& id, bool held)
{
    std::vector<std::string>& buttons = runtime->held;
    auto found = std::find(buttons.begin(), buttons.end(), id);
    if (held && found == buttons.end()) {
        buttons.push_back(id);
    } else if (!held && found != buttons.end()) {
        buttons.erase(found);
    }
}

void JsonUiScreen::draw(Context& ui, const Rect& area, const UiData& data, std::optional<uint64_t> generation)
{
    if (!runtime->root) {
        return;
    }
    JsonUiRuntime& r = *runtime;
    r.ui = &ui;
    r.data = &data;
    r.now = secondsNow();
    r.viewport = area;
    Node& root = *r.root;
    bool sameData = generation ? r.laidDataSource == &data && r.laidDataGeneration == generation
        : !r.laidDataGeneration && r.laidData == data;

    bool stable = r.laidOut && r.madeThisFrame == 0 && r.focused == 0 && r.events.empty() && quiet(ui) && !animating(root)
        && r.laidArea.x == area.x && r.laidArea.y == area.y && r.laidArea.w == area.w && r.laidArea.h == area.h
        && r.laidMouseX == ui.mouseX() && r.laidMouseY == ui.mouseY() && r.laidBlocked == ui.isBlocked() && r.laidHeld == r.held && sameData
        && r.laidFont == &ui.textFont() && r.laidFontRevision == ui.textFont().revision()
        && r.laidLanguageRevision == Localization::shared().revision() && r.laidPixelScale == ui.pixelScale();
    if (stable && (!r.virtualGrids || r.virtualGridsSettled)) {
        for (Node* node : r.painted) {
            r.paint(*node);
        }
        ui.clearClip();
        r.data = nullptr;
        return;
    }
    r.madeThisFrame = 0;

    r.update(root, 0, nullptr);
    r.sweep(root);
    prepareTree(r, root, true);
    if (!root.shown) {
        r.order.clear();
        r.byId.clear();
        r.painted.clear();
        r.laidOut = false;
        r.data = nullptr;
        return;
    }

    root.inherited = 1.0f;
    r.widthReadsHeight = false;
    r.heightsKnown = false;
    r.size(root, 0, area.w);
    r.size(root, 1, area.h);
    if (r.widthReadsHeight) {
        forgetMeasures(root);
        r.heightsKnown = true;
        r.size(root, 0, area.w);
        forgetMeasures(root);
        r.size(root, 1, area.h);
        r.heightsKnown = false;
    }

    // A scroll view scrolls only once its content, viewport, track and box all exist: the
    // offset is clamped to the content less the viewport, the box takes
    // clamp(viewport / content, 0.1, 1) of the track and travels it, and the bar panel hides
    // while the content fits.
    std::function<void(Node&)> scrolls = [&](Node& node) {
        if (!node.shown) {
            return;
        }
        if (node.type == "scroll_view") {
            Node* content = r.nearest(node, r.text(node, "scroll_content"));
            Node* port = r.nearest(node, r.text(node, "scroll_view_port"));
            Node* track = r.nearest(node, r.text(node, "scrollbar_track"));
            Node* box = r.nearest(node, r.text(node, "scrollbar_box"));
            Node* panel = r.nearest(node, r.text(node, "scroll_box_and_track_panel"));
            node.scrollRange = 0.0f;
            if (content && port && track && box) {
                float range = std::max(0.0f, content->h - port->h);
                bool jump = r.flag(node, "jump_to_bottom_on_update", false);
                bool grew = jump && (node.scrolledContent < 0.0f || range != node.scrolledContent);
                if (grew || r.lookup(node, "#force_scroll_to_end").truthy()) {
                    node.scroll = range;
                }
                node.scrolledContent = range;
                node.scroll = std::clamp(node.scroll, 0.0f, range);
                node.scrollRange = range;
                float shown = std::trunc(node.scroll * 8.0f) * 0.125f;
                bool fits = content->h <= 0.0f || port->h / content->h >= 1.0f;
                bool always = r.flag(node, "scrollbar_always_visible", false);
                std::string draggable = r.text(*box, "draggable");
                bool axis = draggable == "vertical" || draggable == "horizontal";
                bool hidden = axis && panel && fits && !always;
                if (hidden) {
                    panel->shown = false;
                } else if (box->parent) {
                    float length = track->h;
                    float height = box->h;
                    if (axis && panel) {
                        float ratio = content->h > 0.0f ? std::clamp(port->h / content->h, 0.1f, 1.0f) : 1.0f;
                        height = std::ceil(ratio * length);
                        r.size(*box, 1, box->parent->h, height);
                    }
                    float fraction = range > 0.5f ? shown / range : 1.0f;
                    box->offsetOverride = std::array<float, 2> { 0.0f, (length - height) * fraction };
                }
                bool hitBottom = hidden || (axis && panel && fits) || std::abs(shown - range) < 0.1f || (shown != 0.0f && range <= shown);
                node.bound["#scrollbar_hit_bottom"] = UiValue::of(hitBottom);
                node.bound["#scrolled_to_end"] = UiValue::of(range <= node.scroll);
                if (panel) {
                    node.bound["#scroll_bar_visible"] = UiValue::of(!hidden);
                }
            }
        }
        for (std::unique_ptr<Node>& child : node.children) {
            scrolls(*child);
        }
    };
    scrolls(root);

    const json::Value* from = r.property(root, "anchor_from");
    const json::Value* to = r.property(root, "anchor_to");
    auto anchor = [](const json::Value* value) {
        std::string name = value && value->isString() ? value->mString : "center";
        float ax = name.find("left") != std::string::npos ? 0.0f : name.find("right") != std::string::npos ? 1.0f : 0.5f;
        float ay = name.find("top") != std::string::npos ? 0.0f : name.find("bottom") != std::string::npos ? 1.0f : 0.5f;
        return std::array<float, 2> { ax, ay };
    };
    std::array<float, 2> a = anchor(from);
    std::array<float, 2> b = anchor(to);
    r.place(root, area.x + a[0] * area.w - b[0] * root.w, area.y + a[1] * area.h - b[1] * root.h, 0.0f, {}, false, 1.0f);

    r.order.clear();
    r.byId.clear();
    r.gather(root);
    r.input();

    std::vector<Node*> painted(r.order);
    std::stable_sort(painted.begin(), painted.end(), [](const Node* x, const Node* y) { return x->z < y->z; });
    for (Node* node : painted) {
        r.paint(*node);
    }
    ui.clearClip();
    r.painted = std::move(painted);
    r.virtualGridsSettled = stable;
    r.laidOut = true;
    r.laidArea = area;
    if (generation) {
        if (!r.laidDataGeneration) {
            r.laidData = {};
        }
    } else if (!sameData) {
        r.laidData = data;
    }
    r.laidDataSource = &data;
    r.laidDataGeneration = generation;
    r.laidFont = &ui.textFont();
    r.laidFontRevision = ui.textFont().revision();
    r.laidLanguageRevision = Localization::shared().revision();
    r.laidPixelScale = ui.pixelScale();
    r.laidMouseX = ui.mouseX();
    r.laidMouseY = ui.mouseY();
    r.laidBlocked = ui.isBlocked();
    r.laidHeld = r.held;
    r.data = nullptr;
}

std::vector<UiEvent> JsonUiScreen::takeEvents()
{
    std::vector<UiEvent> taken = std::move(runtime->events);
    runtime->events.clear();
    return taken;
}

void JsonUiScreen::blur()
{
    runtime->focused = 0;
}

void JsonUiScreen::showListeningCaret(bool shown)
{
    if (runtime->listeningCaret != shown) {
        runtime->listeningCaret = shown;
        runtime->laidOut = false;
    }
}

void JsonUiScreen::showListeningSelection(bool selected)
{
    if (runtime->listeningSelected != selected) {
        runtime->listeningSelected = selected;
        runtime->laidOut = false;
    }
}

bool JsonUiScreen::editing() const
{
    return runtime->focused != 0;
}

float JsonUiScreen::contentHeight() const
{
    if (!runtime->root) {
        return 0.0f;
    }
    float top = runtime->root->y;
    float bottom = top;
    std::function<void(const JsonUiRuntime::Node&)> walk = [&](const JsonUiRuntime::Node& node) {
        if (!node.shown) {
            return;
        }
        if (node.type != "panel" && node.type != "stack_panel" && node.type != "factory" && node.type != "grid") {
            bottom = std::max(bottom, node.y + node.h);
        }
        for (const auto& child : node.children) {
            walk(*child);
        }
    };
    walk(*runtime->root);
    return bottom - top;
}

bool JsonUiScreen::hovering() const
{
    return runtime->hot != 0;
}

std::string JsonUiScreen::describe(size_t maxLines) const
{
    std::string out;
    size_t lines = 0;
    std::function<void(const jsonui::Node&, int)> walk = [&](const jsonui::Node& node, int depth) {
        if (lines >= maxLines) {
            return;
        }
        ++lines;
        char box[96];
        std::snprintf(box, sizeof(box), " [%.0f,%.0f %.0fx%.0f]", node.x, node.y, node.w, node.h);
        out.append(static_cast<size_t>(depth) * 2, ' ');
        out += node.name + " (" + node.type + ")" + (node.visible ? "" : " hidden") + (node.shown ? " shown" : "") + box;
        if (!node.texture.empty()) {
            out += " texture=" + node.texture;
        }
        if (!node.text.empty()) {
            out += " text=" + node.text.substr(0, 40);
        }
        out += "\n";
        for (const std::unique_ptr<jsonui::Node>& child : node.children) {
            walk(*child, depth + 1);
        }
    };
    if (runtime->root) {
        walk(*runtime->root, 0);
    }
    return out;
}

}
