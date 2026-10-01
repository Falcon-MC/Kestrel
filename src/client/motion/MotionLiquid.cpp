#include "MotionMath.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

using namespace motion;

namespace {

constexpr float WaterFlowStrength = 0.014f;
constexpr float LavaFlowStrength = 0.0035f;
constexpr float SwimDescent = 0.04f;
constexpr float SwimPoseHeight = 0.6f;
constexpr float SwimEyeProbe = 1.62f;

struct FlowFace {
    int32_t dx;
    int32_t dz;
};

constexpr FlowFace FlowFaces[] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };

float surfaceHeight(int32_t depth)
{
    if (depth >= 8) {
        return 1.0f;
    }
    return static_cast<float>(9 - depth) / 9.0f;
}

int32_t decay(int32_t depth)
{
    return depth >= 8 ? 0 : depth;
}

float length(const MotionVector& vector)
{
    return std::sqrt(vector.lengthSquared());
}

}

bool PlayerMotion::liquidAt(int32_t x, int32_t y, int32_t z, bool& lava, int32_t& depth) const
{
    MotionCell here = cell(x, y, z);
    for (const world::CollisionState* state : { here.extra, here.primary }) {
        if (!state) {
            continue;
        }
        const std::string& name = table->name(*state);
        if (name == "bubble_column") {
            lava = false;
            depth = 0;
            return true;
        }
        bool water = name == "water" || name == "flowing_water";
        bool molten = name == "lava" || name == "flowing_lava";
        if (water || molten) {
            lava = molten;
            depth = std::max<int32_t>(state->face, 0);
            return true;
        }
    }
    return false;
}

/**
 * The liquid blocks of one kind whose surface the player's box reaches, the
 * box shrunk the way the liquid test shrinks it.
 */
bool PlayerMotion::scanLiquids(bool lava, bool firstOnly, std::vector<std::array<int32_t, 3>>* found) const
{
    world::CollisionBox box = boundingBox();
    float insetX = lava ? 0.1f : FluidHorizontalInset;
    float insetY = lava ? 0.4f : FluidVerticalInset;
    float minX = box.minX + insetX;
    float maxX = box.maxX - insetX;
    float minY = box.minY + insetY;
    float maxY = box.maxY - insetY;
    float minZ = box.minZ + insetX;
    float maxZ = box.maxZ - insetX;
    if (minX > maxX) {
        minX = maxX = (box.minX + box.maxX) * 0.5f;
    }
    if (minY > maxY) {
        minY = maxY = (box.minY + box.maxY) * 0.5f;
    }
    if (minZ > maxZ) {
        minZ = maxZ = (box.minZ + box.maxZ) * 0.5f;
    }
    if (!boundedQuery(box)) return false;
    bool touched = false;
    for (int32_t x = floorInt(minX); x < floorInt(maxX + 1.0f); ++x) {
        for (int32_t y = floorInt(minY); y < floorInt(maxY + 1.0f); ++y) {
            for (int32_t z = floorInt(minZ); z < floorInt(maxZ + 1.0f); ++z) {
                bool isLava = false;
                int32_t depth = 0;
                if (!liquidAt(x, y, z, isLava, depth) || isLava != lava) {
                    continue;
                }
                float surface = static_cast<float>(y) + surfaceHeight(depth);
                if (maxY > static_cast<float>(y) && minY < surface) {
                    if (firstOnly) return true;
                    touched = true;
                    if (found) found->push_back({ x, y, z });
                }
            }
        }
    }
    return touched;
}

bool PlayerMotion::touchesLiquid(bool lava) const
{
    world::CollisionBox box = boundingBox();
    const auto& old = scratch->liquidBox;
    bool same = box.minX == old.minX && box.minY == old.minY && box.minZ == old.minZ && box.maxX == old.maxX && box.maxY == old.maxY && box.maxZ == old.maxZ;
    if (!same) {
        scratch->liquidBox = box;
        scratch->liquidsValid = false;
        scratch->liquidKnown = {};
    }
    size_t kind = lava ? 1 : 0;
    if (scratch->liquidsValid) return !scratch->liquids[kind].empty();
    if (!scratch->liquidKnown[kind]) {
        scratch->liquidPresent[kind] = scanLiquids(lava, true, nullptr);
        scratch->liquidKnown[kind] = true;
    }
    return scratch->liquidPresent[kind];
}

