#pragma once

#include "ui/JsonUi.h"
#include "ui/Types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kestrel::ui {
class Context;
}

namespace kestrel::menu {

/**
 * Where a form button's icon comes from: a texture path in the resource
 * packs, or a web address to download.
 */
struct FormImage {
    bool url = false;
    std::string data;
};

/**
 * What goes back to the server for a form: the response JSON, or nothing
 * with the reason the form went away.
 */
struct FormAnswer {
    uint32_t id = 0;
    std::optional<std::string> data;
    bool busy = false;
};

inline constexpr const char* FormImageLoading = "loading";

/**
 * Server forms drawn from the game's server_form.json, restyled by whatever
 * packs the server sent: the long form for simple and modal forms, the custom
 * form for the rest, fed the way the game's server form screen controller
 * feeds them. The newest form sits on top and the ones under it come back as
 * it closes.
 */
class FormScreen {
public:
    /**
     * The skin sprite of a button icon, FormImageLoading while it downloads,
     * or empty to leave the icon column out.
     */
    std::function<std::string(const FormImage&)> imageSprite;

    bool active() const
    {
        return !forms.empty();
    }

    void setDefinitions(std::shared_ptr<const ui::JsonUi> definitions);
    void open(uint32_t id, const std::string& json);
    void reject(uint32_t id);
    void closeAll();
    void reset();
    void draw(ui::Context& ui, float width, float height);
    std::vector<FormAnswer> takeAnswers();

private:
    enum class Kind {
        Simple,
        Modal,
        Custom,
    };

    enum class ElementType {
        Label,
        Header,
        Divider,
        Button,
        Toggle,
        Slider,
        StepSlider,
        Dropdown,
        Input,
    };

    struct Element {
        ElementType type = ElementType::Label;
        std::string text;
        std::string tooltip;
        std::string placeholder;
        std::vector<std::string> options;
        std::optional<FormImage> image;
        float min = 0.0f;
        float max = 0.0f;
        float step = 1.0f;
        float value = 0.0f;
        int selected = 0;
        bool on = false;
        std::string input;
    };

    struct Form {
        uint32_t id = 0;
        Kind kind = Kind::Simple;
        std::string title;
        std::string content;
        std::string submit;
        std::vector<Element> elements;
    };

    ui::UiData formData(const Form& form) const;
    void handle(Form& form, const ui::UiEvent& event);
    void submit(Form& form, std::optional<std::string> data);
    std::string response(const Form& form) const;

    std::vector<Form> forms;
    std::vector<FormAnswer> answers;
    std::shared_ptr<const ui::JsonUi> definitions;
    std::unique_ptr<ui::JsonUiScreen> screen;
    uint32_t shownForm = 0;
    size_t shownDepth = 0;
};

}
