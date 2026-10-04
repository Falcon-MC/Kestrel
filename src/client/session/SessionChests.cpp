#include "client/session/SessionData.h"

#include <algorithm>

namespace kestrel {

namespace {

constexpr float LidStep = 0.1f;

}

/**
 * Moves every chest lid a tenth of the way per tick toward open or shut, the
 * game's pace. While a lid is off its closed rest the chunk leaves it out and
 * it is drawn on its own; once shut again the chunk takes it back.
 */
void Session::tickChestLids()
{
    std::vector<ChestLidView> views;
    for (auto it = chestLidStates.begin(); it != chestLidStates.end();) {
        const std::array<int32_t, 3>& cell = it->first;
        ChestLidState& state = it->second;
        bool wasMoving = state.openness > 0.0f;
        state.openness = std::clamp(state.openness + (state.open ? LidStep : -LidStep), 0.0f, 1.0f);
        bool moving = state.openness > 0.0f || state.open;
        if (moving != wasMoving) {
            markChestLid(cell, moving);
        }
        if (!moving) {
            it = chestLidStates.erase(it);
            continue;
        }
        if (assets) {
            const world::BlockVisual& visual = assets->visual(blockAt(cell[0], cell[1], cell[2]), ids.hashed, ids.sequential.get());
            const Tag* data = nullptr;
            std::shared_ptr<const world::BlockEntityMap> entities = world.store().blockEntities({ current.dimension, cell[0] >> 4, cell[1] >> 4, cell[2] >> 4 });
            if (entities) {
                auto found = entities->find(static_cast<uint16_t>(world::linearIndex(uint32_t(cell[0] & 15), uint32_t(cell[1] & 15), uint32_t(cell[2] & 15))));
                if (found != entities->end()) {
                    data = &found->second;
                }
            }
            world::ChestLid lid = assets->chestLid(visual, data, cell);
            if (lid.modelTemplate != world::NoModelTemplate) {
                views.push_back({ cell, lid, state.open, state.openness });
            }
        }
        ++it;
    }
    std::lock_guard<std::mutex> guard(mutex);
    current.chestLids = std::move(views);
}

/**
 * Flags the chest's block entity so the mesher draws the chest without its
 * lid, or clears the flag so the lid is baked back in.
 */
void Session::markChestLid(const std::array<int32_t, 3>& cell, bool moving)
{
    world::SubChunkKey key { current.dimension, cell[0] >> 4, cell[1] >> 4, cell[2] >> 4 };
    std::shared_ptr<const world::BlockEntityMap> entities = world.store().blockEntities(key);
    Tag data = Tag::ofCompound();
    if (entities) {
        auto found = entities->find(static_cast<uint16_t>(world::linearIndex(uint32_t(cell[0] & 15), uint32_t(cell[1] & 15), uint32_t(cell[2] & 15))));
        if (found != entities->end()) {
            data = found->second;
        }
    }
    bool marked = data.get(world::ChestLidMovingKey) != nullptr;
    if (marked == moving) {
        return;
    }
    if (moving) {
        data.putByte(world::ChestLidMovingKey, 1);
    } else {
        data.remove(world::ChestLidMovingKey);
    }
    world.store().setBlockEntity(current.dimension, cell[0], cell[1], cell[2], std::move(data));
}

}
