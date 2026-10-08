#pragma once

#include "mod/Ui.h"
#include "modding/LegacyRequests.h"
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

/**
 * One operation on a mod's screens, posted as an event by older mods.
 */
using UiRequest = mod::detail::UiRequest;

/**
 * The screen id of the settings page the client builds from a mod's
 * Ui::addSettings; it is opened as one of that mod's screens.
 */
inline constexpr std::string_view SettingsScreenId = "kestrel:settings";

class ModUi {
public:
    struct Screen { size_t owner; std::string id; UiControlState controls; };
    void process(size_t owner, UiRequest& request);
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
