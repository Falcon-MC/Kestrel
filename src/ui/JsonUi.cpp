#include "ui/JsonUi.h"

#include "JsonUiInternal.h"

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

bool sameValue(const json::Value& a, const json::Value& b)
{
    if (a.mType != b.mType) {
        return false;
    }
    switch (a.mType) {
    case json::Value::Type::Null:
        return true;
    case json::Value::Type::Boolean:
        return a.mBoolean == b.mBoolean;
    case json::Value::Type::Number:
        return a.mNumber == b.mNumber;
    case json::Value::Type::String:
        return a.mString == b.mString;
    case json::Value::Type::Array:
        if (a.mArray.size() != b.mArray.size()) {
            return false;
        }
        for (size_t i = 0; i < a.mArray.size(); ++i) {
            if (!sameValue(*a.mArray[i], *b.mArray[i])) {
                return false;
            }
        }
        return true;
    case json::Value::Type::Object:
        if (a.mObject.size() != b.mObject.size()) {
            return false;
        }
        for (const auto& [key, value] : a.mObject) {
            const json::Value* other = b.get(key);
            if (!other || !sameValue(*value, *other)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

/**
 * jsoncpp's asString: text as is, flags and numbers spelled out, anything
 * else empty.
 */
std::string nativeString(const json::Value* value)
{
    if (!value) {
        return {};
    }
    switch (value->mType) {
    case json::Value::Type::String:
        return value->mString;
    case json::Value::Type::Boolean:
        return value->mBoolean ? "true" : "false";
    case json::Value::Type::Number:
        if (value->mInteger || (value->mNumber == std::floor(value->mNumber) && std::abs(value->mNumber) < 1e15)) {
            return std::to_string(static_cast<long long>(value->mNumber));
        }
        return std::to_string(value->mNumber);
    default:
        return {};
    }
}

/**
 * The element of a modified array a condition names, among the elements it
 * held before any modification: by control_name the object whose first key,
 * before its '@', is that name; by an object the element sharing any member
 * with it; by an array the element it contains; anything else, or nothing,
 * the first element.
 */
std::optional<size_t> findOriginal(const std::vector<const json::Value*>& original, const json::Value* name, const json::Value* condition)
{
    if (name && name->isString() && !name->mString.empty()) {
        for (size_t i = 0; i < original.size(); ++i) {
            const json::Value& element = *original[i];
            if (element.isObject() && !element.mKeys.empty() && controlName(element.mKeys.front()) == name->mString) {
                return i;
            }
        }
        return std::nullopt;
    }
    if (condition && condition->isArray()) {
        for (size_t i = 0; i < original.size(); ++i) {
            if (original[i]->mType == json::Value::Type::Null) {
                continue;
            }
            for (const std::unique_ptr<json::Value>& candidate : condition->mArray) {
                if (sameValue(*candidate, *original[i])) {
                    return i;
                }
            }
        }
        return std::nullopt;
    }
    if (condition && condition->isObject()) {
        for (size_t i = 0; i < original.size(); ++i) {
            const json::Value& element = *original[i];
            if (!element.isObject()) {
                continue;
            }
            for (const std::string& key : condition->mKeys) {
                const json::Value* actual = element.get(key);
                if (actual && sameValue(*actual, *condition->get(key))) {
                    return i;
                }
            }
        }
        return std::nullopt;
    }
    if (original.empty()) {
        return std::nullopt;
    }
    return 0;
}

/**
 * One array the modifications of a control edit: its elements before any of
 * them ran, and the current order of the originals still kept and the values
 * inserted, so every later modification still finds elements by what the
 * array held at first.
 */
struct ModifiedArray {
    std::string name;
    std::unique_ptr<json::Value> before;
    std::vector<const json::Value*> original;
    std::vector<std::pair<std::optional<size_t>, const json::Value*>> slots;

    std::optional<size_t> slotOf(const json::Value* name, const json::Value* condition) const
    {
        std::optional<size_t> index = findOriginal(original, name, condition);
        if (!index) {
            return std::nullopt;
        }
        for (size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].first == index) {
                return i;
            }
        }
        return std::nullopt;
    }
};

void applyModification(ModifiedArray& target, const std::string& operation, const json::Value& modification)
{
    const json::Value* value = modification.get("value");
    if (value && value->mType == json::Value::Type::Null) {
        value = nullptr;
    }
    std::vector<std::pair<std::optional<size_t>, const json::Value*>> added;
    if (value && value->isArray()) {
        for (const std::unique_ptr<json::Value>& item : value->mArray) {
            added.emplace_back(std::nullopt, item.get());
        }
    } else if (value) {
        added.emplace_back(std::nullopt, value);
    }
    const json::Value* name = modification.get("control_name");
    const json::Value* where = modification.get("where");
    auto& slots = target.slots;
    auto insert = [&](size_t at) {
        slots.insert(slots.begin() + static_cast<std::ptrdiff_t>(at), added.begin(), added.end());
    };
    bool needsValue = operation.rfind("insert_", 0) == 0 || operation == "replace";
    if (needsValue && !value) {
        return;
    }
    if (operation == "insert_back") {
        insert(slots.size());
        return;
    }
    if (operation == "insert_front") {
        insert(0);
        return;
    }
    std::optional<size_t> at = target.slotOf(name, where);
    if (!at) {
        return;
    }
    if (operation == "replace") {
        slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(*at));
        insert(*at);
    } else if (operation == "insert_after") {
        insert(*at + 1);
    } else if (operation == "insert_before") {
        insert(*at);
    } else if (operation == "remove") {
        slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(*at));
    } else if (operation == "move_front" || operation == "move_back") {
        auto moved = slots[*at];
        slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(*at));
        slots.insert(operation == "move_front" ? slots.begin() : slots.end(), moved);
    } else if (operation == "move_after" || operation == "move_before" || operation == "swap") {
        const json::Value* targetName = modification.get("target_control");
        std::optional<size_t> other = targetName && targetName->isString()
            ? target.slotOf(targetName, nullptr)
            : target.slotOf(nullptr, modification.get("target"));
        if (!other || *other == *at) {
            return;
        }
        if (operation == "swap") {
            std::swap(slots[*at], slots[*other]);
            return;
        }
        auto moved = slots[*at];
        slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(*at));
        size_t to = (*other > *at ? *other - 1 : *other) + (operation == "move_after" ? 1 : 0);
        slots.insert(slots.begin() + static_cast<std::ptrdiff_t>(to), moved);
    }
}

