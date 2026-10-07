#pragma once

#include "mod/Ui.h"
#include "platform/Input.h"

#include <memory>
#include <vector>

namespace kestrel::modding {

struct UiControlState {
    enum class Kind { Button, Slider, Text };
    struct Item { std::string id; Kind kind; mod::Rect rect; bool enabled; };
    InputState input;
    float scale = 1.0f;
    std::string focus, active;
    size_t caret = 0;
    bool selected = false;
    std::vector<Item> previous, current;

    void begin(const InputState& input, float scale);
    bool add(std::string_view id, Kind kind, mod::Rect rect, bool enabled);
    bool button(std::string_view id, mod::Rect rect);
    bool slider(std::string_view id, mod::Rect rect, float& value, float minimum, float maximum, float step);
    bool textField(std::string_view id, std::string& value, size_t maxBytes);
    void end();
};

class ModUi {
public:
    struct Screen { size_t owner; std::string id; UiControlState controls; };
    void process(size_t owner, mod::detail::UiRequest& request);
    bool open() const { return !screens.empty(); }
    std::shared_ptr<Screen> top() const { return screens.empty() ? nullptr : screens.back(); }
    void capture(InputState& input, float scale);
    void restoreInput(InputState& input);
    void release(size_t owner);
private:
    std::vector<std::shared_ptr<Screen>> screens;
    InputState captured;
    bool consumed = false;
};

}
