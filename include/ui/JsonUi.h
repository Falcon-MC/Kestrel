#pragma once

#include "Core/Json/Json.h"
#include "ui/Types.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kestrel::ui {

class Context;

/**
 * A value a binding or binding expression produces: nothing, a flag, a number
 * or text.
 */
struct UiValue {
    enum class Kind {
        None,
        Bool,
        Number,
        String,
    };

    Kind kind = Kind::None;
    bool flag = false;
    double number = 0.0;
    std::string text;

    static UiValue of(bool value);
    static UiValue of(double value);
    static UiValue of(std::string value);

    bool truthy() const;
    double toNumber() const;
    std::string toText() const;
    bool operator==(const UiValue&) const = default;
};

using UiRow = std::unordered_map<std::string, UiValue>;

/**
 * One control a factory holds: which of its control ids to make, the
 * variables the control is made with and a serial that, when it changes,
 * makes the control again so its animations start over.
 */
struct UiFactoryItem {
    std::string control;
    UiRow variables;
    uint64_t serial = 0;
    bool operator==(const UiFactoryItem&) const = default;
};

/**
 * What a screen binds to, the part the game's screen controllers fill in:
 * global values by binding name (#name), the rows of each collection, and
 * the controls each named factory holds.
 *
 * A collection factory with control_ids picks the id of every row from its
 * UiFactoryControl entry. A collection nested in another one, like the
 * options of each dropdown in a custom form, is looked up as
 * "name:outer_index" before "name".
 */
struct UiData {
    UiRow globals;
    std::unordered_map<std::string, std::vector<UiRow>> collections;
    std::unordered_map<std::string, std::vector<UiFactoryItem>> factories;
    bool operator==(const UiData&) const = default;
};

inline constexpr const char* UiFactoryControl = "@control_id";

/**
 * Something the player did on a screen, the way button mappings, toggles,
 * sliders and edit boxes report it to the game's screen controllers, or the
 * end_event of an animation.
 */
struct UiEvent {
    enum class Kind {
        Button,
        Toggle,
        Slider,
        Text,
        TextDone,
        Animation,
    };

    Kind kind = Kind::Button;
    std::string name;
    std::string collection;
    int index = -1;
    int outerIndex = -1;
    bool state = false;
    double value = 0.0;
    std::string text;
};

using UiLookup = std::function<UiValue(const std::string&)>;

/**
 * Draws a control of type "custom": the renderer it names, where it sits,
 * its alpha and a way to read the bindings it sees.
 */
using UiRenderer = std::function<void(Context& ui, const std::string& renderer, const Rect& rect, float alpha, const UiLookup& lookup)>;

/**
 * The game's JSON UI definitions. Files are merged the way the game merges
 * resource packs: a pack's copy of a file adds to it, a control it redefines
 * takes the pack's properties over the ones before, and "modifications" edit
 * the arrays of the control underneath.
 */
class JsonUi {
public:
    struct Control {
        std::string key;
        const json::Value* value = nullptr;
        const std::string* space = nullptr;
    };

    /**
     * Merges a UI file into the one already added under the same path; add
     * the vanilla file first and the packs from lowest to highest priority.
     */
    void addFile(const std::string& path, const std::string& text);
    void clear();

    /**
     * Every texture the loaded controls name, so pack textures can be put in
     * place before drawing.
     */
    std::vector<std::string> texturePaths() const;

    /**
     * The control namespace.name, its key keeping the base it derives from.
     */
    Control find(std::string_view space, std::string_view name) const;

    /**
     * A variable from _global_variables.json or one the game sets for the
     * platform, like $desktop_screen.
     */
    const json::Value* globalVariable(const std::string& name) const;

    bool has(std::string_view reference) const;

private:
    void index() const;

    std::map<std::string, std::unique_ptr<json::Value>> files;
    mutable std::unique_ptr<json::Value> platform;
    mutable std::unordered_map<std::string, std::unordered_map<std::string, Control>> controls;
    mutable std::vector<const json::Value*> globals;
    mutable bool indexed = false;
};

struct JsonUiRuntime;

/**
 * One screen of the JSON UI built from its root control and kept between
 * frames, so hover, focus, scrolling and animations carry on. Each frame the
 * bindings are read from the data, factories grow or shrink to match it, and
 * the controls are laid out, drawn and handed the input.
 */
class JsonUiScreen {
public:
    JsonUiScreen(std::shared_ptr<const JsonUi> definitions, std::string root);
    ~JsonUiScreen();

    JsonUiScreen(const JsonUiScreen&) = delete;
    JsonUiScreen& operator=(const JsonUiScreen&) = delete;

    const std::shared_ptr<const JsonUi>& definitions() const;

    /**
     * Whether the root control exists, so there is something to draw.
     */
    bool valid() const;

    void setRenderer(UiRenderer renderer);

    /**
     * Starts the animations waiting for play_event.
     */
    void fire(const std::string& event);

    /**
     * Holds a button id down the way a key held on the keyboard does, so a
     * button mapping it globally shows its pressed_control until released.
     */
    void holdButton(const std::string& id, bool held);

    /**
     * Lays out, draws and runs the input of the screen inside area.
     */
    void draw(Context& ui, const Rect& area, const UiData& data);

    std::vector<UiEvent> takeEvents();

    /**
     * An edit box has the keyboard, so keys should type instead of acting.
     */
    bool editing() const;

    /**
     * Takes the keyboard back from the edit box that has it, for screens
     * whose text is typed and kept outside the edit box.
     */
    void blur();

    /**
     * Something under the mouse takes clicks, like a button or a slider.
     */
    bool hovering() const;

    /**
     * The control tree as last laid out, one line per control with its name,
     * type, whether it shows and its box, for diagnosing packs.
     */
    std::string describe(size_t maxLines) const;

private:
    std::unique_ptr<JsonUiRuntime> runtime;
};

}
