#include "JsonUiInternal.h"

#include <algorithm>
#include <iterator>

namespace kestrel::ui {

using namespace jsonui;

namespace {

// Properties an animation can drive when the property itself names it with "@namespace.anim".
constexpr const char* AnimatedProperties[] = { "alpha", "offset", "size", "uv", "color", "clip_ratio" };

bool isVariable(const json::Value* value)
{
    return value && value->isString() && !value->mString.empty() && value->mString.front() == '$';
}

bool isReference(const json::Value* value)
{
    return value && value->isString() && value->mString.size() > 1 && value->mString.front() == '@';
}

std::string animTarget(const std::string& type)
{
    if (type == "alpha" || type == "offset" || type == "size" || type == "color" || type == "uv") {
        return type;
    }
    if (type == "flip_book" || type == "aseprite_flip_book") {
        return "uv";
    }
    if (type == "clip") {
        return "clip_ratio";
    }
    return {};
}

/**
 * What the game's base screen controller binds on every screen, for the
 * bindings a screen's own data leaves out: no safe zone padding, no
 * container, mouse and keyboard.
 */
const UiRow& screenDefaults()
{
    static const UiRow defaults = [] {
        UiRow row;
        for (const char* side : { "outer_left", "outer_right", "outer_top", "outer_bottom", "inner_left", "inner_right", "inner_top", "inner_bottom" }) {
            row[std::string("#safezone_") + side] = UiValue::of(false);
        }
        for (const char* name : { "#is_container_screen", "#is_using_gamepad", "#using_touch", "#tts_enabled", "#gesture_control_enabled", "#is_pregame", "#bar_animation_visible" }) {
            row[name] = UiValue::of(false);
        }
        return row;
    }();
    return defaults;
}

std::unique_ptr<json::Value> toJson(const UiValue& value)
{
    switch (value.kind) {
    case UiValue::Kind::Bool:
        return json::Value::ofBoolean(value.flag);
    case UiValue::Kind::Number:
        return json::Value::ofNumber(value.number);
    case UiValue::Kind::String:
        if (!value.text.empty() && value.text.front() == '[') {
            if (std::unique_ptr<json::Value> array = json::parse(value.text)) {
                return array;
            }
        }
        return json::Value::ofString(value.text);
    case UiValue::Kind::None:
        break;
    }
    return json::Value::ofNull();
}

}

void JsonUiRuntime::collect(std::string_view reference, const std::string* space, PropMap& out, int depth) const
{
    if (depth > MaxDepth || reference.empty()) {
        return;
    }
    if (reference.front() == '@') {
        reference.remove_prefix(1);
    }
    size_t dot = reference.find('.');
    std::string_view ns = dot == std::string_view::npos ? (space ? std::string_view(*space) : std::string_view()) : reference.substr(0, dot);
    std::string_view name = dot == std::string_view::npos ? reference : reference.substr(dot + 1);
    JsonUi::Control control = defs->find(ns, name);
    if (!control.value || !control.value->isObject()) {
        return;
    }
    if (std::string_view base = controlBase(control.key); !base.empty()) {
        collect(base, control.space, out, depth + 1);
    }
    for (const std::string& key : control.value->mKeys) {
        out[key] = { control.value->get(key), control.space };
    }
}

Prop JsonUiRuntime::variable(const Node& node, std::string_view name) const
{
    if (node.vars) {
        if (const Prop* found = node.vars->find(name)) {
            return *found;
        }
    }
    return { defs->globalVariable(std::string(name)), nullptr };
}

Prop JsonUiRuntime::resolveProp(const Node& node, Prop prop) const
{
    for (int depth = 0; isVariable(prop.value) && depth < MaxDepth; ++depth) {
        Prop next = variable(node, prop.value->mString);
        if (!next.value) {
            return {};
        }
        prop = { next.value, next.space ? next.space : prop.space };
    }
    return prop;
}

const json::Value* JsonUiRuntime::resolve(const Node& node, const json::Value* value) const
{
    return resolveProp(node, { value, nullptr }).value;
}

Prop JsonUiRuntime::propertyProp(const Node& node, std::string_view name) const
{
    auto found = node.props.find(name);
    return found == node.props.end() ? Prop {} : resolveProp(node, found->second);
}

const json::Value* JsonUiRuntime::property(const Node& node, std::string_view name) const
{
    return propertyProp(node, name).value;
}

std::string JsonUiRuntime::text(const Node& node, std::string_view name) const
{
    const json::Value* value = property(node, name);
    return value && value->isString() ? value->mString : std::string();
}

double JsonUiRuntime::number(const Node& node, std::string_view name, double fallback) const
{
    UiValue value = valueOf(node, name);
    return value.kind == UiValue::Kind::None ? fallback : value.toNumber();
}

bool JsonUiRuntime::flag(const Node& node, std::string_view name, bool fallback) const
{
    UiValue value = valueOf(node, name);
    return value.kind == UiValue::Kind::None ? fallback : value.truthy();
}

UiValue JsonUiRuntime::lookup(const Node& node, const std::string& name) const
{
    if (name.empty()) {
        return {};
    }
    if (name.front() == '$') {
        return toValue(resolve(node, variable(node, name).value));
    }
    if (auto found = node.bound.find(name); found != node.bound.end()) {
        return found->second;
    }
    if (name == "#collection_index") {
        for (const Node* at = &node; at; at = at->parent) {
            if (at->index >= 0) {
                return UiValue::of(static_cast<double>(at->index));
            }
        }
    }
    for (const Node* at = &node; at; at = at->parent) {
        if (at->index >= 0) {
            if (const UiRow* values = row(node, at->collection)) {
                if (auto found = values->find(name); found != values->end()) {
                    return found->second;
                }
            }
            break;
        }
    }
    if (data) {
        if (auto found = data->globals.find(name); found != data->globals.end()) {
            return found->second;
        }
    }
    if (auto found = screenDefaults().find(name); found != screenDefaults().end()) {
        return found->second;
    }
    return {};
}

UiValue JsonUiRuntime::evaluate(const Node& node, std::string_view source) const
{
    UiLookup find = [&](const std::string& name) { return lookup(node, name); };
    return jsonui::evaluate(source, find);
}

/**
 * A property as a value: bindings and expressions it names are read, so
 * "#text" gives the bound text and "(not #flag)" its result.
 */
UiValue JsonUiRuntime::valueOf(const Node& node, std::string_view name) const
{
    const json::Value* value = property(node, name);
    if (value && value->isString() && !value->mString.empty()) {
        char first = value->mString.front();
        if (first == '#') {
            return lookup(node, value->mString);
        }
        if (first == '(') {
            return evaluate(node, value->mString);
        }
    }
    return toValue(value);
}

bool JsonUiRuntime::condition(const Node& node, const json::Value* value) const
{
    value = resolve(node, value);
    if (!value) {
        return false;
    }
    if (value->isString()) {
        return evaluate(node, value->mString).truthy();
    }
    return toValue(value).truthy();
}

/**
 * Sets the variables a control sees: those of its parent, then the ones it
 * defines itself, then those of every "variables" entry whose "requires"
 * holds, and last the ones the factory that made it passes. A "|default"
 * value only gives way to one set plainly further out; against another
 * default the closer one wins, which is how settings_common moves the label
 * of a one line option past its toggle.
 *
 * A value naming another variable is resolved against the parent's variables
 * right away when it can be, so "$x": "$x" passes the parent's value on;
 * anything else is left for when the value is used.
 */
void JsonUiRuntime::applyVariables(Node& node, const UiRow* variables)
{
    std::vector<std::pair<std::string, Prop>> plain;
    std::vector<std::pair<std::string, Prop>> defaults;
    for (const auto& [key, prop] : node.props) {
        if (key.size() < 2 || key.front() != '$') {
            continue;
        }
        size_t bar = key.find('|');
        if (bar == std::string::npos) {
            plain.emplace_back(key, prop);
        } else {
            defaults.emplace_back(key.substr(0, bar), prop);
        }
    }
    const json::Value* entries = nullptr;
    if (auto found = node.props.find("variables"); found != node.props.end()) {
        entries = found->second.value;
    }
    if (plain.empty() && defaults.empty() && !entries && !variables) {
        return;
    }

    std::shared_ptr<const jsonui::VarScope> inherited = node.vars;
    auto scope = std::make_shared<jsonui::VarScope>();
    scope->parent = inherited;
    auto own = [&](const std::string& name) {
        return std::any_of(plain.begin(), plain.end(), [&](const auto& entry) { return entry.first == name; });
    };
    auto early = [&](const std::string& key, Prop prop) {
        if (!isVariable(prop.value) || (prop.value->mString != key && own(prop.value->mString))) {
            return prop;
        }
        Node probe;
        probe.vars = inherited;
        Prop resolved = resolveProp(probe, prop);
        return resolved.value ? resolved : prop;
    };
    for (const auto& [key, prop] : plain) {
        Prop value = early(key, prop);
        value.fallback = false;
        scope->own[key] = value;
    }
    for (const auto& [key, prop] : defaults) {
        const Prop* existing = scope->find(key);
        if (!existing || (existing->fallback && !own(key))) {
            Prop value = early(key, prop);
            value.fallback = true;
            scope->own[key] = value;
        }
    }
    node.vars = scope;
    if (entries && entries->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : entries->mArray) {
            if (!entry->isObject() || !condition(node, entry->get("requires"))) {
                continue;
            }
            for (const std::string& key : entry->mKeys) {
                if (key.size() < 2 || key.front() != '$') {
                    continue;
                }
                size_t bar = key.find('|');
                std::string name = key.substr(0, bar);
                Prop prop { entry->get(key), node.props.at("variables").space };
                const Prop* existing = scope->find(name);
                if (bar == std::string::npos || !existing || existing->fallback) {
                    Node probe;
                    probe.vars = scope;
                    Prop resolved = isVariable(prop.value) ? resolveProp(probe, prop) : prop;
                    resolved = resolved.value ? resolved : prop;
                    resolved.fallback = bar != std::string::npos;
                    scope->own[name] = resolved;
                }
            }
        }
    }
    if (variables) {
        for (const auto& [name, value] : *variables) {
            node.owned.push_back(toJson(value));
            scope->own[name.front() == '$' ? name : "$" + name] = { node.owned.back().get(), nullptr };
        }
    }
}

