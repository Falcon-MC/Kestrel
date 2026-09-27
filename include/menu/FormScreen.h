#pragma once

#include "ui/Types.h"

#include <cstdint>
#include <functional>
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
 * Server forms drawn the way the game's server_form.json lays them out: the
 * long form for simple and modal forms, the custom form for the rest. The
 * newest form sits on top and the ones under it come back as it closes.
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
        double openedAt = 0.0;
        float scroll = 0.0f;
        int dropdown = -1;
        float dropdownScroll = 0.0f;
        int focused = -1;
    };

    struct Tooltip {
        std::string text;
        ui::Rect anchor;
    };

    float elementHeight(ui::Context& ui, const Element& element, float width, bool spaced) const;
    void drawElement(ui::Context& ui, Form& form, size_t index, const ui::Rect& rect, bool spaced);
    bool formButton(ui::Context& ui, size_t index, const Element& element, const ui::Rect& rect);
    void drawTooltipBulb(ui::Context& ui, const Element& element, float right, float top);
    void drawDropdown(ui::Context& ui, Form& form, const ui::Rect& toggle, const ui::Rect& bounds);
    void drawTooltip(ui::Context& ui, const ui::Rect& area);
    float scrollBar(ui::Context& ui, const ui::Rect& track, float offset, float view, float content, bool& dragging);
    void editText(ui::Context& ui, Element& element);
    void submit(Form& form, std::optional<std::string> data);
    std::string response(const Form& form) const;

    std::vector<Form> forms;
    std::vector<FormAnswer> answers;
    std::optional<Tooltip> tooltip;
    ui::Rect dropdownToggle;
    ui::Rect currentClip;
    float tooltipWidth = 0.0f;
    int dragging = -1;
    bool scrollDragging = false;
    bool dropdownScrollDragging = false;
    float dragGrab = 0.0f;
};

}