bool knownOperation(const std::string& operation)
{
    static const char* const Operations[] = {
        "insert_back", "insert_front", "insert_after", "insert_before", "move_back", "move_front",
        "move_after", "move_before", "swap", "remove", "replace",
    };
    for (const char* known : Operations) {
        if (operation == known) {
            return true;
        }
    }
    return false;
}

/**
 * Runs a control's modifications the way the game's UIModification does:
 * each one names its array (controls when it names a control), finds its
 * element among the ones the array held before the first modification, and
 * the arrays are written back once all ran, an emptied one other than
 * controls becoming null.
 */
void applyModifications(json::Value& control, const json::Value& modifications)
{
    if (!modifications.isArray()) {
        return;
    }
    std::vector<ModifiedArray> targets;
    for (const std::unique_ptr<json::Value>& modification : modifications.mArray) {
        if (!modification->isObject()) {
            continue;
        }
        std::string arrayName = nativeString(modification->get("array_name"));
        if (arrayName.empty() && modification->get("control_name") && modification->get("control_name")->isString()) {
            arrayName = "controls";
        }
        std::string operation = nativeString(modification->get("operation"));
        if (arrayName.empty() || !knownOperation(operation)) {
            continue;
        }
        auto found = std::find_if(targets.begin(), targets.end(), [&](const ModifiedArray& target) {
            return target.name == arrayName;
        });
        if (found == targets.end()) {
            ModifiedArray target;
            target.name = arrayName;
            const json::Value* current = control.get(arrayName);
            target.before = current && current->isArray() ? current->clone() : json::Value::ofArray();
            for (size_t i = 0; i < target.before->mArray.size(); ++i) {
                target.original.push_back(target.before->mArray[i].get());
                target.slots.emplace_back(i, target.before->mArray[i].get());
            }
            targets.push_back(std::move(target));
            found = targets.end() - 1;
        }
        applyModification(*found, operation, *modification);
    }
    for (ModifiedArray& target : targets) {
        std::unique_ptr<json::Value> values = json::Value::ofArray();
        for (const auto& [index, value] : target.slots) {
            values->push(value->clone());
        }
        if (values->mArray.empty() && target.name != "controls") {
            setKey(control, target.name, json::Value::ofNull());
        } else {
            setKey(control, target.name, std::move(values));
        }
    }
}

/**
 * A pack layer's value over the one below: objects merge member by member,
 * anything else replaces.
 */
