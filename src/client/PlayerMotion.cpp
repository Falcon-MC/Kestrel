#include "client/PlayerMotion.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace kestrel {

namespace {

constexpr float AirFriction = 0.91f;
constexpr float NormalGravity = 0.08f;
constexpr float SlowFallingGravity = 0.01f;
constexpr float GravityMultiplier = 0.98f;
constexpr float LevitationMultiplier = 0.05f;
constexpr float StepHeight = 0.5625f;
constexpr float ClimbSpeed = 0.2f;
constexpr float PreventedJumpMultiplier = 0.6f;
constexpr float SneakInput = 0.3f;
constexpr int32_t JumpDelayTicks = 10;
constexpr float CollisionEpsilon = 1.0E-5f;
constexpr float PenetrationEpsilonSquared = 1.0E-11f;
constexpr float EdgeInset = 0.025f;
constexpr float EdgeStep = 0.05f;
constexpr float WaterAcceleration = 0.02f;
constexpr float WaterDrag = 0.8f;
constexpr float WaterFastDrag = 0.9f;
constexpr float WaterAscent = 0.04f;
constexpr float WaterGravity = 0.02f;
constexpr float LedgeClimb = 0.3f;
constexpr float LavaDrag = 0.5f;
constexpr float FluidHorizontalInset = 0.001f;
constexpr float FluidVerticalInset = 0.40099999f;
constexpr int32_t FlyToggleWindow = 7;
constexpr float Pi = 3.1415927f;

constexpr int32_t GameCreative = 1;
constexpr int32_t GameSpectator = 6;

float floatSin(float value)
{
    static constexpr float SinCoefficients[] = { 1.5896230e-10f, -2.5050748e-8f, 2.7557314e-6f, -1.9841270e-4f, 8.333334e-3f, -1.6666667e-1f };
    static constexpr float CosCoefficients[] = { -1.1358537e-11f, 2.0875701e-9f, -2.7557314e-7f, 2.4801588e-5f, -1.3888889e-3f, 4.1666668e-2f };
    if (value == 0.0f || std::isnan(value)) {
        return value;
    }
    if (std::isinf(value)) {
        return NAN;
    }
    bool negative = false;
    if (value < 0.0f) {
        value = -value;
        negative = true;
    }
    int64_t octant = static_cast<int64_t>(value * (4.0f / Pi));
    float octantValue = static_cast<float>(octant);
    if ((octant & 1) == 1) {
        octant++;
        octantValue++;
    }
    octant &= 7;
    float r = value - octantValue * 0.7853981f;
    r = r - octantValue * 3.7748947e-8f;
    r = r - octantValue * 2.6951514e-15f;
    if (octant > 3) {
        negative = !negative;
        octant -= 4;
    }
    float squared = r * r;
    float result;
    if (octant == 1 || octant == 2) {
        float series = CosCoefficients[0] * squared + CosCoefficients[1];
        series = series * squared + CosCoefficients[2];
        series = series * squared + CosCoefficients[3];
        series = series * squared + CosCoefficients[4];
        series = series * squared + CosCoefficients[5];
        float half = 0.5f * squared;
        float fourth = squared * squared;
        result = 1.0f - half + fourth * series;
    } else {
        float series = SinCoefficients[0] * squared + SinCoefficients[1];
        series = series * squared + SinCoefficients[2];
        series = series * squared + SinCoefficients[3];
        series = series * squared + SinCoefficients[4];
        series = series * squared + SinCoefficients[5];
        float cube = r * squared;
        result = r + cube * series;
    }
    return negative ? -result : result;
}

const std::array<float, 65536>& sineTable()
{
    static const std::array<float, 65536> table = [] {
        std::array<float, 65536> values {};
        for (size_t index = 0; index < values.size(); ++index) {
            float angle = static_cast<float>(index) * Pi;
            angle = angle * 2.0f;
            angle = angle / 65536.0f;
            values[index] = floatSin(angle);
        }
        return values;
    }();
    return table;
}

float sine(float value)
{
    return sineTable()[static_cast<size_t>(static_cast<int32_t>(value * 10430.378f) & 65535)];
}

float cosine(float value)
{
    return sineTable()[static_cast<size_t>(static_cast<int32_t>(value * 10430.378f + 16384.0f) & 65535)];
}

int32_t floorInt(float value)
{
    return static_cast<int32_t>(std::floor(value));
}

world::CollisionBox offset(const world::CollisionBox& box, const MotionVector& vector)
{
    return { box.minX + vector.x, box.minY + vector.y, box.minZ + vector.z, box.maxX + vector.x, box.maxY + vector.y, box.maxZ + vector.z };
}

world::CollisionBox grow(const world::CollisionBox& box, float x, float y, float z)
{
    return { box.minX - x, box.minY - y, box.minZ - z, box.maxX + x, box.maxY + y, box.maxZ + z };
}

world::CollisionBox extend(const world::CollisionBox& box, const MotionVector& vector)
{
    return {
        vector.x < 0.0f ? box.minX + vector.x : box.minX,
        vector.y < 0.0f ? box.minY + vector.y : box.minY,
        vector.z < 0.0f ? box.minZ + vector.z : box.minZ,
        vector.x > 0.0f ? box.maxX + vector.x : box.maxX,
        vector.y > 0.0f ? box.maxY + vector.y : box.maxY,
        vector.z > 0.0f ? box.maxZ + vector.z : box.maxZ,
    };
}

MotionVector feetOf(const world::CollisionBox& box)
{
    return { (box.minX + box.maxX) * 0.5f, box.minY, (box.minZ + box.maxZ) * 0.5f };
}

float length(const MotionVector& vector)
{
    return static_cast<float>(std::sqrt(static_cast<double>(vector.lengthSquared())));
}

struct ClipResult {
    MotionVector movement;
    int axis = 0;
    float penetration = 0.0f;
};

ClipResult clip(const world::CollisionBox& stationary, const world::CollisionBox& moving, const MotionVector& movement, bool oneWay)
{
    if (stationary.minX == stationary.maxX && stationary.minY == stationary.maxY && stationary.minZ == stationary.maxZ) {
        return { movement, 0, 0.0f };
    }
    float stationaryMinimum[3] = { stationary.minX, stationary.minY, stationary.minZ };
    float stationaryMaximum[3] = { stationary.maxX, stationary.maxY, stationary.maxZ };
    float movingMinimum[3] = { moving.minX, moving.minY, moving.minZ };
    float movingMaximum[3] = { moving.maxX, moving.maxY, moving.maxZ };
    float requested[3] = { movement.x, movement.y, movement.z };
    float penetration[3] = {};
    float signedPenetration[3] = {};
    float normal[3] = {};
    int separatingAxes = 0;
    int separatingAxis = 0;
    float minimumPenetration = FLT_MAX - 1.0f;

    for (int axis = 0; axis < 3; ++axis) {
        float minimum = movingMaximum[axis] - stationaryMinimum[axis];
        float maximum = stationaryMaximum[axis] - movingMinimum[axis];
        if (std::abs(minimum) <= 1.0E-7f) {
            minimum = 0.0f;
        }
        if (std::abs(maximum) <= 1.0E-7f) {
            maximum = 0.0f;
        }
        float positiveMinimum = std::max(0.0f, minimum);
        float positiveMaximum = std::max(0.0f, maximum);
        if (positiveMinimum == 0.0f) {
            signedPenetration[axis] = minimum;
            normal[axis] = -1.0f;
            separatingAxes++;
            separatingAxis = axis;
        } else if (positiveMaximum == 0.0f) {
            signedPenetration[axis] = maximum;
            normal[axis] = 1.0f;
            separatingAxes++;
            separatingAxis = axis;
        } else if (positiveMinimum < positiveMaximum) {
            penetration[axis] = positiveMinimum;
            signedPenetration[axis] = positiveMinimum;
            normal[axis] = -1.0f;
        } else {
            penetration[axis] = positiveMaximum;
            signedPenetration[axis] = positiveMaximum;
            normal[axis] = 1.0f;
        }
        if (separatingAxes > 1) {
            return { movement, 0, 0.0f };
        }
        minimumPenetration = std::min(minimumPenetration, penetration[axis]);
    }

    if (separatingAxes == 0) {
        int axis = 0;
        if (penetration[1] < penetration[axis]) {
            axis = 1;
        }
        if (penetration[2] < penetration[axis]) {
            axis = 2;
        }
        if (!oneWay) {
            float desired = penetration[axis] * normal[axis];
            requested[axis] = desired > 0.0f ? std::max(desired, requested[axis]) : std::min(desired, requested[axis]);
        }
        return { { requested[0], requested[1], requested[2] }, axis, minimumPenetration };
    }

    float product = normal[separatingAxis] * requested[separatingAxis];
    float sweptPenetration = signedPenetration[separatingAxis] - product;
    if (sweptPenetration <= 0.0f) {
        return { movement, 0, 0.0f };
    }
    requested[separatingAxis] = signedPenetration[separatingAxis] * normal[separatingAxis];
    return { { requested[0], requested[1], requested[2] }, 0, 0.0f };
}

MotionVector clipAll(const std::vector<world::CollisionBox>& collisions, const world::CollisionBox& moving, MotionVector movement, bool oneWay, float* penetration)
{
    for (size_t index = collisions.size(); index-- > 0;) {
        ClipResult clipped = clip(collisions[index], moving, movement, oneWay);
        movement = clipped.movement;
        if (penetration && penetration[clipped.axis] < clipped.penetration) {
            penetration[clipped.axis] = clipped.penetration;
        }
    }
    return movement;
}

}

