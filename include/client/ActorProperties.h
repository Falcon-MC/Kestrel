#pragma once

#include "Core/NBT/Tag.h"
#include "Protocol/Types/EntityProperties.h"
#include "world/MolangScript.h"

#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kestrel {

struct ActorProperty {
    std::string name;
    int type = 0;
    std::vector<std::string> values;
};

struct ActorPropertySchema {
    std::string identifier;
    std::vector<ActorProperty> properties;

    static std::optional<ActorPropertySchema> read(const Tag& data)
    {
        if (!data.isCompound() || !data.contains("type", Tag::Type::String) || !data.contains("properties", Tag::Type::List)) {
            return {};
        }
        ActorPropertySchema schema;
        schema.identifier = data.getString("type");
        const Tag& list = *data.get("properties");
        if (schema.identifier.empty() || schema.identifier.size() > 256
            || list.getListType() != Tag::Type::Compound || list.size() > 256) {
            return {};
        }
        std::unordered_set<std::string> names;
        size_t bytes = schema.identifier.size();
        for (const Tag& entry : list.getList()) {
            if (!entry.contains("name", Tag::Type::String) || !entry.contains("type", Tag::Type::Int)) {
                return {};
            }
            ActorProperty property;
            property.name = entry.getString("name");
            property.type = entry.getInt("type");
            if (property.name.empty() || property.name.size() > 256 || !names.insert(property.name).second
                || property.type < 0 || property.type > 3) {
                return {};
            }
            bytes += property.name.size();
            if (property.type == 3) {
                const Tag* values = entry.get("enum");
                if (!values || !values->isList() || values->getListType() != Tag::Type::String
                    || values->isEmpty() || values->size() > 256) {
                    return {};
                }
                for (const Tag& value : values->getList()) {
                    if (value.asString().size() > 256) {
                        return {};
                    }
                    bytes += value.asString().size();
                    property.values.push_back(value.asString());
                }
            }
            if (bytes > 65536) {
                return {};
            }
            schema.properties.push_back(std::move(property));
        }
        return schema;
    }

    void apply(const EntityProperties& update, std::unordered_map<std::string, double>& output) const
    {
        for (const auto& entry : update.mIntProperties) {
            if (entry.mIndex < 0 || size_t(entry.mIndex) >= properties.size()) {
                continue;
            }
            const ActorProperty& property = properties[size_t(entry.mIndex)];
            if (property.type == 1) {
                continue;
            }
            double value = entry.mValue;
            if (property.type == 2) {
                if (entry.mValue != 0 && entry.mValue != 1) {
                    continue;
                }
            } else if (property.type == 3) {
                if (entry.mValue < 0 || size_t(entry.mValue) >= property.values.size()) {
                    continue;
                }
                value = world::molang::internString(property.values[size_t(entry.mValue)]);
            }
            output[property.name] = value;
        }
        for (const auto& entry : update.mFloatProperties) {
            if (entry.mIndex < 0 || size_t(entry.mIndex) >= properties.size() || !std::isfinite(entry.mValue)) {
                continue;
            }
            const ActorProperty& property = properties[size_t(entry.mIndex)];
            if (property.type == 1) {
                output[property.name] = entry.mValue;
            }
        }
    }
};

}
