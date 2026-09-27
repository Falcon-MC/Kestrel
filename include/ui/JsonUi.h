#pragma once

#include "Core/Json/Json.h"
#include "ui/Types.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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
};

using UiRow = std::unordered_map<std::string, UiValue>;

/**
 * What a screen binds to: global values by binding name (#name) and the rows
 * of each collection its factories repeat over.
 */
struct UiData {
    UiRow globals;
    std::unordered_map<std::string, std::vector<UiRow>> collections;
};

/**
 * The game's JSON UI for the parts of the HUD Kestrel draws from it. Files are
 * merged the way the game merges resource packs: a pack's copy of a file adds
 * to it, a control it redefines takes the pack's properties over the ones
 * before, and "modifications" edit the arrays of the control underneath.
 * Controls are laid out and drawn with their sizes, anchors, offsets, stack
 * panels, factories, variables and bindings, view binding expressions
 * included.
 */
class JsonUi {
public:
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
     * Draws namespace.control laid out inside area.
     */
    void draw(Context& ui, std::string_view control, const Rect& area, const UiData& data) const;

private:
    friend struct JsonUiLayout;

    std::map<std::string, std::unique_ptr<json::Value>> files;
};

}
