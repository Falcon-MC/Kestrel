#include "SessionData.h"

#include "Protocol/Packets/LevelEventPacket.h"
#include "Protocol/Packets/PlayerAuthInputPacket.h"
#include "world/BlockBreaking.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

using session::EyeHeight;

constexpr double BreakReach = 6.0;
constexpr int32_t DestroyDelayTicks = 5;
constexpr uint32_t HitSoundTicks = 4;
constexpr double LocalBreakMemory = 1.0;
constexpr double BreakAnswerWait = 1.0;
constexpr int32_t ObstacleRadius = 2;
constexpr size_t MaxPendingBursts = 256;
constexpr int32_t SurvivalMode = 0;
constexpr int32_t CreativeMode = 1;
constexpr int32_t BreakBlockAction = 2;
constexpr int16_t AquaAffinityEnchantment = 8;
constexpr int16_t EfficiencyEnchantment = 15;
constexpr int32_t HasteEffect = 3;
constexpr int32_t MiningFatigueEffect = 4;
constexpr int32_t ConduitPowerEffect = 26;
constexpr uint8_t WaterMedium = 1;

// Network faces run down, up, north, south, west, east; block visuals keep theirs by axis.
constexpr world::Face VisualFaces[6] = {
    world::Face::NegativeY,
    world::Face::PositiveY,
    world::Face::NegativeZ,
    world::Face::PositiveZ,
    world::Face::NegativeX,
    world::Face::PositiveX,
};

std::array<int32_t, 3> cellOf(const Vector3f& position)
{
    return { int32_t(std::floor(position.x)), int32_t(std::floor(position.y)), int32_t(std::floor(position.z)) };
}

int32_t enchantmentLevel(const ItemStack& stack, int16_t id)
{
    if (stack.isAir() || !stack.mTag.isCompound()) {
        return 0;
    }
    const Tag* list = stack.mTag.get("ench");
    if (!list || !list->isList()) {
        return 0;
    }
    for (const Tag& entry : list->getList()) {
        const Tag* entryId = entry.isCompound() ? entry.get("id") : nullptr;
        const Tag* level = entry.isCompound() ? entry.get("lvl") : nullptr;
        if (entryId && level && entryId->getType() == Tag::Type::Short && entryId->asShort() == id && level->getType() == Tag::Type::Short) {
            return level->asShort();
        }
    }
    return 0;
}

world::CollisionBox unitBox()
{
    return { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
}

}

void Session::setAttackHeld(bool held)
{
    attackHeld = held;
}

std::vector<ParticleBurst> Session::takeParticleBursts()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<ParticleBurst> bursts = std::move(pendingBursts);
    pendingBursts.clear();
    return bursts;
}

uint32_t Session::blockAt(int32_t x, int32_t y, int32_t z, uint32_t layer)
{
    std::shared_ptr<const world::SubChunk> sub = world.store().subChunk({ current.dimension, x >> 4, y >> 4, z >> 4 });
    return sub ? sub->runtimeId(layer, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15)) : world::ImplicitAir;
}

/**
 * The collision boxes of a block relative to its cell, with fences, walls
 * and panes joined to their neighbours. Blocks the collision table does not
 * know count as full cubes unless a model draws them.
 */
std::vector<world::CollisionBox> Session::shapeBoxes(uint32_t value, int32_t x, int32_t y, int32_t z)
{
    std::vector<world::CollisionBox> boxes;
    if (!assets || value == world::ImplicitAir) {
        return boxes;
    }
    const world::BlockCollisions& table = world::BlockCollisions::shared();
    const world::CollisionState* state = table.find(assets->stateHash(value, ids.hashed, ids.sequential.get()));
    if (!state) {
        const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
        if ((visual.flags & world::FlagAir) || visual.hasModel()) {
            return boxes;
        }
        state = table.fullBlock();
    }
    world::BlockCollisions::Lookup lookup = [this](int32_t cx, int32_t cy, int32_t cz) {
        return motionCell(cx, cy, cz).primary;
    };
    table.boxes(*state, x, y, z, lookup, boxes);
    for (world::CollisionBox& box : boxes) {
        box.minX -= float(x);
        box.minY -= float(y);
        box.minZ -= float(z);
        box.maxX -= float(x);
        box.maxY -= float(y);
        box.maxZ -= float(z);
    }
    return boxes;
}

