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
 * when it misses or the box is behind. The axis it enters across is stored
 * in entryAxis, -1 when the ray starts inside.
 */
std::optional<double> enterBox(const std::array<double, 3>& origin, const std::array<double, 3>& direction, const std::array<double, 3>& low, const std::array<double, 3>& high, int* entryAxis = nullptr)
{
    double entry = 0.0;
    double exit = std::numeric_limits<double>::max();
    int axisOfEntry = -1;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1.0e-12) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                return std::nullopt;
            }
            continue;
        }
        double first = (low[axis] - origin[axis]) / direction[axis];
        double second = (high[axis] - origin[axis]) / direction[axis];
        if (std::min(first, second) > entry) {
            entry = std::min(first, second);
            axisOfEntry = int(axis);
        }
        exit = std::min(exit, std::max(first, second));
    }
    if (entry > exit) {
        return std::nullopt;
    }
    if (entryAxis) {
        *entryAxis = axisOfEntry;
    }
    return entry;
}

}

void Session::requestInteraction(bool use)
{
    (use ? useRequested : attackRequested) = true;
}

/**
 * The first block along the look ray within reach whose outline box the ray
 * passes through: its cell, the face of that box the ray enters through
 * (down, up, north, south, west, east) and the point it hits. Rays slip past
 * the empty part of slabs, torches and the like, as in the game.
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
    std::array<double, 3> direction { lookDirection[0], lookDirection[1], lookDirection[2] };
    double travelled = 0.0;
    while (travelled <= reach) {
        uint32_t value = blockAt(int32_t(cell[0]), int32_t(cell[1]), int32_t(cell[2]));
        const world::BlockVisual& visual = assets->visual(value, ids.hashed, ids.sequential.get());
        if (value != world::ImplicitAir && visual.flags != 0 && !(visual.flags & world::FlagAir) && !visual.liquid) {
            world::CollisionBox box = selectionBox(value, int32_t(cell[0]), int32_t(cell[1]), int32_t(cell[2]));
            std::array<double, 3> low { double(cell[0]) + box.minX, double(cell[1]) + box.minY, double(cell[2]) + box.minZ };
            std::array<double, 3> high { double(cell[0]) + box.maxX, double(cell[1]) + box.maxY, double(cell[2]) + box.maxZ };
            int axis = -1;
            std::optional<double> entry = enterBox(lookOrigin, direction, low, high, &axis);
            if (entry && *entry <= reach) {
                BlockHit hit;
                hit.cell = { int32_t(cell[0]), int32_t(cell[1]), int32_t(cell[2]) };
                hit.value = value;
                hit.distance = *entry;
                hit.face = axis < 0 ? 1 : Faces[axis][direction[axis] < 0.0 ? 1 : 0];
                for (int i = 0; i < 3; ++i) {
                    hit.point[i] = lookOrigin[i] + direction[i] * *entry;
                }
                hit.name = assets->describe(value, ids.hashed, ids.sequential.get());
                return hit;
            }
        }
        int axis = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        travelled = next[axis];
        next[axis] += delta[axis];
        cell[axis] += step[axis];
    }
    return std::nullopt;
}

/**
 * The nearest other entity whose box the ray enters within reach, with the
 * distance along the ray it is hit at.
 */
const ActorView* Session::traceActor(const std::array<double, 3>& origin, const std::array<double, 3>& direction, double reach, double& distance) const
{
    const ActorView* target = nullptr;
    distance = reach;
    for (const auto& [runtimeId, actor] : actors) {
        if (runtimeId == localRuntimeId) {
            continue;
        }
        double half = (actor.width > 0.0f ? actor.width : DefaultActorWidth) * actor.scale * 0.5;
        double height = actor.height > 0.0f ? actor.height : DefaultActorHeight * actor.scale;
        std::optional<double> entry = enterBox(origin, direction, { actor.x - half, actor.y, actor.z - half }, { actor.x + half, actor.y + height, actor.z + half });
        if (entry && *entry < distance) {
            distance = *entry;
            target = &actor;
        }
    }
    return target;
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
    double nearest = 0.0;
    const ActorView* target = traceActor(origin, direction, block ? block->distance : InteractReach, nearest);
    if (!use) {
        attackOnEntity = target != nullptr;
    }

    InventoryTransactionPacket packet;
    packet.mHotbarSlot = slot;
    packet.mItemInHand = inventoryModel.slots[size_t(slot)];
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
