#include "modding/ModManager.h"
#include "modding/HostState.h"
#include "modding/ModSlot.h"
#include "modding/Painters.h"

#include "mod/Events.h"
#include "platform/Keys.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace kestrel::modding {

namespace {

/**
 * A Range setting's value as the settings page stores and shows it, without
 * trailing zeros.
 */
std::string settingNumber(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

class Controls final : public mod::Controls {
public:
    Controls(UiControlState& state, ui::Context& context, mod::Canvas& canvas, std::string prefix)
        : state(state), context(context), canvas(canvas), prefix(std::move(prefix)) { }

    bool button(std::string_view id, std::string_view label, mod::Rect rect, bool enabled) override
    {
        if (!state.add(id, UiControlState::Kind::Button, rect, enabled)) return false;
        bool pressed = enabled && state.active == id && state.input.mouseDown;
        context.classicButton(prefix + std::string(id), label, rect, enabled);
        if (pressed) {
            context.nineSlice(rect.inset(1), "ui/button_borderless_lightpressed");
            float y = rect.y + std::floor((rect.h - 8) / 2) + 1;
            context.text(label, ui::TextStyle::Pixel, rect.x + std::floor((rect.w - context.measure(label, ui::TextStyle::Pixel)) / 2), y, { 255, 255, 255, 255 }, rect.w - 4);
        }
        outline(id, rect);
        bool clicked = enabled && state.button(id, rect);
        if (clicked) context.countClick();
        return clicked;
    }

    bool slider(std::string_view id, mod::Rect rect, float& value, float minimum, float maximum, float step, bool enabled) override
    {
        if (!state.add(id, UiControlState::Kind::Slider, rect, enabled)) return false;
        bool interactive = enabled;
        if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum || !std::isfinite(maximum - minimum)) return false;
        bool changed = interactive && state.slider(id, rect, value, minimum, maximum, step);
        if (enabled) context.interact(prefix + std::string(id), rect);
        context.fill(rect, { 35, 35, 35, 255 });
        float fraction = std::clamp((value - minimum) / (maximum - minimum), 0.0f, 1.0f);
        context.fill({ rect.x, rect.y + rect.h / 2 - 2, rect.w * fraction, 4 }, enabled ? mod::Color { 100, 180, 70, 255 } : mod::Color { 90, 90, 90, 255 });
        float knob = std::min(8.0f, rect.w);
        context.fill({ rect.x + fraction * (rect.w - knob), rect.y, knob, rect.h }, { 210, 210, 210, 255 });
        outline(id, rect);
        return changed;
    }

    bool textField(std::string_view id, mod::Rect rect, std::string& value, std::string_view placeholder, size_t maxBytes, bool enabled) override
    {
        if (!state.add(id, UiControlState::Kind::Text, rect, enabled)) return false;
        bool interactive = enabled;
        bool changed = interactive && state.textField(id, value, maxBytes);
        if (enabled) context.interact(prefix + std::string(id), rect);
        context.fill(rect, { 20, 20, 20, 255 });
        context.outline(rect, { 130, 130, 130, 255 });
        outline(id, rect);
        canvas.setClip(rect.inset(2));
        float x = rect.x + 4, y = rect.y + (rect.h - canvas.lineHeight(mod::TextStyle::Ui)) / 2;
        float caretX = canvas.measure(std::string_view(value).substr(0, std::min(state.caret, value.size())), mod::TextStyle::Ui);
        if (focused(id)) x -= std::max(0.0f, caretX - rect.w + 10);
        if (focused(id) && state.selected) context.fill(rect.inset(2), { 50, 80, 140, 255 });
        canvas.text(value.empty() ? placeholder : std::string_view(value), x, y, value.empty() ? mod::Color { 150, 150, 150, 255 } : mod::Color { 255, 255, 255, 255 }, mod::TextStyle::Ui, false);
        if (focused(id)) context.fill({ x + caretX, y, 1, canvas.lineHeight(mod::TextStyle::Ui) }, { 255, 255, 255, 255 });
        canvas.clearClip();
        return changed;
    }

    void focus(std::string_view id) override
    {
        if (id.size() > 256) return;
        state.focus = id;
        state.caret = SIZE_MAX;
        state.selected = false;
    }
    bool focused(std::string_view id) const override { return !id.empty() && state.focus == id; }
private:
    void outline(std::string_view id, mod::Rect rect)
    {
        if (focused(id)) context.outline(rect, { 160, 230, 100, 255 }, 1);
    }
    UiControlState& state;
    ui::Context& context;
    mod::Canvas& canvas;
    std::string prefix;
};
}