PlayerMotion::PlayerMotion()
    : table(&world::BlockCollisions::shared())
{
    sineTable();
}

void PlayerMotion::reset(const MotionVector& position)
{
    feet = position;
    velocity = {};
    pendingKnockback = {};
    hasKnockback = false;
    onGround = false;
    collideX = false;
    collideY = false;
    collideZ = false;
    jumpDelay = 0;
    penetratedLastFrame = false;
    stuckInCollider = false;
    hasSupportingBlock = false;
    teleported = false;
    ready = true;
}

void PlayerMotion::teleport(const MotionVector& position)
{
    if (!ready) {
        reset(position);
        return;
    }
    feet = position;
    velocity = {};
    hasKnockback = false;
    teleported = true;
}

void PlayerMotion::anchor(const MotionVector& position)
{
    feet = position;
}

void PlayerMotion::correct(const MotionVector& position, const MotionVector& motion, bool grounded)
{
    if (!ready) {
        reset(position);
    }
    feet = position;
    velocity = motion;
    onGround = grounded;
    hasKnockback = false;
}

void PlayerMotion::knockback(const MotionVector& motion)
{
    pendingKnockback = motion;
    hasKnockback = true;
}

void PlayerMotion::setMovementSpeed(float current, float base)
{
    if (std::isfinite(current)) {
        movementSpeed = current;
    }
    if (std::isfinite(base) && base > 0.0f) {
        defaultMovementSpeed = base;
    }
}