/**
 * Makes one control and everything under it. The key is "name" or
 * "name@base", either part possibly a variable, and the instance holds the
 * properties written where the control is placed, which win over the base's.
 */
std::unique_ptr<Node> JsonUiRuntime::make(Node* parent, std::string_view key, const json::Value* instance, const std::string* space, std::shared_ptr<const jsonui::VarScope> scope, const UiRow* variables, int depth)
{
    if (depth > MaxDepth || ++madeThisFrame > MaxControlsPerFrame) {
        return nullptr;
    }
    auto node = std::make_unique<Node>();
    node->id = nextId++;
    node->parent = parent;
    node->created = now;
    node->vars = std::move(scope);

    std::string name(controlName(key));
    std::string base(controlBase(key));
    if ((!name.empty() && name.front() == '$') || (!base.empty() && base.front() == '$')) {
        Node probe;
        auto names = std::make_shared<jsonui::VarScope>();
        names->parent = node->vars;
        if (instance && instance->isObject()) {
            for (const std::string& property : instance->mKeys) {
                if (property.size() > 1 && property.front() == '$' && property.find('|') == std::string::npos) {
                    names->own[property] = { instance->get(property), space };
                }
            }
        }
        probe.vars = names;
        auto named = [&](const std::string& text) {
            if (text.empty() || text.front() != '$') {
                return text;
            }
            const json::Value* value = resolve(probe, variable(probe, text).value);
            return value && value->isString() ? value->mString : std::string();
        };
        name = named(name);
        base = named(base);
        if (!base.empty() && base.front() == '@') {
            base.erase(0, 1);
        }
    }
    node->name = name;
    if (!base.empty()) {
        collect(base, space, node->props, 0);
    }
    if (instance && instance->isObject()) {
        for (const std::string& property : instance->mKeys) {
            node->props[property] = { instance->get(property), space };
        }
    }
    applyVariables(*node, variables);

    if (auto ignored = node->props.find("ignored"); ignored != node->props.end() && condition(*node, ignored->second.value)) {
        return nullptr;
    }
    const json::Value* type = property(*node, "type");
    node->type = type && type->isString() ? type->mString : "panel";
    if (const json::Value* bag = property(*node, "property_bag"); bag && bag->isObject()) {
        for (const std::string& entry : bag->mKeys) {
            node->bound[entry] = toValue(resolve(*node, bag->get(entry)));
        }
    }
    if (node->type == "toggle" || node->type == "dropdown") {
        node->toggled = flag(*node, "toggle_default_state", false);
    }
    // A control can name its row itself, in the collection of the closest control above it
    // that has one.
    if (const json::Value* index = property(*node, "collection_index"); index && index->isNumber()) {
        node->index = static_cast<int>(index->mNumber);
        for (Node* at = parent; at; at = at->parent) {
            if (std::string name = text(*at, "collection_name"); !name.empty()) {
                node->collection = std::move(name);
                break;
            }
        }
    }
    for (const char* target : AnimatedProperties) {
        Prop prop = propertyProp(*node, target);
        if (isReference(prop.value)) {
            addAnim(*node, target, prop.value->mString, prop.space);
        }
    }
    if (Prop anims = propertyProp(*node, "anims"); anims.value && anims.value->isArray()) {
        for (const std::unique_ptr<json::Value>& entry : anims.value->mArray) {
            const json::Value* reference = resolve(*node, entry.get());
            if (isReference(reference)) {
                addAnim(*node, {}, reference->mString, anims.space);
            }
        }
    }
    addChildren(*node, depth);
    return node;
}

