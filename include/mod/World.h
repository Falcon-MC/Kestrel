#pragma once

#include "mod/Types.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::mod {

class World {
public:
    virtual ~World() = default;

    virtual ConnectionState state() const = 0;
    virtual std::string serverName() const = 0;
    virtual std::string serverAddress() const = 0;
    virtual std::string levelName() const = 0;
    virtual int dimension() const = 0;
    // Ticks, 24000 per day.
    virtual int64_t time() const = 0;
    virtual float rain() const = 0;
    virtual float thunder() const = 0;

    virtual std::vector<Entity> entities() const = 0;
    virtual std::optional<Entity> entity(uint64_t runtimeId) const = 0;
    virtual std::optional<TargetBlock> targetBlock() const = 0;
    // Names in the player list.
    virtual std::vector<std::string> players() const = 0;
    virtual Sidebar sidebar() const = 0;

    // As of the last frame that drew the world.
    virtual Environment environment() const = 0;

    /**
     * Whether the chunk holding the position is loaded. The world mods read
     * is refreshed every tick and right after the server changes a block or
     * the player breaks one.
     */
    virtual bool isLoaded(const BlockPos& position) const = 0;
    virtual std::optional<BlockInfo> block(const BlockPos& position) const = 0;

    /**
     * The first block, or entity when entities is set, along a ray from a
     * point in a direction, up to reach blocks away. Blocks count as full
     * cubes; air, liquids and hidden blocks let the ray through.
     */
    virtual std::optional<RaycastHit> raycast(const Vec3& from, const Vec3& direction, double reach, bool entities = true) const = 0;

    /**
     * Draws every block of that name ("minecraft:stone" or "stone") as air,
     * or back as itself. The terrain is redrawn when the list changes, and
     * the blocks come back when the mod unloads.
     */
    virtual void setBlockHidden(std::string_view name, bool hidden) = 0;
    virtual void clearHiddenBlocks() = 0;

    /**
     * Every block name the world can hold, vanilla and the server's own,
     * sorted, each with its namespace.
     */
    virtual std::vector<std::string> blockNames() const = 0;

    /**
     * Draws only the blocks of these names and everything else as air, for
     * as long as the list is not empty; an empty list draws the world again.
     * While it is set it wins over the blocks setBlockHidden hides.
     */
    virtual void setVisibleBlocks(const std::vector<std::string>& names) = 0;

    /**
     * Loaded blocks of these names within radius blocks of center, nearest
     * first, at most limit of them. It reads the same world as block, a
     * tick old at most, and scans whole sub-chunks, so call it now and then
     * rather than every frame.
     */
    virtual std::vector<FoundBlock> findBlocks(const std::vector<std::string>& names, const Vec3& center, double radius, size_t limit) const = 0;

    // Added in API 3 and kept last so older mods still find everything above.
    virtual std::vector<BossBar> bossBars() const = 0;
    /**
     * The host:port the connection actually went to. Unlike serverAddress
     * it is a real address for Realms and featured servers too; empty until
     * the client has looked it up.
     */
    virtual std::string serverEndpoint() const = 0;

    // Added in API 4 and kept last so older mods still find everything above.
    /**
     * The block's collision boxes, joined to neighbouring stairs, fences,
     * walls and panes the way the player collides with them; empty for air,
     * liquids and unloaded blocks.
     */
    virtual std::vector<Box> collision(const BlockPos& position) const = 0;

    /**
     * The box the crosshair aims at and outlines, empty for blocks the look
     * ray passes through.
     */
    virtual std::vector<Box> outline(const BlockPos& position) const = 0;
    virtual std::optional<BlockProps> properties(const BlockPos& position) const = 0;

    /**
     * Like findBlocks by name, with the blocks chosen by what they are.
     * accept runs once per distinct block state, not once per block.
     */
    virtual std::vector<FoundBlock> findBlocksMatching(const std::function<bool(const BlockProps&)>& accept, const Vec3& center, double radius, size_t limit) const = 0;

    /**
     * Roughly how many ticks of holding attack, from the first hit, break
     * the loaded block at position with the item held and the effects,
     * enchantments and footing of the last tick: 1 for one that breaks at
     * the first hit, -1 for one that never breaks or is not loaded.
     */
    virtual int breakTicks(const BlockPos& position) const = 0;

    std::optional<Entity> nearestEntity(const Vec3& from, double radius, const std::function<bool(const Entity&)>& accept = {}) const
    {
        std::optional<Entity> best;
        double bestDistance = radius * radius;
        for (Entity& candidate : entities()) {
            double dx = candidate.position.x - from.x;
            double dy = candidate.position.y - from.y;
            double dz = candidate.position.z - from.z;
            double distance = dx * dx + dy * dy + dz * dz;
            if (distance <= bestDistance && (!accept || accept(candidate))) {
                bestDistance = distance;
                best = std::move(candidate);
            }
        }
        return best;
    }
};

}