void PlayerMotion::setServerSprint(bool sprinting)
{
    serverSprint = sprinting;
    serverSprintApplied = false;
}

void PlayerMotion::setGravity(bool affected)
{
    affectedByGravity = affected;
}

void PlayerMotion::setImmobile(bool value)
{
    immobile = value;
}

void PlayerMotion::setScale(float value)
{
    if (std::isfinite(value) && value > 0.0f) {
        scale = value;
    }
}

void PlayerMotion::setAbilities(bool canFly, bool flying, bool clip, float horizontal, float vertical)
{
    mayFly = canFly;
    isFlying = flying && canFly;
    noClip = clip;
    if (std::isfinite(horizontal) && horizontal > 0.0f) {
        flySpeed = horizontal;
    }
    if (std::isfinite(vertical) && vertical > 0.0f) {
        verticalFlySpeed = vertical;
    }
}

void PlayerMotion::setGameType(int32_t value)
{
    gameType = value;
    if (gameType == GameSpectator) {
        mayFly = true;
        isFlying = true;
        noClip = true;
    }
}

void PlayerMotion::setEffects(int32_t jumpBoost, int32_t levitation, bool slow, bool weave)
{
    weaving = weave;
    jumpBoostLevel = jumpBoost;
    levitationLevel = levitation;
    slowFalling = slow;
}