/**
 * The single box the game outlines and aims at for a block, relative to its
 * cell: the bounds of its collision shape kept inside the cell, or of its
 * model when it has no collision, and at least a sixteenth of a block thick.
 */
world::CollisionBox Session::selectionBox(uint32_t value, int32_t x, int32_t y, int32_t z)
{
    world::CollisionBox bounds = unitBox();
    std::vector<world::CollisionBox> boxes = shapeBoxes(value, x, y, z);
    const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
    if (!boxes.empty()) {
        bounds = boxes.front();
        for (const world::CollisionBox& box : boxes) {
            bounds.minX = std::min(bounds.minX, box.minX);
            bounds.minY = std::min(bounds.minY, box.minY);
            bounds.minZ = std::min(bounds.minZ, box.minZ);
            bounds.maxX = std::max(bounds.maxX, box.maxX);
            bounds.maxY = std::max(bounds.maxY, box.maxY);
            bounds.maxZ = std::max(bounds.maxZ, box.maxZ);
        }
    } else if (visual.hasModel() && visual.modelTemplate < assets->modelTemplates().size()) {
        const world::ModelTemplate& modelTemplate = assets->modelTemplates()[visual.modelTemplate];
        std::array<int32_t, 3> low { 256, 256, 256 };
        std::array<int32_t, 3> high { 0, 0, 0 };
        for (uint32_t q = 0; q < modelTemplate.quadCount; ++q) {
            for (const std::array<int16_t, 3>& corner : assets->modelQuads()[modelTemplate.quadStart + q].positions) {
                int32_t px = corner[0] - 128;
                int32_t pz = corner[2] - 128;
                for (uint32_t turn = 0; turn < (visual.variant & 3); ++turn) {
                    int32_t rotated = -pz;
                    pz = px;
                    px = rotated;
                }
                std::array<int32_t, 3> point { px + 128, corner[1], pz + 128 };
                for (size_t axis = 0; axis < 3; ++axis) {
                    low[axis] = std::min(low[axis], point[axis]);
                    high[axis] = std::max(high[axis], point[axis]);
                }
            }
        }
        if (modelTemplate.quadCount > 0) {
            bounds = { low[0] / 256.0f, low[1] / 256.0f, low[2] / 256.0f, high[0] / 256.0f, high[1] / 256.0f, high[2] / 256.0f };
        }
    }
    constexpr float Thinnest = 1.0f / 16.0f;
    auto fit = [&](float& low, float& high) {
        low = std::clamp(low, 0.0f, 1.0f);
        high = std::clamp(high, 0.0f, 1.0f);
        if (high - low < Thinnest) {
            high = std::min(low + Thinnest, 1.0f);
            low = high - Thinnest;
        }
    };
    fit(bounds.minX, bounds.maxX);
    fit(bounds.minY, bounds.maxY);
    fit(bounds.minZ, bounds.maxZ);
    return bounds;
}

bool Session::locallyBroken(const std::array<int32_t, 3>& cell) const
{
    if (breaking.active && breaking.cell == cell) {
        return true;
    }
    auto found = recentBreaks.find(cell);
    return found != recentBreaks.end() && secondsNow() - found->second < LocalBreakMemory;
}

/**
 * Queues the particles of a block for the render thread, with the texture of
 * the face being mined (the side for a broken block), its biome tint and the
 * blocks around it for the particles to land on.
 */
