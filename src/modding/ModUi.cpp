#include "modding/ModUi.h"
#include "ui/Utf8.h"

#include <algorithm>
#include <cmath>

namespace kestrel::modding {

namespace {
bool valid(std::string_view id) { return !id.empty() && id.size() <= 256; }
void consume(InputState& input)
{
    float x = input.mouseX, y = input.mouseY;
    input = {};
    input.mouseX = x;
    input.mouseY = y;
}
size_t before(const std::string& text, size_t caret)
{
    if (!caret) return 0;
    --caret;
    while (caret && (static_cast<unsigned char>(text[caret]) & 0xc0) == 0x80) --caret;
    return caret;
}
size_t after(const std::string& text, size_t caret)
{
    if (caret >= text.size()) return text.size();
    ui::nextCodepoint(text, caret);
    return caret;
}
}

void ModUi::process(size_t owner, mod::detail::UiRequest& request)
{
    using Action = mod::detail::UiRequest::Action;
    request.result = false;
    if (request.action == Action::Supported) { request.result = true; return; }
    if (!valid(request.id)) return;
    auto found = std::find_if(screens.begin(), screens.end(), [&](const auto& screen) { return screen->owner == owner && screen->id == request.id; });
    if (request.action == Action::IsOpen) { request.result = found != screens.end(); return; }
    if (request.action == Action::Close) {
        if (found == screens.end()) return;
        auto previous = top();
        screens.erase(found);
        if (auto screen = top(); screen && screen != previous) {
            screen->controls.begin({}, screen->controls.scale);
            screen->controls.active.clear();
        }
        request.result = true;
        return;
    }
    if (request.action != Action::Open) return;
    if (found == screens.end() && (screens.size() >= 32
        || std::count_if(screens.begin(), screens.end(), [owner](const auto& screen) { return screen->owner == owner; }) >= 8)) return;
    if (found != screens.end() && *found == top()) { request.result = true; return; }
    if (auto screen = top()) {
        screen->controls.input = {};
        screen->controls.active.clear();
    }
    if (found != screens.end()) {
        auto screen = *found;
        screens.erase(found);
        screen->controls.begin({}, screen->controls.scale);
        screens.push_back(std::move(screen));
    } else screens.push_back(std::make_shared<Screen>(Screen { owner, request.id, {} }));
    request.result = true;
}

void ModUi::capture(InputState& input, float scale)
{
    auto screen = top();
    if (!screen || consumed) return;
    if (input.escape) {
        screens.pop_back();
        if (auto next = top()) { next->controls.begin({}, scale); next->controls.active.clear(); }
    } else screen->controls.begin(input, scale);
    captured = input;
    consumed = true;
    consume(input);
}

void ModUi::restoreInput(InputState& input)
{
    if (!consumed) return;
    // Restore persistent state before the next platform pump handles releases.
    input.held = captured.held;
    input.mouseDown = captured.mouseDown;
    input.rightMouseDown = captured.rightMouseDown;
    input.gamepad = captured.gamepad;
    consumed = false;
}

void ModUi::release(size_t owner)
{
    auto previous = top();
    std::erase_if(screens, [owner](const auto& screen) { return screen->owner == owner; });
    if (auto screen = top(); screen && screen != previous) {
        screen->controls.begin({}, screen->controls.scale);
        screen->controls.active.clear();
    }
}

void UiControlState::begin(const InputState& next, float nextScale)
{
    input = next;
    scale = std::isfinite(nextScale) && nextScale > 0 ? nextScale : 1.0f;
    current.clear();
    if (input.tab && !previous.empty()) {
        std::vector<std::string> ids;
        for (const auto& item : previous) if (item.enabled) ids.push_back(item.id);
        if (!ids.empty()) {
            auto found = std::find(ids.begin(), ids.end(), focus);
            size_t index = found == ids.end() ? (input.isHeld(Key::Shift) ? ids.size() - 1 : 0)
                : (static_cast<size_t>(found - ids.begin()) + (input.isHeld(Key::Shift) ? ids.size() - 1 : 1)) % ids.size();
            focus = ids[index];
            caret = SIZE_MAX;
            selected = false;
        }
    }
    if (input.mousePressed) {
        focus.clear();
        active.clear();
        selected = false;
    }
}

bool UiControlState::add(std::string_view id, Kind kind, mod::Rect rect, bool enabled)
{
    if (!valid(id) || current.size() >= 256 || !std::isfinite(rect.x) || !std::isfinite(rect.y)
        || !std::isfinite(rect.w) || !std::isfinite(rect.h) || rect.w <= 0 || rect.h <= 0
        || std::any_of(current.begin(), current.end(), [&](const auto& item) { return item.id == id; })) return false;
    current.push_back({ std::string(id), kind, rect, enabled });
    if (!enabled) { if (focus == id) focus.clear(); if (active == id) active.clear(); return true; }
    if (input.mousePressed && rect.contains(input.mouseX / scale, input.mouseY / scale)) {
        focus = id;
        active = id;
        caret = SIZE_MAX;
    }
    return true;
}

bool UiControlState::button(std::string_view id, mod::Rect rect)
{
    return (active == id && input.mouseReleased && rect.contains(input.mouseX / scale, input.mouseY / scale))
        || (focus == id && (input.enter || input.pressedKey == Key::Space));
}

bool UiControlState::slider(std::string_view id, mod::Rect rect, float& value, float minimum, float maximum, float step)
{
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(step)
        || maximum <= minimum || !std::isfinite(maximum - minimum) || step < 0) return false;
    float next = value;
    if (active == id && (input.mouseDown || input.mouseReleased || input.mousePressed)) {
        next = minimum + std::clamp((input.mouseX / scale - rect.x) / rect.w, 0.0f, 1.0f) * (maximum - minimum);
    } else if (focus == id && (input.pressedKey == Key::Left || input.pressedKey == Key::Right)) {
        next += (input.pressedKey == Key::Left ? -1 : 1) * (step > 0 ? step : (maximum - minimum) / 100.0f);
    } else return false;
    if (step > 0) next = minimum + std::round((next - minimum) / step) * step;
    next = std::clamp(next, minimum, maximum);
    if (!std::isfinite(next) || next == value) return false;
    value = next;
    return true;
}

