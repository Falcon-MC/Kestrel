#include "BlockRules.h"

#include "util/Text.h"

#include <algorithm>

namespace kestrel::world::rules {

using util::contains;
using util::endsWith;

std::pair<Face, bool> orientFace(Face face, Axis axis)
{
    switch (axis) {
    case Axis::X:
        switch (face) {
        case Face::West:
            return { Face::Down, false };
        case Face::East:
            return { Face::Up, false };
        case Face::Down:
            return { Face::East, true };
        case Face::Up:
            return { Face::West, true };
        default:
            return { face, true };
        }
    case Axis::Z:
        switch (face) {
        case Face::North:
            return { Face::Down, false };
        case Face::South:
            return { Face::Up, false };
        case Face::Down:
            return { Face::South, true };
        case Face::Up:
            return { Face::North, true };
        default:
            return { face, true };
        }
    case Axis::Y:
        break;
    }
    return { face, false };
}

const char* faceKey(Face face)
{
    switch (face) {
    case Face::West:
        return "west";
    case Face::East:
        return "east";
    case Face::Down:
        return "down";
    case Face::Up:
        return "up";
    case Face::North:
        return "north";
    case Face::South:
        return "south";
    }
    return "side";
}

bool isHorizontal(Face face)
{
    return face != Face::Up && face != Face::Down;
}

std::optional<Face> faceFromName(const std::string& value)
{
    static const std::pair<const char*, Face> names[] = {
        { "down", Face::Down },
        { "up", Face::Up },
        { "north", Face::North },
        { "south", Face::South },
        { "west", Face::West },
        { "east", Face::East },
    };
    for (const auto& [name, face] : names) {
        if (value == name) {
            return face;
        }
    }
    return std::nullopt;
}

Face opposite(Face face)
{
    switch (face) {
    case Face::West:
        return Face::East;
    case Face::East:
        return Face::West;
    case Face::Down:
        return Face::Up;
    case Face::Up:
        return Face::Down;
    case Face::North:
        return Face::South;
    case Face::South:
        return Face::North;
    }
    return face;
}

int horizontalIndex(Face face)
{
    switch (face) {
    case Face::North:
        return 0;
    case Face::East:
        return 1;
    case Face::South:
        return 2;
    case Face::West:
        return 3;
    default:
        return -1;
    }
}

Face horizontalFace(int index)
{
    static constexpr Face order[] = { Face::North, Face::East, Face::South, Face::West };
    return order[((index % 4) + 4) % 4];
}

std::string explicitFaceKey(const json::Value& textures, Face face)
{
    if (const json::Value* key = textures.get(faceKey(face))) {
        return key->string();
    }
    if (isHorizontal(face)) {
        if (const json::Value* side = textures.get("side")) {
            return side->string();
        }
    }
    return {};
}

std::optional<Face> frontFace(const json::Value& textures)
{
    for (Face face : { Face::South, Face::North, Face::East, Face::West }) {
        const json::Value* key = textures.get(faceKey(face));
        if (!key || !key->isString()) {
            continue;
        }
        const std::string& value = key->string();
        if (contains(value, "front") || endsWith(value, "_face") || endsWith(value, "_top")) {
            return face;
        }
    }
    return std::nullopt;
}

Face sourceForFacing(Face world, Face reference, Face facing)
{
    if (isHorizontal(facing)) {
        if (!isHorizontal(world)) {
            return world;
        }
        int steps = horizontalIndex(facing) - horizontalIndex(reference);
        return horizontalFace(horizontalIndex(world) - steps);
    }
    Face back = opposite(reference);
    if (facing == Face::Up) {
        if (world == Face::Up) {
            return reference;
        }
        if (world == Face::Down) {
            return back;
        }
        if (world == reference) {
            return Face::Down;
        }
        if (world == back) {
            return Face::Up;
        }
        return world;
    }
    if (world == Face::Down) {
        return reference;
    }
    if (world == Face::Up) {
        return back;
    }
    if (world == back) {
        return Face::Down;
    }
    if (world == reference) {
        return Face::Up;
    }
    return world;
}

std::string resolveTextureKey(const json::Value* textures, Face face, Axis axis, std::optional<Face> facing, bool& rotate)
{
    rotate = false;
    if (!textures) {
        return {};
    }
    if (textures->isString()) {
        return textures->string();
    }
    if (!textures->isObject()) {
        return {};
    }
    std::optional<Face> reference = facing ? frontFace(*textures) : std::nullopt;
    if (reference) {
        std::string frontKey = explicitFaceKey(*textures, *reference);
        if (!isHorizontal(*facing) && contains(frontKey, "front_horizontal")) {
            if (face == *facing) {
                for (Face candidate : { Face::East, Face::West, Face::North, Face::South }) {
                    std::string key = explicitFaceKey(*textures, candidate);
                    if (contains(key, "front_vertical")) {
                        return key;
                    }
                }
                return frontKey;
            }
            return explicitFaceKey(*textures, Face::Up);
        }
        return explicitFaceKey(*textures, sourceForFacing(face, *reference, *facing));
    }
    auto [source, rotateUv] = orientFace(face, axis);
    rotate = rotateUv;
    return explicitFaceKey(*textures, source);
}

size_t terrainVariantCount(const json::Value& entry)
{
    const json::Value* textures = entry.get("textures");
    if (!textures) {
        return 0;
    }
    return textures->isArray() ? textures->mArray.size() : 1;
}

std::string terrainPath(const json::Value& entry, size_t index)
{
    const json::Value* textures = entry.get("textures");
    if (!textures) {
        return {};
    }
    const json::Value* first = textures;
    if (textures->isArray()) {
        if (textures->mArray.empty()) {
            return {};
        }
        first = textures->mArray[std::min(index, textures->mArray.size() - 1)].get();
    }
    if (first->isString()) {
        return first->string();
    }
    if (const json::Value* path = first->get("path")) {
        return path->string();
    }
    return {};
}

std::optional<Flipbook> parseFlipbook(const json::Value& item)
{
    const json::Value* texture = item.get("flipbook_texture");
    const json::Value* tile = item.get("atlas_tile");
    if (!texture || !tile || !texture->isString() || !tile->isString()) {
        return std::nullopt;
    }
    Flipbook flipbook;
    flipbook.texturePath = texture->string();
    flipbook.atlasTile = tile->string();
    if (const json::Value* index = item.get("atlas_index")) {
        flipbook.atlasIndex = std::max(0, index->integer(0));
    }
    if (const json::Value* ticks = item.get("ticks_per_frame")) {
        flipbook.ticksPerFrame = static_cast<uint32_t>(std::max(1, ticks->integer(1)));
    }
    if (const json::Value* blend = item.get("blend_frames")) {
        flipbook.blendFrames = blend->boolean(true);
    }
    if (const json::Value* frames = item.get("frames"); frames && frames->isArray()) {
        for (const auto& frame : frames->mArray) {
            flipbook.frames.push_back(static_cast<uint32_t>(std::max(0, frame->integer(0))));
        }
    }
    return flipbook;
}

}
