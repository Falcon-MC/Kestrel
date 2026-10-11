#include "menu/SignScreen.h"

#include "ui/Context.h"

#include <utility>
#include <cstdio>

namespace kestrel::menu {

void SignScreen::setDefinitions(std::shared_ptr<const ui::JsonUi> value)
{
    definitions = std::move(value);
    screen.reset();
}

void SignScreen::open(SignEditorRequest value)
{
    request = std::move(value);
    answer.reset();
    screen.reset();
}

void SignScreen::reset()
{
    request.reset();
    answer.reset();
    screen.reset();
}

std::optional<SignEditorAnswer> SignScreen::takeAnswer()
{
    return std::exchange(answer, std::nullopt);
}

void SignScreen::draw(ui::Context& ui, float width, float height)
{
    if (!request) return;
    if (!screen && definitions) {
        ui::UiRow variables;
        variables["$sign_texture"] = ui::UiValue::of(request->texture);
        variables["$text_box_text_color"] = ui::UiValue::of("#color");
        variables["$sign_text_multiline"] = ui::UiValue::of(request->hanging ? "sign.hanging_sign_text_multiline" : "sign.regular_sign_text_multiline");
        screen = std::make_unique<ui::JsonUiScreen>(definitions, "sign.sign_screen", variables);
    }
    if (!screen || !screen->valid()) {
        answer = SignEditorAnswer { request->serial, request->text.text };
        request.reset();
        screen.reset();
        return;
    }
    ui::UiData data;
    data.globals["#sign_text"] = ui::UiValue::of(request->text.text);
    data.globals["#close_button_visible"] = ui::UiValue::of(true);
    data.globals["#is_using_gamepad"] = ui::UiValue::of(false);
    data.globals["#text_box_enabled"] = ui::UiValue::of(true);
    char color[64];
    std::snprintf(color, sizeof(color), "%.3f,%.3f,%.3f,1.0", ((request->text.color >> 16) & 255) / 255.0, ((request->text.color >> 8) & 255) / 255.0, (request->text.color & 255) / 255.0);
    data.globals["#edit_box_text_color"] = ui::UiValue::of(color);
    screen->draw(ui, { 0, 0, width, height }, data);
    bool close = false;
    for (const ui::UiEvent& event : screen->takeEvents()) {
        if (event.kind == ui::UiEvent::Kind::Text || event.kind == ui::UiEvent::Kind::TextDone) {
            if (event.text.size() <= 4096 && event.text.find('\0') == std::string::npos) request->text.text = event.text;
            if (event.kind == ui::UiEvent::Kind::TextDone) close = true;
        } else if (event.kind == ui::UiEvent::Kind::Button && event.name == "button.menu_exit") {
            close = true;
        }
    }
    if (close) {
        answer = SignEditorAnswer { request->serial, std::move(request->text.text) };
        request.reset();
        screen.reset();
    }
}

}
