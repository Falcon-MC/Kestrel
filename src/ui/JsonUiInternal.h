#pragma once

#include "ui/Font.h"
#include "ui/JsonUi.h"

#include <array>
#include <cstdint>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kestrel::ui::jsonui {

constexpr int MaxDepth = 64;
constexpr size_t MaxFactoryItems = 256;
constexpr float LabelLineHeight = 10.0f;
constexpr uint32_t HiddenUpdateInterval = 8;
// A pack can nest factories and controls into far more controls than any screen needs.
constexpr size_t MaxControlsPerFrame = 20000;

std::string_view controlName(std::string_view key);
std::string_view controlBase(std::string_view key);

/**
 * Evaluates the binding expressions of the JSON UI: not, and, or,
 * comparisons, arithmetic, string subtraction (removes every occurrence) and
 * '%.Ns' * text (keeps the first N characters), over #bindings, $variables,
 * 'strings' and numbers.
 */
UiValue evaluate(std::string_view source, const UiLookup& lookup);

UiValue toValue(const json::Value* value);

/**
 * A property value and the namespace of the file it was written in, which is
 * where the control names it holds are looked up.
 */
struct Prop {
    const json::Value* value = nullptr;
    const std::string* space = nullptr;
    bool fallback = false;
};

// Looked up with string views, so reading a property by a literal name allocates nothing.
struct NameHash {
    using is_transparent = void;

    size_t operator()(std::string_view name) const
    {
        return std::hash<std::string_view> {}(name);
    }
};

using PropMap = std::unordered_map<std::string, Prop, NameHash, std::equal_to<>>;

enum class TermKind {
    Pixel,
    Parent,
    Children,
    ChildrenMax,
    SiblingMax,
    OwnX,
    OwnY,
    Default,
    Fill,
};

struct Term {
    TermKind kind = TermKind::Pixel;
    float amount = 0.0f;
};

using Extent = std::vector<Term>;

Extent parseExtent(const json::Value* value, TermKind fallback);
bool uses(const Extent& extent, TermKind kind);

/**
 * One chain of animations driving a property: the anim playing now, when it
 * started (negative while it waits for its play_event), the value the chain
 * holds from the anims that already ran, and the anim it goes back to on its
 * reset_event.
 */
struct AnimTrack {
    std::string target;
    PropMap props;
    PropMap first;
    double start = -1.0;
    std::string playEvent;
    std::string resetEvent;
    bool finished = false;
    const json::Value* held = nullptr;
    double heldNumber = 0.0;
    bool holding = false;
};

struct Node {
    uint64_t id = 0;
    std::string name;
    std::string type;
    PropMap props;
    std::shared_ptr<const PropMap> vars;
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;
    std::vector<std::unique_ptr<json::Value>> owned;

    // Factory made controls remember what made them, so the factory can tell
    // whether the data still asks for them.
    std::string made;
    uint64_t serial = 0;
    bool generated = false;
    std::string collection;
    int index = -1;

    UiRow bound;
    std::vector<AnimTrack> anims;
    double created = 0.0;
    bool destroyed = false;

    bool visible = true;
    uint32_t idleFrames = HiddenUpdateInterval - 1;
    bool shown = false;
    bool forced = false;
    bool enabled = true;
    float alpha = 1.0f;
    bool propagate = false;
    bool vertical = true;
    std::string text;
    std::string texture;
    Node* scroller = nullptr;
    std::optional<std::array<float, 2>> offsetOverride;
    std::vector<std::unique_ptr<json::Value>> scratch;

    Extent width;
    Extent height;
    float w = 0.0f;
    float h = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float paintAlpha = 1.0f;
    float inherited = 1.0f;
    Rect clip {};
    bool clipped = false;
    std::array<float, 2> intrinsic { 0.0f, 0.0f };
    std::array<bool, 2> measured { false, false };

    bool toggled = false;
    bool dataToggle = false;
    bool hover = false;
    float scroll = 0.0f;
    float value = 0.0f;
    std::string edit;
    std::unordered_map<const Node*, bool> states;
};

}