const std::vector<std::array<int32_t, 3>>& PlayerMotion::touchingLiquid(bool lava) const
{
    world::CollisionBox box = boundingBox();
    const auto& old = scratch->liquidBox;
    bool same = box.minX == old.minX && box.minY == old.minY && box.minZ == old.minZ && box.maxX == old.maxX && box.maxY == old.maxY && box.maxZ == old.maxZ;
    if (!scratch->liquidsValid || !same) {
        for (auto& liquid : scratch->liquids) liquid.clear();
        if (boundedQuery(box)) {
            auto bounds = [&](bool molten) {
                float horizontal = molten ? 0.1f : FluidHorizontalInset;
                float vertical = molten ? 0.4f : FluidVerticalInset;
                world::CollisionBox result { box.minX + horizontal, box.minY + vertical, box.minZ + horizontal, box.maxX - horizontal, box.maxY - vertical, box.maxZ - horizontal };
                if (result.minX > result.maxX) result.minX = result.maxX = (box.minX + box.maxX) * 0.5f;
                if (result.minY > result.maxY) result.minY = result.maxY = (box.minY + box.maxY) * 0.5f;
                if (result.minZ > result.maxZ) result.minZ = result.maxZ = (box.minZ + box.maxZ) * 0.5f;
                return result;
            };
            auto water = bounds(false);
            auto lavaBox = bounds(true);
            for (int32_t x = floorInt(std::min(water.minX, lavaBox.minX)); x < floorInt(std::max(water.maxX, lavaBox.maxX) + 1.0f); ++x) {
                for (int32_t y = floorInt(std::min(water.minY, lavaBox.minY)); y < floorInt(std::max(water.maxY, lavaBox.maxY) + 1.0f); ++y) {
                    for (int32_t z = floorInt(std::min(water.minZ, lavaBox.minZ)); z < floorInt(std::max(water.maxZ, lavaBox.maxZ) + 1.0f); ++z) {
                        bool molten = false;
                        int32_t depth = 0;
                        if (!liquidAt(x, y, z, molten, depth)) continue;
                        const auto& probe = molten ? lavaBox : water;
                        if (x < floorInt(probe.minX) || x >= floorInt(probe.maxX + 1.0f) || y < floorInt(probe.minY) || y >= floorInt(probe.maxY + 1.0f) || z < floorInt(probe.minZ) || z >= floorInt(probe.maxZ + 1.0f)) continue;
                        if (probe.maxY > float(y) && probe.minY < float(y) + surfaceHeight(depth)) scratch->liquids[molten ? 1 : 0].push_back({ x, y, z });
                    }
                }
            }
        }
        scratch->liquidBox = box;
        scratch->liquidsValid = true;
        scratch->liquidKnown = { true, true };
        scratch->liquidPresent = { !scratch->liquids[0].empty(), !scratch->liquids[1].empty() };
    }
    return scratch->liquids[lava ? 1 : 0];
}

bool PlayerMotion::closesFlow(int32_t x, int32_t y, int32_t z) const
{
    const world::CollisionState* state = cellState(x, y, z);
    if (!state) {
        return false;
    }
    std::vector<world::CollisionBox> boxes;
    world::BlockCollisions::Lookup neighbours = [this](int32_t nx, int32_t ny, int32_t nz) {
        return cellState(nx, ny, nz);
    };
    table->boxes(*state, x, y, z, neighbours, boxes);
    return !boxes.empty();
}

/**
 * The direction a liquid block pushes along: toward neighbours that are
 * lower, over drops, and down a falling column that hits a wall.
 */