void PlayerMotion::setHunger(float value)
{
    hunger = value;
}

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
    if (name.find("blue_ice") != std::string::npos) {
        return 0.989f;
    }
    if (name.find("ice") != std::string::npos) {
        return 0.98f;
    }
    if (name.find("slime") != std::string::npos) {
        return 0.8f;
    }
    if (name == "honey_block") {
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
    return prevents(blockView(x, feetY, z)) || prevents(blockView(x, feetY - 1, z)) ? PreventedJumpMultiplier : 1.0f;
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

void PlayerMotion::updateInput(const MotionInput& input, MotionTick& tick)
{
    yaw = input.yaw;
    pressingSneak = input.sneak;

    bool wantSprint = (input.sprint || isSprinting) && input.forward > 0.0f && !input.sneak && hunger > 6.0f;
    bool startSprint = wantSprint && !isSprinting;
    bool stopSprint = !wantSprint && isSprinting;
    bool adjustSpeed = false;
    if (!startSprint && !stopSprint && !serverSprintApplied && serverSprint != isSprinting) {
        isSprinting = serverSprint;
        airSpeed = isSprinting ? 0.026f : 0.02f;
    } else if (startSprint) {
        isSprinting = true;
        airSpeed = 0.026f;
        adjustSpeed = true;
    } else if (stopSprint) {
        isSprinting = false;
        airSpeed = 0.02f;
        adjustSpeed = true;
    }
    serverSprintApplied = true;
    if (adjustSpeed) {
        movementSpeed = defaultMovementSpeed;
        if (isSprinting) {
            movementSpeed *= 1.3f;
        }
    }
    tick.startSprinting = startSprint;
    tick.stopSprinting = stopSprint;

    bool wantSneak = input.sneak && !isFlying;
    tick.startSneaking = wantSneak && !isSneaking;
    tick.stopSneaking = !wantSneak && isSneaking;
    isSneaking = wantSneak;
    height = isSneaking ? 1.5f : 1.8f;

    float maximumInput = 1.0f;
    if (isSneaking) {
        maximumInput *= SneakInput;
    }
    impulseSideways = std::clamp(input.sideways, -maximumInput, maximumInput) * 0.98f;
    impulseForward = std::clamp(input.forward, -maximumInput, maximumInput) * 0.98f;

    jumping = input.jump;
    pressingJump = input.jump;
    jumpHeight = 0.42f + static_cast<float>(jumpBoostLevel) * 0.1f;
    if (!pressingJump) {
        jumpDelay = 0;
    }
    gravity = slowFalling ? SlowFallingGravity : NormalGravity;

    if (flyToggleTicks > 0) {
        --flyToggleTicks;
    }
    bool jumpPressed = input.jump && !jumpWasHeld;
    jumpWasHeld = input.jump;
    if (jumpPressed && mayFly && gameType != GameSpectator) {
        if (flyToggleTicks > 0) {
            isFlying = !isFlying;
            tick.startFlying = isFlying;
            tick.stopFlying = !isFlying;
            flyToggleTicks = 0;
        } else {
            flyToggleTicks = FlyToggleWindow;
        }
    }
}

MotionTick PlayerMotion::step(const MotionInput& input, const CellLookup& cells)
{
    lookup = &cells;
    MotionTick tick;
    jumped = false;
    updateInput(input, tick);

    if (teleported) {
        teleported = false;
        velocity = {};
        jumpDelay = 0;
        if (!isFlying) {
            applyJump();
        }
    } else if (isFlying || noClip) {
        runFlight(input);
    } else if (immobile) {
        velocity = {};
    } else {
        if (velocity.lengthSquared() < 1.0E-12f) {
            velocity = {};
        }
        simulate();
    }
    if (isFlying && onGround && !noClip && gameType != GameSpectator) {
        isFlying = false;
        tick.stopFlying = true;
    }
    tick.startedJump = jumped;

    if (jumpDelay > 0) {
        --jumpDelay;
    }
    tick.position = feet;
    tick.velocity = velocity;
    tick.onGround = onGround;
    tick.horizontalCollision = collideX || collideZ;
    tick.verticalCollision = collideY;
    tick.sneaking = isSneaking;
    tick.sprinting = isSprinting;
    tick.flying = isFlying;
    lookup = nullptr;
    return tick;
}

void PlayerMotion::simulate()
{
    Fluid fluid = fluidState(boundingBox());
    if (fluid.water) {
        runWater(fluid);
        return;
    }
    if (fluid.lava) {
        runLava();
        return;
    }
    runGroundAndAir();
}

void PlayerMotion::moveRelative(float speed)
{
    float forward = impulseForward;
    float sideways = impulseSideways;
    float force = forward * forward + sideways * sideways;
    if (force < 1.0E-4f) {
        return;
    }
    force = speed / std::max(static_cast<float>(std::sqrt(static_cast<double>(force))), 1.0f);
    forward *= force;
    sideways *= force;
    float angle = yaw * Pi;
    angle = angle / 180.0f;
    float sin = sine(angle);
    float cos = cosine(angle);
    float dx = sideways * cos;
    dx = dx - forward * sin;
    float dz = forward * cos;
    dz = dz + sideways * sin;
    velocity = { velocity.x + dx, velocity.y + 0.0f, velocity.z + dz };
}

void PlayerMotion::applyKnockback()
{
    if (hasKnockback) {
        velocity = pendingKnockback;
        hasKnockback = false;
    }
}

void PlayerMotion::applyJump()
{
    if (!jumping || !onGround || jumpDelay > 0) {
        return;
    }
    float x = velocity.x;
    float y = jumpHeight * jumpPreventionMultiplier();
    float z = velocity.z;
    jumpDelay = JumpDelayTicks;
    jumped = true;
    if (isSprinting) {
        float direction = yaw * 0.017453292f;
        x -= sine(direction) * 0.2f;
        z += cosine(direction) * 0.2f;
    }
    velocity = { x, y, z };
}

void PlayerMotion::applyClimbable()
{
    if (!climbable(floorInt(feet.x), floorInt(feet.y), floorInt(feet.z))) {
        return;
    }
    float y = std::max(velocity.y, -ClimbSpeed);
    if (pressingJump || collideX || collideZ) {
        y = ClimbSpeed;
    }
    if (isSneaking && y < 0.0f) {
        y = 0.0f;
    }
    velocity.y = y;
}

void PlayerMotion::applyPowderSnowTraversal()
{
    if (!insideBlockNamed("powder_snow")) {
        return;
    }
    if (pressingSneak) {
        velocity.y = -0.15f;
    }
}

void PlayerMotion::applyHoneyWallSlide()
{
    world::CollisionBox box = grow(boundingBox(), 1.0E-3f, 0.0f, 1.0E-3f);
    int32_t maximumX = static_cast<int32_t>(std::ceil(box.maxX));
    int32_t maximumY = static_cast<int32_t>(std::ceil(box.maxY));
    int32_t maximumZ = static_cast<int32_t>(std::ceil(box.maxZ));
    for (int32_t x = floorInt(box.minX); x < maximumX; ++x) {
        for (int32_t y = floorInt(box.minY); y < maximumY; ++y) {
            for (int32_t z = floorInt(box.minZ); z < maximumZ; ++z) {
                world::CollisionBox block { float(x), float(y), float(z), x + 1.0f, y + 1.0f, z + 1.0f };
                if (!named(blockView(x, y, z), "honey_block") || !box.intersects(block)) {
                    continue;
                }
                velocity = { velocity.x * 0.4f, std::max(-0.12f, velocity.y), velocity.z * 0.4f };
            }
        }
    }
}

void PlayerMotion::walkOnBlock(const world::CollisionState* block)
{
    if (!onGround || isSneaking) {
        return;
    }
    if (!named(block, "slime") && !named(block, "honey_block")) {
        return;
    }
    float vertical = std::abs(velocity.y);
    if (vertical < 0.1f && !pressingSneak) {
        float multiplier = 0.4f + vertical * 0.2f;
        velocity = { velocity.x * multiplier, velocity.y, velocity.z * multiplier };
    }
}

void PlayerMotion::postCollisionMotion(const MotionVector& oldVelocity, bool oldOnGround, const world::CollisionState* blockUnderFeet)
{
    float x = collideX ? 0.0f : velocity.x;
    float y = velocity.y;
    float z = collideZ ? 0.0f : velocity.z;
    if (!oldOnGround && collideY) {
        if (oldVelocity.y >= 0.0f || pressingSneak) {
            y = 0.0f;
        } else if (named(blockUnderFeet, "slime")) {
            y = -oldVelocity.y;
            if (std::abs(y) < 1.0E-4f) {
                y = 0.0f;
            }
        } else if (named(blockUnderFeet, "bed")) {
            y = -0.75f * oldVelocity.y;
        } else {
            y = 0.0f;
        }
    } else if (collideY) {
        y = 0.0f;
    }
    velocity = { x, y, z };
}

void PlayerMotion::runGroundAndAir()
{
    const world::CollisionState* under = blockUnder(0.5f);
    float frictionFactor = AirFriction;
    float acceleration = airSpeed;
    if (onGround) {
        float speed = movementSpeed;
        if (named(under, "soul_sand")) {
            speed *= 0.543f;
        }
        frictionFactor *= friction(under);
        float cubed = frictionFactor * frictionFactor;
        cubed = cubed * frictionFactor;
        acceleration = speed * (0.16277136f / cubed);
    }

    applyKnockback();
    moveRelative(acceleration);
    applyJump();
    bool scaffoldDescend = false;
    bool scaffolding = named(blockView(floorInt(feet.x), floorInt(feet.y), floorInt(feet.z)), "scaffolding")
        || (hasSupportingBlock && named(blockView(supportingBlock[0], supportingBlock[1], supportingBlock[2]), "scaffolding"));
    if (scaffolding) {
        if (pressingSneak) {
            velocity.y = -0.15f;
            scaffoldDescend = true;
        } else if (pressingJump) {
            velocity.y = 0.15f;
        }
    }
    applyPowderSnowTraversal();
    applyClimbable();

    bool cobweb = insideBlockNamed("web");
    bool powderSnow = !cobweb && insideBlockNamed("powder_snow");
    bool berryBush = !cobweb && !powderSnow && insideBlockNamed("sweet_berry_bush");
    if (cobweb) {
        float horizontal = weaving ? 0.5f : 0.25f;
        float vertical = weaving ? 0.25f : 0.05f;
        velocity = { velocity.x * horizontal, velocity.y * vertical, velocity.z * horizontal };
    } else if (powderSnow) {
        velocity = { velocity.x * 0.9f, velocity.y * 1.5f, velocity.z * 0.9f };
    } else if (berryBush) {
        velocity = { velocity.x * 0.8f, velocity.y * 0.75f, velocity.z * 0.8f };
    }

    MotionVector oldVelocity = velocity;
    bool oldOnGround = onGround;
    float oldY = feet.y;
    move();

    if (hasSupportingBlock) {
        under = blockView(supportingBlock[0], supportingBlock[1], supportingBlock[2]);
    } else {
        under = blockUnder(0.2f);
        if (!under) {
            const world::CollisionState* below = blockView(floorInt(feet.x), floorInt(feet.y) - 1, floorInt(feet.z));
            if (below) {
                const std::string& name = table->name(*below);
                if (name.find("wall") != std::string::npos || name.find("fence") != std::string::npos) {
                    under = below;
                }
            }
        }
    }

    if (oldY == feet.y) {
        walkOnBlock(under);
    }
    postCollisionMotion(oldVelocity, oldOnGround, under);

    if (!oldOnGround && onGround) {
        jumpDelay = 0;
    }
    if (cobweb || powderSnow || berryBush) {
        velocity = {};
    }

    float x = velocity.x;
    float y = velocity.y;
    float z = velocity.z;
    if (!scaffoldDescend && levitationLevel > 0) {
        float levitationSpeed = LevitationMultiplier * static_cast<float>(levitationLevel);
        y += (levitationSpeed - y) * 0.2f;
    } else if (!scaffoldDescend && affectedByGravity) {
        y -= gravity;
        y *= GravityMultiplier;
    }
    x *= frictionFactor;
    z *= frictionFactor;
    velocity = { x, y, z };
    applyHoneyWallSlide();
}

void PlayerMotion::runWater(const Fluid& fluid)
{
    applyKnockback();
    if (jumping && onGround && jumpDelay <= 0) {
        velocity.y = jumpHeight * jumpPreventionMultiplier();
        jumpDelay = JumpDelayTicks;
        jumped = true;
    } else if (pressingJump) {
        velocity.y = velocity.y + WaterAscent;
    }
    moveRelative(WaterAcceleration);

    float boxBottom = boundingBox().minY;
    move();

    float drag = isSprinting ? WaterFastDrag : WaterDrag;
    velocity = { velocity.x * drag, velocity.y * WaterDrag, velocity.z * drag };
    if (levitationLevel > 0) {
        float target = static_cast<float>(levitationLevel + 1) * 0.05f;
        velocity.y = velocity.y + (target - velocity.y) * 0.2f;
    } else if (gravity != 0.0f) {
        velocity.y = velocity.y - WaterGravity;
    }

    if ((collideX || collideZ) && canClimbOut(boxBottom)) {
        velocity.y = LedgeClimb;
    }

    if (fluid.bubbleDirection != 0) {
        float y = std::max(velocity.y, -0.3f);
        if (fluid.bubbleDirection < 0) {
            y = std::max(fluid.bubbleSurface ? -0.9f : -0.3f, y - 0.03f);
        } else {
            y = fluid.bubbleSurface ? std::min(1.8f, y + 0.1f) : std::min(0.7f, y + 0.08f);
        }
        velocity.y = y;
    }
}

void PlayerMotion::runLava()
{
    applyKnockback();
    if (pressingJump) {
        velocity.y = velocity.y + WaterAscent;
    }
    float boxBottom = boundingBox().minY;
    moveRelative(WaterAcceleration);
    move();

    velocity = velocity.scaled(LavaDrag);
    if (gravity != 0.0f) {
        velocity.y = velocity.y - gravity / 4.0f;
    }
    if ((collideX || collideZ) && canClimbOut(boxBottom)) {
        velocity.y = LedgeClimb;
    }
}

void PlayerMotion::runFlight(const MotionInput& input)
{
    applyKnockback();
    float speed = flySpeed * (isSprinting ? 2.0f : 1.0f);
    moveRelative(speed);
    float vertical = flySpeed * 3.0f * verticalFlySpeed;
    if (input.jump) {
        velocity.y += vertical;
    }
    if (input.sneak) {
        velocity.y -= vertical;
    }
    if (noClip) {
        feet = feet + velocity;
        onGround = false;
        collideX = false;
        collideY = false;
        collideZ = false;
    } else {
        move();
        if (collideX) {
            velocity.x = 0.0f;
        }
        if (collideY) {
            velocity.y = 0.0f;
        }
        if (collideZ) {
            velocity.z = 0.0f;
        }
    }
    velocity = { velocity.x * AirFriction, velocity.y * 0.6f, velocity.z * AirFriction };
}

void PlayerMotion::move()
{
    MotionVector requested = velocity;
    if (isSneaking && onGround && requested.y <= 0.0f) {
        requested = avoidEdge(boundingBox(), requested);
        velocity = requested;
    }

    world::CollisionBox original = boundingBox();
    std::vector<world::CollisionBox> nearby = collisionBoxes(extend(original, requested));

    auto resolve = [&](bool oneWay) {
        Resolution resolution;
        world::CollisionBox box = original;
        float penetration[3] = {};
        MotionVector yMovement = clipAll(nearby, box, { 0.0f, requested.y, 0.0f }, oneWay, penetration);
        box = offset(box, yMovement);
        MotionVector xMovement = clipAll(nearby, box, { requested.x, 0.0f, 0.0f }, oneWay, penetration);
        box = offset(box, xMovement);
        MotionVector zMovement = clipAll(nearby, box, { 0.0f, 0.0f, requested.z }, oneWay, penetration);
        box = offset(box, zMovement);
        resolution.box = box;
        resolution.movement = (yMovement + xMovement) + zMovement;
        float squared = penetration[0] * penetration[0] + penetration[1] * penetration[1] + penetration[2] * penetration[2];
        resolution.penetrated = squared >= PenetrationEpsilonSquared;
        return resolution;
    };

    auto autoStep = [&](bool oneWay) {
        std::vector<world::CollisionBox> filtered;
        filtered.reserve(nearby.size());
        for (const world::CollisionBox& box : nearby) {
            if (box.minY < original.maxY) {
                filtered.push_back(box);
            }
        }
        Resolution resolution;
        world::CollisionBox box = original;
        MotionVector up = clipAll(filtered, box, { 0.0f, StepHeight, 0.0f }, oneWay, nullptr);
        box = offset(box, up);
        MotionVector x = clipAll(filtered, box, { requested.x, 0.0f, 0.0f }, oneWay, nullptr);
        box = offset(box, x);
        MotionVector z = clipAll(filtered, box, { 0.0f, 0.0f, requested.z }, oneWay, nullptr);
        box = offset(box, z);
        MotionVector down = clipAll(filtered, box, up.scaled(-1.0f), oneWay, nullptr);
        box = offset(box, down);
        resolution.box = box;
        resolution.movement = ((up + x) + z) + down;
        return resolution;
    };

    Resolution collision = resolve(stuckInCollider);
    MotionVector resolved = collision.movement;
    bool penetrated = collision.penetrated;
    stuckInCollider = penetratedLastFrame && penetrated;
    penetratedLastFrame = penetrated;

    bool xCollision = requested.x != resolved.x;
    bool yCollision = requested.y != resolved.y;
    bool zCollision = requested.z != resolved.z;
    bool mayStep = onGround || (yCollision && requested.y < 0.0f);
    if (mayStep && (xCollision || zCollision)) {
        Resolution stepped = autoStep(stuckInCollider);
        bool stepBlocked = !collisionBoxes(stepped.box).empty();
        if (!stepBlocked && resolved.horizontalLengthSquared() < stepped.movement.horizontalLengthSquared()) {
            collision = stepped;
            resolved = stepped.movement;
        }
    }

    feet = feetOf(collision.box);
    xCollision = std::abs(requested.x - resolved.x) >= CollisionEpsilon;
    yCollision = std::abs(requested.y - resolved.y) >= CollisionEpsilon;
    zCollision = std::abs(requested.z - resolved.z) >= CollisionEpsilon;
    collideX = xCollision;
    collideY = yCollision;
    collideZ = zCollision;
    onGround = (yCollision && requested.y < 0.0f) || (onGround && !yCollision && std::abs(requested.y) <= CollisionEpsilon);
    velocity = resolved;
    updateSupportingBlock(requested);
}

MotionVector PlayerMotion::avoidEdge(const world::CollisionBox& box, MotionVector movement) const
{
    world::CollisionBox support { box.minX + EdgeInset, box.minY, box.minZ + EdgeInset, box.maxX - EdgeInset, box.maxY, box.maxZ - EdgeInset };
    auto supported = [&](float x, float z) {
        world::CollisionBox moved = offset(support, { x, -StepHeight * 1.01f, z });
        return !collisionBoxes(moved).empty();
    };
    auto reduce = [](float value) {
        if (value < EdgeStep && value >= -EdgeStep) {
            return 0.0f;
        }
        return value > 0.0f ? value - EdgeStep : value + EdgeStep;
    };
    float x = movement.x;
    float z = movement.z;
    while (x != 0.0f && !supported(x, 0.0f)) {
        x = reduce(x);
    }
    while (z != 0.0f && !supported(0.0f, z)) {
        z = reduce(z);
    }
    while (x != 0.0f && z != 0.0f && !supported(x, z)) {
        x = reduce(x);
        z = reduce(z);
    }
    return { x, movement.y, z };
}

void PlayerMotion::updateSupportingBlock(const MotionVector& requested)
{
    if (!onGround) {
        hasSupportingBlock = false;
        return;
    }
    world::CollisionBox probe = extend(boundingBox(), { 0.0f, -1.0E-3f, 0.0f });
    std::array<int32_t, 3> found {};
    bool any = findSupportingBlock(probe, found);
    if (!any) {
        probe = offset(probe, { -requested.x, 0.0f, -requested.z });
        any = findSupportingBlock(probe, found);
    }
    hasSupportingBlock = any;
    if (any) {
        supportingBlock = found;
    }
}

}