void JsonUiRuntime::addChildren(Node& node, int depth)
{
    Prop controls = propertyProp(node, "controls");
    if (!controls.value || !controls.value->isArray()) {
        return;
    }
    bool collection = node.type == "collection_panel";
    std::string collectionName = collection ? text(node, "collection_name") : std::string();
    for (const std::unique_ptr<json::Value>& entry : controls.value->mArray) {
        if (!entry->isObject() || entry->mKeys.empty()) {
            continue;
        }
        const std::string& key = entry->mKeys.front();
        if (std::unique_ptr<Node> child = make(&node, key, entry->get(key), controls.space, node.vars, nullptr, depth + 1)) {
            if (collection) {
                child->collection = collectionName;
                child->index = static_cast<int>(node.children.size());
            }
            node.children.push_back(std::move(child));
        }
    }
}

void JsonUiRuntime::addAnim(Node& node, const std::string& target, std::string_view reference, const std::string* space)
{
    AnimTrack track;
    collect(reference, space, track.props, 0);
    if (track.props.empty()) {
        return;
    }
    Node probe;
    probe.vars = node.vars;
    probe.props = track.props;
    track.target = target;
    // An "anims" chain can open with a wait; what it drives is the first anim after it.
    for (int step = 0; track.target.empty() && step < MaxDepth; ++step) {
        std::string type = text(probe, "anim_type");
        if (type != "wait") {
            track.target = animTarget(type);
            break;
        }
        Prop next = resolveProp(probe, probe.props.count("next") ? probe.props.at("next") : Prop {});
        if (!isReference(next.value)) {
            break;
        }
        PropMap props;
        collect(next.value->mString, next.space, props, 0);
        probe.props = std::move(props);
    }
    probe.props = track.props;
    if (track.target.empty()) {
        return;
    }
    track.playEvent = text(probe, "play_event");
    track.resetEvent = text(probe, "reset_event");
    if (track.resetEvent.empty()) {
        track.resetEvent = text(node, "animation_reset_name");
    }
    track.start = track.playEvent.empty() ? now : -1.0;
    track.first = track.props;
    node.anims.push_back(std::move(track));
}