MotionVector PlayerMotion::liquidFlow(int32_t x, int32_t y, int32_t z, bool lava, int32_t depth) const
{
    int32_t current = decay(depth);
    MotionVector flow;
    for (const FlowFace& face : FlowFaces) {
        int32_t nx = x + face.dx;
        int32_t nz = z + face.dz;
        MotionVector direction { static_cast<float>(face.dx), 0.0f, static_cast<float>(face.dz) };
        bool neighbourLava = false;
        int32_t neighbourDepth = 0;
        if (liquidAt(nx, y, nz, neighbourLava, neighbourDepth) && neighbourLava == lava) {
            if (!closesFlow(x, y, z) && !closesFlow(nx, y, nz)) {
                flow = flow + direction.scaled(static_cast<float>(decay(neighbourDepth) - current));
            }
            continue;
        }
        if (closesFlow(x, y, z) || closesFlow(nx, y, nz)) {
            continue;
        }
        bool lowerLava = false;
        int32_t lowerDepth = 0;
        if (liquidAt(nx, y - 1, nz, lowerLava, lowerDepth) && lowerLava == lava) {
            flow = flow + direction.scaled(static_cast<float>(decay(lowerDepth) - current + 8));
        }
    }
    if (depth >= 8) {
        for (const FlowFace& face : FlowFaces) {
            if (closesFlow(x + face.dx, y, z + face.dz) || closesFlow(x + face.dx, y + 1, z + face.dz)) {
                float size = length(flow);
                if (size > 1.0E-4f) {
                    flow = flow.scaled(1.0f / size);
                }
                flow.y -= 6.0f;
                break;
            }
        }
    }
    float size = length(flow);
    return size > 1.0E-4f ? flow.scaled(1.0f / size) : MotionVector {};
}

void PlayerMotion::applyLiquidFlow(const std::vector<std::array<int32_t, 3>>& blocks, bool lava)
{
    MotionVector flow;
    for (const std::array<int32_t, 3>& block : blocks) {
        bool isLava = false;
        int32_t depth = 0;
        if (liquidAt(block[0], block[1], block[2], isLava, depth) && isLava == lava) {
            flow = flow + liquidFlow(block[0], block[1], block[2], lava, depth);
        }
    }
    float size = length(flow);
    if (size >= 1.0E-4f) {
        velocity = velocity + flow.scaled((lava ? LavaFlowStrength : WaterFlowStrength) / size);
    }
}

/**
 * Starts swimming while sprinting with the head under water and stops once
 * the player stops sprinting or leaves the water; the swim pose is as tall
 * as it is wide, and the swim amount approaches the pose by 0.2 per tick.
 */
void PlayerMotion::updateSwimming(bool inWater, MotionTick& tick)
{
    stoppedSwimmingThisTick = false;
    bool headLava = false;
    int32_t headDepth = 0;
    float eyeY = feet.y + SwimEyeProbe * scale;
    int32_t eyeBlock = floorInt(eyeY);
    bool headInWater = liquidAt(floorInt(feet.x), eyeBlock, floorInt(feet.z), headLava, headDepth) && !headLava
        && eyeY < static_cast<float>(eyeBlock) + surfaceHeight(headDepth);
    bool movingForward = impulseForward > 0.0f;
    if (isSwimming && (!isSprinting || !movingForward || !inWater || isFlying || isGliding)) {
        isSwimming = false;
        stoppedSwimmingThisTick = true;
        tick.stopSwimming = true;
    } else if (!isSwimming && isSprinting && movingForward && inWater && headInWater && !isFlying && !isGliding) {
        isSwimming = true;
        tick.startSwimming = true;
    }
    swimAmount = std::clamp(swimAmount + (isSwimming ? 0.2f : -0.2f), 0.0f, 1.0f);
    if (isSwimming) {
        height = SwimPoseHeight;
    }
}

/**
 * While swimming, steers vertical speed toward the player's look direction.
 */
void PlayerMotion::updateSwimTravel()
{
    if (!isSwimming) {
        return;
    }
    float targetY = -sine(pitch * Pi / 180.0f);
    float rate = targetY < -0.2f ? 0.085f : 0.06f;
    velocity.y += (targetY - velocity.y) * rate;
}

void PlayerMotion::applyBubbleColumn(const Fluid& fluid)
{
    if (fluid.bubbleDirection == 0) {
        return;
    }
    if (fluid.bubbleDirection < 0) {
        velocity.y = std::max(fluid.bubbleSurface ? -0.9f : -0.3f, velocity.y - 0.03f);
    } else {
        velocity.y = fluid.bubbleSurface ? std::min(1.8f, velocity.y + 0.1f) : std::min(0.7f, velocity.y + 0.06f);
    }
}

}