void Session::emitBurst(ParticleBurst::Kind kind, const std::array<int32_t, 3>& cell, uint32_t value, int32_t face)
{
    if (!assets || value == world::ImplicitAir) {
        return;
    }
    const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
    if ((visual.flags & world::FlagAir) || visual.liquid) {
        return;
    }
    world::Face side = kind == ParticleBurst::Kind::Crack ? VisualFaces[std::clamp(face, 0, 5)] : world::Face::NegativeZ;
    uint32_t materialId = visual.faces[size_t(side)];
    if (visual.hasModel() && visual.modelTemplate < assets->modelTemplates().size()) {
        const world::ModelTemplate& modelTemplate = assets->modelTemplates()[visual.modelTemplate];
        if (modelTemplate.quadCount > 0) {
            materialId = assets->modelQuads()[modelTemplate.quadStart].material;
        }
    }
    const std::vector<world::Material>& materials = assets->materials();
    if (materialId >= materials.size()) {
        return;
    }
    const world::Material& material = materials[materialId];

    ParticleBurst burst;
    burst.kind = kind;
    burst.cell = cell;
    burst.face = face;
    burst.material = material.gpuWord();
    if (material.tintKind() != world::TintKind::None && !(material.tint & world::TintOverlay)) {
        world::SubChunkKey key { current.dimension, cell[0] >> 4, cell[1] >> 4, cell[2] >> 4 };
        uint32_t biome = 0;
        if (std::shared_ptr<const world::PalettedStorage> biomes = world.store().biomes(key)) {
            biome = biomes->runtimeId(uint32_t(cell[0] & 15), uint32_t(cell[1] & 15), uint32_t(cell[2] & 15));
        }
        burst.tint = assets->biomeTints().colors(biome).domain(material.tintKind(), material.foliageVariant());
    }
    burst.shape = selectionBox(value, cell[0], cell[1], cell[2]);

    auto obstacles = std::make_shared<std::vector<world::CollisionBox>>();
    for (int32_t dx = -ObstacleRadius; dx <= ObstacleRadius; ++dx) {
        for (int32_t dy = -ObstacleRadius; dy <= ObstacleRadius; ++dy) {
            for (int32_t dz = -ObstacleRadius; dz <= ObstacleRadius; ++dz) {
                if (kind == ParticleBurst::Kind::Destroy && dx == 0 && dy == 0 && dz == 0) {
                    continue;
                }
                int32_t x = cell[0] + dx;
                int32_t y = cell[1] + dy;
                int32_t z = cell[2] + dz;
                for (world::CollisionBox box : shapeBoxes(blockAt(x, y, z), x, y, z)) {
                    box.minX += float(dx);
                    box.maxX += float(dx);
                    box.minY += float(dy);
                    box.maxY += float(dy);
                    box.minZ += float(dz);
                    box.maxZ += float(dz);
                    obstacles->push_back(box);
                }
            }
        }
    }
    burst.obstacles = std::move(obstacles);

    std::lock_guard<std::mutex> guard(mutex);
    if (pendingBursts.size() < MaxPendingBursts) {
        pendingBursts.push_back(std::move(burst));
    }
}

/**
 * Finishes breaking the block being mined the way the game predicts it: the
 * server hears a predicted destroy with the break transaction beside it,
 * and the client clears the block at once, keeping any water it held. The
 * particles and break sound wait for the server to agree, so a break it
 * cancels only puts the block back.
 */
void Session::destroyPredicted(PlayerAuthInputPacket& packet, int32_t face, const std::array<double, 3>& point)
{
    const std::array<int32_t, 3>& cell = breaking.cell;
    uint32_t value = breaking.value;
    PlayerBlockActionData predict;
    predict.mAction = PlayerActionType::BlockPredictDestroy;
    predict.mBlockPosition = Vector3i(cell[0], cell[1], cell[2]);
    predict.mFace = face;
    packet.mPlayerActions.push_back(predict);

    int32_t slot = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        slot = std::clamp(current.hud.selectedSlot, 0, 8);
    }
    std::string name = assets->blockName(value, ids.hashed, ids.sequential.get());
    ItemUseTransaction& transaction = packet.mItemUseTransaction;
    transaction.mActionType = BreakBlockAction;
    transaction.mBlockPosition = predict.mBlockPosition;
    transaction.mBlockFace = face;
    transaction.mClickPosition = Vector3f(float(point[0] - cell[0]), float(point[1] - cell[1]), float(point[2] - cell[2]));
    transaction.mHotbarSlot = slot;
    transaction.mItemInHand = inventoryModel.slots[size_t(slot)];
    transaction.mPlayerPosition = packet.mPosition;
    transaction.mBlockDefinition = std::make_shared<BlockDefinition>(name, static_cast<int>(value), Tag {});
    transaction.mTriggerType = ItemUseTriggerType::PlayerInput;
    transaction.mClientInteractPrediction = ItemUsePredictedResult::Success;
    packet.mHasItemUseTransaction = true;
    packet.mInputData.push_back(static_cast<int32_t>(PlayerAuthInputData::PerformItemInteraction));

    uint32_t air = ids.hashed ? assets->airNetworkHash() : assets->airSequentialId();
    uint32_t extra = blockAt(cell[0], cell[1], cell[2], 1);
    bool keepsLiquid = extra != world::ImplicitAir && extra != air;
    world::SubChunkKey key { current.dimension, cell[0] >> 4, cell[1] >> 4, cell[2] >> 4 };
    uint8_t x = uint8_t(cell[0] & 15);
    uint8_t y = uint8_t(cell[1] & 15);
    uint8_t z = uint8_t(cell[2] & 15);
    world.store().updateBlocks(key, { { x, y, z, 0, keepsLiquid ? extra : air }, { x, y, z, 1, air } });

    double now = secondsNow();
    std::erase_if(predictedBreaks, [&](const PredictedBreak& entry) {
        return entry.cell == cell;
    });
    predictedBreaks.push_back({ cell, value, face, now });
    recentBreaks[cell] = now;
    remoteCracks.erase(cell);
}