/**
 * The key to make a factory's control with: control_ids map the id the data
 * asks for to "name@namespace.control", "@namespace.control" or just
 * "namespace.control".
 */
std::string JsonUiRuntime::factoryKey(const Node& node, const json::Value* ids, const std::string& id, const json::Value* fallback) const
{
    const json::Value* target = nullptr;
    if (ids && ids->isObject()) {
        target = ids->get(id);
        if (!target && !ids->mKeys.empty() && id.empty()) {
            target = ids->get(ids->mKeys.front());
        }
    }
    target = resolve(node, target ? target : fallback);
    if (!target || !target->isString() || target->mString.empty()) {
        return {};
    }
    std::string key = target->mString;
    if (target == fallback || id.empty()) {
        std::string_view reference = key.front() == '@' ? std::string_view(key).substr(1) : std::string_view(key);
        size_t dot = reference.rfind('.');
        std::string name(dot == std::string_view::npos ? reference : reference.substr(dot + 1));
        return key.find('@') != std::string::npos && key.front() != '@' ? key : name + "@" + std::string(reference);
    }
    if (key.find('@') == std::string::npos) {
        return id + "@" + key;
    }
    if (key.front() == '@') {
        return id + key;
    }
    return key;
}

const UiRow* JsonUiRuntime::row(const Node& node, const std::string& collection) const
{
    if (!data) {
        return nullptr;
    }
    const Node* item = nullptr;
    for (const Node* at = &node; at; at = at->parent) {
        if (at->index >= 0 && (at->collection == collection || collection.empty())) {
            item = at;
            break;
        }
    }
    if (!item) {
        for (const Node* at = &node; at; at = at->parent) {
            if (at->index >= 0) {
                item = at;
                break;
            }
        }
    }
    if (!item) {
        return nullptr;
    }
    std::string name = collection.empty() ? item->collection : collection;
    const std::vector<UiRow>* rows = nullptr;
    for (const Node* outer = item->parent; outer; outer = outer->parent) {
        if (outer->index >= 0) {
            if (auto found = data->collections.find(name + ":" + std::to_string(outer->index)); found != data->collections.end()) {
                rows = &found->second;
            }
            break;
        }
    }
    if (!rows) {
        auto found = data->collections.find(name);
        if (found == data->collections.end()) {
            return nullptr;
        }
        rows = &found->second;
    }
    size_t index = static_cast<size_t>(item->index);
    return index < rows->size() ? &(*rows)[index] : nullptr;
}

