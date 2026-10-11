#pragma once

#include "client/BlockEntityText.h"

#include <optional>

namespace kestrel {

struct SignEditorRequest {
    uint64_t serial = 0;
    std::array<int32_t, 3> cell {};
    int dimension = 0;
    bool front = true;
    bool hanging = false;
    std::string texture;
    SignText text;
};

struct SignEditorAnswer {
    uint64_t serial = 0;
    std::string text;
};

inline std::optional<Tag> editedSignData(Tag data, bool front, const std::string& text)
{
    if (!data.isCompound() || (data.getString("id") != "Sign" && data.getString("id") != "HangingSign") || data.getBool("IsWaxed") || text.size() > 4096 || text.find('\0') != std::string::npos) {
        return std::nullopt;
    }
    const char* side = front ? "FrontText" : "BackText";
    Tag* face = data.get(side);
    if (face && face->isCompound()) {
        face->putString("Text", text);
        face->putString("FilteredText", "");
    } else if (front && !data.contains("FrontText") && !data.contains("BackText")) {
        data.putString("Text", text);
    } else {
        return std::nullopt;
    }
    data.putLong("LockedForEditingBy", -1);
    return data;
}

inline std::string signEditorTexture(std::string_view name, bool hanging)
{
    constexpr std::string_view woods[] = { "oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "darkoak", "mangrove", "cherry", "bamboo", "crimson", "warped", "pale_oak", "poplar" };
    if (name.starts_with("minecraft:")) name.remove_prefix(10);
    std::string wood = "oak";
    for (auto candidate : woods) {
        if (name.starts_with(std::string(candidate) + "_")) wood = candidate;
    }
    if (wood == "dark_oak") wood = "darkoak";
    if (hanging) return "textures/ui/hanging_sign" + (wood == "oak" ? std::string() : "_" + wood);
    if (wood == "oak") return "textures/ui/sign";
    if (wood == "bamboo" || wood == "cherry" || wood == "mangrove" || wood == "pale_oak" || wood == "poplar") return "textures/ui/" + wood + "_sign";
    return "textures/ui/sign_" + wood;
}

}
