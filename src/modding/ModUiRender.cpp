#include "modding/ModManager.h"
#include "modding/HostState.h"
#include "modding/Painters.h"

#include <algorithm>
#include <cmath>

namespace kestrel::modding {

namespace {
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
    mod::UiRenderEvent event(screen->id, canvas, controls);
    host->events.dispatchTo(screen->owner, event);
    screen->controls.end();
    context.clearClip();
}

}