/**
 * Brings a factory's children in line with what the data asks for, keeping
 * the ones that match so their state and animations carry on.
 */
void JsonUiRuntime::syncItems(Node& node, const std::vector<UiFactoryItem>& items, const json::Value* ids, const json::Value* fallback, size_t limit, int depth)
{
    size_t first = items.size() > limit ? items.size() - limit : 0;
    std::vector<std::unique_ptr<Node>> previous = takeGenerated(node);
    for (size_t i = first; i < items.size(); ++i) {
        const UiFactoryItem& item = items[i];
        auto matches = [&](const std::unique_ptr<Node>& child) { return child && child->made == item.control && child->serial == item.serial; };
        auto existing = std::find_if(previous.begin(), previous.end(), matches);
        if (existing != previous.end()) {
            node.children.push_back(std::move(*existing));
            node.children.back()->index = static_cast<int>(i - first);
            continue;
        }
        auto gone = std::find(destroyedItems.begin(), destroyedItems.end(), std::make_pair(item.control, item.serial));
        if (gone != destroyedItems.end()) {
            continue;
        }
        std::string key = factoryKey(node, ids, item.control, fallback);
        if (key.empty()) {
            continue;
        }
        if (std::unique_ptr<Node> child = make(&node, key, nullptr, node.props.count("factory") ? node.props.at("factory").space : nullptr, node.vars, &item.variables, depth + 1)) {
            child->made = item.control;
            child->serial = item.serial;
            child->generated = true;
            child->index = static_cast<int>(i - first);
            node.children.push_back(std::move(child));
        }
    }
}

/**
 * Takes the controls a factory made out of node, leaving the ones written in
 * its "controls" in place.
 */
std::vector<std::unique_ptr<Node>> JsonUiRuntime::takeGenerated(Node& node)
{
    std::vector<std::unique_ptr<Node>> generated;
    auto split = std::stable_partition(node.children.begin(), node.children.end(), [](const std::unique_ptr<Node>& child) { return !child->generated; });
    std::move(split, node.children.end(), std::back_inserter(generated));
    node.children.erase(split, node.children.end());
    return generated;
}

void JsonUiRuntime::syncCollection(Node& node, const std::string& collection, size_t count, const json::Value* factory, const std::string& templateControl, int depth, const std::vector<std::string>& roles)
{
    count = std::min(count, MaxFactoryItems);
    const std::vector<UiRow>* rows = nullptr;
    if (data) {
        std::string name = collection;
        for (const Node* outer = &node; outer; outer = outer->parent) {
            if (outer->index >= 0) {
                if (auto found = data->collections.find(collection + ":" + std::to_string(outer->index)); found != data->collections.end()) {
                    rows = &found->second;
                }
                break;
            }
        }
        if (!rows) {
            if (auto found = data->collections.find(collection); found != data->collections.end()) {
                rows = &found->second;
            }
        }
    }
    const json::Value* ids = factory ? resolve(node, factory->get("control_ids")) : nullptr;
    const json::Value* named = factory ? resolve(node, factory->get("control_name")) : nullptr;
    const std::string* space = node.props.count("factory") ? node.props.at("factory").space : node.props.count("grid_item_template") ? node.props.at("grid_item_template").space : nullptr;
    std::vector<std::unique_ptr<Node>> previous = takeGenerated(node);
    for (size_t i = 0; i < count; ++i) {
        std::string id = i < roles.size() ? roles[i] : std::string();
        if (id.empty() && rows && i < rows->size()) {
            if (auto found = (*rows)[i].find(UiFactoryControl); found != (*rows)[i].end()) {
                id = found->second.toText();
            }
        }
        std::string key;
        if (!templateControl.empty()) {
            key = "item@" + templateControl;
        } else {
            key = factoryKey(node, ids, id, named);
        }
        if (i < previous.size() && previous[i] && previous[i]->made == key) {
            node.children.push_back(std::move(previous[i]));
            continue;
        }
        if (key.empty()) {
            continue;
        }
        if (std::unique_ptr<Node> child = make(&node, key, nullptr, space, node.vars, nullptr, depth + 1)) {
            child->made = key;
            child->generated = true;
            child->collection = collection;
            child->index = static_cast<int>(i);
            node.children.push_back(std::move(child));
        }
    }
}

