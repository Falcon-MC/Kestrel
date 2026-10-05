#include "ui/JsonUiInternal.h"

#include "platform/Input.h"
#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Utf8.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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
        && in.wheel == 0.0f && in.text.empty() && in.pressedKey == Key::None && !in.backspace && !in.enter && !in.escape && !in.tab
        && std::none_of(in.gamepad.pressed.begin(), in.gamepad.pressed.end(), [](bool pressed) { return pressed; });
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

/**
 * Focus directions, in the order the focus_change_* and
 * focus_navigation_mode_* properties name them.
 */
constexpr const char* FocusSides[] = { "up", "down", "left", "right" };
constexpr const char* FocusOverrideStop = "FOCUS_OVERRIDE_STOP";
constexpr float FocusSweepCone = 0.02f;
constexpr float FocusEdgeInset = 2.0f;
constexpr float FocusDirectionEpsilon = 1.1920929e-7f;

bool anyCornerInside(const Node& node)
{
    if (!node.clipped) {
        return true;
    }
    const Rect& clip = node.clip;
    for (float x : { node.x, node.x + node.w }) {
        for (float y : { node.y, node.y + node.h }) {
            if (x >= clip.x && x <= clip.right() && y >= clip.y && y <= clip.bottom()) {
                return true;
            }
        }
    }
    return false;
}

/**
 * The nearest candidate ahead of start toward side, within the cone the
 * game sweeps (narrowed to the current control's own corner when that is
 * flatter); clipped drops candidates with no corner inside their clip.
 */
Node* focusSweepAt(const std::vector<Node*>& candidates, const Node& current, int side, std::array<float, 2> origin, std::array<float, 2> start, bool clipped)
{
    std::array<float, 2> axis = side == 0 ? std::array<float, 2> { 0.0f, -1.0f }
        : side == 1                       ? std::array<float, 2> { 0.0f, 1.0f }
        : side == 2                       ? std::array<float, 2> { -1.0f, 0.0f }
                                          : std::array<float, 2> { 1.0f, 0.0f };
    std::array<float, 2> corner = side == 0 || side == 2 ? std::array<float, 2> { current.x, current.y } : std::array<float, 2> { current.x + current.w, current.y + current.h };
    float toX = corner[0] - origin[0];
    float toY = corner[1] - origin[1];
    float length = std::sqrt(toX * toX + toY * toY);
    float reach = length > 1.0e-4f ? (toX * axis[0] + toY * axis[1]) / length : toX * axis[0] + toY * axis[1];
    float cone = std::min(reach, FocusSweepCone);
    Node* best = nullptr;
    float nearest = std::numeric_limits<float>::infinity();
    for (Node* candidate : candidates) {
        if (candidate == &current || (clipped && !anyCornerInside(*candidate))) {
            continue;
        }
        float x0 = candidate->x;
        float x1 = std::max(x0, candidate->x + candidate->w);
        float y0 = candidate->y;
        float y1 = std::max(y0, candidate->y + candidate->h);
        std::array<float, 2> point = side == 0 ? std::array<float, 2> { std::clamp(start[0], x0, x1), y1 }
            : side == 1                        ? std::array<float, 2> { std::clamp(start[0], x0, x1), y0 }
            : side == 2                        ? std::array<float, 2> { x1, std::clamp(start[1], y0, y1) }
                                               : std::array<float, 2> { x0, std::clamp(start[1], y0, y1) };
        float dx = point[0] - start[0];
        float dy = point[1] - start[1];
        float distance = std::sqrt(dx * dx + dy * dy);
        float cosine = distance > FocusDirectionEpsilon ? (dx * axis[0] + dy * axis[1]) / distance : 1.0f;
        if (cosine < cone) {
            continue;
        }
        if (distance < nearest) {
            nearest = distance;
            best = candidate;
        }
    }
    return best;
}

/**
 * The game's directional sweep from current's leading edge, retrying from
 * the far side of bounds when wrap is on.
 */
