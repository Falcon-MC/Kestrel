#include "menu/FormScreen.h"

#include "Core/Json/Json.h"
#include "ui/Context.h"
#include "ui/Localization.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace kestrel::menu {

namespace {

constexpr const char* FormRoot = "server_form.third_party_server_screen";
// $transition_time_pop, how long the screen's exit_pop anims run once the last form closes.
constexpr float LeaveSeconds = 0.4f;

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
    form.json = json;
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
}

void FormScreen::reject(uint32_t id)
{
    answers.push_back({ id, std::nullopt, true });
}

void FormScreen::closeAll()
{
    forms.clear();
    screen.reset();
    leaving.reset();
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

std::vector<std::pair<uint32_t, std::string>> FormScreen::openForms() const
{
    std::vector<std::pair<uint32_t, std::string>> open;
    for (const Form& form : forms) {
        open.emplace_back(form.id, form.json);
    }
    return open;
}

bool FormScreen::answer(uint32_t id, std::optional<std::string> data)
{
    auto found = std::find_if(forms.begin(), forms.end(), [id](const Form& form) { return form.id == id; });
    if (found == forms.end()) {
        return false;
    }
    submit(*found, std::move(data));
    return true;
}

void FormScreen::submit(Form& form, std::optional<std::string> data)
{
    answers.push_back({ form.id, std::move(data), false });
    auto found = std::find_if(forms.begin(), forms.end(), [&](const Form& entry) { return &entry == &form; });
    if (found != forms.end()) {
        forms.erase(found);
    }
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

void FormScreen::setDefinitions(std::shared_ptr<const ui::JsonUi> value)
{
    definitions = std::move(value);
    screen.reset();
    leaving.reset();
}

/**
 * The bindings server_form.json reads, filled the way the game's server form
 * screen controller fills them: the title, the long form's text and button
 * rows, or one custom_form row per element with the option rows of each
 * dropdown in its own custom_dropdown collection.
 */
ui::UiData FormScreen::formData(const Form& form) const
{
    using ui::UiValue;
    ui::UiData data;
    ui::UiRow& globals = data.globals;
    globals["#title_text"] = UiValue::of(form.title);
    bool custom = form.kind == Kind::Custom;
    data.factories["server_form_factory"].push_back({ custom ? "custom_form" : "long_form", {}, form.id });

    if (!custom) {
        globals["#form_text"] = UiValue::of(form.content);
        std::vector<ui::UiRow>& rows = data.collections["form_buttons"];
        for (const Element& element : form.elements) {
            ui::UiRow row;
            const char* control = element.type == ElementType::Label ? "label" : element.type == ElementType::Header ? "header" : element.type == ElementType::Divider ? "divider" : "button";
            row[ui::UiFactoryControl] = UiValue::of(std::string(control));
            row["#form_button_text"] = UiValue::of(element.text);
            std::string texture = element.image && imageSprite ? imageSprite(*element.image) : std::string();
            row["#form_button_texture"] = UiValue::of(std::move(texture));
            row["#form_button_texture_file_system"] = UiValue::of(std::string(element.image && element.image->url ? "Internet" : "InUserPackage"));
            rows.push_back(std::move(row));
        }
        globals["#form_button_contents"] = UiValue::of(static_cast<double>(rows.size()));
        globals["#form_button_length"] = UiValue::of(static_cast<double>(rows.size()));
        return data;
    }

    globals["#submit_text"] = UiValue::of(form.submit);
    globals["#submit_button_visible"] = UiValue::of(true);
    std::vector<ui::UiRow>& rows = data.collections["custom_form"];
    for (size_t index = 0; index < form.elements.size(); ++index) {
        const Element& element = form.elements[index];
        ui::UiRow row;
        auto control = [&](const char* id) { row[ui::UiFactoryControl] = UiValue::of(std::string(id)); };
        row["#custom_text"] = UiValue::of(element.text);
        row["#custom_tooltip_text"] = UiValue::of(element.tooltip);
        switch (element.type) {
        case ElementType::Label:
            control("label");
            break;
        case ElementType::Header:
            control("header");
            break;
        case ElementType::Divider:
            control("divider");
            break;
        case ElementType::Button:
            break;
        case ElementType::Toggle:
            control("toggle");
            row["#custom_toggle_state"] = UiValue::of(element.on);
            break;
        case ElementType::Slider: {
            control("slider");
            row["#custom_slider_text"] = UiValue::of(element.text + ": " + shortNumber(element.value));
            double range = element.max - element.min;
            row["#custom_slider_value"] = UiValue::of(range > 0.0 ? (element.value - element.min) / range : 0.0);
            break;
        }
        case ElementType::StepSlider: {
            control("step_slider");
            std::string option = element.options.empty() ? std::string() : element.options[static_cast<size_t>(element.selected)];
            row["#custom_slider_step_text"] = UiValue::of(element.text + ": " + option);
            size_t count = element.options.size();
            row["#custom_slider_steps"] = UiValue::of(static_cast<double>(count));
            row["#custom_slider_step_value"] = UiValue::of(count > 1 ? static_cast<double>(element.selected) / static_cast<double>(count - 1) : 0.0);
            break;
        }
        case ElementType::Dropdown: {
            control("dropdown");
            row["#dropdown_option_text"] = UiValue::of(element.options.empty() ? std::string() : element.options[static_cast<size_t>(element.selected)]);
            row["#custom_dropdown_length"] = UiValue::of(static_cast<double>(element.options.size()));
            std::vector<ui::UiRow>& options = data.collections["custom_dropdown:" + std::to_string(index)];
            for (size_t option = 0; option < element.options.size(); ++option) {
                options.push_back({
                    { "#custom_radio_text", UiValue::of(element.options[option]) },
                    { "#custom_radio_toggled", UiValue::of(static_cast<int>(option) == element.selected) },
                });
            }
            break;
        }
        case ElementType::Input:
            control("input");
            row["#custom_placeholder_text"] = UiValue::of(element.placeholder);
            row["#custom_input_text"] = UiValue::of(element.input);
            break;
        }
        rows.push_back(std::move(row));
    }
    globals["#custom_form_length"] = UiValue::of(static_cast<double>(rows.size()));
    return data;
}

/**
 * What a control on the form did, as the controller hears it: a button id,
 * or the new state of a toggle, slider, dropdown option or text box, which
 * goes into the element it belongs to.
 */
void FormScreen::handle(Form& form, const ui::UiEvent& event)
{
    using Kind = ui::UiEvent::Kind;
    if (event.kind == Kind::Button) {
        if (event.name == "button.menu_exit") {
            submit(form, std::nullopt);
        } else if (event.name == "button.submit_custom_form" && form.kind == FormScreen::Kind::Custom) {
            submit(form, response(form));
        } else if (event.name == "button.form_button_click" && event.index >= 0 && static_cast<size_t>(event.index) < form.elements.size()) {
            // Labels, headers and dividers share the row list but don't count as buttons.
            int button = 0;
            for (int i = 0; i < event.index; ++i) {
                button += form.elements[static_cast<size_t>(i)].type == ElementType::Button;
            }
            if (form.kind == FormScreen::Kind::Modal) {
                submit(form, button == 0 ? "true\n" : "false\n");
            } else {
                submit(form, std::to_string(button) + "\n");
            }
        }
        return;
    }
    if (form.kind != FormScreen::Kind::Custom) {
        return;
    }
    if (event.kind == Kind::Toggle && event.name == "custom_dropdown_radio_toggle") {
        if (event.outerIndex >= 0 && static_cast<size_t>(event.outerIndex) < form.elements.size()) {
            Element& element = form.elements[static_cast<size_t>(event.outerIndex)];
            element.selected = std::clamp(event.index, 0, std::max(0, static_cast<int>(element.options.size()) - 1));
        }
        return;
    }
    if (event.index < 0 || static_cast<size_t>(event.index) >= form.elements.size()) {
        return;
    }
    Element& element = form.elements[static_cast<size_t>(event.index)];
    if (event.kind == Kind::Toggle && element.type == ElementType::Toggle) {
        element.on = event.state;
    } else if (event.kind == Kind::Slider && element.type == ElementType::Slider) {
        element.value = snapSlider(element.min + static_cast<float>(event.value) * (element.max - element.min), element.min, element.max, element.step);
    } else if (event.kind == Kind::Slider && element.type == ElementType::StepSlider && !element.options.empty()) {
        element.selected = static_cast<int>(std::round(event.value * static_cast<double>(element.options.size() - 1)));
    } else if ((event.kind == Kind::Text || event.kind == Kind::TextDone) && element.type == ElementType::Input) {
        element.input = event.text;
    }
}

void FormScreen::draw(ui::Context& ui, float width, float height)
{
    if (leaving) {
        bool blocked = ui.isBlocked();
        ui.setBlocked(true);
        leaving->draw(ui, { 0.0f, 0.0f, width, height }, leavingData);
        leaving->takeEvents();
        ui.setBlocked(blocked);
        if (std::chrono::steady_clock::now() - leftAt >= std::chrono::duration<float>(LeaveSeconds)) {
            leaving.reset();
        }
    }
    if (forms.empty()) {
        return;
    }
    Form& form = forms.back();
    if (!screen || shownForm != form.id || shownDepth != forms.size()) {
        screen = definitions ? std::make_unique<ui::JsonUiScreen>(definitions, FormRoot) : nullptr;
        shownForm = form.id;
        shownDepth = forms.size();
    }
    if (!screen || !screen->valid()) {
        // Without the game's UI files there is nothing to draw the form with.
        answers.push_back({ form.id, std::nullopt, true });
        forms.pop_back();
        return;
    }
    ui::UiData data = formData(form);
    screen->draw(ui, { 0.0f, 0.0f, width, height }, data);
    uint32_t id = form.id;
    size_t depth = forms.size();
    for (const ui::UiEvent& event : screen->takeEvents()) {
        if (forms.size() != depth || forms.back().id != id) {
            break;
        }
        handle(forms.back(), event);
    }
    if (forms.empty()) {
        // The last form keeps drawing a moment longer so it can play its way out.
        leaving = std::move(screen);
        leavingData = std::move(data);
        leftAt = std::chrono::steady_clock::now();
        leaving->fire("screen.exit_pop");
    }
}

}
