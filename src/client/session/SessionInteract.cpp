#include "SessionData.h"

#include "Network/BedrockConnection.h"
#include "Protocol/Packets/InventoryTransactionPacket.h"
#include "client/DebugLog.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kestrel {

namespace {

constexpr double InteractReach = 6.0;
constexpr int32_t ClickBlock = 0;
constexpr int32_t ClickAir = 1;
constexpr int32_t InteractEntity = 0;
constexpr int32_t AttackEntity = 1;
constexpr float DefaultActorWidth = 0.6f;
constexpr float DefaultActorHeight = 1.8f;

/**
 * Where a ray first enters a box, as a distance along the ray, or nothing
 * when it misses or the box is behind.
 */
std::optional<double> enterBox(const std::array<double, 3>& origin, const std::array<double, 3>& direction, const std::array<double, 3>& low, const std::array<double, 3>& high)
{
    double entry = 0.0;
    double exit = std::numeric_limits<double>::max();
    for (size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1.0e-12) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                return std::nullopt;
            }
            continue;
        }
        double first = (low[axis] - origin[axis]) / direction[axis];
        double second = (high[axis] - origin[axis]) / direction[axis];
        entry = std::max(entry, std::min(first, second));
        exit = std::min(exit, std::max(first, second));
    }
    if (entry > exit) {
        return std::nullopt;
    }
    return entry;
}

}

void Session::requestInteraction(bool use)
{
    (use ? useRequested : attackRequested) = true;
}

/**
 * The first solid block along the look ray within reach: its cell, the face
 * the ray enters through (down, up, north, south, west, east) and the point
 * it hits.
 */
std::optional<BlockHit> Session::traceBlock(double reach)
{
    if (!assets) {
        return std::nullopt;
    }
    std::array<int64_t, 3> cell {};
    std::array<int64_t, 3> step {};
    std::array<double, 3> next {};
    std::array<double, 3> delta {};
    for (int axis = 0; axis < 3; ++axis) {
        double origin = lookOrigin[axis];
        double direction = lookDirection[axis];
        cell[axis] = static_cast<int64_t>(std::floor(origin));
        step[axis] = direction > 0.0 ? 1 : (direction < 0.0 ? -1 : 0);
        delta[axis] = direction != 0.0 ? std::abs(1.0 / direction) : 1e30;
        double boundary = direction > 0.0 ? double(cell[axis] + 1) - origin : origin - double(cell[axis]);
        next[axis] = direction != 0.0 ? boundary * delta[axis] : 1e30;
    }
    static constexpr int32_t Faces[3][2] = { { 4, 5 }, { 0, 1 }, { 2, 3 } };
    int lastAxis = -1;
    double travelled = 0.0;
    while (travelled <= reach) {
        world::SubChunkKey key { current.dimension, int32_t(cell[0] >> 4), int32_t(cell[1] >> 4), int32_t(cell[2] >> 4) };
        if (std::shared_ptr<const world::SubChunk> sub = world.store().subChunk(key)) {
            uint32_t value = sub->runtimeId(0, uint32_t(cell[0] & 15), uint32_t(cell[1] & 15), uint32_t(cell[2] & 15));
            const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
            if (value != world::ImplicitAir && visual.flags != 0 && !(visual.flags & world::FlagAir) && !visual.liquid) {
                BlockHit hit;
                hit.cell = { int32_t(cell[0]), int32_t(cell[1]), int32_t(cell[2]) };
                hit.value = value;
                hit.distance = travelled;
                hit.face = lastAxis < 0 ? 1 : Faces[lastAxis][step[lastAxis] < 0 ? 1 : 0];
                for (int axis = 0; axis < 3; ++axis) {
                    hit.point[axis] = lookOrigin[axis] + lookDirection[axis] * travelled;
                }
                hit.name = assets->describe(value, ids.hashed, ids.sequential.get());
                return hit;
            }
        }
        int axis = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        travelled = next[axis];
        next[axis] += delta[axis];
        cell[axis] += step[axis];
        lastAxis = axis;
    }
    return std::nullopt;
}

/**
 * What a click does in the game: the entity under the crosshair is used or
 * hit, otherwise a right click uses the held item on the block in reach or,
 * with nothing there, on the air. Servers open their menus from exactly
 * these transactions.
 */
void Session::interact(bool use)
{
    if (!connection || !codecContext) {
        return;
    }
    std::array<double, 3> origin;
    std::array<double, 3> direction;
    int32_t slot = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        origin = lookOrigin;
        direction = { lookDirection[0], lookDirection[1], lookDirection[2] };
        slot = std::clamp(current.hud.selectedSlot, 0, 8);
    }
    std::optional<BlockHit> block = traceBlock(InteractReach);
    double limit = block ? block->distance : InteractReach;

    const ActorView* target = nullptr;
    double nearest = limit;
    for (const auto& [runtimeId, actor] : actors) {
        if (runtimeId == localRuntimeId) {
            continue;
        }
        double half = (actor.width > 0.0f ? actor.width : DefaultActorWidth) * actor.scale * 0.5;
        double height = actor.height > 0.0f ? actor.height : DefaultActorHeight * actor.scale;
        std::optional<double> entry = enterBox(origin, direction, { actor.x - half, actor.y, actor.z - half }, { actor.x + half, actor.y + height, actor.z + half });
        if (entry && *entry < nearest) {
            nearest = *entry;
            target = &actor;
        }
    }

    InventoryTransactionPacket packet;
    packet.mHotbarSlot = slot;
    packet.mItemInHand = inventoryStacks[size_t(slot)];
    packet.mPlayerPosition = Vector3f(float(origin[0]), float(origin[1]), float(origin[2]));
    packet.mTriggerType = ItemUseTriggerType::PlayerInput;
    packet.mClientInteractPrediction = ItemUsePredictedResult::Success;
    if (target) {
        packet.mTransactionType = InventoryTransactionType::ItemUseOnEntity;
        packet.mActionType = use ? InteractEntity : AttackEntity;
        packet.mRuntimeActorId = static_cast<int64_t>(target->runtimeId);
        packet.mClickPosition = Vector3f(float(origin[0] + direction[0] * nearest - target->x), float(origin[1] + direction[1] * nearest - target->y), float(origin[2] + direction[2] * nearest - target->z));
        debugLog(std::string(use ? "interact with " : "attack ") + target->identifier + " " + std::to_string(target->runtimeId));
    } else if (!use) {
        return;
    } else if (block) {
        packet.mTransactionType = InventoryTransactionType::ItemUse;
        packet.mActionType = ClickBlock;
        packet.mBlockPosition = Vector3i(block->cell[0], block->cell[1], block->cell[2]);
        packet.mBlockFace = block->face;
        packet.mClickPosition = Vector3f(float(block->point[0] - block->cell[0]), float(block->point[1] - block->cell[1]), float(block->point[2] - block->cell[2]));
        packet.mBlockDefinition = std::make_shared<BlockDefinition>(block->name, static_cast<int>(block->value), Tag {});
        debugLog("use item on " + block->name);
    } else {
        packet.mTransactionType = InventoryTransactionType::ItemUse;
        packet.mActionType = ClickAir;
        packet.mBlockFace = -1;
        debugLog("use item in the air");
    }
    connection->send(packet);
}

}
