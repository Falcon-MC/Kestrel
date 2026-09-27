#include "menu/FormScreen.h"

#include "Core/Json/Json.h"
#include "platform/Input.h"
#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Utf8.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>

namespace kestrel::menu {

namespace {

using ui::Color;
using ui::Rect;
using ui::TextStyle;

// Sizes straight from server_form.json and the common_dialogs, common_buttons
// and settings_common controls it builds on, in JSON UI pixels.
constexpr float DialogWidth = 225.0f;
constexpr float DialogHeight = 200.0f;
constexpr float TitleTop = 9.0f;
constexpr float TitleInset = 15.0f;
constexpr float CloseSize = 21.0f;
constexpr float CloseImage = 15.0f;
constexpr float IndentSide = 8.0f;
constexpr float IndentTop = 23.0f;
constexpr float IndentBottom = 8.0f;
constexpr float PanePadding = 2.0f;
constexpr float ScrollBarWidth = 5.0f;
constexpr float ScrollBarGap = 2.0f;
constexpr float ContentInset = 4.0f;
constexpr float ButtonHeight = 32.0f;
constexpr float IconColumn = 34.0f;
constexpr float IconSize = 32.0f;
constexpr float LineHeight = 10.0f;
// font_size "large"; the game keeps the scale in code, this is what it looks like next to "normal".
constexpr float HeaderScale = 1.5f;
constexpr float DividerHeight = 9.0f;
constexpr float OptionSpacing = 4.0f;
constexpr float LabelGap = 2.0f;
constexpr float ToggleWidth = 30.0f;
constexpr float ToggleHeight = 16.0f;
constexpr float ToggleLabelX = 34.0f;
constexpr float ToggleLabelY = 3.0f;
constexpr float SliderHeight = 16.0f;
constexpr float SliderBoxWidth = 10.0f;
constexpr float BoxHeight = 30.0f;
constexpr float TooltipReserve = 28.0f;
constexpr float BulbWidth = 7.0f;
constexpr float BulbHeight = 11.0f;
constexpr float BulbRight = 14.0f;
constexpr float DropdownHeight = 60.0f;
constexpr float RadioHeight = 17.0f;
constexpr float ScrollStep = 24.0f;
constexpr float MinHandle = 8.0f;
constexpr size_t MaxInputLength = 100;
constexpr size_t MaxButtonLines = 2;
constexpr double TransitionSeconds = 0.4;

constexpr Color White { 255, 255, 255, 255 };
constexpr Color TitleInk { 76, 76, 76, 255 };
constexpr Color ButtonInk { 76, 76, 76, 255 };
constexpr Color CheckedInk { 31, 31, 31, 255 };
constexpr Color PlaceholderInk { 217, 217, 217, 255 };
constexpr Color BorderDark { 19, 19, 19, 255 };
constexpr Color ControlTint { 255, 255, 255, 204 };

double nowSeconds()
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

// Labels localize what they show, so a string that is a language key reads as its text.
std::string formText(const json::Value* value)
{
    if (!value) {
        return {};
    }
    if (value->isString()) {
        const std::string& text = value->mString;
        return ui::Localization::shared().has(text) ? ui::tr(text, text) : text;
    }
    return value->isObject() ? ui::rawText(*value) : std::string();
}

std::optional<FormImage> formImage(const json::Value* image)
{
    if (!image || !image->isObject()) {
        return std::nullopt;
    }
    std::string type = image->get("type") ? image->get("type")->string() : std::string();
    std::string data = image->get("data") ? image->get("data")->string() : std::string();
    if (data.empty() || (type != "path" && type != "url")) {
        return std::nullopt;
    }
    return FormImage { type == "url", std::move(data) };
}

std::string shortNumber(double value)
{
    char buffer[32];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

// The game writes floats through jsoncpp, which always keeps a decimal point.
std::string jsonNumber(double value)
{
    std::string text = shortNumber(value);
    if (text.find_first_of(".eEn") == std::string::npos) {
        text += ".0";
    }
    return text;
}

size_t codepoints(std::string_view text)
{
    size_t count = 0;
    for (char c : text) {
        count += (static_cast<unsigned char>(c) & 0xc0) != 0x80;
    }
    return count;
}

float wrappedHeight(ui::Context& ui, std::string_view text, float width, float scale = 1.0f)
{
    std::vector<std::string_view> lines;
    return static_cast<float>(ui.wrap(text, TextStyle::Pixel, width / scale, lines)) * LineHeight * scale;
}

int sliderSteps(float min, float max, float step)
{
    return step > 0.0f && max > min ? static_cast<int>(std::round((max - min) / step)) : 0;
}

float snapSlider(float value, float min, float max, float step)
{
    int steps = sliderSteps(min, max, step);
    if (steps <= 0) {
        return min;
    }
    int index = std::clamp(static_cast<int>(std::round((value - min) / step)), 0, steps);
    return std::min(max, min + static_cast<float>(index) * step);
}

void thinBorder(ui::Context& ui, const Rect& rect, bool hovered)
{
    ui.nineSlice(rect, "ui/focus_border_white", hovered ? White : BorderDark);
}

}

void FormScreen::open(uint32_t id, const std::string& json)
{
    std::unique_ptr<json::Value> root = json::parse(json);
    std::string type = root && root->isObject() && root->get("type") ? root->get("type")->string() : std::string();
    if (type != "form" && type != "modal" && type != "custom_form") {
        answers.push_back({ id, std::nullopt, false });
        return;
    }

    Form form;
    form.id = id;
    form.openedAt = nowSeconds();
    form.title = formText(root->get("title"));
    if (type == "modal") {
        form.kind = Kind::Modal;
        form.content = formText(root->get("content"));
        for (const char* key : { "button1", "button2" }) {
            Element& button = form.elements.emplace_back();
            button.type = ElementType::Button;
            button.text = formText(root->get(key));
        }
    } else if (type == "form") {
        form.kind = Kind::Simple;
        form.content = formText(root->get("content"));
        // Older servers send "buttons", newer ones mix labels, headers and dividers in "elements".
        const json::Value* list = root->get("elements");
        if (!list || !list->isArray()) {
            list = root->get("buttons");
        }
        if (list && list->isArray()) {
            for (const std::unique_ptr<json::Value>& item : list->mArray) {
                if (!item->isObject()) {
                    continue;
                }
                std::string kind = item->get("type") ? item->get("type")->string() : std::string("button");
                Element element;
                element.text = formText(item->get("text"));
                if (kind == "label") {
                    element.type = ElementType::Label;
                } else if (kind == "header") {
                    element.type = ElementType::Header;
                } else if (kind == "divider") {
                    element.type = ElementType::Divider;
                } else {
                    element.type = ElementType::Button;
                    element.image = formImage(item->get("image"));
                }
                form.elements.push_back(std::move(element));
            }
        }
    } else {
        form.kind = Kind::Custom;
        form.submit = root->get("submit") ? formText(root->get("submit")) : ui::tr("gui.submit", "Submit");
        const json::Value* list = root->get("content");
        if (list && list->isArray()) {
            for (const std::unique_ptr<json::Value>& item : list->mArray) {
                if (!item->isObject()) {
                    continue;
                }
                std::string kind = item->get("type") ? item->get("type")->string() : std::string();
                const json::Value* fallback = item->get("default");
                Element element;
                element.text = formText(item->get("text"));
                element.tooltip = formText(item->get("tooltip"));
                auto readOptions = [&](const char* key) {
                    if (const json::Value* options = item->get(key); options && options->isArray()) {
                        for (const std::unique_ptr<json::Value>& option : options->mArray) {
                            element.options.push_back(formText(option.get()));
                        }
                    }
                    int last = std::max(0, static_cast<int>(element.options.size()) - 1);
                    element.selected = std::clamp(fallback ? fallback->integer(0) : 0, 0, last);
                };
                if (kind == "label") {
                    element.type = ElementType::Label;
                } else if (kind == "header") {
                    element.type = ElementType::Header;
                } else if (kind == "divider") {
                    element.type = ElementType::Divider;
                } else if (kind == "toggle") {
                    element.type = ElementType::Toggle;
                    element.on = fallback && fallback->boolean(false);
                } else if (kind == "slider") {
                    element.type = ElementType::Slider;
                    element.min = static_cast<float>(item->get("min") ? item->get("min")->number(0.0) : 0.0);
                    element.max = static_cast<float>(item->get("max") ? item->get("max")->number(0.0) : 0.0);
                    element.step = static_cast<float>(item->get("step") ? item->get("step")->number(1.0) : 1.0);
                    element.value = snapSlider(static_cast<float>(fallback ? fallback->number(element.min) : element.min), element.min, element.max, element.step);
                } else if (kind == "step_slider") {
                    element.type = ElementType::StepSlider;
                    readOptions("steps");
                } else if (kind == "dropdown") {
                    element.type = ElementType::Dropdown;
                    readOptions("options");
                } else if (kind == "input") {
                    element.type = ElementType::Input;
                    element.placeholder = formText(item->get("placeholder"));
                    element.input = fallback ? fallback->string() : std::string();
                } else {
                    continue;
                }
                form.elements.push_back(std::move(element));
            }
        }
    }
    forms.push_back(std::move(form));
    dragging = -1;
    scrollDragging = false;
    dropdownScrollDragging = false;
}

void FormScreen::reject(uint32_t id)
{
    answers.push_back({ id, std::nullopt, true });
}

void FormScreen::closeAll()
{
    forms.clear();
    dragging = -1;
    scrollDragging = false;
    dropdownScrollDragging = false;
}

void FormScreen::reset()
{
    closeAll();
    answers.clear();
}

std::vector<FormAnswer> FormScreen::takeAnswers()
{
    std::vector<FormAnswer> taken = std::move(answers);
    answers.clear();
    return taken;
}

void FormScreen::submit(Form& form, std::optional<std::string> data)
{
    answers.push_back({ form.id, std::move(data), false });
    auto found = std::find_if(forms.begin(), forms.end(), [&](const Form& entry) { return &entry == &form; });
    if (found != forms.end()) {
        forms.erase(found);
    }
    dragging = -1;
    scrollDragging = false;
    dropdownScrollDragging = false;
}

std::string FormScreen::response(const Form& form) const
{
    std::string json = "[";
    for (size_t i = 0; i < form.elements.size(); ++i) {
        const Element& element = form.elements[i];
        if (i > 0) {
            json += ',';
        }
        switch (element.type) {
        case ElementType::Toggle:
            json += element.on ? "true" : "false";
            break;
        case ElementType::Slider:
            json += jsonNumber(element.value);
            break;
        case ElementType::StepSlider:
        case ElementType::Dropdown:
            json += std::to_string(element.selected);
            break;
        case ElementType::Input:
            json += '"' + json::escape(element.input) + '"';
            break;
        default:
            json += "null";
            break;
        }
    }
    return json + "]\n";
}

float FormScreen::elementHeight(ui::Context& ui, const Element& element, float width, bool spaced) const
{
    float labelWidth = element.tooltip.empty() ? width : width - TooltipReserve;
    switch (element.type) {
    case ElementType::Label:
        return wrappedHeight(ui, element.text, width) + (spaced ? 11.0f : 9.0f);
    case ElementType::Header:
        return wrappedHeight(ui, element.text, width, HeaderScale) + (spaced ? 9.0f : 5.0f);
    case ElementType::Divider:
        return DividerHeight;
    case ElementType::Button:
        return ButtonHeight;
    case ElementType::Toggle:
        // The row never grows past the toggle, long labels spill into the next one like they do in game.
        return ToggleHeight + OptionSpacing;
    case ElementType::Slider:
    case ElementType::StepSlider:
        return wrappedHeight(ui, element.text, labelWidth) + LabelGap + SliderHeight + OptionSpacing;
    case ElementType::Dropdown:
    case ElementType::Input:
        return wrappedHeight(ui, element.text, labelWidth) + LabelGap + BoxHeight + OptionSpacing;
    }
    return 0.0f;
}

bool FormScreen::formButton(ui::Context& ui, size_t index, const Element& element, const Rect& rect)
{
    Rect face = rect;
    std::string sprite = element.image && imageSprite ? imageSprite(*element.image) : std::string();
    if (!sprite.empty()) {
        if (sprite == FormImageLoading) {
            ui.spriteRegion({ rect.x, rect.y + 30.0f, 30.0f, 4.0f }, "ui/loading_bar", { 0.0f, 0.0f, 64.0f, 8.0f }, { 179, 179, 179, 255 });
        } else {
            ui.sprite({ rect.x + 1.0f, rect.y, IconSize, IconSize }, sprite);
        }
        face.x += IconColumn;
        face.w -= IconColumn;
    }

    ui::Interaction hit = ui.interact("form:button:" + std::to_string(index), face);
    const char* texture = hit.pressed ? "ui/button_borderless_lightpressed" : hit.hovered ? "ui/button_borderless_lighthover" : "ui/button_borderless_light";
    ui.nineSlice(face.inset(1.0f), texture);
    thinBorder(ui, face, hit.hovered);

    Rect content = face.inset(3.0f);
    std::vector<std::string_view> lines;
    ui.wrap(element.text, TextStyle::Pixel, content.w, lines);
    lines.resize(std::min(lines.size(), MaxButtonLines));
    float y = std::floor(content.y + (content.h - static_cast<float>(lines.size()) * LineHeight) * 0.5f) + 1.0f + (hit.pressed ? 1.0f : 0.0f);
    Color ink = hit.hovered ? White : ButtonInk;
    for (std::string_view line : lines) {
        float lineWidth = std::min(ui.measure(line, TextStyle::Pixel), content.w);
        ui.text(line, TextStyle::Pixel, std::floor(content.x + (content.w - lineWidth) * 0.5f), y, ink, content.w);
        y += LineHeight;
    }
    return hit.clicked;
}

void FormScreen::drawTooltipBulb(ui::Context& ui, const Element& element, float right, float top)
{
    if (element.tooltip.empty()) {
        return;
    }
    Rect bulb { right - BulbRight - BulbWidth, top, BulbWidth, BulbHeight };
    ui.sprite(bulb, "ui/infobulb");
    if (ui.hovered(bulb)) {
        tooltip = Tooltip { element.tooltip, { right - 0.0f, top, 0.0f, 0.0f } };
    }
}

void FormScreen::drawElement(ui::Context& ui, Form& form, size_t index, const Rect& rect, bool spaced)
{
    Element& element = form.elements[index];
    const InputState& input = ui.input();
    std::string id = "form:" + std::to_string(index);
    float labelWidth = element.tooltip.empty() ? rect.w : rect.w - TooltipReserve;

    switch (element.type) {
    case ElementType::Label:
        ui.pixelParagraph(element.text, rect.x, rect.y + (spaced ? 6.0f : 4.0f), rect.w, 1.0f, White);
        return;
    case ElementType::Header:
        ui.pixelParagraph(element.text, rect.x, rect.y + (spaced ? 8.0f : 4.0f), rect.w, HeaderScale, White);
        return;
    case ElementType::Divider:
        ui.sprite({ rect.x, rect.y + 4.0f, rect.w, 1.0f }, "ui/list_item_divider_line_light");
        return;
    case ElementType::Button:
        if (formButton(ui, index, element, rect)) {
            if (form.kind == Kind::Modal) {
                submit(form, index == 0 ? "true\n" : "false\n");
            } else {
                int button = 0;
                for (size_t i = 0; i < index; ++i) {
                    button += form.elements[i].type == ElementType::Button;
                }
                submit(form, std::to_string(button) + "\n");
            }
        }
        return;
    case ElementType::Toggle: {
        float rowWidth = element.tooltip.empty() ? rect.w : rect.w - TooltipReserve;
        ui::Interaction hit = ui.interact(id, { rect.x, rect.y, rowWidth, ToggleHeight });
        if (hit.clicked) {
            element.on = !element.on;
        }
        std::string texture = element.on ? "ui/toggle_on" : "ui/toggle_off";
        ui.sprite({ rect.x, rect.y, ToggleWidth, ToggleHeight }, hit.hovered ? texture + "_hover" : texture);
        ui.pixelParagraph(element.text, rect.x + ToggleLabelX, rect.y + ToggleLabelY, rowWidth - ToggleLabelX, 1.0f, White);
        drawTooltipBulb(ui, element, rect.right(), rect.y);
        return;
    }
    default:
        break;
    }

    std::string label = element.text;
    if (element.type == ElementType::Slider) {
        label += ": " + shortNumber(element.value);
    } else if (element.type == ElementType::StepSlider && !element.options.empty()) {
        label += ": " + element.options[static_cast<size_t>(element.selected)];
    }
    float labelHeight = ui.pixelParagraph(label, rect.x, rect.y, labelWidth, 1.0f, White);
    drawTooltipBulb(ui, element, rect.right(), rect.y);
    float top = rect.y + labelHeight + LabelGap;

    if (element.type == ElementType::Slider || element.type == ElementType::StepSlider) {
        Rect area { rect.x, top, rect.w - 2.0f, SliderHeight };
        Rect bar { area.x + 5.0f, area.y + 3.0f, area.w - 10.0f, 10.0f };
        Rect inner = bar.inset(1.0f);
        ui::Interaction hit = ui.interact(id, area);
        if (hit.hovered && input.mousePressed) {
            dragging = static_cast<int>(index);
        }
        bool active = dragging == static_cast<int>(index);
        if (active) {
            float fraction = bar.w > 0.0f ? std::clamp((ui.mouseX() - bar.x) / bar.w, 0.0f, 1.0f) : 0.0f;
            if (element.type == ElementType::Slider) {
                element.value = snapSlider(element.min + fraction * (element.max - element.min), element.min, element.max, element.step);
            } else if (!element.options.empty()) {
                element.selected = static_cast<int>(std::round(fraction * static_cast<float>(element.options.size() - 1)));
            }
            if (!input.mouseDown) {
                dragging = -1;
            }
        }
        float fraction = 0.0f;
        if (element.type == ElementType::Slider) {
            fraction = element.max > element.min ? (element.value - element.min) / (element.max - element.min) : 0.0f;
        } else if (element.options.size() > 1) {
            fraction = static_cast<float>(element.selected) / static_cast<float>(element.options.size() - 1);
        }
        bool lit = hit.hovered || active;
        std::string suffix = lit ? "_hover" : "";
        ui.nineSlice(bar, "ui/slider_border", lit ? White : Color { 0, 0, 0, 255 });
        ui.nineSlice(inner, "ui/slider_background" + suffix);
        Rect viewport = currentClip;
        ui.setClip(intersect(viewport, { inner.x, inner.y, inner.w * fraction, inner.h }));
        ui.nineSlice(inner, "ui/slider_progress" + suffix);
        ui.setClip(viewport);
        if (element.type == ElementType::StepSlider && element.options.size() > 1) {
            float count = static_cast<float>(element.options.size() - 1);
            for (size_t i = 0; i < element.options.size(); ++i) {
                float x = std::round(inner.x + inner.w * static_cast<float>(i) / count - 1.0f);
                bool filled = static_cast<int>(i) <= element.selected;
                ui.nineSlice({ x, bar.y + 2.0f, 2.0f, 6.0f }, std::string(filled ? "ui/slider_step_progress" : "ui/slider_step_background") + suffix);
            }
        }
        float boxX = std::round(bar.x + bar.w * fraction - SliderBoxWidth * 0.5f);
        ui.nineSlice({ boxX, area.y, SliderBoxWidth, SliderHeight }, lit ? "ui/slider_button_hover" : "ui/slider_button_default");
        return;
    }

    Rect box { rect.x, top, rect.w, BoxHeight };
    if (element.type == ElementType::Dropdown) {
        bool open = form.dropdown == static_cast<int>(index);
        ui::Interaction hit = ui.interact(id, box);
        if (hit.clicked) {
            form.dropdown = static_cast<int>(index);
            form.dropdownScroll = 0.0f;
        }
        const char* face = open ? (hit.hovered ? "ui/button_borderless_lightpressed" : "ui/button_borderless_lightpressednohover")
                                : (hit.hovered ? "ui/button_borderless_lighthover" : "ui/button_borderless_light");
        ui.nineSlice(box.inset(1.0f), face);
        thinBorder(ui, box, hit.hovered);
        Rect content { box.x + 3.0f, box.y + 7.0f, box.w - 6.0f, 16.0f };
        Color ink = hit.hovered ? White : open ? CheckedInk : ButtonInk;
        if (!element.options.empty()) {
            ui.text(element.options[static_cast<size_t>(element.selected)], TextStyle::Pixel, content.x, content.y + 3.0f, ink, content.w - 8.0f);
        }
        ui.sprite({ content.right() - 8.0f, content.y + 4.0f, 8.0f, 8.0f }, hit.hovered && !open ? "ui/chevron_white_down" : "ui/dropdown_chevron");
        if (open) {
            dropdownToggle = box;
        }
        return;
    }

    bool focused = form.focused == static_cast<int>(index);
    bool hovered = ui.hovered(box);
    ui.interact(id, box);
    if (input.mousePressed) {
        if (hovered) {
            form.focused = static_cast<int>(index);
        } else if (focused) {
            form.focused = -1;
        }
        focused = form.focused == static_cast<int>(index);
    }
    ui.nineSlice(box, hovered || focused ? "ui/edit_box_indent_hover" : "ui/edit_box_indent");
    Rect field { box.x + 3.0f, box.y + 2.0f, box.w - 6.0f, box.h - 4.0f };
    float textY = field.y + std::floor((field.h - LineHeight) * 0.5f) + 1.0f;
    Rect viewport = currentClip;
    ui.setClip(intersect(viewport, field));
    if (element.input.empty() && !focused) {
        ui.text(element.placeholder, TextStyle::Pixel, field.x, textY, PlaceholderInk);
    } else {
        std::string shown = element.input;
        if (focused && static_cast<int>(nowSeconds() * 2.0) % 2 == 0) {
            shown += '_';
        }
        float textWidth = ui.measure(shown, TextStyle::Pixel);
        float x = textWidth > field.w ? field.right() - textWidth : field.x;
        ui.text(shown, TextStyle::Pixel, x, textY, White);
    }
    ui.setClip(viewport);
}

void FormScreen::editText(ui::Context& ui, Element& element)
{
    const InputState& input = ui.input();
    std::u32string typed = input.text;
    if (input.isHeld(Key::Control) && input.pressedKey == Key::V) {
        std::string pasted = platform::pasteText();
        size_t i = 0;
        while (i < pasted.size()) {
            typed.push_back(ui::nextCodepoint(pasted, i));
        }
    }
    if (input.backspace && !element.input.empty()) {
        ui::popUtf8(element.input);
    }
    size_t length = codepoints(element.input);
    for (char32_t cp : typed) {
        if (cp < 32 || cp == 127 || length >= MaxInputLength) {
            continue;
        }
        ui::appendUtf8(element.input, cp);
        ++length;
    }
}

float FormScreen::scrollBar(ui::Context& ui, const Rect& track, float offset, float view, float content, bool& held)
{
    const InputState& input = ui.input();
    float range = content - view;
    if (range <= 0.0f) {
        held = false;
        return 0.0f;
    }
    float handleHeight = std::max(MinHandle, std::round(track.h * view / content));
    float travel = track.h - handleHeight;
    Rect handle { track.x, track.y + travel * offset / range, track.w, handleHeight };
    if (input.mousePressed && ui.hovered(track)) {
        held = true;
        dragGrab = handle.contains(ui.mouseX(), ui.mouseY()) ? ui.mouseY() - handle.y : handleHeight * 0.5f;
    }
    if (held) {
        if (!input.mouseDown) {
            held = false;
        } else if (travel > 0.0f) {
            offset = std::clamp((ui.mouseY() - dragGrab - track.y) / travel, 0.0f, 1.0f) * range;
            handle.y = track.y + travel * offset / range;
        }
    }
    ui.nineSlice({ track.x + 1.0f, track.y, 3.0f, track.h }, "ui/ScrollRail");
    ui.nineSlice(handle, "ui/ScrollHandle");
    return offset;
}

void FormScreen::drawDropdown(ui::Context& ui, Form& form, const Rect& toggle, const Rect& bounds)
{
    Element& element = form.elements[static_cast<size_t>(form.dropdown)];
    const InputState& input = ui.input();
    float listHeight = static_cast<float>(element.options.size()) * RadioHeight + 4.0f;
    float height = std::min(DropdownHeight, listHeight + 4.0f);
    Rect area { toggle.x, toggle.bottom(), toggle.w, height };
    if (area.bottom() > bounds.bottom() && toggle.y - height >= bounds.y) {
        area.y = toggle.y - height;
    }
    if (input.mousePressed && !ui.hovered(area)) {
        form.dropdown = -1;
        return;
    }

    ui.nineSlice(area, "ui/dropdown_background");
    Rect pane = area.inset(2.0f);
    bool scrolls = listHeight > pane.h;
    Rect list = pane;
    if (scrolls) {
        list.w -= ScrollBarWidth + ScrollBarGap;
        if (ui.hovered(pane) && input.wheel != 0.0f) {
            form.dropdownScroll -= input.wheel * ScrollStep;
        }
        form.dropdownScroll = std::clamp(form.dropdownScroll, 0.0f, listHeight - pane.h);
        form.dropdownScroll = scrollBar(ui, { pane.right() - ScrollBarWidth, pane.y, ScrollBarWidth, pane.h }, form.dropdownScroll, pane.h, listHeight, dropdownScrollDragging);
    }

    Rect viewport = currentClip;
    ui.setClip(intersect(viewport, list));
    currentClip = intersect(viewport, list);
    for (size_t i = 0; i < element.options.size(); ++i) {
        Rect row { list.x + 2.0f + 3.0f, list.y + 1.0f + static_cast<float>(i) * RadioHeight - form.dropdownScroll, list.w - 4.0f - 3.0f, 16.0f };
        if (row.bottom() < list.y || row.y > list.bottom()) {
            continue;
        }
        ui::Interaction hit = ui.interact("form:option:" + std::to_string(i), { row.x - 2.0f, row.y - 1.0f, row.w + 4.0f, RadioHeight });
        bool selected = static_cast<int>(i) == element.selected;
        if (hit.hovered || selected) {
            ui.sprite({ row.x - 2.0f, row.y - 1.0f, row.w + 4.0f, row.h + 2.0f }, hit.hovered ? "ui/dropDownHoverBG" : "ui/dropDownSelectBG");
        }
        std::string radio = selected ? "ui/radio_on" : "ui/radio_off";
        ui.sprite({ row.x, row.y + 3.0f, 10.0f, 10.0f }, hit.hovered ? radio + "_hover" : radio);
        ui.text(element.options[i], TextStyle::Pixel, row.x + 16.0f, row.y + 4.0f, White, row.w - 16.0f);
        if (hit.clicked) {
            element.selected = static_cast<int>(i);
            form.dropdown = -1;
        }
    }
    currentClip = viewport;
    ui.setClip(viewport);
}

void FormScreen::drawTooltip(ui::Context& ui, const Rect& area)
{
    if (!tooltip) {
        return;
    }
    const float width = tooltipWidth;
    float textWidth = width - 10.0f - 12.0f - 3.0f;
    float textHeight = wrappedHeight(ui, tooltip->text, textWidth);
    float icon = std::clamp(textHeight, 8.0f, 12.0f);
    textWidth = width - 10.0f - icon - 3.0f;
    textHeight = wrappedHeight(ui, tooltip->text, textWidth);
    float height = std::max(textHeight, icon) + 8.0f;
    Rect popup { tooltip->anchor.x - width - 2.0f, tooltip->anchor.y - 10.0f - height, width, height };
    bool below = popup.y < area.y;
    if (below) {
        popup.y = tooltip->anchor.y + 20.0f;
    }
    ui.nineSlice(popup, "ui/tooltip_default_background");
    float chevronX = std::round(popup.right() - 19.0f - 19.0f * 0.3f);
    if (below) {
        ui.sprite({ chevronX, popup.y - 10.0f, 19.0f, 13.0f }, "ui/tooltip_inverted_chevron");
    } else {
        ui.sprite({ chevronX, popup.bottom() - 3.0f, 19.0f, 13.0f }, "ui/tooltip_default_chevron");
    }
    float bulbWidth = icon * BulbWidth / BulbHeight;
    ui.sprite({ popup.x + 5.0f + (icon - bulbWidth) * 0.5f, popup.y + 4.0f, bulbWidth, icon }, "ui/infobulb");
    ui.pixelParagraph(tooltip->text, popup.x + 5.0f + icon + 3.0f, popup.y + 4.0f, textWidth, 1.0f, White);
}

void FormScreen::draw(ui::Context& ui, float width, float height)
{
    if (forms.empty()) {
        return;
    }
    Form& form = forms.back();
    const InputState& input = ui.input();

    if (input.escape) {
        if (form.dropdown >= 0) {
            form.dropdown = -1;
        } else {
            submit(form, std::nullopt);
            return;
        }
    }
    if (form.focused >= 0) {
        Element& element = form.elements[static_cast<size_t>(form.focused)];
        if (input.enter) {
            form.focused = -1;
        } else {
            editText(ui, element);
        }
    }

    double t = std::clamp((nowSeconds() - form.openedAt) / TransitionSeconds, 0.0, 1.0);
    float shown = 1.0f - static_cast<float>(std::pow(1.0 - t, 3.0));
    ui.setLayer((1.0f - shown) * width * 0.25f, 0.0f, shown);

    Rect dialog { std::floor((width - DialogWidth) * 0.5f), std::floor((height - DialogHeight) * 0.5f), DialogWidth, DialogHeight };
    ui.nineSlice(dialog.inset(4.0f), "ui/control", ControlTint);
    ui.nineSlice(dialog, "ui/dialog_background_hollow_3");

    float titleMax = dialog.w - TitleInset;
    float titleWidth = std::min(ui.measure(form.title, TextStyle::Pixel), titleMax);
    ui.text(form.title, TextStyle::Pixel, std::floor(dialog.x + (dialog.w - titleWidth) * 0.5f), dialog.y + TitleTop, TitleInk, titleMax);

    bool dropdownOpen = form.dropdown >= 0;
    ui.setBlocked(dropdownOpen);
    Rect close { dialog.right() - CloseSize, dialog.y, CloseSize, CloseSize };
    ui::Interaction closeHit = ui.interact("form:close", close);
    const char* closeTexture = closeHit.pressed ? "ui/close_button_pressed" : closeHit.hovered ? "ui/close_button_hover" : "ui/close_button_default";
    ui.sprite({ close.x + (CloseSize - CloseImage) * 0.5f, close.y + (CloseSize - CloseImage) * 0.5f, CloseImage, CloseImage }, closeTexture);
    if (closeHit.clicked) {
        ui.setBlocked(false);
        ui.clearLayer();
        submit(form, std::nullopt);
        return;
    }

    Rect indent { dialog.x + IndentSide, dialog.y + IndentTop, dialog.w - IndentSide * 2.0f, dialog.h - IndentTop - IndentBottom };
    Rect pane { indent.x + PanePadding, indent.y, indent.w - PanePadding * 2.0f, indent.h - PanePadding };
    Rect viewport { pane.x, pane.y, pane.w - ScrollBarGap - ScrollBarWidth, pane.h };
    float contentWidth = viewport.w - ContentInset;

    bool custom = form.kind == Kind::Custom;
    float elementX = custom ? 2.0f : 4.0f;
    float elementWidth = custom ? contentWidth : contentWidth - 4.0f;
    float top = 0.0f;
    if (!custom) {
        top = wrappedHeight(ui, form.content, contentWidth) + 4.0f;
    }
    std::vector<float> heights;
    heights.reserve(form.elements.size());
    float contentHeight = top;
    for (const Element& element : form.elements) {
        heights.push_back(elementHeight(ui, element, elementWidth, !custom));
        contentHeight += heights.back();
    }
    if (custom) {
        contentHeight += ButtonHeight;
    }

    if (!dropdownOpen && ui.hovered(pane) && input.wheel != 0.0f) {
        form.scroll -= input.wheel * ScrollStep;
    }
    form.scroll = std::clamp(form.scroll, 0.0f, std::max(0.0f, contentHeight - viewport.h));
    form.scroll = scrollBar(ui, { pane.right() - ScrollBarWidth, pane.y + 2.0f, ScrollBarWidth, pane.h - 4.0f }, form.scroll, viewport.h, contentHeight, scrollDragging);

    uint32_t id = form.id;
    size_t depth = forms.size();
    tooltip.reset();
    tooltipWidth = elementWidth - 4.0f;
    currentClip = viewport;
    ui.setClip(viewport);
    float y = viewport.y - form.scroll;
    if (!custom) {
        ui.pixelParagraph(form.content, viewport.x + 2.0f, y + 2.0f, contentWidth, 1.0f, White);
    }
    y += top;
    for (size_t i = 0; i < form.elements.size(); ++i) {
        Rect rect { viewport.x + elementX, y, elementWidth, heights[i] };
        if (rect.bottom() >= viewport.y && rect.y <= viewport.bottom()) {
            drawElement(ui, form, i, rect, !custom);
            // A button press answers the form and takes it off the stack.
            if (forms.size() != depth || forms.back().id != id) {
                ui.clearClip();
                ui.setBlocked(false);
                ui.clearLayer();
                return;
            }
        }
        y += heights[i];
    }
    if (custom) {
        Element submitButton;
        submitButton.text = form.submit;
        if (formButton(ui, form.elements.size(), submitButton, { viewport.x + elementX, y, elementWidth, ButtonHeight })) {
            ui.clearClip();
            ui.setBlocked(false);
            ui.clearLayer();
            submit(form, response(form));
            return;
        }
    }
    ui.setBlocked(false);

    if (form.dropdown >= 0 && static_cast<size_t>(form.dropdown) < form.elements.size()) {
        currentClip = indent;
        ui.setClip(indent);
        drawDropdown(ui, form, dropdownToggle, indent);
    }
    ui.clearClip();
    if (form.dropdown < 0) {
        drawTooltip(ui, indent);
    }
    ui.clearLayer();
}

}