void JsonUiRuntime::syncFactories(Node& node, int depth)
{
    const json::Value* factory = property(node, "factory");
    if (node.type == "grid") {
        std::string collection = text(node, "collection_name");
        std::string templateControl = text(node, "grid_item_template");
        UiValue dimensions;
        std::string binding = text(node, "grid_dimension_binding");
        if (!binding.empty()) {
            dimensions = lookup(node, binding);
        }
        size_t count = 0;
        if (dimensions.kind != UiValue::Kind::None) {
            std::string value = dimensions.toText();
            int columns = std::atoi(value.c_str());
            size_t comma = value.find(',');
            int rowsCount = comma == std::string::npos ? 1 : std::atoi(value.c_str() + comma + 1);
            count = static_cast<size_t>(std::max(0, columns) * std::max(0, rowsCount));
        } else if (const json::Value* grid = property(node, "grid_dimensions"); grid && grid->isArray() && grid->mArray.size() == 2) {
            count = static_cast<size_t>(std::max(0.0, resolve(node, grid->mArray[0].get()) ? resolve(node, grid->mArray[0].get())->number() : 0.0) * std::max(0.0, resolve(node, grid->mArray[1].get()) ? resolve(node, grid->mArray[1].get())->number() : 0.0));
        } else if (data) {
            if (auto found = data->collections.find(collection); found != data->collections.end()) {
                count = found->second.size();
            }
        }
        if (auto length = node.bound.find("#collection_length"); length != node.bound.end()) {
            count = static_cast<size_t>(std::max(0.0, length->second.toNumber()));
        }
        if (auto maximum = node.bound.find("#maximum_grid_items"); maximum != node.bound.end()) {
            count = static_cast<size_t>(std::max(0.0, maximum->second.toNumber()));
        }
        syncCollection(node, collection, count, nullptr, templateControl, depth);
        return;
    }
    if (node.type == "factory") {
        const json::Value* ids = property(node, "control_ids");
        const json::Value* named = property(node, "control_name");
        static const std::vector<UiFactoryItem> none;
        const std::vector<UiFactoryItem>* items = &none;
        if (data) {
            if (auto found = data->factories.find(node.name); found != data->factories.end()) {
                items = &found->second;
            }
        }
        syncItems(node, *items, ids, named, MaxFactoryItems, depth);
        return;
    }
    if (!factory || !factory->isObject()) {
        return;
    }
    std::string collection = text(node, "collection_name");
    if (!collection.empty() || node.bound.count("#collection_length")) {
        size_t count = 0;
        bool supplied = false;
        if (auto length = node.bound.find("#collection_length"); length != node.bound.end() && length->second.kind == UiValue::Kind::Number) {
            count = static_cast<size_t>(std::max(0.0, length->second.toNumber()));
            supplied = true;
        } else if (data) {
            if (auto found = data->collections.find(collection); found != data->collections.end()) {
                count = found->second.size();
                supplied = true;
            }
        }
        std::vector<std::string> roles;
        if (!supplied) {
            const json::Value* bag = property(node, "property_bag");
            const json::Value* ids = bag && bag->isObject() ? resolve(node, bag->get("#collection_length")) : nullptr;
            if (ids && ids->isArray()) {
                for (const std::unique_ptr<json::Value>& id : ids->mArray) {
                    if (id && id->isString() && roles.size() < MaxFactoryItems) {
                        roles.push_back(id->mString);
                    }
                }
                count = roles.size();
            }
        }
        syncCollection(node, collection, count, factory, {}, depth, roles);
        return;
    }
    const json::Value* nameValue = resolve(node, factory->get("name"));
    std::string name = nameValue && nameValue->isString() ? nameValue->mString : std::string();
    if (!data || name.empty()) {
        return;
    }
    auto found = data->factories.find(name);
    static const std::vector<UiFactoryItem> none;
    const json::Value* limit = resolve(node, factory->get("max_children_size"));
    syncItems(node, found == data->factories.end() ? none : found->second, resolve(node, factory->get("control_ids")), resolve(node, factory->get("control_name")), limit && limit->isNumber() ? static_cast<size_t>(std::max(0.0, limit->mNumber)) : MaxFactoryItems, depth);
    for (std::unique_ptr<Node>& child : node.children) {
        if (child->generated) {
            child->collection = name;
        }
    }
}