void mergeValue(json::Value& object, const std::string& key, const json::Value& value)
{
    json::Value* old = object.isObject() ? const_cast<json::Value*>(object.get(key)) : nullptr;
    if (old && old->isObject() && value.isObject()) {
        for (const std::string& member : value.mKeys) {
            mergeValue(*old, member, *value.get(member));
        }
        return;
    }
    setKey(object, key, value.clone());
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

/**
 * The variables a retail, full game, desktop build of the game sets in code
 * before any definition is read, the false ones included, since a variable
 * nothing sets reads as its own name.
 */
std::unique_ptr<json::Value> platformVariables()
{
#ifdef __APPLE__
    constexpr bool Mac = true;
#else
    constexpr bool Mac = false;
#endif
    static const std::pair<const char*, bool> Flags[] = {
        { "desktop_screen", true },
        { "pocket_screen", false },
        { "touch", false },
        { "is_pc", true },
        { "win10_edition", !Mac },
        { "microsoft_os", !Mac },
        { "ms_platform", !Mac },
        { "osx_edition", Mac },
        { "apple_os", Mac },
        { "is_desktop", true },
        { "mouse", true },
        { "is_publish", true },
        { "test_infrastructure_disabled", true },
        { "new_video_settings", true },
        { "is_improve_input_response_platform_supported", true },
        { "is_xboxlive_enabled", true },
        { "is_realms_enabled", true },
        { "is_seeds_enabled", true },
        { "is_creative_enabled", true },
        { "is_multiplayer_enabled", true },
        { "is_packs_enabled", true },
        { "is_server_enabled", true },
        { "is_store_enabled", true },
        { "file_picking_supported", true },
        { "supports_clipboard_set", true },
        { "supports_add_friend", true },
        { "supports_xbl_achievements", true },
        { "pre_release", false },
        { "beta_build", false },
        { "is_preview_app", false },
        { "trial", false },
        { "education_edition", false },
        { "store_disabled", false },
        { "creator_build", false },
        { "pocket_edition", false },
        { "console_edition", false },
        { "is_console", false },
        { "game_pad", false },
        { "can_splitscreen", false },
        { "is_secondary_client", false },
        { "requires_xbl_signin_to_play", false },
        { "is_editor_mode_enabled", false },
        { "can_quit", true },
        { "world_archive_support", true },
        { "is_dynamic_textures_platform_supported", true },
        { "is_pregame", false },
        { "screen_transitions_enabled", false },
        { "use_normalized_font_size", false },
        { "image_picking_not_supported", false },
        { "vibration_supported", false },
        { "supports_share", false },
        { "hide_xbox_live_icon", false },
        { "disable_gamertag_controls", false },
        { "multiplayer_requires_live_gold", false },
        { "device_must_be_removed_for_xbl_signin", false },
        { "is_low_memory_device", false },
        { "ignore_3rd_party_servers", false },
        { "ignore_add_servers", false },
        { "is_on_3p_server", false },
        { "is_editor_playtest_roundtrip", false },
        { "edu_save_to_cloud_on", false },
        { "edu_save_to_cloud_general_toggle_on", false },
        { "built_with_ore_ui_docs_and_tests", false },
        { "build_platform_UWP", false },
        { "google_os", false },
        { "is_ios", false },
        { "is_android", false },
        { "is_chromebook", false },
        { "fire_tv", false },
        { "nx_os", false },
        { "is_ps4", false },
        { "is_ps5", false },
        { "xbox_one", false },
        { "thirdpartyconsole", false },
        { "is_settopbox", false },
        { "is_win10_arm", false },
        { "is_windows_10_mobile", false },
        { "is_mobile_vr", false },
        { "gear_vr", false },
        { "oculus_rift", false },
        { "psvr", false },
        { "is_holographic", false },
        { "supports_hand_controllers", false },
        { "is_living_room_mode", false },
        { "is_reality_mode", false },
    };
    auto values = json::Value::ofObject();
    for (const auto& [name, value] : Flags) {
        values->set(std::string("$") + name, json::Value::ofBoolean(value));
    }
    auto size = [](bool vertical) {
        auto pair = json::Value::ofArray();
        pair->push(vertical ? json::Value::ofString("100%") : json::Value::ofInteger(0));
        pair->push(vertical ? json::Value::ofInteger(0) : json::Value::ofString("100%"));
        return pair;
    };
    values->set("$top_vertical_safezone_size", size(true));
    values->set("$bottom_vertical_safezone_size", size(true));
    values->set("$left_horizontal_safezone_size", size(false));
    values->set("$right_horizontal_safezone_size", size(false));
    return values;
}

void mergeControl(json::Value& target, const json::Value& value)
{
    for (const std::string& property : value.mKeys) {
        if (property == "modifications") {
            continue;
        }
        if (property == "controls") {
            setKey(target, property, value.get(property)->clone());
            continue;
        }
        mergeValue(target, property, *value.get(property));
    }
    if (const json::Value* modifications = value.get("modifications")) {
        applyModifications(target, *modifications);
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
    std::unique_ptr<json::Value> incoming = jsonui::readUiJson(text);
    if (!incoming || !incoming->isObject()) {
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
        if (match.empty() || !base.get(match)->isObject()) {
            setKey(base, key, std::move(value));
            continue;
        }
        if (!value->isObject()) {
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
        platform = platformVariables();
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
    if (const json::Value* value = platform->get(name)) {
        return value;
    }
    for (auto it = globals.rbegin(); it != globals.rend(); ++it) {
        if (const json::Value* value = (*it)->get(name)) {
            return value;
        }
    }
    return nullptr;
}

}