void ModManager::drawUi(ui::Context& context, float width, float height)
{
    auto screen = host->ui.top();
    if (!screen) return;
    context.setBlocked(false);
    context.setOrigin(0, 0);
    context.clearLayer();
    context.clearClip();
    context.fill({ 0, 0, width, height }, { 0, 0, 0, 180 });
    UiCanvas canvas(context, host->shaders, width, height);
    Controls controls(screen->controls, context, canvas, "mod-ui:" + std::to_string(screen->owner) + ":" + screen->id + ":");
    if (screen->id == SettingsScreenId) {
        drawSettings(screen->owner, canvas, controls, screen->controls.input, width, height);
    } else {
        mod::UiRenderEvent event(screen->id, canvas, controls);
        host->events.dispatchTo(screen->owner, event);
    }
    screen->controls.end();
    context.clearClip();
}

/**
 * The page built from a mod's Ui::addSettings: a row per setting with its
 * label on the left and a control bound to the mod's Config on the right,
 * scrolled with the wheel when it does not fit. A drag is saved once the
 * mouse is let go, everything else right away.
 */
void ModManager::drawSettings(size_t owner, mod::Canvas& canvas, mod::Controls& controls, const InputState& input, float width, float height)
{
    constexpr float RowHeight = 26.0f;
    constexpr float FieldWidth = 140.0f;
    constexpr size_t MaxTextBytes = 256;
    constexpr mod::Color White { 255, 255, 255, 255 };
    constexpr mod::Color Muted { 170, 170, 170, 255 };
    ModSlot* running = slot(owner);
    auto found = host->settings.find(owner);
    if (!running || found == host->settings.end()) {
        UiRequest close;
        close.action = UiRequest::Action::Close;
        close.id = std::string(SettingsScreenId);
        host->ui.process(owner, close);
        return;
    }
    std::vector<mod::SettingSpec> specs = found->second;
    ModConfig& config = running->configStore();
    const mod::ModInfo& info = running->info();
    float panelWidth = std::min(360.0f, width - 16.0f);
    float listHeight = static_cast<float>(specs.size()) * RowHeight;
    float panelHeight = std::min(listHeight + 70.0f, height - 16.0f);
    mod::Rect panel { std::floor((width - panelWidth) / 2.0f), std::floor((height - panelHeight) / 2.0f), panelWidth, panelHeight };
    canvas.fill(panel, { 35, 39, 45, 245 });
    canvas.textCentered((info.name.empty() ? info.id : info.name) + " settings", { panel.x, panel.y + 6.0f, panel.w, 16.0f }, White, mod::TextStyle::Ui);
    mod::Rect list { panel.x + 10.0f, panel.y + 28.0f, panel.w - 20.0f, panel.h - 64.0f };
    float& scroll = settingsScroll[owner];
    scroll = std::clamp(scroll - input.wheel * RowHeight, 0.0f, std::max(0.0f, listHeight - list.h));
    float textOffset = std::floor((RowHeight - canvas.lineHeight(mod::TextStyle::Ui)) / 2.0f);
    for (const mod::SettingSpec& spec : specs) {
        float y = list.y + static_cast<float>(&spec - specs.data()) * RowHeight - scroll;
        if (y < list.y - 0.5f || y + RowHeight > list.bottom() + 0.5f) {
            continue;
        }
        canvas.setClip({ list.x, y, list.w - FieldWidth - 6.0f, RowHeight });
        canvas.text(spec.label.empty() ? spec.key : spec.label, list.x, y + textOffset, White, mod::TextStyle::Ui, false);
        canvas.clearClip();
        mod::Rect field { list.right() - FieldWidth, y + 3.0f, FieldWidth, RowHeight - 6.0f };
        std::string id = "setting:" + spec.key;
        std::string value = config.find(spec.key).value_or(spec.defaultValue);
        std::optional<std::string> changed;
        switch (spec.kind) {
        case mod::SettingSpec::Kind::Toggle: {
            bool on = value == "true" || value == "1";
            if (controls.button(id, on ? "On" : "Off", field)) {
                changed = on ? "false" : "true";
            }
            break;
        }
        case mod::SettingSpec::Kind::Range: {
            float number = static_cast<float>(spec.minimum);
            char* end = nullptr;
            double parsed = std::strtod(value.c_str(), &end);
            if (end != value.c_str() && std::isfinite(parsed)) {
                number = static_cast<float>(std::clamp(parsed, spec.minimum, spec.maximum));
            }
            mod::Rect bar { field.x, field.y + 3.0f, field.w - 44.0f, field.h - 6.0f };
            if (controls.slider(id, bar, number, static_cast<float>(spec.minimum), static_cast<float>(spec.maximum), static_cast<float>(spec.step))) {
                changed = settingNumber(number);
            }
            canvas.text(settingNumber(number), bar.right() + 6.0f, y + textOffset, Muted, mod::TextStyle::Ui, false);
            break;
        }
        case mod::SettingSpec::Kind::Choice: {
            auto current = std::find(spec.choices.begin(), spec.choices.end(), value);
            if (controls.button(id, current == spec.choices.end() ? spec.choices.front() : *current, field)) {
                bool wraps = current == spec.choices.end() || current + 1 == spec.choices.end();
                changed = wraps ? spec.choices.front() : *(current + 1);
            }
            break;
        }
        case mod::SettingSpec::Kind::Key: {
            std::string tag = std::to_string(owner) + ":" + spec.key;
            bool listening = settingsListening == tag;
            bool bound = false;
            if (listening && input.pressedKey != Key::None) {
                changed = keyName(input.pressedKey);
                settingsListening.clear();
                listening = false;
                bound = true;
            }
            if (controls.button(id, listening ? "Press a key" : (value.empty() ? "None" : value), field) && !listening && !bound) {
                settingsListening = tag;
            }
            break;
        }
        case mod::SettingSpec::Kind::Text: {
            std::string text = value;
            if (controls.textField(id, field, text, spec.defaultValue, MaxTextBytes)) {
                changed = std::move(text);
            }
            break;
        }
        }
        if (changed) {
            config.put(spec.key, std::move(*changed));
            settingsUnsaved = true;
        }
    }
    if (settingsUnsaved && !input.mouseDown) {
        commitSettings(owner);
    }
    if (controls.button("done", "Done", { panel.x + std::floor((panel.w - 100.0f) / 2.0f), panel.bottom() - 30.0f, 100.0f, 22.0f })) {
        UiRequest close;
        close.action = UiRequest::Action::Close;
        close.id = std::string(SettingsScreenId);
        host->ui.process(owner, close);
        settingsListening.clear();
    }
}

/**
 * Saves what the settings page changed and tells the mod, which reads its
 * Config again on ConfigReloadEvent.
 */
void ModManager::commitSettings(size_t owner)
{
    settingsUnsaved = false;
    ModSlot* running = slot(owner);
    if (!running) {
        return;
    }
    running->configStore().save();
    mod::ConfigReloadEvent event;
    event.modId = running->info().id;
    host->events.dispatchTo(owner, event);
}

}
