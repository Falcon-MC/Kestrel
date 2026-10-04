#include "client/Client.h"

#include "Core/NBT/Tag.h"
#include "render/Renderer.h"
#include "world/PackSource.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace kestrel {

namespace {

constexpr double BlockRefreshSeconds = 0.5;
constexpr int32_t BlockRadius = 16;
constexpr size_t MaxEmittingBlocks = 2048;
constexpr size_t MaxQueuedSpawns = 1024;
constexpr size_t MaxQueuedAttacks = 16;
constexpr double MaxFrameSeconds = 0.25;
constexpr int SprintingFlag = 3;
constexpr float DefaultActorWidth = 0.6f;
constexpr float DefaultActorHeight = 1.8f;

/**
 * The world particles see during one frame: the blocks the session shared
 * around the player, the light the mesher solved and where every entity is
 * drawn.
 */
class FrameWorld : public world::ParticleWorld {
public:
    const NearbyBlocks* blocks = nullptr;
    std::function<uint8_t(int32_t, int32_t, int32_t)> lightOf;
    std::unordered_map<uint64_t, std::array<double, 3>> actors;

    std::vector<std::array<double, 6>> collisionBoxes(const std::array<double, 3>& low, const std::array<double, 3>& high) const override
    {
        std::vector<std::array<double, 6>> boxes;
        if (blocks) {
            blocks->collisionBoxes(low, high, boxes);
        }
        return boxes;
    }

    std::string blockName(int32_t x, int32_t y, int32_t z) const override
    {
        return blocks ? blocks->name(x, y, z) : std::string("minecraft:air");
    }

    uint8_t light(int32_t x, int32_t y, int32_t z) const override
    {
        return lightOf ? lightOf(x, y, z) : uint8_t(0xFF);
    }

    std::optional<std::array<double, 3>> actorPosition(uint64_t runtimeId) const override
    {
        auto found = actors.find(runtimeId);
        if (found == actors.end()) {
            return std::nullopt;
        }
        return found->second;
    }
};

/**
 * Whether a block state burns: false when its lit state is off or its
 * extinguished state is on, true for blocks with neither.
 */
bool litState(const Tag* states)
{
    if (!states || !states->isCompound()) {
        return true;
    }
    const std::vector<std::string>& keys = states->getKeys();
    const std::vector<Tag>& values = states->getValues();
    for (size_t index = 0; index < keys.size() && index < values.size(); ++index) {
        const Tag& value = values[index];
        int32_t number = value.getType() == Tag::Type::Byte ? value.asByte() : value.getType() == Tag::Type::Int ? value.asInt() : 0;
        if (keys[index] == "lit") {
            return number != 0;
        }
        if (keys[index] == "extinguished") {
            return number == 0;
        }
    }
    return true;
}

}

/**
 * Loads every effect of the game and the server packs, and lays the textures
 * they sample onto entity texture layers from firstLayer on, appended to
 * entityPixels; returns how many layers that took.
 */
uint32_t Client::loadParticles(const std::vector<std::shared_ptr<const world::PackFiles>>& packs, uint32_t firstLayer, std::vector<uint8_t>& entityPixels)
{
    clearParticles();
    particleLibrary = world::ParticleLibrary();
    std::filesystem::path root = world::PackSource::locateVanilla();
    if (root.empty()) {
        return 0;
    }
    world::PackSource game(root);
    game.setOverlays(packs);
    particleLibrary.load(game, packs);
    uint32_t capacity = EntityTexturePageLayers * EntityTexturePages;
    uint32_t room = capacity > firstLayer ? capacity - firstLayer : 0;
    std::vector<uint8_t> pixels = particleRenderer.registerTextures(particleLibrary, packs, firstLayer, world::EntityTextureSize, room);
    entityPixels.insert(entityPixels.end(), pixels.begin(), pixels.end());
    return static_cast<uint32_t>(pixels.size() / (size_t(world::EntityTextureSize) * world::EntityTextureSize * 4));
}

/**
 * Takes the effects and attacks the session queued. A new dimension starts
 * without the particles of the old one.
 */
void Client::takeSessionParticles(const SessionSnapshot& snapshot)
{
    std::vector<world::ParticleSpawn> spawns = session.takeParticles();
    std::vector<uint64_t> attacks = session.takeAttacks();
    if (snapshot.state != SessionState::Joined) {
        return;
    }
    if (snapshot.dimension != particleDimension || snapshot.changingDimension) {
        particleDimension = snapshot.dimension;
        clearParticles();
    }
    nearbyBlocks = snapshot.nearby;
    for (world::ParticleSpawn& spawn : spawns) {
        if (particleSpawns.size() >= MaxQueuedSpawns) {
            break;
        }
        particleSpawns.push_back(std::move(spawn));
    }
    for (uint64_t target : attacks) {
        if (particleAttacks.size() < MaxQueuedAttacks) {
            particleAttacks.push_back(target);
        }
    }
}

void Client::clearParticles()
{
    particleSystem.clear();
    particleSpawns.clear();
    particleAttacks.clear();
    particleBlocks.clear();
    nearbyBlocks.reset();
    particleBlocksAt = 0.0;
}

/**
 * Lists, twice a second, the blocks within BlockRadius of the camera that
 * give off particles on their own. Whether a block value does is worked out
 * once per value and kept until the block assets or ids change.
 */