/**
 * Settles a predicted break once the server says what the block became:
 * anything but the block that was broken means the break went through and
 * its particles and sound play, the same block back means it was cancelled.
 */
void Session::answerPredictedBreak(const std::array<int32_t, 3>& cell, uint32_t value)
{
    auto found = std::find_if(predictedBreaks.begin(), predictedBreaks.end(), [&](const PredictedBreak& entry) {
        return entry.cell == cell;
    });
    if (found == predictedBreaks.end()) {
        return;
    }
    PredictedBreak broken = *found;
    predictedBreaks.erase(found);
    if (value == broken.value) {
        recentBreaks.erase(cell);
        return;
    }
    emitBurst(ParticleBurst::Kind::Destroy, cell, broken.value, broken.face);
    SoundRequest sound;
    sound.name = "break";
    sound.position = { cell[0] + 0.5, cell[1] + 0.5, cell[2] + 0.5 };
    sound.block = assets->blockName(broken.value, ids.hashed, ids.sequential.get());
    queueSound(std::move(sound));
}

/**
 * One tick of holding the attack button, as the game's GameMode runs it:
 * the first hit starts breaking, and so does the next block once one is
 * broken (sliding onto another block in the middle of breaking continues
 * instead), every tick adds the dig speed and chips the face,
 * and the block goes once the progress is full, then a short delay passes
 * before the next one starts. Creative breaks at the first hit. Letting go
 * or looking away from every block aborts; sliding onto another block while
 * held only sends the continue for it.
 */
