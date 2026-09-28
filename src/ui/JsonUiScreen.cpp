#include "JsonUiInternal.h"

#include "platform/Input.h"
#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Utf8.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

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

constexpr float MinScrollBox = 8.0f;
constexpr float WheelStep = 1.6f;
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

/**
 * The section sign codes still in effect at the end of text, so a wrapped
 * line starts in the color and style the one before it ended in.
 */
std::string formatting(std::string_view text)
{
    std::string color;
    std::string styles;
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        if (static_cast<unsigned char>(text[i]) != 0xC2 || static_cast<unsigned char>(text[i + 1]) != 0xA7) {
            continue;
        }
        char code = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i + 2])));
        std::string sequence(text.substr(i, 3));
        if ((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') || (code >= 'g' && code <= 'v' && code != 'k' && code != 'l' && code != 'm' && code != 'n' && code != 'o' && code != 'r')) {
            color = sequence;
            styles.clear();
        } else if (code == 'r') {
            color.clear();
            styles.clear();
        } else if (code == 'k' || code == 'l' || code == 'm' || code == 'n' || code == 'o') {
            styles += sequence;
        }
        i += 2;
    }
    return color + styles;
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
 * Picks which state children of a button, toggle, slider or edit box show,
 * from whether the mouse is over it, pressing it or it is locked, the way
 * default_control, hover_control and the rest name them.
 */