Node* focusSweep(const std::vector<Node*>& candidates, const Node& current, int side, const Rect& bounds, bool wrap, bool clipped)
{
    float cx = current.x + current.w * 0.5f;
    float cy = current.y + current.h * 0.5f;
    float hw = current.w * 0.5f;
    float hh = current.h * 0.5f;
    std::array<float, 2> start = side == 0 ? std::array<float, 2> { cx, cy - (hh - FocusEdgeInset) }
        : side == 1                        ? std::array<float, 2> { cx, cy + hh - FocusEdgeInset }
        : side == 2                        ? std::array<float, 2> { cx - (hw - FocusEdgeInset), cy }
                                           : std::array<float, 2> { cx + hw - FocusEdgeInset, cy };
    Node* found = focusSweepAt(candidates, current, side, { cx, cy }, start, clipped);
    if (found || !wrap) {
        return found;
    }
    std::array<float, 2> from = side == 0 ? std::array<float, 2> { cx, bounds.bottom() }
        : side == 1                       ? std::array<float, 2> { cx, bounds.y }
        : side == 2                       ? std::array<float, 2> { bounds.right(), cy }
                                          : std::array<float, 2> { bounds.x, cy };
    return focusSweepAt(candidates, current, side, from, from, clipped);
}

bool under(const Node* node, const Node* ancestor)
{
    for (const Node* at = node ? node->parent : nullptr; at; at = at->parent) {
        if (at == ancestor) {
            return true;
        }
    }
    return false;
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

/**
 * How many steps a slider has: the bound #slider_steps, else its
 * slider_steps property. More than one makes it a step slider, whose
 * #slider_value is a step index rather than a 0..1 fraction.
 */
int JsonUiRuntime::sliderSteps(const Node& node) const
{
    if (auto bound = node.bound.find("#slider_steps"); bound != node.bound.end()) {
        return std::max(1, static_cast<int>(bound->second.toNumber()));
    }
    return std::max(1, static_cast<int>(number(node, "slider_steps", 1.0)));
}

bool JsonUiRuntime::isControl(const Node& node)
{
    return node.type == "button" || node.type == "toggle" || node.type == "dropdown" || node.type == "slider" || node.type == "slider_box" || node.type == "edit_box"
        || node.type == "selection_wheel";
}

/**
 * The slice of a selection wheel under a point, counted clockwise from the
 * top, or -1 inside its inner radius or past its outer one.
 */
int JsonUiRuntime::wheelSliceAt(const Node& node, float x, float y) const
{
    int slices = static_cast<int>(number(node, "slice_count", 1.0));
    float radius = std::min(node.w, node.h) * 0.5f;
    if (slices <= 0 || radius <= 0.0f) {
        return -1;
    }
    float dx = x - (node.x + node.w * 0.5f);
    float dy = y - (node.y + node.h * 0.5f);
    float distance = std::sqrt(dx * dx + dy * dy) / radius;
    if (distance < number(node, "inner_radius", 0.0) || distance > number(node, "outer_radius", 1.0)) {
        return -1;
    }
    constexpr float Turn = 6.28318530718f;
    float angle = std::atan2(dx, -dy);
    if (angle < 0.0f) {
        angle += Turn;
    }
    float width = Turn / static_cast<float>(slices);
    return static_cast<int>(std::floor((angle + width * 0.5f) / width)) % slices;
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
    if (node.type == "selection_wheel") {
        // state_controls lists the idle state first, then one per slice; they may sit below the wheel's children.
        const json::Value* list = resolve(node, property(node, "state_controls"));
        if (!list || !list->isArray()) {
            return;
        }
        std::function<void(Node&, const std::string&, bool)> mark = [&](Node& at, const std::string& name, bool visible) {
            for (std::unique_ptr<Node>& child : at.children) {
                if (child->name == name) {
                    node.states[child.get()] = visible;
                }
                if (!isControl(*child)) {
                    mark(*child, name, visible);
                }
            }
        };
        size_t chosen = node.wheelSlice >= 0 ? size_t(node.wheelSlice) + 1 : 0;
        for (size_t i = 0; i < list->mArray.size(); ++i) {
            const json::Value* entry = list->mArray[i].get();
            const json::Value* name = entry && entry->isObject() ? resolve(node, entry->get("control_name")) : nullptr;
            if (name && name->isString()) {
                mark(node, name->mString, i == chosen);
            }
        }
        return;
    }
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
        int steps = sliderSteps(node);
        if (active != node.id) {
            if (auto value = node.bound.find("#slider_value"); value != node.bound.end()) {
                double raw = value->second.toNumber();
                double fraction = steps > 1 ? raw / static_cast<double>(steps - 1) : raw;
                node.value = static_cast<float>(std::clamp(fraction, 0.0, 1.0));
            }
        }
        bool inverted = flag(node, "slider_inverted", false);
        bool vertical = text(node, "slider_direction") == "vertical";
        float shown = inverted ? 1.0f - node.value : node.value;
        float length = vertical ? node.h : node.w;
        if (Node* box = find(node, text(node, "slider_box_control")); box && box != &node) {
            float along = std::round(shown * length - length * 0.5f);
            box->offsetOverride = vertical ? std::array<float, 2> { 0.0f, along } : std::array<float, 2> { along, 0.0f };
        }
        std::vector<UiFactoryItem> marks;
        if (steps > 2 && steps - 2 <= static_cast<int>(MaxFactoryItems)) {
            int current = static_cast<int>(std::round(node.value * static_cast<float>(steps - 1)));
            for (int step = 1; step < steps - 1; ++step) {
                UiFactoryItem mark;
                mark.control = current < step ? "slider_step_progress" : "slider_step";
                mark.serial = static_cast<uint64_t>(step);
                marks.push_back(std::move(mark));
            }
        }
        const json::Value* factory = property(node, "factory");
        syncItems(node, marks, factory ? resolve(node, factory->get("control_ids")) : nullptr, nullptr, MaxFactoryItems, 0);
        for (std::unique_ptr<Node>& child : node.children) {
            if (child->generated && steps > 2) {
                float x = node.w * static_cast<float>(child->serial) / static_cast<float>(steps - 1) - node.w * 0.5f;
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
        node.caretOffset = !typing && listening ? listeningCaretOffset : std::nullopt;
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
            double shown = flag(control, "slider_inverted", false) ? 1.0 - static_cast<double>(control.value) : static_cast<double>(control.value);
            node.bound["#clip_ratio"] = UiValue::of(1.0 - shown);
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
    if (kind == UiEvent::Kind::Button) {
        for (const Node* at = &node; at; at = at->parent) {
            if (auto link = at->bound.find("#hyperlink"); link != at->bound.end() && !link->second.toText().empty()) {
                event.text = link->second.toText();
                break;
            }
        }
    }
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
 * Whether a control can take the focus: one of the types with a focus
 * component, enabled, laid out and with focus_enabled (or a bound
 * #focus_enabled) set.
 */
bool JsonUiRuntime::focusable(const Node& node) const
{
    static const char* const types[] = { "button", "toggle", "dropdown", "slider", "edit_box", "input_panel", "scroll_view", "selection_wheel", "custom" };
    if (std::none_of(std::begin(types), std::end(types), [&](const char* type) { return node.type == type; })) {
        return false;
    }
    if (!node.enabled || !node.shown || node.w <= 0.0f || node.h <= 0.0f) {
        return false;
    }
    if (auto bound = node.bound.find("#focus_enabled"); bound != node.bound.end()) {
        return bound->second.truthy();
    }
    return flag(node, "focus_enabled", false);
}

/**
 * Moves the keyboard and gamepad focus the way the game's focus manager
 * does. With nothing focused, an arrow focuses the control with the highest
 * whole default_focus_precedence (the first in reading order on a tie) and
 * Tab the first in order. An arrow then takes the control's
 * focus_change_* (or the focus_mapping for its focus_identifier), where
 * FOCUS_OVERRIDE_STOP keeps the focus in place, else sweeps that way inside
 * the scroll views around it before the whole screen, wrapping with
 * focus_wrap_enabled, and applies the focus_navigation_mode_* of the focus
 * containers it leaves and the use_last_focus of the one it enters. Tab goes
 * through the controls in order. Enter or the gamepad's A presses the
 * focused control, and moving the mouse hands focus back to it.
 */
void JsonUiRuntime::navigateFocus(const InputState& in, float mouseX, float mouseY)
{
    if (laidOut && (mouseX != laidMouseX || mouseY != laidMouseY)) {
        keyFocus = 0;
    }
    Node* modal = nullptr;
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if ((*it)->type == "input_panel" && flag(**it, "modal", false)) {
            modal = *it;
            break;
        }
    }
    std::vector<Node*> candidates;
    for (Node* node : order) {
        if (focusable(*node) && (!modal || node == modal || under(node, modal))) {
            candidates.push_back(node);
        }
    }
    auto current = std::find_if(candidates.begin(), candidates.end(), [&](const Node* node) { return node->id == keyFocus; });
    if (current == candidates.end()) {
        keyFocus = 0;
    }
    const GamepadState& pad = in.gamepad;
    if ((in.enter || pad.wasPressed(PadButton::A)) && keyFocus) {
        click(**current);
        return;
    }
    Key key = in.pressedKey;
    int side = key == Key::Up || pad.wasPressed(PadButton::DpadUp) ? 0
        : key == Key::Down || pad.wasPressed(PadButton::DpadDown)  ? 1
        : key == Key::Left || pad.wasPressed(PadButton::DpadLeft)  ? 2
        : key == Key::Right || pad.wasPressed(PadButton::DpadRight) ? 3
                                                                    : -1;
    if ((side < 0 && !in.tab) || candidates.empty()) {
        return;
    }
    auto focus = [&](Node* node) {
        if (!node) {
            return;
        }
        for (const Node* at = node->parent; at; at = at->parent) {
            if (flag(*at, "focus_container", false)) {
                lastFocus[at->id] = node->id;
            }
        }
        keyFocus = node->id;
    };
    if (in.tab) {
        size_t count = candidates.size();
        bool backwards = in.isHeld(Key::Shift);
        size_t index = !keyFocus ? (backwards ? count - 1 : 0) : static_cast<size_t>(current - candidates.begin());
        if (keyFocus) {
            index = backwards ? (index + count - 1) % count : (index + 1) % count;
        }
        focus(candidates[index]);
        return;
    }
    if (!keyFocus) {
        auto precedence = [&](const Node& node) {
            double value = number(node, "default_focus_precedence", 0.0);
            return std::floor(value) == value ? std::max(0.0, value) : 0.0;
        };
        double best = 0.0;
        for (Node* node : candidates) {
            best = std::max(best, precedence(*node));
        }
        float width = root ? root->w : 0.0f;
        Node* first = nullptr;
        for (Node* node : candidates) {
            if (precedence(*node) == best && (!first || node->y * width + node->x < first->y * width + first->x)) {
                first = node;
            }
        }
        focus(first);
        return;
    }
    Node& from = **current;
    if (flag(from, "always_handle_controller_direction", false)) {
        return;
    }
    std::string sideName = FocusSides[side];
    std::string over = text(from, "focus_change_" + sideName);
    std::string identifier = text(from, "focus_identifier");
    if (over.empty() && !identifier.empty()) {
        for (Node* node : order) {
            const json::Value* mapping = property(*node, "focus_mapping");
            if (!mapping || !mapping->isArray()) {
                continue;
            }
            for (const std::unique_ptr<json::Value>& entry : mapping->mArray) {
                const json::Value* id = entry->isObject() ? resolve(*node, entry->get("focus_identifier")) : nullptr;
                if (id && id->isString() && id->mString == identifier) {
                    const json::Value* change = resolve(*node, entry->get("focus_change_" + sideName));
                    over = change && change->isString() ? change->mString : std::string();
                    break;
                }
            }
            if (!over.empty()) {
                break;
            }
        }
    }
    if (over == FocusOverrideStop) {
        return;
    }
    if (!over.empty()) {
        for (Node* node : candidates) {
            if (text(*node, "focus_identifier") == over) {
                focus(node);
                return;
            }
        }
    }
    std::vector<Node*> sections;
    for (Node* at = from.parent; at; at = at->parent) {
        if (at->type == "scroll_view") {
            sections.push_back(at);
        }
    }
    Node* target = nullptr;
    for (Node* view : sections) {
        std::vector<Node*> inside;
        for (Node* node : candidates) {
            if (under(node, view)) {
                inside.push_back(node);
            }
        }
        Rect bounds { view->x, view->y, view->w, view->h };
        target = focusSweep(inside, from, side, view->clipped ? intersect(bounds, view->clip) : bounds, false, false);
        if (target) {
            break;
        }
    }
    if (!target) {
        std::vector<Node*> outside;
        for (Node* node : candidates) {
            if (sections.empty() || !under(node, sections.front())) {
                outside.push_back(node);
            }
        }
        Rect screen = root ? Rect { root->x, root->y, root->w, root->h } : Rect {};
        target = focusSweep(outside, from, side, screen, sections.empty() && flag(from, "focus_wrap_enabled", true), true);
    }
    auto holds = [&](const Node* container, const Node* node) {
        return node && under(node, container);
    };
    auto identified = [&](const Node* node) {
        return text(*node, "focus_identifier");
    };
    for (Node* container = from.parent; container; container = container->parent) {
        if (!flag(*container, "focus_container", false)) {
            continue;
        }
        if (holds(container, target)) {
            break;
        }
        std::string mode = text(*container, "focus_navigation_mode_" + sideName);
        if (mode == "stop") {
            return;
        }
        if (mode == "contained") {
            std::vector<Node*> inside;
            for (Node* node : candidates) {
                if (holds(container, node)) {
                    inside.push_back(node);
                }
            }
            focus(focusSweep(inside, from, side, Rect { container->x, container->y, container->w, container->h }, flag(*container, "focus_wrap_enabled", true), true));
            return;
        }
        if (mode == "custom") {
            const json::Value* routes = property(*container, "focus_container_custom_" + sideName);
            for (size_t i = 0; routes && routes->isArray() && i < routes->mArray.size(); ++i) {
                const json::Value* route = routes->mArray[i].get();
                const json::Value* name = route->isObject() ? resolve(*container, route->get("other_focus_container_name")) : nullptr;
                if (!name || !name->isString() || name->mString.empty()) {
                    continue;
                }
                const Node* other = nullptr;
                for (Node* node : candidates) {
                    for (const Node* at = node->parent; at && !other; at = at->parent) {
                        if (at->name == name->mString && flag(*at, "focus_container", false)) {
                            other = at;
                        }
                    }
                    if (other) {
                        break;
                    }
                }
                if (!other) {
                    continue;
                }
                const json::Value* insideId = resolve(*container, route->get("focus_id_inside"));
                if (insideId && insideId->isString() && !insideId->mString.empty()) {
                    for (Node* node : candidates) {
                        if (holds(other, node) && identified(node) == insideId->mString) {
                            focus(node);
                            return;
                        }
                    }
                    continue;
                }
                bool nested = holds(container, other) || holds(other, container);
                if (flag(*other, "use_last_focus", false) && !nested) {
                    if (auto last = lastFocus.find(other->id); last != lastFocus.end()) {
                        for (Node* node : candidates) {
                            if (node->id == last->second && holds(other, node)) {
                                focus(node);
                                return;
                            }
                        }
                    }
                }
                Node* closest = nullptr;
                float closestDistance = std::numeric_limits<float>::infinity();
                float ox = from.x + from.w * 0.5f;
                float oy = from.y + from.h * 0.5f;
                for (Node* node : candidates) {
                    if (!holds(other, node)) {
                        continue;
                    }
                    float dx = node->x + node->w * 0.5f - ox;
                    float dy = node->y + node->h * 0.5f - oy;
                    if (dx * dx + dy * dy < closestDistance) {
                        closestDistance = dx * dx + dy * dy;
                        closest = node;
                    }
                }
                if (closest) {
                    focus(closest);
                    return;
                }
            }
            return;
        }
    }
    if (!target) {
        return;
    }
    const Node* entered = nullptr;
    for (const Node* at = target->parent; at; at = at->parent) {
        if (flag(*at, "focus_container", false) && !holds(at, &from)) {
            entered = at;
        }
    }
    if (entered && flag(*entered, "use_last_focus", false)) {
        if (auto last = lastFocus.find(entered->id); last != lastFocus.end()) {
            for (Node* node : candidates) {
                if (node->id == last->second) {
                    focus(node);
                    return;
                }
            }
        }
    }
    focus(target);
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
    if (node.type == "selection_wheel") {
        if (node.wheelSlice >= 0) {
            emit(UiEvent::Kind::Button, node, text(node, "select_button_name"));
            events.back().index = node.wheelSlice;
        }
        return;
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
        if (!mapButton(node, "button.menu_select", "pressed")) {
            mapButton(node, "button.menu_select", "focused");
        }
        return;
    }
    if (node.type == "toggle" || node.type == "dropdown") {
        bool checked = node.dataToggle ? node.bound["#toggle_state"].truthy() : node.toggled;
        bool next = flag(node, "radio_toggle_group", false) ? true : !checked;
        node.toggled = next;
        node.bound["#toggle_state"] = UiValue::of(next);
        std::string name = text(node, "toggle_name");
        if (!name.empty() && name.front() == '(') {
            UiLookup find = [&](const std::string& key) { return lookup(node, key); };
            name = jsonui::evaluateName(name, find).toText();
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
    for (Node* node : order) {
        if (node->type != "selection_wheel") {
            continue;
        }
        int slice = hot == node->id ? wheelSliceAt(*node, mx, my) : -1;
        if (slice >= 0 && slice != node->wheelSlice) {
            emit(UiEvent::Kind::Button, *node, text(*node, "hover_button_name"));
            events.back().index = slice;
        }
        node->wheelSlice = slice;
    }
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
        bool vertical = dragged->type == "slider" && text(*dragged, "slider_direction") == "vertical";
        float length = vertical ? dragged->h : dragged->w;
        if (in.mouseDown && dragged->type == "slider" && length > 0.0f) {
            float along = vertical ? my - dragged->y : mx - dragged->x;
            float fraction = std::clamp(along / length, 0.0f, 1.0f);
            if (flag(*dragged, "slider_inverted", false)) {
                fraction = 1.0f - fraction;
            }
            int steps = sliderSteps(*dragged);
            double reported = fraction;
            if (steps > 1) {
                int index = std::clamp(static_cast<int>(std::floor(fraction * static_cast<float>(steps - 1) + 0.5f)), 0, steps - 1);
                fraction = static_cast<float>(index) / static_cast<float>(steps - 1);
                reported = static_cast<double>(index);
            }
            if (fraction != dragged->value || in.mousePressed) {
                dragged->value = fraction;
                emit(UiEvent::Kind::Slider, *dragged, text(*dragged, "slider_name"));
                events.back().value = reported;
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
        if (sprite.slice.declared) {
            draw(rect, source);
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
            if (node.caretOffset && *node.caretOffset < node.text.size()) {
                const Font& font = ui->textFont();
                std::vector<std::string_view> parts;
                font.wrap(node.text, style, rect.w / scale + 0.01f, parts);
                size_t offset = std::min(*node.caretOffset, node.text.size());
                for (size_t i = 0; i < parts.size(); ++i) {
                    size_t begin = static_cast<size_t>(parts[i].data() - node.text.data());
                    if (i + 1 < parts.size() && offset >= static_cast<size_t>(parts[i + 1].data() - node.text.data())) continue;
                    float width = font.measure(parts[i], style) * scale;
                    float lineX = alignment == "center" ? rect.x + std::floor((rect.w - width) * 0.5f) : alignment == "right" ? rect.right() - width : rect.x;
                    size_t length = offset > begin ? std::min(offset - begin, parts[i].size()) : 0;
                    caretX = lineX + std::max(0.0f, font.measure(parts[i].substr(0, length), style) * scale - (length > 0 ? scale : 0.0f));
                    caretY = rect.y + static_cast<float>(i) * (LabelLineHeight + padding) * scale;
                    break;
                }
            }
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
    for (Node* node : r.order) {
        if (node->type != "dropdown" || !node->parent || !r.lookup(*node, "#toggle_state").truthy()) continue;
        Node* popup = r.nearest(*node->parent, r.text(*node, "dropdown_content_control"));
        std::string boundsName = r.text(*node, "dropdown_area");
        Node* boundsNode = boundsName.empty() ? nullptr : r.named(boundsName);
        if (!popup || !popup->parent || popup == node || popup == node->parent || !popup->shown || !boundsNode || !boundsNode->shown) continue;
        Rect bounds = intersect(area, { boundsNode->x, boundsNode->y, boundsNode->w, boundsNode->h });
        if (boundsNode->clipped) bounds = intersect(bounds, boundsNode->clip);
        if (!(bounds.w > 0.0f && bounds.h > 0.0f)) continue;
        r.size(*popup, 0, popup->parent->w, std::min(popup->w, bounds.w));
        r.size(*popup, 1, popup->parent->h, std::min(popup->h, bounds.h));
        scrolls(*popup);
        float x = std::clamp(popup->x, bounds.x, std::max(bounds.x, bounds.right() - popup->w));
        float y = std::clamp(popup->y, bounds.y, std::max(bounds.y, bounds.bottom() - popup->h));
        // A dropdown overlays the scrolled row and stays inside its declared dropdown_area.
        r.place(*popup, x, y, popup->z, bounds, true, popup->inherited);
    }
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
    showListeningCaret(shown, std::nullopt);
}

void JsonUiScreen::showListeningCaret(bool shown, std::optional<size_t> byteOffset)
{
    if (runtime->listeningCaret != shown || runtime->listeningCaretOffset != byteOffset) {
        runtime->listeningCaret = shown;
        runtime->listeningCaretOffset = byteOffset;
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

std::optional<Rect> JsonUiScreen::controlRect(const std::string& name) const
{
    if (!runtime->root) {
        return std::nullopt;
    }
    std::vector<const JsonUiRuntime::Node*> queue { runtime->root.get() };
    for (size_t i = 0; i < queue.size(); ++i) {
        const JsonUiRuntime::Node* node = queue[i];
        if (!node->shown) {
            continue;
        }
        if (node->name == name) {
            return Rect { node->x, node->y, node->w, node->h };
        }
        for (const auto& child : node->children) {
            queue.push_back(child.get());
        }
    }
    return std::nullopt;
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

namespace kestrel::ui {

std::vector<Rect> JsonUiScreen::controlRects(const std::string& name) const
{
    std::vector<Rect> found;
    if (!runtime->root) {
        return found;
    }
    std::function<void(const JsonUiRuntime::Node&)> walk = [&](const JsonUiRuntime::Node& node) {
        if (!node.shown) {
            return;
        }
        if (node.name == name) {
            found.push_back({ node.x, node.y, node.w, node.h });
        }
        for (const auto& child : node.children) {
            walk(*child);
        }
    };
    walk(*runtime->root);
    return found;
}

}