void Session::tickBreaking(PlayerAuthInputPacket& packet, const MotionTick& tick)
{
    bool held = attackHeld.load();
    bool heldBefore = attackHeldBefore;
    attackHeldBefore = held;
    if (!held) {
        attackOnEntity = false;
    }
    if (destroyDelay > 0) {
        --destroyDelay;
    }

    int32_t gameType = SurvivalMode;
    int32_t slot = 0;
    bool dead = false;
    world::MiningConditions conditions;
    double now = secondsNow();
    {
        std::lock_guard<std::mutex> guard(mutex);
        gameType = current.hud.gameType;
        slot = std::clamp(current.hud.selectedSlot, 0, 8);
        dead = current.dead;
        for (const HudEffect& effect : current.hud.effects) {
            if (effect.expires >= 0.0 && effect.expires < now) {
                continue;
            }
            int32_t level = effect.amplifier + 1;
            if (effect.id == HasteEffect) {
                conditions.haste = level;
            } else if (effect.id == MiningFatigueEffect) {
                conditions.miningFatigue = level;
            } else if (effect.id == ConduitPowerEffect) {
                conditions.conduitPower = level;
            }
        }
    }
    const ItemStack& heldStack = inventoryModel.slots[size_t(slot)];
    conditions.heldItem = heldStack.isAir() ? std::string() : heldStack.mDefinition->getIdentifier();
    conditions.efficiency = enchantmentLevel(heldStack, EfficiencyEnchantment);
    conditions.aquaAffinity = enchantmentLevel(inventoryModel.slots[inventory::Armor], AquaAffinityEnchantment) > 0;
    conditions.onGround = tick.onGround;
    conditions.flying = tick.flying;
    conditions.underwater = mediumAt({ tick.position.x, tick.position.y + EyeHeight, tick.position.z }) == WaterMedium;

    auto action = [&](PlayerActionType type, const std::array<int32_t, 3>& cell, int32_t face) {
        PlayerBlockActionData data;
        data.mAction = type;
        data.mBlockPosition = Vector3i(cell[0], cell[1], cell[2]);
        data.mFace = face;
        packet.mPlayerActions.push_back(data);
    };

    bool mayBreak = held && !attackOnEntity && !dead && (gameType == SurvivalMode || gameType == CreativeMode);
    std::optional<BlockHit> hit = mayBreak ? traceBlock(BreakReach) : std::nullopt;
    if (hit) {
        double distance = 0.0;
        std::array<double, 3> direction { lookDirection[0], lookDirection[1], lookDirection[2] };
        if (traceActor(lookOrigin, direction, hit->distance, distance)) {
            hit.reset();
        }
    }
    if (breaking.active && (!hit || hit->cell != breaking.cell)) {
        if (!hit) {
            action(PlayerActionType::AbortBreak, breaking.cell, breaking.face);
        }
        breakSwitched = hit.has_value();
        breaking.active = false;
    }

    if (hit) {
        {
            std::lock_guard<std::mutex> guard(mutex);
            current.hud.lastSwing = now;
        }
        std::string name = assets->blockName(hit->value, ids.hashed, ids.sequential.get());
        float speed = gameType == CreativeMode ? 1.0f : world::destroyProgressPerTick(name, conditions);
        if (breaking.active) {
            breaking.face = hit->face;
            if (breaking.ticks++ % HitSoundTicks == 0) {
                SoundRequest sound;
                sound.name = "hit";
                sound.position = { breaking.cell[0] + 0.5, breaking.cell[1] + 0.5, breaking.cell[2] + 0.5 };
                sound.block = name;
                queueSound(std::move(sound));
            }
            emitBurst(ParticleBurst::Kind::Crack, breaking.cell, breaking.value, breaking.face);
            breaking.progress += speed;
            if (breaking.progress >= 1.0f) {
                destroyPredicted(packet, breaking.face, hit->point);
                breaking.active = false;
                destroyDelay = DestroyDelayTicks;
            }
        } else if (destroyDelay == 0 && !(gameType == CreativeMode && world::preventsCreativeBreaking(conditions.heldItem))) {
            action(heldBefore && breakSwitched ? PlayerActionType::BlockContinueDestroy : PlayerActionType::StartBreak, hit->cell, hit->face);
            breakSwitched = false;
            breaking = { true, hit->cell, hit->face, hit->value, 0.0f, 0 };
            if (speed >= 1.0f) {
                destroyPredicted(packet, hit->face, hit->point);
                breaking.active = false;
                if (gameType == CreativeMode) {
                    destroyDelay = DestroyDelayTicks;
                }
            }
        }
    }

    if (!packet.mPlayerActions.empty()) {
        packet.mInputData.push_back(static_cast<int32_t>(PlayerAuthInputData::PerformBlockActions));
    }
}

/**
 * Cracks other players make, from the level events the server sends about
 * them: a start with the progress one tick adds, updates to that speed, a
 * stop, and the chips and bursts of their mining. Events about the block the
 * local player is breaking are left out, the client draws those itself.
 */
