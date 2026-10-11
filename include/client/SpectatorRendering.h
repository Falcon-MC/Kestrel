#pragma once

#include "client/motion/MotionMath.h"
#include "world/EntityAnimation.h"

#include <cctype>
#include <span>

namespace kestrel {

inline constexpr uint32_t SpectatorHeadQuadFlag = 1u << 15;

inline bool localSpectatorRendering(bool localPlayer, int32_t gameType)
{
    return localPlayer && gameType == motion::GameSpectator;
}

inline std::vector<uint8_t> spectatorHeadBones(std::span<const world::EntityBone> bones)
{
    std::vector<uint8_t> visible(bones.size(), 0), state(bones.size(), 0);
    std::vector<size_t> path;
    for (size_t bone = 0; bone < bones.size(); ++bone) {
        std::string name = bones[bone].name;
        for (char& c : name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (name == "head" || name == "hat") {
            visible[bone] = 1;
            state[bone] = 2;
        }
    }
    for (size_t bone = 0; bone < bones.size(); ++bone) {
        if (state[bone] == 2) {
            continue;
        }
        path.clear();
        int32_t parent = static_cast<int32_t>(bone);
        while (parent >= 0 && size_t(parent) < bones.size() && state[size_t(parent)] == 0) {
            state[size_t(parent)] = 1;
            path.push_back(size_t(parent));
            parent = bones[size_t(parent)].parent;
        }
        uint8_t inherited = parent >= 0 && size_t(parent) < bones.size() && state[size_t(parent)] == 2
            ? visible[size_t(parent)] : 0;
        for (size_t part : path) {
            visible[part] = inherited;
            state[part] = 2;
        }
    }
    return visible;
}

}
