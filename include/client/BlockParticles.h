#pragma once

#include "client/Session.h"
#include "world/Mesher.h"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace kestrel {

/**
 * The terrain chips of broken and mined blocks, run the way the vanilla
 * minecraft:block_destruct particle effect describes them: small billboards
 * cut from a quarter of the block texture, thrown up and out, pulled down by
 * gravity and drag, bouncing a little off the blocks around them.
 */
class BlockParticles {
public:
    void spawn(const ParticleBurst& burst);
    void update(double now);
    void clear();

    /**
     * Appends one camera facing quad per particle, placed in 1/256 block
     * around origin.
     */
    void append(const std::array<int32_t, 3>& origin, const std::array<double, 3>& camera, std::vector<world::ModelQuadGpu>& out) const;

private:
    struct Particle {
        std::array<int32_t, 3> cell {};
        std::array<float, 3> position {};
        std::array<float, 3> velocity {};
        float age = 0.0f;
        float lifetime = 0.0f;
        float size = 0.0f;
        std::array<float, 2> uv {};
        uint32_t material = 0;
        uint32_t tint = 0;
        bool grounded = false;
        std::shared_ptr<const std::vector<world::CollisionBox>> obstacles;
    };

    float uniform(float low, float high);
    void emit(const ParticleBurst& burst, const std::array<float, 3>& position, float velocityScalar);
    void move(Particle& particle, float seconds) const;

    std::vector<Particle> particles;
    std::mt19937 random { std::random_device {}() };
    double lastUpdate = 0.0;
};

}
