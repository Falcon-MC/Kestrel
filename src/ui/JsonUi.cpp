#include "ui/JsonUi.h"

#include "JsonUiInternal.h"

#include "util/JsonText.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>

namespace kestrel::ui {

UiValue UiValue::of(bool value)
{
    UiValue result;
    result.kind = Kind::Bool;
    result.flag = value;
    return result;
}

UiValue UiValue::of(double value)
{
    UiValue result;
    result.kind = Kind::Number;
    result.number = value;
    return result;
}

UiValue UiValue::of(std::string value)
{
    UiValue result;
    result.kind = Kind::String;
    result.text = std::move(value);
    return result;
}

UiValue UiValue::of(const char* value)
{
    return of(std::string(value ? value : ""));
}

bool UiValue::truthy() const
{
    switch (kind) {
    case Kind::Bool:
        return flag;
    case Kind::Number:
        return number != 0.0;
    case Kind::String:
        return !text.empty() && text != "false" && text != "0";
    case Kind::None:
        break;
    }
    return false;
}

double UiValue::toNumber() const
{
    switch (kind) {
    case Kind::Bool:
        return flag ? 1.0 : 0.0;
    case Kind::Number:
        return number;
    case Kind::String:
        return std::strtod(text.c_str(), nullptr);
    case Kind::None:
        break;
    }
    return 0.0;
}

std::string UiValue::toText() const
{
    switch (kind) {
    case Kind::Bool:
        return flag ? "true" : "false";
    case Kind::Number: {
        if (number == std::floor(number) && std::abs(number) < 1e15) {
            return std::to_string(static_cast<long long>(number));
        }
        return std::to_string(number);
    }
    case Kind::String:
        return text;
    case Kind::None:
        break;
    }
    return {};
}

namespace {

using jsonui::controlName;

/**
 * Sets key on an object, keeping its place when it already exists.
 */
void setKey(json::Value& object, const std::string& key, std::unique_ptr<json::Value> value)
{
    object.set(key, std::move(value));
}

void renameKey(json::Value& object, const std::string& from, const std::string& to)
{
    if (from == to) {
        return;
    }
    auto found = object.mObject.find(from);
    if (found == object.mObject.end()) {
        return;
    }
    std::unique_ptr<json::Value> value = std::move(found->second);
    object.mObject.erase(found);
    object.mObject[to] = std::move(value);
    std::replace(object.mKeys.begin(), object.mKeys.end(), from, to);
}

bool matchesWhere(const json::Value& item, const json::Value* where)
{
    if (!where || !where->isObject() || !item.isObject()) {
        return false;
    }
    for (const std::string& key : where->mKeys) {
        const json::Value* expected = where->get(key);
        const json::Value* actual = item.get(key);
        if (!actual || actual->mType != expected->mType || actual->mString != expected->mString || actual->mNumber != expected->mNumber || actual->mBoolean != expected->mBoolean) {
            return false;
        }
    }
    return true;
}

/**
 * The index of the array item a modification points at: a control by name
 * (controls hold one key each) or any item matching the "where" object.
 */
std::optional<size_t> findItem(const json::Value& array, const json::Value& modification)
{
    const json::Value* name = modification.get("control_name");
    const json::Value* where = modification.get("where");
    for (size_t i = 0; i < array.mArray.size(); ++i) {
        const json::Value& item = *array.mArray[i];
        if (name && item.isObject() && !item.mKeys.empty() && controlName(item.mKeys.front()) == controlName(name->string())) {
            return i;
        }
        if (where && matchesWhere(item, where)) {
            return i;
        }
    }
    return std::nullopt;
}

void applyModifications(json::Value& control, const json::Value& modifications)
{
    for (const std::unique_ptr<json::Value>& modification : modifications.mArray) {
        if (!modification->isObject()) {
            continue;
        }
        std::string arrayName = modification->get("array_name") ? modification->get("array_name")->string() : "controls";
        std::string operation = modification->get("operation") ? modification->get("operation")->string() : "";
        if (!control.get(arrayName)) {
            setKey(control, arrayName, json::Value::ofArray());
        }
        json::Value& array = *control.mObject[arrayName];
        if (!array.isArray()) {
            continue;
        }
        const json::Value* value = modification->get("value");
        std::vector<std::unique_ptr<json::Value>> items;
        if (value && value->isArray()) {
            for (const std::unique_ptr<json::Value>& item : value->mArray) {
                items.push_back(item->clone());
            }
        } else if (value) {
            items.push_back(value->clone());
        }
        auto insertAt = [&](size_t index) {
            for (std::unique_ptr<json::Value>& item : items) {
                array.mArray.insert(array.mArray.begin() + static_cast<std::ptrdiff_t>(index++), std::move(item));
            }
        };
        std::optional<size_t> target = findItem(array, *modification);
        if (operation == "insert_back") {
            insertAt(array.mArray.size());
        } else if (operation == "insert_front") {
            insertAt(0);
        } else if (operation == "insert_after" && target) {
            insertAt(*target + 1);
        } else if (operation == "insert_before" && target) {
            insertAt(*target);
        } else if (operation == "remove" && target) {
            array.mArray.erase(array.mArray.begin() + static_cast<std::ptrdiff_t>(*target));
        } else if (operation == "replace" && target) {
            array.mArray.erase(array.mArray.begin() + static_cast<std::ptrdiff_t>(*target));
            insertAt(*target);
        }
    }
}

/**
 * The control a pack reaches with "parent/child/grandchild": the top level
 * control, then at each step the child of that name among the controls of
 * the one before.
 */
json::Value* findPath(json::Value& base, const std::string& path)
{
    size_t slash = path.find('/');
    std::string first = path.substr(0, slash);
    json::Value* current = nullptr;
    for (const std::string& known : base.mKeys) {
        if (controlName(known) == controlName(first)) {
            current = base.mObject[known].get();
            break;
        }
    }
    while (current && slash != std::string::npos) {
        size_t next = path.find('/', slash + 1);
        std::string step = path.substr(slash + 1, next == std::string::npos ? std::string::npos : next - slash - 1);
        slash = next;
        auto controls = current->isObject() ? current->mObject.find("controls") : current->mObject.end();
        json::Value* found = nullptr;
        if (controls != current->mObject.end() && controls->second->isArray()) {
            for (const std::unique_ptr<json::Value>& item : controls->second->mArray) {
                if (item->isObject() && !item->mKeys.empty() && controlName(item->mKeys.front()) == controlName(step)) {
                    found = item->mObject[item->mKeys.front()].get();
                    break;
                }
            }
        }
        current = found;
    }
    return current && current->isObject() ? current : nullptr;
}

void mergeControl(json::Value& target, const json::Value& value)
{
    for (const std::string& property : value.mKeys) {
        if (property == "modifications") {
            applyModifications(target, *value.get(property));
        } else {
            setKey(target, property, value.get(property)->clone());
        }
    }
}

}

void JsonUi::clear()
{
    files.clear();
    indexed = false;
}

void JsonUi::addFile(const std::string& path, const std::string& text)
{
    // Packs saved from Windows editors often start with a byte order mark.
    constexpr std::string_view Bom = "\xEF\xBB\xBF";
    std::unique_ptr<json::Value> incoming = util::parseJsonObject(text.compare(0, Bom.size(), Bom) == 0 ? text.substr(Bom.size()) : text);
    if (!incoming) {
        return;
    }
    indexed = false;
    auto existing = files.find(path);
    if (existing == files.end()) {
        for (const std::string& key : incoming->mKeys) {
            json::Value* control = incoming->mObject[key].get();
            if (control && control->isObject() && control->get("modifications")) {
                control->mObject.erase("modifications");
                control->mKeys.erase(std::remove(control->mKeys.begin(), control->mKeys.end(), "modifications"), control->mKeys.end());
            }
        }
        files.emplace(path, std::move(incoming));
        return;
    }
    json::Value& base = *existing->second;
    for (const std::string& key : incoming->mKeys) {
        std::unique_ptr<json::Value>& value = incoming->mObject[key];
        if (key == "namespace") {
            if (!base.get("namespace")) {
                setKey(base, key, std::move(value));
            }
            continue;
        }
        if (key.find('/') != std::string::npos) {
            if (json::Value* target = value->isObject() ? findPath(base, key) : nullptr) {
                mergeControl(*target, *value);
            }
            continue;
        }
        std::string match;
        for (const std::string& known : base.mKeys) {
            if (controlName(known) == controlName(key)) {
                match = known;
                break;
            }
        }
        if (match.empty() || !value->isObject() || !base.get(match)->isObject()) {
            setKey(base, key, std::move(value));
            continue;
        }
        if (key.find('@') != std::string::npos) {
            renameKey(base, match, key);
            match = key;
        }
        mergeControl(*base.mObject[match], *value);
    }
}

std::vector<std::string> JsonUi::texturePaths() const
{
    std::vector<std::string> paths;
    std::function<void(const json::Value&)> walk = [&](const json::Value& value) {
        if (value.isObject()) {
            for (const std::string& key : value.mKeys) {
                const json::Value* child = value.get(key);
                if (key == "texture" && child->isString() && !child->mString.empty() && child->mString.front() != '$' && child->mString.front() != '#') {
                    paths.push_back(child->mString);
                } else if (key.find("texture") != std::string::npos && key.front() == '$' && child->isString()) {
                    paths.push_back(child->mString);
                }
                walk(*child);
            }
        } else if (value.isArray()) {
            for (const std::unique_ptr<json::Value>& item : value.mArray) {
                walk(*item);
            }
        }
    };
    for (const auto& [path, file] : files) {
        walk(*file);
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

void JsonUi::index() const
{
    if (indexed) {
        return;
    }
    indexed = true;
    if (!platform) {
        // What a retail build of the game sets for a Windows desktop with mouse and keyboard; anything else
        // it checks, like $touch or $education_edition, reads as false.
        auto values = json::Value::ofObject();
        values->set("$desktop_screen", json::Value::ofBoolean(true));
        values->set("$win10_edition", json::Value::ofBoolean(true));
        values->set("$is_pc", json::Value::ofBoolean(true));
        values->set("$is_publish", json::Value::ofBoolean(true));
        values->set("$pocket_screen", json::Value::ofBoolean(false));
        values->set("$touch", json::Value::ofBoolean(false));
        platform = std::move(values);
    }
    controls.clear();
    globals.clear();
    for (const auto& [path, file] : files) {
        if (path.find("_global_variables") != std::string::npos) {
            globals.push_back(file.get());
            continue;
        }
        const json::Value* ns = file->get("namespace");
        if (!ns || !ns->isString()) {
            continue;
        }
        auto& space = controls[ns->mString];
        for (const std::string& key : file->mKeys) {
            if (key == "namespace") {
                continue;
            }
            space[std::string(controlName(key))] = { key, file->get(key), nullptr };
        }
    }
    for (auto& [name, space] : controls) {
        for (auto& [control, entry] : space) {
            entry.space = &name;
        }
    }
}

JsonUi::Control JsonUi::find(std::string_view space, std::string_view name) const
{
    index();
    auto ns = controls.find(std::string(space));
    if (ns == controls.end()) {
        return {};
    }
    auto found = ns->second.find(std::string(name));
    return found == ns->second.end() ? Control {} : found->second;
}

bool JsonUi::has(std::string_view reference) const
{
    size_t dot = reference.find('.');
    return dot != std::string_view::npos && find(reference.substr(0, dot), reference.substr(dot + 1)).value;
}

const json::Value* JsonUi::globalVariable(const std::string& name) const
{
    index();
    for (auto it = globals.rbegin(); it != globals.rend(); ++it) {
        if (const json::Value* value = (*it)->get(name)) {
            return value;
        }
    }
    return platform->get(name);
}

}
