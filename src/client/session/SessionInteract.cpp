#include "SessionData.h"

#include "Network/BedrockConnection.h"
#include "Protocol/Packets/AnimatePacket.h"
#include "Protocol/Packets/BlockPickRequestPacket.h"
#include "Protocol/Packets/InventoryTransactionPacket.h"
#include "client/DebugLog.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string_view>
#include <limits>

namespace kestrel {

namespace {

constexpr double InteractReach = 5.7;
// Hitting and using entities reaches three blocks from the eyes, seven in creative.
constexpr double EntityReach = 3.0;
constexpr double CreativeEntityReach = 7.0;
constexpr double ActorPickMargin = 0.1;

constexpr std::string_view UnpickableActors[] = {
    "minecraft:item",
    "minecraft:xp_orb",
    "minecraft:arrow",
    "minecraft:thrown_trident",
    "minecraft:snowball",
    "minecraft:egg",
    "minecraft:ender_pearl",
    "minecraft:splash_potion",
    "minecraft:lingering_potion",
    "minecraft:xp_bottle",
    "minecraft:fireball",
    "minecraft:small_fireball",
    "minecraft:wither_skull",
    "minecraft:wither_skull_dangerous",
    "minecraft:dragon_fireball",
    "minecraft:wind_charge_projectile",
    "minecraft:breeze_wind_charge_projectile",
    "minecraft:fishing_hook",
    "minecraft:falling_block",
    "minecraft:lightning_bolt",
    "minecraft:area_effect_cloud",
    "minecraft:evocation_fang",
    "minecraft:eye_of_ender_signal",
    "minecraft:fireworks_rocket",
    "minecraft:llama_spit",
    "minecraft:shulker_bullet",
};

bool pickable(const ActorView& actor)
{
    return std::find(std::begin(UnpickableActors), std::end(UnpickableActors), actor.identifier) == std::end(UnpickableActors);
}
constexpr int32_t ClickBlock = 0;
constexpr int32_t ClickAir = 1;
constexpr int32_t InteractEntity = 0;
constexpr int32_t AttackEntity = 1;
constexpr float DefaultActorWidth = 0.6f;
constexpr float DefaultActorHeight = 1.8f;
constexpr std::array<std::array<int32_t, 3>, 6> FaceOffsets { { { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } } };

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
 * Whether the crosshair passes through a block, as it does through fire: it
 * shows no outline and cannot be hit, only put out through the block it
 * burns on.
 */
bool Session::unselectable(uint32_t value) const
{
    std::string name = assets->blockName(value, ids.hashed, ids.sequential.get());
    if (name.rfind("minecraft:", 0) == 0) {
        name.erase(0, 10);
    }
    return name == "fire" || name == "soul_fire";
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
        if (value != world::ImplicitAir && visual.flags != 0 && !(visual.flags & world::FlagAir) && !visual.liquid && !unselectable(value)) {
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
        if (runtimeId == localRuntimeId || !pickable(actor)) {
            continue;
        }
        double half = (actor.width > 0.0f ? actor.width : DefaultActorWidth) * actor.scale * 0.5 + ActorPickMargin;
        double height = actor.height > 0.0f ? actor.height : DefaultActorHeight * actor.scale;
        int axis = -1;
        std::optional<double> entry = enterBox(origin, direction, { actor.x - half, actor.y - ActorPickMargin, actor.z - half }, { actor.x + half, actor.y + height + ActorPickMargin, actor.z + half }, &axis);
        if (entry && axis >= 0 && *entry + ActorPickMargin < reach && *entry < distance) {
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
    bool creative = false;
    {
        std::lock_guard<std::mutex> guard(mutex);
        origin = lookOrigin;
        direction = { lookDirection[0], lookDirection[1], lookDirection[2] };
        slot = std::clamp(current.hud.selectedSlot, 0, 8);
        creative = current.gameMode == "Creative";
    }
    std::optional<BlockHit> block = traceBlock(InteractReach);
    double nearest = 0.0;
    double entityReach = creative ? CreativeEntityReach : EntityReach;
    const ActorView* target = traceActor(origin, direction, std::min(block ? block->distance : InteractReach, entityReach), nearest);
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
        if (!block) {
            missedSwing = true;
        }
        return;
    } else if (block) {
        useOnBlock(*block);
        return;
    } else if (std::optional<BlockHit> ahead = heldBlockBridge()) {
        useOnBlock(*ahead);
        return;
    } else {
        buildLast.reset();
        packet.mTransactionType = InventoryTransactionType::ItemUse;
        packet.mActionType = ClickAir;
        packet.mBlockFace = -1;
        packet.mTriggerType = ItemUseTriggerType::Unknown;
        debugLog("use item in the air");
        // Throwing swings the arm; items used in place, like food or a bow, do not.
        static constexpr std::string_view Thrown[] = {
            "minecraft:snowball", "minecraft:egg", "minecraft:ender_pearl", "minecraft:ender_eye", "minecraft:splash_potion",
            "minecraft:lingering_potion", "minecraft:experience_bottle", "minecraft:fishing_rod", "minecraft:wind_charge",
            "minecraft:blue_egg", "minecraft:brown_egg",
        };
        if (!packet.mItemInHand.isAir() && std::find(std::begin(Thrown), std::end(Thrown), packet.mItemInHand.mDefinition->getIdentifier()) != std::end(Thrown)) {
            std::lock_guard<std::mutex> guard(mutex);
            current.hud.lastSwing = secondsNow();
        }
    }
    if (target) {
        buildLast.reset();
    }
    if (target && !use) {
        constexpr size_t MaxPendingAttacks = 16;
        std::lock_guard<std::mutex> guard(mutex);
        if (pendingAttacks.size() < MaxPendingAttacks) {
            pendingAttacks.push_back(target->runtimeId);
        }
    }
    transmit(packet);
    if (!target) {
        startItemUse(slot, packet.mItemInHand);
    }
}

std::vector<uint64_t> Session::takeAttacks()
{
    std::lock_guard<std::mutex> guard(mutex);
    std::vector<uint64_t> attacks = std::move(pendingAttacks);
    pendingAttacks.clear();
    return attacks;
}

/**
 * Uses the held item on one face of a block, and remembers the cell a block
 * placed there would fill as the head of the line held use extends.
 */
void Session::useOnBlock(const BlockHit& block)
{
    int32_t slot = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        slot = std::clamp(current.hud.selectedSlot, 0, 8);
    }
    InventoryTransactionPacket packet;
    packet.mHotbarSlot = slot;
    packet.mItemInHand = inventoryModel.slots[size_t(slot)];
    packet.mPlayerPosition = Vector3f(float(lookOrigin[0]), float(lookOrigin[1]), float(lookOrigin[2]));
    bool holding = !packet.mItemInHand.isAir();
    std::array<int32_t, 3> placed = replaceableAt(block.cell) ? block.cell : placedCell(block);
    BlockUse outcome = BlockUse::Nothing;
    if (usableBlock(block.name) && !(motion.sneaking() && holding)) {
        outcome = BlockUse::Interact;
    } else if (holdsBlock(packet.mItemInHand) && placeableAt(placed)) {
        outcome = BlockUse::Place;
    }
    if (outcome != BlockUse::Nothing) {
        AnimatePacket swing;
        swing.mAction = AnimatePacket::Action::SwingArm;
        swing.mRuntimeActorId = localRuntimeId;
        swing.mData = 0.0f;
        swing.mSwingSource = outcome == BlockUse::Interact ? "Interact" : "Build";
        transmit(swing);
        std::lock_guard<std::mutex> guard(mutex);
        current.hud.lastSwing = secondsNow();
    }
    packet.mTriggerType = ItemUseTriggerType::PlayerInput;
    packet.mClientInteractPrediction = outcome == BlockUse::Nothing ? ItemUsePredictedResult::Failure : ItemUsePredictedResult::Success;
    packet.mTransactionType = InventoryTransactionType::ItemUse;
    packet.mActionType = ClickBlock;
    packet.mBlockPosition = Vector3i(block.cell[0], block.cell[1], block.cell[2]);
    packet.mBlockFace = block.face;
    packet.mClickPosition = Vector3f(float(block.point[0] - block.cell[0]), float(block.point[1] - block.cell[1]), float(block.point[2] - block.cell[2]));
    packet.mBlockDefinition = std::make_shared<BlockDefinition>(block.name, static_cast<int>(block.value), Tag {});
    debugLog("use item on " + block.name);
    transmit(packet);

    buildFace = block.face;
    buildLast = placed;
}

bool Session::holdsBlock(const ItemStack& item) const
{
    return !item.isAir() && assets && assets->placesBlock(item.mDefinition->getIdentifier());
}

std::optional<BlockHit> Session::heldBlockBridge()
{
    int32_t slot = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        slot = std::clamp(current.hud.selectedSlot, 0, 8);
    }
    return holdsBlock(inventoryModel.slots[size_t(slot)]) ? bridgeHit() : std::nullopt;
}

std::array<int32_t, 3> Session::placedCell(const BlockHit& hit)
{
    const std::array<int32_t, 3>& offset = FaceOffsets[size_t(std::clamp(hit.face, 0, 5))];
    return { hit.cell[0] + offset[0], hit.cell[1] + offset[1], hit.cell[2] + offset[2] };
}

/**
 * Whether cell holds nothing a block would not replace: air, liquids and the
 * plants and snow a placed block takes the place of.
 */
bool Session::replaceableAt(const std::array<int32_t, 3>& cell)
{
    static constexpr std::string_view Replaceable[] = {
        "minecraft:air", "minecraft:water", "minecraft:flowing_water", "minecraft:lava", "minecraft:flowing_lava", "minecraft:short_grass",
        "minecraft:tall_grass", "minecraft:fern", "minecraft:large_fern", "minecraft:deadbush", "minecraft:vine", "minecraft:snow_layer",
        "minecraft:fire", "minecraft:soul_fire", "minecraft:seagrass", "minecraft:structure_void",
    };
    if (!assets) {
        return true;
    }
    uint32_t value = blockAt(cell[0], cell[1], cell[2]);
    if (value == world::ImplicitAir) {
        return true;
    }
    std::string name = assets->blockName(value, ids.hashed, ids.sequential.get());
    return std::find(std::begin(Replaceable), std::end(Replaceable), name) != std::end(Replaceable);
}

/**
 * Whether a block could go into cell: it holds nothing a block would not
 * replace, and it stays clear of the player's own body.
 */
bool Session::placeableAt(const std::array<int32_t, 3>& cell)
{
    if (!replaceableAt(cell)) {
        return false;
    }
    constexpr double HalfWidth = DefaultActorWidth * 0.5;
    constexpr double Inset = 1.0E-5;
    auto overlaps = [&](double x, double y, double z, double half, double height) {
        return x + half - Inset > cell[0] && x - half + Inset < cell[0] + 1.0
            && y + height - Inset > cell[1] && y + Inset < cell[1] + 1.0
            && z + half - Inset > cell[2] && z - half + Inset < cell[2] + 1.0;
    };
    const MotionVector& feet = motion.position();
    if (overlaps(feet.x, feet.y, feet.z, HalfWidth, DefaultActorHeight)) {
        return false;
    }
    for (const auto& [runtimeId, actor] : actors) {
        if (runtimeId == localRuntimeId || !pickable(actor)) {
            continue;
        }
        double half = (actor.width > 0.0f ? actor.width : DefaultActorWidth) * actor.scale * 0.5;
        double height = actor.height > 0.0f ? actor.height : DefaultActorHeight * actor.scale;
        if (overlaps(actor.x, actor.y, actor.z, half, height)) {
            return false;
        }
    }
    return true;
}

/**
 * Blocks whose own use answers a click, like containers, doors and redstone
 * controls; iron doors and trapdoors only open with redstone.
 */
bool Session::usableBlock(std::string_view name)
{
    static constexpr std::string_view Usable[] = {
        "minecraft:crafting_table", "minecraft:furnace", "minecraft:lit_furnace", "minecraft:blast_furnace", "minecraft:lit_blast_furnace",
        "minecraft:smoker", "minecraft:lit_smoker", "minecraft:barrel", "minecraft:lever", "minecraft:anvil", "minecraft:enchanting_table",
        "minecraft:brewing_stand", "minecraft:hopper", "minecraft:dropper", "minecraft:dispenser", "minecraft:crafter", "minecraft:loom",
        "minecraft:stonecutter_block", "minecraft:grindstone", "minecraft:cartography_table", "minecraft:smithing_table", "minecraft:beacon",
        "minecraft:noteblock", "minecraft:unpowered_repeater", "minecraft:powered_repeater", "minecraft:unpowered_comparator",
        "minecraft:powered_comparator", "minecraft:daylight_detector", "minecraft:daylight_detector_inverted", "minecraft:bell", "minecraft:bed",
    };
    static constexpr std::string_view Suffixes[] = { "_door", "_trapdoor", "_button", "fence_gate", "chest", "shulker_box", "_bed" };
    if (std::find(std::begin(Usable), std::end(Usable), name) != std::end(Usable)) {
        return true;
    }
    if (name == "minecraft:iron_door" || name == "minecraft:iron_trapdoor") {
        return false;
    }
    return std::any_of(std::begin(Suffixes), std::end(Suffixes), [name](std::string_view suffix) {
        return name.size() >= suffix.size() && name.substr(name.size() - suffix.size()) == suffix;
    });
}

/**
 * Bedrock builds against blocks the crosshair never touches: with nothing in
 * reach, the look ray is walked cell by cell and the first empty cell with a
 * solid block behind it, on a side turned toward the viewer, takes the block.
 * Looking ahead and down past an edge finds the cell in front of the block
 * underfoot, which is how players bridge. Only the block the player stands on
 * can be built off this way, so it takes walking up to the edge.
 */
std::optional<BlockHit> Session::bridgeHit()
{
    // A side counts when the look runs at least this much along it, away from the block built off.
    constexpr double SideAlignment = 0.1;
    if (!assets) {
        return std::nullopt;
    }
    std::array<size_t, 6> sides { 0, 1, 2, 3, 4, 5 };
    auto alignment = [&](size_t face) {
        const std::array<int32_t, 3>& offset = FaceOffsets[face];
        return offset[0] * lookDirection[0] + offset[1] * lookDirection[1] + offset[2] * lookDirection[2];
    };
    std::sort(sides.begin(), sides.end(), [&](size_t a, size_t b) { return alignment(a) > alignment(b); });
    constexpr double HalfWidth = DefaultActorWidth * 0.5;
    const MotionVector& feet = motion.position();
    int32_t underfoot = int32_t(std::floor(feet.y - 0.01));
    auto standingOn = [&](const std::array<int32_t, 3>& block) {
        return block[1] == underfoot && feet.x + HalfWidth > block[0] && feet.x - HalfWidth < block[0] + 1.0
            && feet.z + HalfWidth > block[2] && feet.z - HalfWidth < block[2] + 1.0;
    };

    std::array<int32_t, 3> cell { int32_t(std::floor(lookOrigin[0])), int32_t(std::floor(lookOrigin[1])), int32_t(std::floor(lookOrigin[2])) };
    std::array<int32_t, 3> step {};
    std::array<double, 3> next {};
    std::array<double, 3> delta {};
    for (int axis = 0; axis < 3; ++axis) {
        double along = lookDirection[axis];
        step[axis] = along > 0.0 ? 1 : -1;
        delta[axis] = std::abs(along) > 1.0e-9 ? std::abs(1.0 / along) : std::numeric_limits<double>::infinity();
        double boundary = along > 0.0 ? cell[axis] + 1.0 - lookOrigin[axis] : lookOrigin[axis] - cell[axis];
        next[axis] = std::abs(along) > 1.0e-9 ? boundary * delta[axis] : std::numeric_limits<double>::infinity();
    }
    for (double travelled = 0.0; travelled <= InteractReach;) {
        if (placeableAt(cell)) {
            for (size_t face : sides) {
                if (alignment(face) < SideAlignment) {
                    break;
                }
                const std::array<int32_t, 3>& offset = FaceOffsets[face];
                std::array<int32_t, 3> against { cell[0] - offset[0], cell[1] - offset[1], cell[2] - offset[2] };
                uint32_t value = blockAt(against[0], against[1], against[2]);
                if (value == world::ImplicitAir || !standingOn(against) || replaceableAt(against)) {
                    continue;
                }
                BlockHit hit;
                hit.cell = against;
                hit.value = value;
                hit.name = assets->describe(value, ids.hashed, ids.sequential.get());
                hit.face = int32_t(face);
                hit.distance = travelled;
                for (int axis = 0; axis < 3; ++axis) {
                    hit.point[axis] = against[axis] + 0.5 + offset[axis] * 0.5;
                }
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
 * Held use places again every few ticks, the way the game builds while the
 * button stays down. A placement keeps to the face the first one used: when
 * the crosshair has moved on, onto the top of the new block or past its edge
 * into the air, the next block still goes against that face of the last one
 * placed, as long as it is in reach. That is what lets a player bridge
 * forward.
 */
void Session::tickHeldUse()
{
    constexpr uint32_t RepeatTicks = 4;
    if (!useHeld.load() || !connection || !codecContext || !assets) {
        useRepeatTicks = 0;
        buildLast.reset();
        return;
    }
    // A drawn bow stays drawn; repeating the click would only start it over.
    if (itemInUse || ++useRepeatTicks < RepeatTicks) {
        return;
    }
    int32_t slot = 0;
    {
        std::lock_guard<std::mutex> guard(mutex);
        slot = std::clamp(current.hud.selectedSlot, 0, 8);
    }
    if (!holdsBlock(inventoryModel.slots[size_t(slot)])) {
        return;
    }
    std::optional<BlockHit> hit = traceBlock(InteractReach);
    if (hit && hit->face == buildFace) {
        useRepeatTicks = 0;
        interact(true);
        return;
    }
    // With nothing to extend, held use keeps building out from underfoot like a fresh click would.
    auto bridge = [&] {
        if (std::optional<BlockHit> ahead = hit ? std::nullopt : heldBlockBridge()) {
            useRepeatTicks = 0;
            useOnBlock(*ahead);
        }
    };
    if (!buildLast) {
        bridge();
        return;
    }
    const std::array<int32_t, 3>& last = *buildLast;
    uint32_t air = ids.hashed ? assets->airNetworkHash() : assets->airSequentialId();
    auto isAir = [&](const std::array<int32_t, 3>& cell) {
        uint32_t value = blockAt(cell[0], cell[1], cell[2]);
        return value == world::ImplicitAir || value == air;
    };
    if (isAir(last)) {
        return;
    }
    const std::array<int32_t, 3>& offset = FaceOffsets[std::clamp(buildFace, 0, 5)];
    std::array<int32_t, 3> nextCell { last[0] + offset[0], last[1] + offset[1], last[2] + offset[2] };
    double reach = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        double gap = nextCell[axis] + 0.5 - lookOrigin[axis];
        reach += gap * gap;
    }
    if (!isAir(nextCell) || reach > InteractReach * InteractReach) {
        return;
    }
    BlockHit extend;
    extend.cell = last;
    extend.value = blockAt(last[0], last[1], last[2]);
    extend.name = assets->describe(extend.value, ids.hashed, ids.sequential.get());
    extend.face = buildFace;
    if (!faceClickPoint(last, buildFace, extend.point)) {
        bridge();
        return;
    }
    useRepeatTicks = 0;
    useOnBlock(extend);
}

/**
 * The point of one face of a cell nearest the crosshair: where the look ray
 * crosses the face's plane, kept inside the face. The point must lie within
 * a comfortable cone of the look direction, as a placement the player aims at
 * would; otherwise there is none.
 */
bool Session::faceClickPoint(const std::array<int32_t, 3>& cell, int32_t face, std::array<double, 3>& point) const
{
    constexpr double MaximumAngleCosine = 0.5;
    const std::array<int32_t, 3>& offset = FaceOffsets[std::clamp(face, 0, 5)];
    int normalAxis = offset[0] != 0 ? 0 : (offset[1] != 0 ? 1 : 2);
    double plane = cell[normalAxis] + (offset[normalAxis] > 0 ? 1.0 : 0.0);
    double along = lookDirection[normalAxis];
    double travel = std::abs(along) > 1.0e-6 ? (plane - lookOrigin[normalAxis]) / along : -1.0;
    for (int axis = 0; axis < 3; ++axis) {
        double ahead = travel > 0.0 ? lookOrigin[axis] + lookDirection[axis] * travel : cell[axis] + 0.5;
        point[axis] = axis == normalAxis ? plane : std::clamp(ahead, double(cell[axis]) + 0.01, double(cell[axis]) + 0.99);
    }
    double length = 0.0;
    double dot = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        double to = point[axis] - lookOrigin[axis];
        length += to * to;
        dot += to * lookDirection[axis];
    }
    return length > 1.0e-8 && dot / std::sqrt(length) >= MaximumAngleCosine;
}

void Session::setUseHeld(bool held)
{
    useHeld = held;
}

void Session::requestPickBlock(bool withData)
{
    pickRequested = withData ? 2 : 1;
}

void Session::pickBlock(bool withData)
{
    if (!connection || !codecContext) {
        return;
    }
    std::optional<BlockHit> block = traceBlock(InteractReach);
    if (!block) {
        return;
    }
    BlockPickRequestPacket packet;
    packet.mBlockPosition = Vector3i(block->cell[0], block->cell[1], block->cell[2]);
    packet.mAddUserData = withData;
    {
        std::lock_guard<std::mutex> guard(mutex);
        packet.mHotbarSlot = std::clamp(current.hud.selectedSlot, 0, 8);
    }
    debugLog("pick block " + block->name);
    transmit(packet);
}

}
