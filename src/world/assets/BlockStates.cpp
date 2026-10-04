#include "world/assets/BlockRules.h"

#include "BlockLightJson.h"

#include <algorithm>
#include <climits>
#include <unordered_map>

namespace kestrel::world::rules {

std::string stateString(const Tag& states, const std::string& key)
{
    const Tag* value = states.get(key);
    if (!value || value->getType() != Tag::Type::String) {
        return {};
    }
    return value->asString();
}

std::optional<int32_t> stateInt(const Tag& states, const std::string& key)
{
    const Tag* value = states.get(key);
    if (!value) {
        return std::nullopt;
    }
    switch (value->getType()) {
    case Tag::Type::Byte:
        return static_cast<int32_t>(value->asByte());
    case Tag::Type::Short:
        return static_cast<int32_t>(value->asShort());
    case Tag::Type::Int:
        return value->asInt();
    default:
        return std::nullopt;
    }
}

Axis stateAxis(const Tag& states)
{
    std::string axis = stateString(states, "pillar_axis");
    if (axis.empty()) {
        axis = stateString(states, "axis");
    }
    if (axis == "x") {
        return Axis::X;
    }
    if (axis == "z") {
        return Axis::Z;
    }
    return Axis::Y;
}

std::optional<Face> stateFacing(const Tag& states)
{
    for (const char* key : { "minecraft:cardinal_direction", "minecraft:facing_direction" }) {
        if (std::optional<Face> face = faceFromName(stateString(states, key))) {
            return face;
        }
    }
    if (std::optional<int32_t> facing = stateInt(states, "facing_direction")) {
        static constexpr Face byIndex[] = { Face::Down, Face::Up, Face::North, Face::South, Face::West, Face::East };
        if (*facing >= 0 && *facing < 6) {
            return byIndex[*facing];
        }
    }
    if (std::optional<int32_t> direction = stateInt(states, "direction")) {
        static constexpr Face byIndex[] = { Face::South, Face::West, Face::North, Face::East };
        if (*direction >= 0 && *direction < 4) {
            return byIndex[*direction];
        }
    }
    return std::nullopt;
}

bool stateMatches(const Tag& states, const json::Value& expected)
{
    for (const std::string& key : expected.mKeys) {
        const json::Value& value = *expected.mObject.at(key);
        const Tag* actual = states.get(key);
        if (!actual) {
            return false;
        }
        if (value.isString()) {
            if (actual->getType() != Tag::Type::String || actual->asString() != value.mString) {
                return false;
            }
        } else if (value.mType == json::Value::Type::Boolean) {
            if (stateInt(states, key).value_or(-1) != (value.mBoolean ? 1 : 0)) {
                return false;
            }
        } else if (stateInt(states, key).value_or(INT32_MIN) != value.integer()) {
            return false;
        }
    }
    return true;
}

float tagNumber(const Tag* value, float fallback)
{
    if (!value) {
        return fallback;
    }
    switch (value->getType()) {
    case Tag::Type::Byte:
        return value->asByte();
    case Tag::Type::Short:
        return value->asShort();
    case Tag::Type::Int:
        return static_cast<float>(value->asInt());
    case Tag::Type::Long:
        return static_cast<float>(value->asLong());
    case Tag::Type::Float:
        return value->asFloat();
    case Tag::Type::Double:
        return static_cast<float>(value->asDouble());
    default:
        return fallback;
    }
}

void collectComponents(const Tag* components, std::map<std::string, const Tag*>& out)
{
    if (!components || components->getType() != Tag::Type::Compound) {
        return;
    }
    const std::vector<std::string>& keys = components->getKeys();
    const std::vector<Tag>& values = components->getValues();
    for (size_t i = 0; i < keys.size(); ++i) {
        out[keys[i]] = &values[i];
    }
}

void applyBlockLight(const BlockRegistry& registry, std::vector<BlockVisual>& visuals)
{
    std::string text(reinterpret_cast<const char*>(KestrelBlockLightData::kBlockLightJson), KestrelBlockLightData::kBlockLightJsonSize);
    std::unique_ptr<json::Value> root = json::parse(text);
    std::unordered_map<std::string, const json::Value*> byName;
    if (const json::Value* blocks = root ? root->get("blocks") : nullptr; blocks && blocks->isArray()) {
        for (const std::unique_ptr<json::Value>& block : blocks->mArray) {
            if (const json::Value* name = block->get("name"); name && name->isString()) {
                byName.emplace(name->mString, block.get());
            }
        }
    }
    auto level = [](const json::Value& entry, const char* key) {
        const json::Value* value = entry.get(key);
        return static_cast<uint8_t>(std::clamp(value ? value->integer() : 0, 0, 15));
    };
    for (size_t i = 0; i < registry.records().size() && i < visuals.size(); ++i) {
        const BlockRecord& record = registry.records()[i];
        BlockVisual& visual = visuals[i];
        auto found = byName.find(record.name);
        if (found == byName.end()) {
            visual.lightFilter = (visual.flags & FlagOccludesFullFace) ? 15 : 0;
            continue;
        }
        const json::Value* chosen = found->second;
        if (const json::Value* overrides = chosen->get("overrides"); overrides && overrides->isArray()) {
            for (const std::unique_ptr<json::Value>& entry : overrides->mArray) {
                const json::Value* states = entry->get("states");
                if (states && states->isObject() && stateMatches(record.states, *states)) {
                    chosen = entry.get();
                    break;
                }
            }
        }
        visual.lightEmission = level(*chosen, "emission");
        visual.lightFilter = level(*chosen, "filter");
    }
}

std::vector<Tag> enumerateCustomStates(const Tag& definition)
{
    std::vector<std::pair<std::string, std::vector<Tag>>> properties;
    if (const Tag* list = definition.get("properties"); list && list->getType() == Tag::Type::List) {
        for (const Tag& property : list->getList()) {
            const Tag* name = property.get("name");
            const Tag* values = property.get("enum");
            if (name && values && name->getType() == Tag::Type::String && values->getType() == Tag::Type::List && !values->getList().empty()) {
                properties.emplace_back(name->asString(), values->getList());
            }
        }
    }
    if (const Tag* traits = definition.get("traits"); traits && traits->getType() == Tag::Type::List) {
        auto strings = [](std::initializer_list<const char*> values) {
            std::vector<Tag> tags;
            for (const char* value : values) {
                tags.push_back(Tag::ofString(value));
            }
            return tags;
        };
        for (const Tag& trait : traits->getList()) {
            const Tag* name = trait.get("name");
            const Tag* enabled = trait.get("enabled_states");
            if (!name || name->getType() != Tag::Type::String) {
                continue;
            }
            auto isEnabled = [&](const char* state) {
                const Tag* flag = enabled ? enabled->get(state) : nullptr;
                return flag && tagNumber(flag) != 0.0f;
            };
            std::string traitName = name->asString();
            if (traitName == "minecraft:connection" && isEnabled("cardinal_connections")) {
                for (const char* direction : { "north", "south", "west", "east" }) {
                    properties.emplace_back(std::string("minecraft:connection_") + direction, std::vector<Tag> { Tag::ofByte(0), Tag::ofByte(1) });
                }
            } else if (traitName == "minecraft:multi_block" && isEnabled("multi_block_part")) {
                int32_t parts = static_cast<int32_t>(tagNumber(trait.get("parts")));
                if (parts >= 2 && parts <= 4) {
                    std::vector<Tag> values;
                    for (int32_t part = 0; part < parts; ++part) {
                        values.push_back(Tag::ofInt(part));
                    }
                    properties.emplace_back("minecraft:multi_block_part", std::move(values));
                }
            } else if (traitName == "minecraft:placement_direction") {
                if (isEnabled("cardinal_direction") || isEnabled("corner_and_cardinal_direction")) {
                    properties.emplace_back("minecraft:cardinal_direction", strings({ "south", "north", "west", "east" }));
                }
                if (isEnabled("facing_direction")) {
                    properties.emplace_back("minecraft:facing_direction", strings({ "down", "up", "south", "north", "west", "east" }));
                }
                if (isEnabled("corner_and_cardinal_direction")) {
                    properties.emplace_back("minecraft:corner", strings({ "none", "inner_left", "inner_right", "outer_left", "outer_right" }));
                }
            } else if (traitName == "minecraft:placement_position") {
                if (isEnabled("block_face")) {
                    properties.emplace_back("minecraft:block_face", strings({ "down", "up", "south", "north", "west", "east" }));
                }
                if (isEnabled("vertical_half")) {
                    properties.emplace_back("minecraft:vertical_half", strings({ "bottom", "top" }));
                }
            }
        }
    }

    std::vector<Tag> states { Tag::ofCompound() };
    for (const auto& [name, values] : properties) {
        std::vector<Tag> next;
        next.reserve(states.size() * values.size());
        for (const Tag& state : states) {
            for (const Tag& value : values) {
                Tag expanded = state;
                expanded.put(name, value);
                next.push_back(std::move(expanded));
            }
        }
        states = std::move(next);
    }
    return states;
}

}
