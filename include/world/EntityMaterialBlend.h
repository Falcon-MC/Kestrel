#pragma once

#include "Core/Json/Json.h"
#include "world/BlockAssets.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace kestrel::world {

class EntityMaterialBlendLibrary {
public:
    void parse(const json::Value& document)
    {
        const json::Value* materials = document.get("materials");
        if (!materials || !materials->isObject()) {
            return;
        }
        for (const std::string& key : materials->mKeys) {
            const json::Value& value = *materials->get(key);
            if (!value.isObject() || key.size() > 256 || entries.size() >= 8192) {
                continue;
            }
            size_t colon = key.find(':');
            Entry entry;
            if (colon != std::string::npos) {
                entry.parent = key.substr(colon + 1);
            }
            for (const char* field : { "states", "+states", "-states" }) {
                const json::Value* states = value.get(field);
                if (!states || !states->isArray()) {
                    continue;
                }
                if (std::string_view(field) == "states") {
                    entry.blending = false;
                }
                for (const auto& state : states->mArray) {
                    if (state->isString() && state->mString == "Blending") {
                        entry.blending = std::string_view(field) != "-states";
                    }
                }
            }
            for (const char* field : { "defines", "+defines", "-defines" }) {
                const json::Value* defines = value.get(field);
                if (!defines || !defines->isArray()) {
                    continue;
                }
                if (std::string_view(field) == "defines") {
                    entry.emissive = false;
                    entry.multitexture = false;
                }
                for (const auto& define : defines->mArray) {
                    if (define->isString() && define->mString == "USE_EMISSIVE") {
                        entry.emissive = std::string_view(field) != "-defines";
                    }
                    if (define->isString() && define->mString == "USE_MULTITEXTURE") {
                        entry.multitexture = std::string_view(field) != "-defines";
                    }
                }
            }
            for (const auto& [field, output] : { std::pair { "blendSrc", &entry.source }, std::pair { "blendDst", &entry.destination } }) {
                if (const json::Value* factor = value.get(field); factor && factor->isString()) {
                    *output = factor->mString;
                }
            }
            entries[key.substr(0, colon)] = std::move(entry);
        }
    }

    std::optional<EntityBlend> find(const std::string& name) const
    {
        const Entry* chain[64];
        size_t count = 0;
        std::string current = name;
        while (!current.empty()) {
            auto found = entries.find(current);
            if (found == entries.end()) {
                break;
            }
            if (count == 64) {
                return {};
            }
            for (size_t index = 0; index < count; ++index) {
                if (chain[index] == &found->second) {
                    return {};
                }
            }
            chain[count++] = &found->second;
            current = found->second.parent;
        }
        if (!count) {
            return {};
        }
        bool blending = false;
        std::string source, destination;
        while (count) {
            const Entry& entry = *chain[--count];
            if (entry.blending) {
                blending = *entry.blending;
            }
            if (!entry.source.empty()) {
                source = entry.source;
            }
            if (!entry.destination.empty()) {
                destination = entry.destination;
            }
        }
        if (!blending) {
            return EntityBlend::Opaque;
        }
        return source == "One" && destination == "One" ? EntityBlend::Additive : EntityBlend::Blend;
    }

    std::optional<bool> emissive(const std::string& name) const
    {
        return feature(name, &Entry::emissive);
    }

    std::optional<bool> multitexture(const std::string& name) const
    {
        return feature(name, &Entry::multitexture);
    }

private:
    struct Entry {
        std::string parent;
        std::optional<bool> blending;
        std::optional<bool> emissive;
        std::optional<bool> multitexture;
        std::string source;
        std::string destination;
    };

    std::optional<bool> feature(const std::string& name, std::optional<bool> Entry::* member) const
    {
        std::string current = name;
        for (size_t depth = 0; depth < 64; ++depth) {
            auto found = entries.find(current);
            if (found == entries.end()) {
                return depth ? std::optional<bool>(false) : std::nullopt;
            }
            if (found->second.*member) {
                return found->second.*member;
            }
            current = found->second.parent;
            if (current.empty()) {
                return false;
            }
        }
        return {};
    }

    std::unordered_map<std::string, Entry> entries;
};

}
