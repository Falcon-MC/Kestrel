#include "client/BlockParticles.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

// The numbers of resource_packs/vanilla/particles/block_destruct.json.
constexpr float Gravity = 9.8f;
constexpr float LinearDrag = 0.5f;
constexpr float CollisionRadius = 0.1f;
constexpr float CollisionDrag = 5.0f;
constexpr float Restitution = 0.1f;
constexpr float MaxInitialSpeed = 4.0f;
constexpr float MinSize = 0.0375f;
constexpr float TextureShare = 0.25f;

// A block without a destruction_particles component throws 100 chips.
constexpr int DestroyParticles = 100;
constexpr float DestroySpeed = 1.0f;
constexpr float CrackSpeed = 0.2f;
constexpr float CrackInset = 0.1f;
constexpr size_t MaxParticles = 4096;
constexpr float MaxStep = 1.0f / 60.0f;
constexpr uint32_t FullSkyLight = 0xF0F0F0F0u;
constexpr uint32_t UnshadedFace = 4;

float boxMin(const world::CollisionBox& box, size_t axis)
{
    return axis == 0 ? box.minX : axis == 1 ? box.minY : box.minZ;
}

float boxMax(const world::CollisionBox& box, size_t axis)
{
    return axis == 0 ? box.maxX : axis == 1 ? box.maxY : box.maxZ;
}

int16_t roundToShort(double value)
{
    return static_cast<int16_t>(std::clamp(std::lround(value), -32768L, 32767L));
}

}

float BlockParticles::uniform(float low, float high)
{
    return std::uniform_real_distribution<float>(low, high)(random);
}

void BlockParticles::emit(const ParticleBurst& burst, const std::array<float, 3>& position, float velocityScalar)
{
    if (particles.size() >= MaxParticles) {
        return;
    }
    Particle particle;
    particle.cell = burst.cell;
    particle.position = position;
    std::array<float, 3> direction { uniform(-1.0f, 1.0f), 1.0f, uniform(-1.0f, 1.0f) };
    float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    float speed = uniform(0.0f, MaxInitialSpeed) * velocityScalar;
    for (size_t axis = 0; axis < 3; ++axis) {
        particle.velocity[axis] = direction[axis] / length * speed;
    }
    particle.lifetime = 0.2f / (uniform(0.0f, 1.0f) * 0.9f + 0.1f);
    float first = uniform(0.0f, 1.0f);
    float second = uniform(0.0f, 1.0f);
    particle.size = first * MinSize + MinSize;
    particle.uv = { first * 3.0f * TextureShare, second * 3.0f * TextureShare };
    particle.material = burst.material;
    particle.tint = burst.tint;
    particle.obstacles = burst.obstacles;
    particles.push_back(std::move(particle));
}

/**
 * A broken block fills its shape with chips; a mined one knocks a single
 * chip off just outside the face being hit, placed the way the game's crack
 * particles are.
 */
void BlockParticles::spawn(const ParticleBurst& burst)
{
    const world::CollisionBox& shape = burst.shape;
    if (burst.kind == ParticleBurst::Kind::Destroy) {
        for (int i = 0; i < DestroyParticles; ++i) {
            emit(burst, { uniform(shape.minX, shape.maxX), uniform(shape.minY, shape.maxY), uniform(shape.minZ, shape.maxZ) }, DestroySpeed);
        }
        return;
    }
    std::array<float, 3> position;
    for (size_t axis = 0; axis < 3; ++axis) {
        float low = boxMin(shape, axis);
        float span = std::max(boxMax(shape, axis) - low - CrackInset * 2.0f, 0.0f);
        position[axis] = low + CrackInset + uniform(0.0f, 1.0f) * span;
    }
    switch (burst.face) {
    case 0:
        position[1] = shape.minY - CrackInset;
        break;
    case 1:
        position[1] = shape.maxY + CrackInset;
        break;
    case 2:
        position[2] = shape.minZ - CrackInset;
        break;
    case 3:
        position[2] = shape.maxZ + CrackInset;
        break;
    case 4:
        position[0] = shape.minX - CrackInset;
        break;
    default:
        position[0] = shape.maxX + CrackInset;
        break;
    }
    emit(burst, position, CrackSpeed);
}

/**
 * Moves a particle axis by axis, vertical first, stopping it at the blocks
 * it runs into and bouncing it back off them with a tenth of its speed.
 */