void JsonUiRuntime::chooseStates(Node& node)
{
    node.states.clear();
    bool hovered = hot == node.id;
    bool pressed = active == node.id && hovered && ui && ui->input().mouseDown;
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
    if (control.type == "edit_box" && focused == control.id && node.name == text(control, "text_control")) {
        bool caret = static_cast<long long>(now * 2.0) % 2 == 0;
        node.bound["#item_name"] = UiValue::of(control.edit + (caret ? "_" : ""));
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
        if (runtime.flag(node, "localize", true) && !node.text.empty() && Localization::shared().has(node.text)) {
            node.text = tr(node.text, node.text);
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
        node.alpha = *alpha;
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
        TermKind fallback = node.type == "label" || node.type == "grid" ? TermKind::Default : TermKind::Parent;
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
        bool fits = node.type == "label" || node.type == "image" || node.type == "grid" || (node.type == "stack_panel" && (axis == 1) == node.vertical);
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
        if (Node* content = runtime.find(node, runtime.text(node, "scroll_content")); content && content != &node) {
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
 * it as their play_event start, and chains with it as their reset_event go
 * back to their first anim.
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
    for (const Node* at = &node; at; at = at->parent) {
        if (at->index < 0) {
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

void JsonUiRuntime::click(Node& node)
{
    if (!node.enabled) {
        return;
    }
    if (!text(node, "sound_name").empty()) {
        ui->countClick();
    }
    if (node.type == "button") {
        const json::Value* mappings = property(node, "button_mappings");
        if (!mappings || !mappings->isArray()) {
            return;
        }
        for (const std::unique_ptr<json::Value>& mapping : mappings->mArray) {
            if (!mapping->isObject() || (mapping->get("ignored") && condition(node, mapping->get("ignored")))) {
                continue;
            }
            const json::Value* from = resolve(node, mapping->get("from_button_id"));
            const json::Value* type = resolve(node, mapping->get("mapping_type"));
            const json::Value* to = resolve(node, mapping->get("to_button_id"));
            if (from && from->string() == "button.menu_select" && type && type->string() == "pressed" && to && to->isString() && !to->mString.empty()) {
                emit(UiEvent::Kind::Button, node, to->mString);
                return;
            }
        }
        return;
    }
    if (node.type == "toggle" || node.type == "dropdown") {
        bool checked = node.dataToggle ? node.bound["#toggle_state"].truthy() : node.toggled;
        bool next = flag(node, "radio_toggle_group", false) ? true : !checked;
        node.toggled = next;
        node.bound["#toggle_state"] = UiValue::of(next);
        emit(UiEvent::Kind::Toggle, node, text(node, "toggle_name"));
        events.back().state = next;
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
                target = &node;
                break;
            }
        }
    }
    hot = target ? target->id : 0;

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
                Node* content = find(*view, text(*view, "scroll_content"));
                Node* port = find(*view, text(*view, "scroll_view_port"));
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

    if (in.wheel != 0.0f) {
        for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
            if ((*it)->type == "scroll_view" && under(**it)) {
                (*it)->scroll -= in.wheel * static_cast<float>(number(**it, "scroll_speed", 15.0)) * WheelStep;
                fire(**it, "scrollbar.active");
                fire(**it, "scrollbar.released");
                break;
            }
        }
    }

    auto typing = byId.find(focused);
    Node* editing = focused && typing != byId.end() ? typing->second : nullptr;
    if (!editing) {
        focused = 0;
    }
    if (editing) {
        std::u32string typed = in.text;
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
            if (cp < 32 || cp == 127 || length >= limit) {
                continue;
            }
            appendUtf8(editing->edit, cp);
            ++length;
        }
        if (editing->edit != before) {
            emit(UiEvent::Kind::Text, *editing, text(*editing, "text_box_name"));
            events.back().text = editing->edit;
        }
        if (in.enter || in.escape) {
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
        }
        if (!node.enabled) {
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
        if (ratio >= 1.0) {
            return;
        }
        if (ratio > 0.0) {
            // clip_ratio is the part cut away; clip_direction is the side the rest stays on.
            std::string direction = text(node, "clip_direction");
            float keep = static_cast<float>(1.0 - ratio);
            Rect visible = rect;
            if (direction == "right") {
                visible.x = rect.right() - rect.w * keep;
                visible.w = rect.w * keep;
            } else if (direction == "up") {
                visible.h = rect.h * keep;
            } else if (direction == "down") {
                visible.y = rect.bottom() - rect.h * keep;
                visible.h = rect.h * keep;
            } else if (direction == "center") {
                visible = { rect.x + rect.w * (1.0f - keep) * 0.5f, rect.y + rect.h * (1.0f - keep) * 0.5f, rect.w * keep, rect.h * keep };
            } else {
                visible.w = rect.w * keep;
            }
            ui->setClip(node.clipped ? intersect(node.clip, visible) : visible);
        }
        const json::Value* uv = property(node, "uv");
        const json::Value* uvSize = property(node, "uv_size");
        std::array<float, 2> origin { 0.0f, 0.0f };
        bool region = false;
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
            const json::Value* initial = property(probe, "initial_uv");
            int frames = std::max(1, static_cast<int>(number(probe, "frame_count", 1.0)));
            float stepSize = static_cast<float>(number(probe, "frame_step", 0.0));
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
        if (region || (uvSize && uvSize->isArray())) {
            float uw = uvSize && uvSize->isArray() && uvSize->mArray.size() == 2 ? term(node, uvSize->mArray[0].get(), 0.0f) : sprite.width;
            float uh = uvSize && uvSize->isArray() && uvSize->mArray.size() == 2 ? term(node, uvSize->mArray[1].get(), 0.0f) : sprite.height;
            ui->spriteRegion(rect, node.texture, { origin[0], origin[1], uw, uh }, color);
            return;
        }
        const json::Value* tiled = property(node, "tiled");
        bool tileX = tiled && (tiled->boolean(false) || tiled->string() == "x");
        bool tileY = tiled && (tiled->boolean(false) || tiled->string() == "y");
        if ((tileX || tileY) && sprite.width > 0.0f && sprite.height > 0.0f) {
            float tw = tileX ? sprite.width : rect.w;
            float th = tileY ? sprite.height : rect.h;
            if (const json::Value* scale = property(node, "tiled_scale"); scale && scale->isArray() && scale->mArray.size() == 2) {
                tw *= tileX ? static_cast<float>(scale->mArray[0]->number(1.0)) : 1.0f;
                th *= tileY ? static_cast<float>(scale->mArray[1]->number(1.0)) : 1.0f;
            }
            Rect bounds = node.clipped ? intersect(node.clip, rect) : rect;
            ui->setClip(bounds);
            for (float ty = rect.y; ty < rect.bottom() && th > 0.0f; ty += th) {
                for (float tx = rect.x; tx < rect.right() && tw > 0.0f; tx += tw) {
                    ui->sprite({ tx, ty, tw, th }, node.texture, color);
                }
            }
            return;
        }
        if (sprite.slice.left > 0.0f || sprite.slice.top > 0.0f || sprite.slice.right > 0.0f || sprite.slice.bottom > 0.0f) {
            ui->nineSlice(rect, node.texture, color);
        } else {
            ui->sprite(rect, node.texture, color);
        }
        return;
    }

    if (node.type == "label") {
        if (node.text.empty()) {
            return;
        }
        float scale = labelScale(node);
        float padding = static_cast<float>(number(node, "line_padding", 0.0));
        Color color = tint();
        bool shadow = flag(node, "shadow", false);
        TextStyle style = labelStyle(node);
        std::string alignment = text(node, "text_alignment");
        float lineY = rect.y;
        size_t start = 0;
        std::string carried;
        std::vector<std::string_view> wrapped;
        while (true) {
            size_t end = node.text.find('\n', start);
            std::string segment = carried + node.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            wrapped.clear();
            if (ui->wrap(segment, style, rect.w / scale + 0.01f, wrapped) == 0) {
                wrapped.assign(1, segment);
            }
            for (size_t i = 0; i < wrapped.size(); ++i) {
                std::string line = i == 0 ? std::string(wrapped[i]) : carried + std::string(wrapped[i]);
                carried = formatting(line);
                float width = ui->measure(line, style) * scale;
                float lineX = alignment == "center" ? rect.x + std::floor((rect.w - width) * 0.5f) : alignment == "right" ? rect.right() - width : rect.x;
                if (shadow) {
                    ui->textScaled(line, style, lineX + scale, lineY + scale, scale, color, true);
                }
                ui->textScaled(line, style, lineX, lineY, scale, color);
                lineY += (LabelLineHeight + padding) * scale;
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return;
    }

    if (node.type == "custom" && text(node, "renderer") == "hover_text_renderer") {
        paintHoverText(node, alpha);
        return;
    }
    if (node.type == "custom" && renderer) {
        std::string name = text(node, "renderer");
        UiLookup find = [&](const std::string& key) { return lookup(node, key); };
        renderer(*ui, name, rect, alpha, find);
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
    if (value.empty() || !root) {
        return;
    }
    if (Localization::shared().has(value)) {
        value = tr(value, value);
    }
    float limit = static_cast<float>(number(node, "hover_text_max_width", 0.0));
    std::vector<std::string_view> lines;
    if (limit <= 0.0f || ui->wrap(value, TextStyle::Pixel, limit, lines) == 0) {
        lines.assign(1, value);
    }
    float width = 0.0f;
    for (std::string_view line : lines) {
        width = std::max(width, ui->measure(line, TextStyle::Pixel));
    }
    float height = static_cast<float>(lines.size()) * LabelLineHeight;
    Rect box { ui->mouseX() + MouseGap, ui->mouseY() - MouseGap, width + Padding * 2.0f, height + Padding * 2.0f };
    box.x = std::min(box.x, root->w - box.w);
    box.y = std::clamp(box.y, 0.0f, std::max(0.0f, root->h - box.h));
    Color tint { 255, 255, 255, static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) };
    ui->clearClip();
    ui->nineSlice(box, "textures/ui/purpleBorder", tint);
    float y = box.y + Padding;
    for (std::string_view line : lines) {
        ui->pixelTextScaled(line, box.x + Padding + 1.0f, y + 1.0f, 1.0f, tint, true);
        ui->pixelTextScaled(line, box.x + Padding, y, 1.0f, tint);
        y += LabelLineHeight;
    }
}

JsonUiScreen::JsonUiScreen(std::shared_ptr<const JsonUi> definitions, std::string root)
    : runtime(std::make_unique<JsonUiRuntime>())
{
    runtime->defs = std::move(definitions);
    runtime->rootReference = std::move(root);
    runtime->now = secondsNow();
    if (!runtime->defs) {
        return;
    }
    runtime->root = runtime->make(nullptr, "root@" + runtime->rootReference, nullptr, nullptr, nullptr, nullptr, 0);
    fire("screen.entrance_push");
}

JsonUiScreen::~JsonUiScreen() = default;

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

void JsonUiScreen::fire(const std::string& event)
{
    if (runtime->root) {
        runtime->fire(*runtime->root, event);
    }
}

void JsonUiScreen::draw(Context& ui, const Rect& area, const UiData& data)
{
    if (!runtime->root) {
        return;
    }
    JsonUiRuntime& r = *runtime;
    r.ui = &ui;
    r.data = &data;
    r.now = secondsNow();
    Node& root = *r.root;

    if (r.laidOut && r.madeThisFrame == 0 && r.focused == 0 && r.events.empty() && quiet(ui) && !animating(root)
        && r.laidArea.x == area.x && r.laidArea.y == area.y && r.laidArea.w == area.w && r.laidArea.h == area.h
        && r.laidMouseX == ui.mouseX() && r.laidMouseY == ui.mouseY() && r.laidBlocked == ui.isBlocked() && r.laidData == data) {
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
    r.size(root, 0, area.w);
    r.size(root, 1, area.h);

    // Scrolling is clamped once the content is sized, and the scroll box takes the share of
    // its track the view does of the content.
    std::function<void(Node&)> scrolls = [&](Node& node) {
        if (!node.shown) {
            return;
        }
        if (node.type == "scroll_view") {
            Node* content = r.find(node, r.text(node, "scroll_content"));
            Node* port = r.find(node, r.text(node, "scroll_view_port"));
            Node* box = r.find(node, r.text(node, "scrollbar_box"));
            if (content && port) {
                float range = std::max(0.0f, content->h - port->h);
                node.scroll = std::clamp(node.scroll, 0.0f, range);
                if (box && box->parent) {
                    bool always = r.flag(node, "scrollbar_always_visible", false);
                    if (range <= 0.0f && !always) {
                        box->shown = false;
                        if (Node* track = r.find(node, r.text(node, "scrollbar_track"))) {
                            track->shown = false;
                        }
                    } else {
                        float track = box->parent->h;
                        float height = content->h > 0.0f ? std::max(MinScrollBox, std::round(track * port->h / content->h)) : track;
                        height = std::min(height, track);
                        r.size(*box, 1, track, height);
                        box->offsetOverride = std::array<float, 2> { 0.0f, range > 0.0f ? std::round((track - height) * node.scroll / range) : 0.0f };
                    }
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
    r.laidOut = true;
    r.laidArea = area;
    r.laidData = data;
    r.laidMouseX = ui.mouseX();
    r.laidMouseY = ui.mouseY();
    r.laidBlocked = ui.isBlocked();
    r.data = nullptr;
}

std::vector<UiEvent> JsonUiScreen::takeEvents()
{
    std::vector<UiEvent> taken = std::move(runtime->events);
    runtime->events.clear();
    return taken;
}

bool JsonUiScreen::editing() const
{
    return runtime->focused != 0;
}

bool JsonUiScreen::hovering() const
{
    return runtime->hot != 0;
}

}
