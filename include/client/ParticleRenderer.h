#pragma once

#include "world/Mesher.h"
#include "world/Particles.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kestrel {

/**
 * Turns particle quads into entity quads the renderer already draws:
 * particle textures are given entity texture layers once, and every quad is
 * packed relative to the entity draw origin into the opaque or blended run
 * its material asks for.
 */
class ParticleRenderer {
public:
    /**
     * Registers the particle textures the library uses with the entity
     * texture layers from firstLayer on, reading them through the pack stack
     * in use (the server packs over the game), and never taking more than
     * maxLayers; returns the pixels to append after the existing ones.
     */
    std::vector<uint8_t> registerTextures(const world::ParticleLibrary& library, const std::vector<std::shared_ptr<const world::PackFiles>>& packs, uint32_t firstLayer, uint32_t layerSize, uint32_t maxLayers);

    void build(const std::vector<world::ParticleQuad>& quads, const std::array<double, 3>& origin, std::vector<world::ModelQuadGpu>& opaque, std::vector<world::ModelQuadGpu>& blended) const;

private:
    std::unordered_map<std::string, uint32_t> layers;
};

/**
 * The effects the client starts itself rather than on the server's word:
 * attack criticals, sprint dust, potion swirls, the flames and smoke of
 * lit blocks near the player, and the particle_effects of entity animations.
 */
class ClientParticleEmitters {
public:
    struct ActorState {
        uint64_t runtimeId = 0;
        std::array<double, 3> position {};
        float width = 0.6f;
        float height = 1.8f;
        bool sprinting = false;
        bool onGround = false;
        std::vector<uint32_t> effectColors;
    };

    struct BlockState {
        std::array<int32_t, 3> cell {};
        std::string name;
        bool lit = true;
    };

    /**
     * The swirl color of a status effect as 0xAARRGGBB, or 0 for an effect
     * id the game does not have.
     */
    static uint32_t effectColor(int32_t effectId);

    /**
     * Whether a block of that name gives off particles on its own, so the
     * client only hands those to blocks().
     */
    static bool emitsParticles(std::string_view name);

    void attacked(uint64_t targetRuntimeId, const std::array<double, 3>& targetPosition, float targetHeight, bool critical, bool enchanted, std::vector<world::ParticleSpawn>& out);
    void actors(const std::vector<ActorState>& actors, double seconds, std::vector<world::ParticleSpawn>& out);
    void blocks(const std::vector<BlockState>& nearby, double seconds, std::vector<world::ParticleSpawn>& out);

private:
    std::unordered_map<uint64_t, double> actorClock;
    double blockClock = 0.0;
};

}
