#include "world/BlockCollisions.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

using namespace kestrel::world;

namespace {

constexpr int32_t X = 10;
constexpr int32_t Y = 64;
constexpr int32_t Z = -5;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

bool near(float actual, float expected)
{
    return std::abs(actual - expected) < 1.0e-4f;
}

std::vector<CollisionBox> alone(const CollisionState& state)
{
    const BlockCollisions& table = BlockCollisions::shared();
    BlockCollisions::Lookup nothing = [](int32_t, int32_t, int32_t) -> const CollisionState* {
        return nullptr;
    };
    std::vector<CollisionBox> boxes;
    table.boxes(state, X, Y, Z, nothing, boxes);
    return boxes;
}

std::vector<const CollisionState*> statesOf(std::string_view name)
{
    std::vector<const CollisionState*> states = BlockCollisions::shared().statesNamed(name);
    require(!states.empty(), "The collision table must know the block");
    return states;
}

void slabIsHalfABlock()
{
    bool bottom = false;
    bool top = false;
    for (const CollisionState* state : statesOf("smooth_stone_slab")) {
        std::vector<CollisionBox> boxes = alone(*state);
        require(boxes.size() == 1, "A slab must be one box");
        const CollisionBox& box = boxes.front();
        require(near(box.maxY - box.minY, 0.5f), "A slab must be half a block high");
        require(near(box.maxX - box.minX, 1.0f) && near(box.maxZ - box.minZ, 1.0f), "A slab must cover the whole cell");
        bottom = bottom || near(box.minY, float(Y));
        top = top || near(box.minY, float(Y) + 0.5f);
    }
    require(bottom && top, "Slabs must come as bottom and top halves");
}

void stairsAreTwoBoxes()
{
    for (const CollisionState* state : statesOf("oak_stairs")) {
        std::vector<CollisionBox> boxes = alone(*state);
        require(boxes.size() == 2, "Straight stairs must be a half slab and a step");
        for (const CollisionBox& box : boxes) {
            require(box.minY >= float(Y) - 1.0e-4f && box.maxY <= float(Y) + 1.0f + 1.0e-4f, "Stairs must stay inside their cell");
        }
    }
}

void fenceIsOneAndAHalfHigh()
{
    for (const CollisionState* state : statesOf("oak_fence")) {
        std::vector<CollisionBox> boxes = alone(*state);
        require(boxes.size() == 1, "A lone fence must be one post");
        const CollisionBox& box = boxes.front();
        require(near(box.maxY - box.minY, 1.5f), "A fence must be a block and a half high");
        require(near(box.maxX - box.minX, 0.25f) && near(box.maxZ - box.minZ, 0.25f), "A lone fence post must be a quarter block wide");
    }
    const BlockCollisions& table = BlockCollisions::shared();
    BlockCollisions::Lookup solidNorth = [&table](int32_t x, int32_t y, int32_t z) -> const CollisionState* {
        return x == X && y == Y && z == Z - 1 ? table.fullBlock() : nullptr;
    };
    std::vector<CollisionBox> joined;
    table.boxes(*statesOf("oak_fence").front(), X, Y, Z, solidNorth, joined);
    require(joined.size() == 1 && near(joined.front().minZ, float(Z)), "A fence must reach a solid block beside it");
}

void carpetIsThin()
{
    for (const CollisionState* state : statesOf("white_carpet")) {
        std::vector<CollisionBox> boxes = alone(*state);
        require(boxes.size() == 1, "A carpet must be one box");
        require(near(boxes.front().maxY - boxes.front().minY, 1.0f / 16.0f), "A carpet must be a sixteenth of a block thick");
    }
}

}

int main()
{
    slabIsHalfABlock();
    stairsAreTwoBoxes();
    fenceIsOneAndAHalfHigh();
    carpetIsThin();
}
