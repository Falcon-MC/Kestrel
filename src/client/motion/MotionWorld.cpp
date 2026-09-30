#include "MotionMath.h"

#include <cfloat>

namespace kestrel {

using namespace motion;

world::CollisionBox PlayerMotion::boundingBox() const
{
    float halfWidth = width * 0.5f * scale;
    float scaledHeight = height * scale;
    return {
        feet.x - halfWidth + 1.0E-4f,
        feet.y,
        feet.z - halfWidth + 1.0E-4f,
        feet.x + halfWidth - 1.0E-4f,
        feet.y + scaledHeight,
        feet.z + halfWidth - 1.0E-4f,
    };
}

MotionCell PlayerMotion::cell(int32_t x, int32_t y, int32_t z) const
{
    return lookup ? (*lookup)(x, y, z) : MotionCell {};
}

const world::CollisionState* PlayerMotion::cellState(int32_t x, int32_t y, int32_t z) const
{
    return cell(x, y, z).primary;
}

bool PlayerMotion::named(const world::CollisionState* state, std::string_view name) const
{
    return table->named(state, name);
}

std::vector<world::CollisionBox> PlayerMotion::collisionBoxes(const world::CollisionBox& area) const
{
    std::vector<world::CollisionBox> found;
    std::vector<world::CollisionBox> boxes;
    world::BlockCollisions::Lookup neighbours = [this](int32_t x, int32_t y, int32_t z) {
        return cellState(x, y, z);
    };
    int32_t minX = floorInt(area.minX) - 1;
    int32_t minY = floorInt(area.minY) - 1;
    int32_t minZ = floorInt(area.minZ) - 1;
    int32_t maxX = floorInt(area.maxX) + 1;
    int32_t maxY = floorInt(area.maxY) + 1;
    int32_t maxZ = floorInt(area.maxZ) + 1;
    for (int32_t x = minX; x <= maxX; ++x) {
        for (int32_t z = minZ; z <= maxZ; ++z) {
            for (int32_t y = minY; y <= maxY; ++y) {
                const world::CollisionState* state = cellState(x, y, z);
                if (!state || named(state, "powder_snow")) {
                    continue;
                }
                boxes.clear();
                table->boxes(*state, x, y, z, neighbours, boxes);
                for (const world::CollisionBox& box : boxes) {
                    if (box.intersects(area)) {
                        found.push_back(box);
                    }
                }
            }
        }
    }
    return found;
}

const world::CollisionState* PlayerMotion::blockView(int32_t x, int32_t y, int32_t z) const
{
    MotionCell here = cell(x, y, z);
    const world::CollisionState* state = here.primary;
    if (!state) {
        return nullptr;
    }
    const std::string& name = table->name(*state);
    bool relevant = state->shape < 0 || (state->flags & (world::CollisionClimbable | world::CollisionLiquid)) || friction(state) != 0.6f
        || name.find("water") != std::string::npos || name.find("lava") != std::string::npos;
    if (!relevant && state->shape >= 0) {
        std::vector<world::CollisionBox> boxes;
        world::BlockCollisions::Lookup neighbours = [this](int32_t nx, int32_t ny, int32_t nz) {
            return cellState(nx, ny, nz);
        };
        table->boxes(*state, x, y, z, neighbours, boxes);
        relevant = !boxes.empty();
    }
    if (!relevant) {
        static constexpr std::string_view Special[] = { "web", "sweet_berry_bush", "honey_block", "slime", "bed", "soul_sand", "bamboo", "scaffolding", "moving_block", "powder_snow", "bubble_column" };
        for (std::string_view special : Special) {
            relevant = relevant || name.find(special) != std::string::npos;
        }
    }
    return relevant ? state : nullptr;
}

float PlayerMotion::friction(const world::CollisionState* state) const
{
    if (!state) {
        return 0.6f;
    }
    const std::string& name = table->name(*state);
    if (name == "blue_ice") {
        return 0.989f;
    }
    if (name == "ice" || name == "packed_ice" || name == "frosted_ice") {
        return 0.98f;
    }
    if (name == "slime" || name == "honey_block") {
        return 0.8f;
    }
    return 0.6f;
}

bool PlayerMotion::climbable(int32_t x, int32_t y, int32_t z) const
{
    MotionCell here = cell(x, y, z);
    return (here.primary && (here.primary->flags & world::CollisionClimbable)) || (here.extra && (here.extra->flags & world::CollisionClimbable));
}

PlayerMotion::Fluid PlayerMotion::fluidState(const world::CollisionBox& area) const
{
    Fluid fluid;
    float minX = area.minX + FluidHorizontalInset;
    float maxX = area.maxX - FluidHorizontalInset;
    float minZ = area.minZ + FluidHorizontalInset;
    float maxZ = area.maxZ - FluidHorizontalInset;
    float minY = area.minY + FluidVerticalInset;
    float maxY = area.maxY - FluidVerticalInset;
    if (minY > maxY) {
        minY = (area.minY + area.maxY) * 0.5f;
        maxY = minY;
    }
    auto kind = [&](const world::CollisionState* state, bool& water, bool& lava, int& bubble) {
        if (!state) {
            return;
        }
        const std::string& name = table->name(*state);
        if (name == "bubble_column") {
            water = true;
            bubble = state->face > 0 ? -1 : 1;
        } else if (name.find("water") != std::string::npos) {
            water = true;
        }
        if (name.find("lava") != std::string::npos) {
            lava = true;
        }
    };
    for (int32_t x = floorInt(minX); x <= floorInt(maxX); ++x) {
        for (int32_t y = floorInt(minY); y <= floorInt(maxY); ++y) {
            for (int32_t z = floorInt(minZ); z <= floorInt(maxZ); ++z) {
                MotionCell here = cell(x, y, z);
                bool water = false;
                bool lava = false;
                int bubble = 0;
                kind(here.primary, water, lava, bubble);
                kind(here.extra, water, lava, bubble);
                if (!water && !lava) {
                    continue;
                }
                fluid.water = fluid.water || water;
                fluid.lava = fluid.lava || lava;
                if (bubble != 0 && fluid.bubbleDirection == 0) {
                    fluid.bubbleDirection = bubble;
                    fluid.bubbleSurface = blockView(x, y + 1, z) == nullptr;
                }
            }
        }
    }
    return fluid;
}

bool PlayerMotion::insideBlockNamed(std::string_view name) const
{
    world::CollisionBox box = boundingBox();
    int32_t minX = floorInt(box.minX - 1.0f);
    int32_t minY = floorInt(box.minY - 1.0f);
    int32_t minZ = floorInt(box.minZ - 1.0f);
    int32_t maxX = floorInt(box.maxX + 1.0f);
    int32_t maxY = floorInt(box.maxY + 1.0f);
    int32_t maxZ = floorInt(box.maxZ + 1.0f);
    for (int32_t x = minX; x <= maxX; ++x) {
        for (int32_t y = minY; y <= maxY; ++y) {
            for (int32_t z = minZ; z <= maxZ; ++z) {
                if (!named(blockView(x, y, z), name)) {
                    continue;
                }
                world::CollisionBox block { float(x), float(y), float(z), x + 1.0f, y + 1.0f, z + 1.0f };
                if (box.intersects(block)) {
                    return true;
                }
            }
        }
    }
    return false;
}

const world::CollisionState* PlayerMotion::blockUnder(float distance) const
{
    float y = feet.y + -distance;
    return blockView(floorInt(feet.x), floorInt(y), floorInt(feet.z));
}

float PlayerMotion::jumpPreventionMultiplier() const
{
    if (!onGround) {
        return 1.0f;
    }
    world::CollisionBox box = boundingBox();
    int32_t x = floorInt(feet.x);
    int32_t z = floorInt(feet.z);
    int32_t feetY = floorInt(box.minY);
    auto prevents = [&](const world::CollisionState* state) {
        return named(state, "honey") || named(state, "honey_block");
    };
    return prevents(blockView(x, feetY, z)) || prevents(blockView(x, feetY - 1, z)) ? HoneyJumpFactor : 1.0f;
}

bool PlayerMotion::canClimbOut(float boxBottom) const
{
    world::CollisionBox box = boundingBox();
    float lift = velocity.y + 0.6f;
    lift = lift - box.minY;
    lift = lift + boxBottom;
    world::CollisionBox probe = offset(box, { velocity.x, lift, velocity.z });
    if (!collisionBoxes(probe).empty()) {
        return false;
    }
    Fluid probed = fluidState(probe);
    return !probed.water && !probed.lava;
}

bool PlayerMotion::findSupportingBlock(const world::CollisionBox& area, std::array<int32_t, 3>& found) const
{
    int32_t playerX = floorInt(feet.x);
    int32_t playerY = floorInt(feet.y);
    int32_t playerZ = floorInt(feet.z);
    float centerX = playerX + 0.5f;
    float centerY = playerY + 0.5f;
    float centerZ = playerZ + 0.5f;
    float closest = FLT_MAX - 1.0f;
    bool any = false;
    std::vector<world::CollisionBox> boxes;
    world::BlockCollisions::Lookup neighbours = [this](int32_t x, int32_t y, int32_t z) {
        return cellState(x, y, z);
    };
    for (int32_t x = floorInt(area.minX) - 1; x <= floorInt(area.maxX) + 1; ++x) {
        for (int32_t z = floorInt(area.minZ) - 1; z <= floorInt(area.maxZ) + 1; ++z) {
            for (int32_t y = floorInt(area.minY) - 1; y <= floorInt(area.maxY) + 1; ++y) {
                const world::CollisionState* state = cellState(x, y, z);
                if (!state || named(state, "powder_snow")) {
                    continue;
                }
                boxes.clear();
                table->boxes(*state, x, y, z, neighbours, boxes);
                bool intersects = false;
                for (const world::CollisionBox& box : boxes) {
                    intersects = intersects || area.intersects(box);
                }
                if (!intersects) {
                    continue;
                }
                float dx = x - centerX;
                float dy = y - centerY;
                float dz = z - centerZ;
                float distance = dx * dx + dy * dy + dz * dz;
                if (distance < closest) {
                    closest = distance;
                    found = { x, y, z };
                    any = true;
                }
            }
        }
    }
    return any;
}

}