void Session::handleBreakingEvent(const LevelEventPacket& event)
{
    std::array<int32_t, 3> cell = cellOf(event.mPosition);
    int32_t id = event.mEventId;
    if (id == LevelEventPacket::BlockStartBreak) {
        if (event.mData <= 0) {
            remoteCracks.erase(cell);
        } else {
            remoteCracks[cell] = { blockAt(cell[0], cell[1], cell[2]), 0.0f, float(event.mData) / 65535.0f };
        }
    } else if (id == LevelEventPacket::BlockUpdateBreak) {
        if (auto found = remoteCracks.find(cell); found != remoteCracks.end()) {
            found->second.speed = float(event.mData) / 65535.0f;
        }
    } else if (id == LevelEventPacket::BlockStopBreak) {
        remoteCracks.erase(cell);
    } else if (id == LevelEventPacket::ParticleDestroy) {
        remoteCracks.erase(cell);
        uint32_t air = ids.hashed ? assets->airNetworkHash() : assets->airSequentialId();
        answerPredictedBreak(cell, air);
        if (!locallyBroken(cell)) {
            emitBurst(ParticleBurst::Kind::Destroy, cell, static_cast<uint32_t>(event.mData), 0);
        }
    } else if (id == LevelEventPacket::ParticlePunchBlock || (id >= 3603 && id <= 3608)) {
        int32_t face = id == LevelEventPacket::ParticlePunchBlock ? (event.mData >> 24) & 0xFF : id - 3603;
        if (!locallyBroken(cell)) {
            emitBurst(ParticleBurst::Kind::Crack, cell, blockAt(cell[0], cell[1], cell[2]), face);
        }
    }
}

void Session::tickCracks()
{
    for (auto& [cell, crack] : remoteCracks) {
        crack.progress = std::min(crack.progress + crack.speed, 1.0f);
    }
    double now = secondsNow();
    std::erase_if(recentBreaks, [now](const auto& entry) {
        return now - entry.second >= LocalBreakMemory;
    });
    std::erase_if(predictedBreaks, [now](const PredictedBreak& entry) {
        return now - entry.time >= BreakAnswerWait;
    });
}

/**
 * Publishes the outline under the crosshair and every crack in view. Nothing
 * is outlined while an entity is in the way, as the entity is the target.
 */
void Session::publishBreaking()
{
    std::optional<BlockSelection> selection;
    int32_t gameType = 0;
    bool dead = false;
    {
        std::lock_guard<std::mutex> guard(mutex);
        gameType = current.hud.gameType;
        dead = current.dead;
    }
    constexpr int32_t SpectatorMode = 6;
    if (!dead && gameType != SpectatorMode) {
        if (std::optional<BlockHit> hit = traceBlock(BreakReach)) {
            double distance = 0.0;
            std::array<double, 3> direction { lookDirection[0], lookDirection[1], lookDirection[2] };
            if (!traceActor(lookOrigin, direction, hit->distance, distance)) {
                world::CollisionBox box = selectionBox(hit->value, hit->cell[0], hit->cell[1], hit->cell[2]);
                selection = BlockSelection {
                    hit->cell,
                    { hit->cell[0] + double(box.minX), hit->cell[1] + double(box.minY), hit->cell[2] + double(box.minZ) },
                    { hit->cell[0] + double(box.maxX), hit->cell[1] + double(box.maxY), hit->cell[2] + double(box.maxZ) },
                };
            }
        } else if (std::optional<BlockHit> ahead = heldBlockBridge()) {
            // Looking out past the edge with a block in hand outlines where building ahead puts it.
            std::array<int32_t, 3> cell = placedCell(*ahead);
            selection = BlockSelection { cell, { double(cell[0]), double(cell[1]), double(cell[2]) }, { cell[0] + 1.0, cell[1] + 1.0, cell[2] + 1.0 } };
        }
    }

    std::vector<BlockCrack> cracks;
    auto addCrack = [&](const std::array<int32_t, 3>& cell, uint32_t value, float progress) {
        if (progress <= 0.0f || value == world::ImplicitAir) {
            return;
        }
        BlockCrack crack;
        crack.cell = cell;
        crack.visual = assets->visual(value, ids.hashed, ids.sequential.get());
        crack.boxes = shapeBoxes(value, cell[0], cell[1], cell[2]);
        if (crack.boxes.empty()) {
            crack.boxes.push_back(selectionBox(value, cell[0], cell[1], cell[2]));
        }
        crack.progress = std::min(progress, 1.0f);
        cracks.push_back(std::move(crack));
    };
    if (breaking.active) {
        addCrack(breaking.cell, breaking.value, breaking.progress);
    }
    for (const auto& [cell, crack] : remoteCracks) {
        if (!(breaking.active && cell == breaking.cell)) {
            addCrack(cell, blockAt(cell[0], cell[1], cell[2]), crack.progress);
        }
    }

    std::lock_guard<std::mutex> guard(mutex);
    current.selection = std::move(selection);
    current.cracks = std::move(cracks);
}

}