namespace kestrel::ui {

struct JsonUiRuntime {
    using Node = jsonui::Node;
    using Prop = jsonui::Prop;
    using PropMap = jsonui::PropMap;

    std::shared_ptr<const JsonUi> defs;
    std::string rootReference;
    std::unique_ptr<Node> root;
    UiRenderer renderer;
    const UiData* data = nullptr;
    Context* ui = nullptr;
    double now = 0.0;
    uint64_t nextId = 1;
    size_t madeThisFrame = 0;
    std::vector<UiEvent> events;
    std::vector<std::pair<std::string, uint64_t>> destroyedItems;
    std::vector<Node*> order;
    std::unordered_map<uint64_t, Node*> byId;
    uint64_t hot = 0;
    uint64_t active = 0;
    uint64_t focused = 0;
    float grab = 0.0f;

    // JsonUiBuild.cpp
    std::unique_ptr<Node> make(Node* parent, std::string_view key, const json::Value* instance, const std::string* space, std::shared_ptr<const PropMap> scope, const UiRow* variables, int depth);
    void collect(std::string_view reference, const std::string* space, PropMap& out, int depth) const;
    void applyVariables(Node& node, const UiRow* variables);
    Prop variable(const Node& node, std::string_view name) const;
    Prop resolveProp(const Node& node, Prop prop) const;
    const json::Value* resolve(const Node& node, const json::Value* value) const;
    const json::Value* property(const Node& node, std::string_view name) const;
    Prop propertyProp(const Node& node, std::string_view name) const;
    std::string text(const Node& node, std::string_view name) const;
    double number(const Node& node, std::string_view name, double fallback) const;
    bool flag(const Node& node, std::string_view name, bool fallback) const;
    UiValue lookup(const Node& node, const std::string& name) const;
    UiValue evaluate(const Node& node, std::string_view source) const;
    UiValue bindingLookup(const Node& node, const std::string& key, const UiLookup& binding) const;
    UiValue valueOf(const Node& node, std::string_view name) const;
    bool condition(const Node& node, const json::Value* value) const;
    void addChildren(Node& node, int depth);
    void addAnim(Node& node, const std::string& target, std::string_view reference, const std::string* space);
    std::string factoryKey(const Node& node, const json::Value* ids, const std::string& id, const json::Value* fallback) const;
    void syncFactories(Node& node, int depth);
    void syncItems(Node& node, const std::vector<UiFactoryItem>& items, const json::Value* ids, const json::Value* fallback, size_t limit, int depth);
    void syncCollection(Node& node, const std::string& collection, size_t count, const json::Value* factory, const std::string& templateControl, int depth);
    const UiRow* row(const Node& node, const std::string& collection) const;
    void bind(Node& node);
    void animate(Node& node);
    void update(Node& node, int depth, Node* control);
    static bool isControl(const Node& node);
    void chooseStates(Node& node);
    void overrideState(Node& control, Node& node);
    std::vector<std::unique_ptr<Node>> takeGenerated(Node& node);
    void sweep(Node& node);

    // JsonUiLayout.cpp
    float natural(Node& node, int axis);
    TextStyle labelStyle(const Node& node) const;
    float labelScale(const Node& node) const;
    std::array<int, 2> gridCells(Node& node);
    float intrinsic(Node& node, int axis);
    void size(Node& node, int axis, float parent, std::optional<float> forced = std::nullopt);
    float term(const Node& node, const json::Value* value, float parent) const;
    std::optional<float> animated(const Node& node, const std::string& target, int axis, float parent) const;
    void place(Node& node, float x, float y, float z, const Rect& clip, bool clipped, float alpha);

    // JsonUiScreen.cpp
    void gather(Node& node);
    Node* find(Node& from, const std::string& name) const;
    Node* ancestor(Node& node, const std::string& type) const;
    void input();
    void click(Node& node);
    void paint(Node& node);
    void paintHoverText(const Node& node, float alpha);
    void emit(UiEvent::Kind kind, const Node& node, std::string name);
    void fire(Node& node, const std::string& event);
};

}