void Client::refreshParticleBlocks(double now)
{
    if (now - particleBlocksAt < BlockRefreshSeconds) {
        return;
    }
    particleBlocksAt = now;
    particleBlocks.clear();
    const NearbyBlocks* area = nearbyBlocks.get();
    if (!area || !area->assets || area->dimension != timeState.dimension) {
        return;
    }
    std::pair<const void*, const void*> key { area->assets.get(), area->ids.sequential.get() };
    if (key != particleBlockKindsKey) {
        particleBlockKinds.clear();
        particleBlockKindsKey = key;
    }
    const world::BlockAssets& blocks = *area->assets;
    auto wanted = [&](uint32_t value) {
        if (value == world::ImplicitAir) {
            return false;
        }
        auto found = particleBlockKinds.find(value);
        if (found != particleBlockKinds.end()) {
            return found->second;
        }
        bool emits = ClientParticleEmitters::emitsParticles(blocks.blockName(value, area->ids.hashed, area->ids.sequential.get()));
        particleBlockKinds.emplace(value, emits);
        return emits;
    };
    std::vector<std::pair<std::array<int32_t, 3>, uint32_t>> cells;
    area->find({ camera.x(), camera.y(), camera.z() }, BlockRadius, wanted, cells);
    for (const auto& [cell, value] : cells) {
        if (particleBlocks.size() >= MaxEmittingBlocks) {
            break;
        }
        ClientParticleEmitters::BlockState block;
        block.cell = cell;
        block.name = blocks.blockName(value, area->ids.hashed, area->ids.sequential.get());
        block.lit = litState(blocks.blockStates(value, area->ids.hashed, area->ids.sequential.get()));
        particleBlocks.push_back(std::move(block));
    }
}

/**
 * Runs the particles for one frame: starts what the server and the client's
 * own emitters asked for, moves every particle through the world around the
 * player, then packs them facing the camera into the entity runs, the alpha
 * tested ones with the opaque entities and the others with the blended ones.
 * They go after the entities were lit, so entity lighting never touches them.
 */
void Client::appendParticles(double deltaSeconds, const std::array<int32_t, 3>& origin, std::vector<world::ModelQuadGpu>& opaque, std::vector<world::ModelQuadGpu>& blended)
{
    if (!worldShown || particleLibrary.size() == 0) {
        particleSpawns.clear();
        particleAttacks.clear();
        return;
    }
    double seconds = std::clamp(deltaSeconds, 0.0, MaxFrameSeconds);
    refreshParticleBlocks(secondsNow());
    std::vector<world::ParticleSpawn> spawns = std::move(particleSpawns);
    particleSpawns.clear();

    FrameWorld frame;
    std::vector<ClientParticleEmitters::ActorState> states;
    for (const ActorView& actor : actorViews) {
        if (actor.runtimeId == LocalActorId || actor.runtimeId == localRuntime) {
            continue;
        }
        frame.actors[actor.runtimeId] = { actor.x, actor.y, actor.z };
        ClientParticleEmitters::ActorState state;
        state.runtimeId = actor.runtimeId;
        state.position = { actor.x, actor.y, actor.z };
        state.width = (actor.width > 0.0f ? actor.width : DefaultActorWidth) * actor.scale;
        state.height = actor.height > 0.0f ? actor.height : DefaultActorHeight * actor.scale;
        state.sprinting = ((actor.flags[0] >> SprintingFlag) & 1) != 0;
        state.onGround = actor.onGround;
        if (actor.effectColor != 0) {
            state.effectColors.push_back(actor.effectColor);
        }
        states.push_back(std::move(state));
    }
    if (playerView.active) {
        frame.actors[localRuntime] = playerView.current;
        ClientParticleEmitters::ActorState self;
        self.runtimeId = localRuntime;
        self.position = playerView.current;
        self.sprinting = playerView.sprinting && perspective != PerspectiveFirst;
        self.onGround = playerView.onGround;
        double now = secondsNow();
        for (const HudEffect& effect : hudState.effects) {
            if (effect.expires >= 0.0 && effect.expires < now) {
                continue;
            }
            if (uint32_t color = ClientParticleEmitters::effectColor(effect.id)) {
                self.effectColors.push_back(color);
            }
        }
        states.push_back(std::move(self));
    }

    bool critical = playerView.active && !playerView.onGround && playerView.current[1] < playerView.previous[1] && !playerView.swimming && !playerView.flying;
    const HudItem& held = hudState.inventory[size_t(std::clamp(hudState.selectedSlot, 0, 8))];
    for (uint64_t target : particleAttacks) {
        auto actor = std::find_if(actorViews.begin(), actorViews.end(), [target](const ActorView& view) {
            return view.runtimeId == target;
        });
        if (actor == actorViews.end()) {
            continue;
        }
        float height = actor->height > 0.0f ? actor->height : DefaultActorHeight * actor->scale;
        particleEmitters.attacked(target, { actor->x, actor->y, actor->z }, height, critical, held.enchanted, spawns);
    }
    particleAttacks.clear();
    particleEmitters.actors(states, seconds, spawns);
    particleEmitters.blocks(particleBlocks, seconds, spawns);
    for (const world::ParticleSpawn& spawn : spawns) {
        particleSystem.spawn(spawn);
    }

    frame.blocks = nearbyBlocks && nearbyBlocks->dimension == timeState.dimension ? nearbyBlocks.get() : nullptr;
    frame.lightOf = [this](int32_t x, int32_t y, int32_t z) {
        return lightAt(x + 0.5, y + 0.5, z + 0.5);
    };
    particleSystem.tick(seconds, frame);

    world::ParticleCamera view;
    view.position = { camera.x(), camera.y(), camera.z() };
    view.forward = camera.forward();
    if (perspective == PerspectiveFront) {
        view.forward = { -view.forward[0], -view.forward[1], -view.forward[2] };
    }
    std::vector<world::ParticleQuad> quads;
    particleSystem.collect(view, quads);
    particleRenderer.build(quads, { double(origin[0]), double(origin[1]), double(origin[2]) }, opaque, blended);
}

}
