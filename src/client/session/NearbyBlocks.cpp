#include "SessionData.h"

#include "world/BlockCollisions.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

namespace {

constexpr int32_t MaxCollisionSpan = 8;

int32_t floorCell(double value)
{
    return static_cast<int32_t>(std::floor(value));
}

/**
 * How the block with the given value collides: its state in the collision
 * table, a full cube for a state the table does not know unless it is air or
 * drawn by a model, and nothing for liquids.
 */
const world::CollisionState* collisionState(const NearbyBlocks& area, uint32_t value)
{
    if (!area.assets || value == world::ImplicitAir) {
        return nullptr;
    }
    const world::BlockCollisions& table = world::BlockCollisions::shared();
    const world::CollisionState* state = table.find(area.assets->stateHash(value, area.ids.hashed, area.ids.sequential.get()));
    if (!state) {
        const world::BlockVisual& visual = area.assets->visual(value, area.ids.hashed, area.ids.sequential.get());
        if ((visual.flags & world::FlagAir) || visual.hasModel() || visual.liquid) {
            return nullptr;
        }
        return table.fullBlock();
    }
    if (state->flags & world::CollisionLiquid) {
        return nullptr;
    }
    return state;
}

}

uint32_t NearbyBlocks::value(int32_t x, int32_t y, int32_t z) const
{
    int32_t sx = (x >> 4) - base[0];
    int32_t sy = (y >> 4) - base[1];
    int32_t sz = (z >> 4) - base[2];
    if (sx < 0 || sy < 0 || sz < 0 || sx >= Span || sy >= Span || sz >= Span) {
        return world::ImplicitAir;
    }
    const std::shared_ptr<const world::SubChunk>& sub = subChunks[size_t((sx * Span + sy) * Span + sz)];
    if (!sub) {
        return world::ImplicitAir;
    }
    return sub->runtimeId(0, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15));
}

std::string NearbyBlocks::name(int32_t x, int32_t y, int32_t z) const
{
    uint32_t found = value(x, y, z);
    if (!assets || found == world::ImplicitAir) {
        return "minecraft:air";
    }
    return assets->blockName(found, ids.hashed, ids.sequential.get());
}

void NearbyBlocks::collisionBoxes(const std::array<double, 3>& low, const std::array<double, 3>& high, std::vector<std::array<double, 6>>& out) const
{
    std::array<int32_t, 3> from { floorCell(low[0]), floorCell(low[1]), floorCell(low[2]) };
    std::array<int32_t, 3> to { floorCell(high[0]), floorCell(high[1]), floorCell(high[2]) };
    for (size_t axis = 0; axis < 3; ++axis) {
        if (to[axis] - from[axis] > MaxCollisionSpan) {
            return;
        }
    }
    const world::BlockCollisions& table = world::BlockCollisions::shared();
    world::BlockCollisions::Lookup lookup = [this](int32_t x, int32_t y, int32_t z) {
        return collisionState(*this, value(x, y, z));
    };
    std::vector<world::CollisionBox> boxes;
    for (int32_t x = from[0]; x <= to[0]; ++x) {
        for (int32_t y = from[1]; y <= to[1]; ++y) {
            for (int32_t z = from[2]; z <= to[2]; ++z) {
                const world::CollisionState* state = collisionState(*this, value(x, y, z));
                if (!state) {
                    continue;
                }
                boxes.clear();
                table.boxes(*state, x, y, z, lookup, boxes);
                for (const world::CollisionBox& box : boxes) {
                    out.push_back({ box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ });
                }
            }
        }
    }
}

void NearbyBlocks::find(const std::array<double, 3>& center, int32_t radius, const std::function<bool(uint32_t)>& wanted, std::vector<std::pair<std::array<int32_t, 3>, uint32_t>>& out) const
{
    std::array<int32_t, 3> low { floorCell(center[0]) - radius, floorCell(center[1]) - radius, floorCell(center[2]) - radius };
    std::array<int32_t, 3> high { floorCell(center[0]) + radius, floorCell(center[1]) + radius, floorCell(center[2]) + radius };
    for (int32_t sx = 0; sx < Span; ++sx) {
        for (int32_t sy = 0; sy < Span; ++sy) {
            for (int32_t sz = 0; sz < Span; ++sz) {
                const std::shared_ptr<const world::SubChunk>& sub = subChunks[size_t((sx * Span + sy) * Span + sz)];
                if (!sub || sub->storages().empty()) {
                    continue;
                }
                std::array<int32_t, 3> origin { (base[0] + sx) * 16, (base[1] + sy) * 16, (base[2] + sz) * 16 };
                bool outside = false;
                for (size_t axis = 0; axis < 3; ++axis) {
                    if (origin[axis] + 15 < low[axis] || origin[axis] > high[axis]) {
                        outside = true;
                    }
                }
                if (outside) {
                    continue;
                }
                const std::vector<uint32_t>& palette = sub->storages().front().palette();
                if (std::none_of(palette.begin(), palette.end(), wanted)) {
                    continue;
                }
                for (int32_t x = std::max(low[0], origin[0]); x <= std::min(high[0], origin[0] + 15); ++x) {
                    for (int32_t y = std::max(low[1], origin[1]); y <= std::min(high[1], origin[1] + 15); ++y) {
                        for (int32_t z = std::max(low[2], origin[2]); z <= std::min(high[2], origin[2] + 15); ++z) {
                            uint32_t found = sub->runtimeId(0, uint32_t(x & 15), uint32_t(y & 15), uint32_t(z & 15));
                            if (wanted(found)) {
                                out.push_back({ { x, y, z }, found });
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * Shares the sub-chunks around the local player's feet with the client.
 */
void Session::publishNearby()
{
    auto area = std::make_shared<NearbyBlocks>();
    MotionVector feet = motion.position();
    area->dimension = motionDimension;
    area->base = {
        (floorCell(feet.x) >> 4) - NearbyBlocks::Radius,
        (floorCell(feet.y) >> 4) - NearbyBlocks::Radius,
        (floorCell(feet.z) >> 4) - NearbyBlocks::Radius,
    };
    area->assets = assets;
    area->ids = ids;
    area->subChunks.resize(size_t(NearbyBlocks::Span) * NearbyBlocks::Span * NearbyBlocks::Span);
    for (int32_t sx = 0; sx < NearbyBlocks::Span; ++sx) {
        for (int32_t sy = 0; sy < NearbyBlocks::Span; ++sy) {
            for (int32_t sz = 0; sz < NearbyBlocks::Span; ++sz) {
                world::SubChunkKey key { motionDimension, area->base[0] + sx, area->base[1] + sy, area->base[2] + sz };
                area->subChunks[size_t((sx * NearbyBlocks::Span + sy) * NearbyBlocks::Span + sz)] = world.store().subChunk(key);
            }
        }
    }
    std::lock_guard<std::mutex> guard(mutex);
    current.nearby = std::move(area);
}

}
