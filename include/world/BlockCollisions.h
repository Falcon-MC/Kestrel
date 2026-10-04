#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kestrel::world {

/**
 * An axis aligned box in single precision, the precision movement is
 * simulated in.
 */
struct CollisionBox {
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;

    bool intersects(const CollisionBox& other) const
    {
        return maxX > other.minX && minX < other.maxX && maxY > other.minY && minY < other.maxY && maxZ > other.minZ && minZ < other.maxZ;
    }
};

enum CollisionFlag : uint16_t {
    CollisionSolid = 1,
    CollisionTransparent = 2,
    CollisionClimbable = 4,
    CollisionFence = 8,
    CollisionFenceGate = 16,
    CollisionWall = 32,
    CollisionStairs = 64,
    CollisionTrapdoor = 128,
    CollisionOpen = 256,
    CollisionStainedGlass = 512,
    CollisionLiquid = 1024,
    CollisionUpsideDown = 2048,
};

enum CollisionShape : int16_t {
    ShapeFence = -1,
    ShapeWall = -2,
    ShapeThin = -3,
    ShapeVine = -4,
    // A block the server defines, its boxes held by the state.
    ShapeCustom = -5,
};

/**
 * How one block state collides: its name, its fixed shape or the kind of
 * shape its neighbours decide, its flags and its facing (a block face index,
 * the depth of a liquid, the drag of a bubble column or the side bits of a
 * vine).
 */
struct CollisionState {
    uint16_t name = 0;
    int16_t shape = 0;
    uint16_t flags = 0;
    int8_t face = -1;
    const CollisionBox* box = nullptr;
    uint16_t boxCount = 0;
};

/**
 * The collision shapes of every vanilla block state, keyed by block state
 * hash, as the server resolves movement against them.
 */
class BlockCollisions {
public:
    using Lookup = std::function<const CollisionState*(int32_t x, int32_t y, int32_t z)>;

    static const BlockCollisions& shared();

    const CollisionState* find(uint32_t hash) const;

    /**
     * A plain full cube, for block states the table does not know.
     */
    const CollisionState* fullBlock() const
    {
        return &cube;
    }
    const std::string& name(const CollisionState& state) const;
    bool named(const CollisionState* state, std::string_view name) const;

    /**
     * Appends the boxes of the block at the given position, in world
     * coordinates, resolving connected shapes through the neighbour lookup.
     */
    void boxes(const CollisionState& state, int32_t x, int32_t y, int32_t z, const Lookup& lookup, std::vector<CollisionBox>& out) const;

private:
    BlockCollisions();

    bool fenceConnects(const CollisionState& self, const CollisionState* other, int face) const;
    bool wallConnects(const CollisionState* other, int face) const;
    bool thinConnects(const CollisionState* other, int face) const;
    void stairBoxes(const CollisionState& state, int32_t x, int32_t y, int32_t z, const Lookup& lookup, std::vector<CollisionBox>& out) const;

    std::vector<std::string> names;
    std::vector<std::vector<CollisionBox>> shapes;
    std::unordered_map<uint32_t, CollisionState> states;
    std::vector<bool> thinNames;
    CollisionState cube;
    uint16_t netherBrickFence = 0xFFFF;
    uint16_t glass = 0xFFFF;
};

}