void BlockParticles::move(Particle& particle, float seconds) const
{
    bool landed = false;
    for (size_t axis : { size_t(1), size_t(0), size_t(2) }) {
        float wanted = particle.velocity[axis] * seconds;
        float allowed = wanted;
        if (particle.obstacles && wanted != 0.0f) {
            for (const world::CollisionBox& box : *particle.obstacles) {
                bool overlaps = true;
                for (size_t other = 0; other < 3 && overlaps; ++other) {
                    if (other != axis) {
                        overlaps = particle.position[other] + CollisionRadius > boxMin(box, other) && particle.position[other] - CollisionRadius < boxMax(box, other);
                    }
                }
                if (!overlaps) {
                    continue;
                }
                float low = particle.position[axis] - CollisionRadius;
                float high = particle.position[axis] + CollisionRadius;
                if (allowed > 0.0f && high <= boxMin(box, axis)) {
                    allowed = std::min(allowed, boxMin(box, axis) - high);
                } else if (allowed < 0.0f && low >= boxMax(box, axis)) {
                    allowed = std::max(allowed, boxMax(box, axis) - low);
                }
            }
        }
        if (allowed != wanted) {
            landed = landed || (axis == 1 && wanted < 0.0f);
            particle.velocity[axis] = -particle.velocity[axis] * Restitution;
        }
        particle.position[axis] += allowed;
    }
    particle.grounded = landed;
}

void BlockParticles::update(double now)
{
    float elapsed = lastUpdate > 0.0 ? static_cast<float>(std::clamp(now - lastUpdate, 0.0, 0.1)) : 0.0f;
    lastUpdate = now;
    while (elapsed > 0.0f) {
        float step = std::min(elapsed, MaxStep);
        elapsed -= step;
        for (Particle& particle : particles) {
            particle.age += step;
            particle.velocity[1] -= Gravity * step;
            for (float& component : particle.velocity) {
                component -= component * LinearDrag * step;
            }
            if (particle.grounded) {
                float horizontal = std::hypot(particle.velocity[0], particle.velocity[2]);
                float slowed = std::max(horizontal - CollisionDrag * step, 0.0f);
                float factor = horizontal > 0.0f ? slowed / horizontal : 0.0f;
                particle.velocity[0] *= factor;
                particle.velocity[2] *= factor;
            }
            move(particle, step);
        }
        std::erase_if(particles, [](const Particle& particle) {
            return particle.age >= particle.lifetime;
        });
    }
}

void BlockParticles::clear()
{
    particles.clear();
    lastUpdate = 0.0;
}

void BlockParticles::append(const std::array<int32_t, 3>& origin, const std::array<double, 3>& camera, std::vector<world::ModelQuadGpu>& out) const
{
    for (const Particle& particle : particles) {
        std::array<double, 3> world;
        std::array<double, 3> local;
        for (size_t axis = 0; axis < 3; ++axis) {
            world[axis] = particle.cell[axis] + double(particle.position[axis]);
            local[axis] = double(particle.cell[axis] - origin[axis]) + particle.position[axis];
        }
        std::array<double, 3> facing { camera[0] - world[0], camera[1] - world[1], camera[2] - world[2] };
        double distance = std::sqrt(facing[0] * facing[0] + facing[1] * facing[1] + facing[2] * facing[2]);
        if (distance < 1.0e-4 || distance > 128.0) {
            continue;
        }
        std::array<double, 3> right { facing[2], 0.0, -facing[0] };
        double rightLength = std::hypot(right[0], right[2]);
        if (rightLength < 1.0e-6) {
            right = { 1.0, 0.0, 0.0 };
            rightLength = 1.0;
        }
        for (double& component : right) {
            component /= rightLength;
        }
        std::array<double, 3> up {
            (facing[1] * right[2] - facing[2] * right[1]) / distance,
            (facing[2] * right[0] - facing[0] * right[2]) / distance,
            (facing[0] * right[1] - facing[1] * right[0]) / distance,
        };
        double size = particle.size;
        const double signs[4][2] = { { -1.0, 1.0 }, { 1.0, 1.0 }, { 1.0, -1.0 }, { -1.0, -1.0 } };
        world::ModelQuadGpu gpu;
        std::array<int16_t, 12> positions {};
        for (size_t corner = 0; corner < 4; ++corner) {
            for (size_t axis = 0; axis < 3; ++axis) {
                double offset = (right[axis] * signs[corner][0] + up[axis] * signs[corner][1]) * size;
                positions[corner * 3 + axis] = roundToShort((local[axis] + offset) * 256.0);
            }
        }
        for (size_t word = 0; word < 6; ++word) {
            gpu.words[word] = uint32_t(uint16_t(positions[word * 2])) | (uint32_t(uint16_t(positions[word * 2 + 1])) << 16);
        }
        float u0 = particle.uv[0];
        float v0 = particle.uv[1];
        float u1 = u0 + TextureShare;
        float v1 = v0 + TextureShare;
        const float uvs[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
        for (size_t corner = 0; corner < 4; ++corner) {
            gpu.words[6 + corner] = uint32_t(uvs[corner][0] * 4096.0f) | (uint32_t(uvs[corner][1] * 4096.0f) << 16);
        }
        gpu.words[10] = particle.material;
        gpu.words[11] = UnshadedFace | (particle.tint << 8);
        gpu.words[12] = FullSkyLight;
        out.push_back(gpu);
    }
}

}