/**
 * Reads the bindings of a control into its bound properties. A binding that
 * finds nothing leaves the property as it was, so property_bag defaults and
 * the state a control keeps itself stay in place.
 */
void JsonUiRuntime::bind(Node& node)
{
    node.dataToggle = false;
    const json::Value* bindings = property(node, "bindings");
    if (!bindings || !bindings->isArray()) {
        return;
    }
    for (const std::unique_ptr<json::Value>& binding : bindings->mArray) {
        if (!binding->isObject()) {
            continue;
        }
        if (binding->get("ignored") && condition(node, binding->get("ignored"))) {
            continue;
        }
        const json::Value* typeValue = resolve(node, binding->get("binding_type"));
        std::string type = typeValue && typeValue->isString() ? typeValue->mString : "global";
        if (type == "view") {
            const json::Value* source = resolve(node, binding->get("source_property_name"));
            const json::Value* target = resolve(node, binding->get("target_property_name"));
            if (!source || !source->isString() || !target || !target->isString()) {
                continue;
            }
            Node* from = &node;
            if (const json::Value* control = resolve(node, binding->get("source_control_name")); control && control->isString() && !control->mString.empty()) {
                std::string name = control->mString.front() == '(' ? evaluate(node, control->mString).toText() : control->mString;
                const json::Value* sibling = resolve(node, binding->get("resolve_sibling_scope"));
                Node* scope = sibling && sibling->boolean(false) && node.parent ? node.parent : &node;
                from = find(*scope, name);
            }
            if (from) {
                UiLookup find = [&](const std::string& key) { return bindingLookup(*from, key, [&](const std::string& name) { return lookup(*from, name); }); };
                UiValue value = source->mString.front() == '(' ? jsonui::evaluate(source->mString, find) : lookup(*from, source->mString);
                if (source->mString.front() == '$' && value.kind == UiValue::Kind::String && !value.text.empty() && value.text.front() == '(') {
                    value = jsonui::evaluate(value.text, find);
                }
                node.bound[target->mString] = std::move(value);
            }
            continue;
        }
        // "none" bindings are never read; a property naming the binding still finds the
        // screen's value through lookup.
        if (type == "collection_details" || type == "none") {
            continue;
        }
        const json::Value* nameValue = resolve(node, binding->get("binding_name"));
        if (!nameValue || !nameValue->isString() || nameValue->mString.empty()) {
            continue;
        }
        std::string name = nameValue->mString;
        const json::Value* overrideValue = resolve(node, binding->get("binding_name_override"));
        std::string target = overrideValue && overrideValue->isString() && !overrideValue->mString.empty() ? overrideValue->mString : name;
        UiValue value;
        const json::Value* collectionValue = resolve(node, binding->get("binding_collection_name"));
        std::string collection = collectionValue && collectionValue->isString() ? collectionValue->mString : std::string();
        auto read = [&](const std::string& key) -> UiValue {
            if (type == "collection") {
                if (const UiRow* values = row(node, collection)) {
                    if (auto found = values->find(key); found != values->end()) {
                        return found->second;
                    }
                }
                return {};
            }
            if (data) {
                if (auto found = data->globals.find(key); found != data->globals.end()) {
                    return found->second;
                }
            }
            if (auto found = screenDefaults().find(key); found != screenDefaults().end()) {
                return found->second;
            }
            return {};
        };
        if (name.front() == '(') {
            auto binding = [&](const std::string& key) {
                UiValue found = read(key);
                return found.kind == UiValue::Kind::None ? lookup(node, key) : found;
            };
            UiLookup find = [&](const std::string& key) { return bindingLookup(node, key, binding); };
            value = jsonui::evaluate(name, find);
            // An expression can build a binding name, like ('#' + $dropdown_name).
            if (value.kind == UiValue::Kind::String && value.text.size() > 1 && value.text.front() == '#') {
                value = read(value.text);
                if (target == name) {
                    continue;
                }
            }
        } else if (name.front() == '#') {
            value = read(name);
        }
        if (value.kind != UiValue::Kind::None) {
            node.dataToggle = node.dataToggle || target == "#toggle_state";
            node.bound[target] = std::move(value);
        }
    }
}

/**
 * A name inside a binding expression. The game puts variables into binding
 * names as text, so a variable holding "#name" stands for that binding
 * there, unlike in "ignored" or "requires" where it is just a string.
 */