bool UiControlState::textField(std::string_view id, std::string& value, size_t maxBytes)
{
    if (focus != id) return false;
    maxBytes = std::min(maxBytes, size_t(65536));
    caret = std::min(caret, value.size());
    while (caret && caret < value.size() && (static_cast<unsigned char>(value[caret]) & 0xc0) == 0x80) --caret;
    if (input.isHeld(Key::Control) && input.pressedKey == Key::A) { selected = true; return false; }
    if (input.pressedKey == Key::Left) { caret = selected ? 0 : before(value, caret); selected = false; }
    if (input.pressedKey == Key::Right) { caret = selected ? value.size() : after(value, caret); selected = false; }
    bool changed = false;
    if (input.backspace && (caret || selected)) {
        if (selected) { value.clear(); caret = 0; selected = false; }
        else { size_t start = before(value, caret); value.erase(start, caret - start); caret = start; }
        changed = true;
    }
    if (!input.isHeld(Key::Control) && !input.isHeld(Key::Alt)) for (char32_t cp : input.text) {
        if (cp < 32 || cp == 127 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) continue;
        std::string encoded;
        ui::appendUtf8(encoded, cp);
        size_t bytes = selected ? 0 : value.size();
        if (bytes > maxBytes || encoded.size() > maxBytes - bytes) continue;
        if (selected) { value.clear(); caret = 0; selected = false; }
        value.insert(caret, encoded);
        caret += encoded.size();
        changed = true;
    }
    return changed;
}

void UiControlState::end()
{
    auto exists = [&](const std::string& id) { return std::any_of(current.begin(), current.end(), [&](const auto& item) { return item.enabled && item.id == id; }); };
    if (!exists(focus)) { focus.clear(); selected = false; }
    if (!input.mouseDown || !exists(active)) active.clear();
    previous.swap(current);
    current.clear();
    input = {};
}

}