UiValue JsonUiRuntime::bindingLookup(const Node& node, const std::string& key, const UiLookup& binding) const
{
    if (key.front() == '#') {
        return binding(key);
    }
    UiValue value = lookup(node, key);
    if (key.front() == '$' && value.kind == UiValue::Kind::String && value.text.size() > 1 && value.text.front() == '#') {
        return binding(value.text);
    }
    return value;
}

void JsonUiRuntime::animate(Node& node)
{
    for (AnimTrack& track : node.anims) {
        if (track.start < 0.0 || track.finished) {
            continue;
        }
        for (int step = 0; step < MaxDepth; ++step) {
            Node probe;
            probe.vars = node.vars;
            probe.props = track.props;
            std::string type = text(probe, "anim_type");
            if (type == "flip_book" || type == "aseprite_flip_book") {
                break;
            }
            double duration = std::max(0.0, number(probe, "duration", 0.0));
            if (now - track.start < duration) {
                break;
            }
            if (type != "wait") {
                track.held = resolve(probe, probe.props.count("to") ? probe.props.at("to").value : nullptr);
                track.holding = true;
            }
            if (std::string event = text(probe, "end_event"); !event.empty()) {
                UiEvent done;
                done.kind = UiEvent::Kind::Animation;
                done.name = event;
                events.push_back(std::move(done));
            }
            if (std::string destroy = text(probe, "destroy_at_end"); !destroy.empty()) {
                for (Node* at = &node; at; at = at->parent) {
                    if (at->name == destroy) {
                        at->destroyed = true;
                        if (!at->made.empty() || at->serial) {
                            destroyedItems.emplace_back(at->made, at->serial);
                            if (destroyedItems.size() > MaxFactoryItems) {
                                destroyedItems.erase(destroyedItems.begin());
                            }
                        }
                        break;
                    }
                }
            }
            Prop next = resolveProp(probe, probe.props.count("next") ? probe.props.at("next") : Prop {});
            if (!isReference(next.value)) {
                track.finished = true;
                break;
            }
            PropMap props;
            collect(next.value->mString, next.space, props, 0);
            if (props.empty()) {
                track.finished = true;
                break;
            }
            track.props = std::move(props);
            track.start += duration;
            // An anim waiting for an event holds what the one before it ended on until then.
            probe.props = track.props;
            if (std::string event = text(probe, "play_event"); !event.empty()) {
                track.playEvent = std::move(event);
                track.start = -1.0;
                break;
            }
        }
    }
}

void JsonUiRuntime::sweep(Node& node)
{
    node.children.erase(std::remove_if(node.children.begin(), node.children.end(), [](const std::unique_ptr<Node>& child) { return child->destroyed; }), node.children.end());
    for (std::unique_ptr<Node>& child : node.children) {
        sweep(*child);
    }
}

/**
 * Reads bindings, runs animations and keeps factories in step with the data,
 * parents before children so a factory's length binding is read before it
 * makes its controls. Control is the closest button, toggle, slider or edit
 * box above node, which decides which of its state children show.
 */
void JsonUiRuntime::update(Node& node, int depth, Node* control)
{
    bind(node);
    if ((node.type == "toggle" || node.type == "dropdown") && !node.dataToggle) {
        node.bound["#toggle_state"] = UiValue::of(node.toggled);
    }
    if (control) {
        overrideState(*control, node);
    }
    syncFactories(node, depth);
    animate(node);

    node.visible = true;
    if (auto bound = node.bound.find("#visible"); bound != node.bound.end()) {
        node.visible = bound->second.truthy();
    } else if (node.props.count("visible")) {
        node.visible = flag(node, "visible", true);
    }
    node.enabled = true;
    if (auto bound = node.bound.find("#enabled"); bound != node.bound.end()) {
        node.enabled = bound->second.truthy();
    } else if (node.props.count("enabled")) {
        node.enabled = flag(node, "enabled", true);
    }
    if (control && control->parent && control != &node) {
        node.enabled = node.enabled && control->enabled;
    }
    if (control) {
        if (auto state = control->states.find(&node); state != control->states.end()) {
            node.visible = state->second;
        }
    }
    if (node.forced) {
        node.visible = true;
    }
    // Controls under a hidden one still read their bindings now and then, since a view
    // binding on a parent may be waiting on them to show it again.
    if (!node.visible && ++node.idleFrames < HiddenUpdateInterval) {
        return;
    }
    node.idleFrames = 0;
    Node* inner = control;
    if (isControl(node)) {
        chooseStates(node);
        inner = &node;
    }
    for (std::unique_ptr<Node>& child : node.children) {
        update(*child, depth + 1, inner);
    }
}

}
