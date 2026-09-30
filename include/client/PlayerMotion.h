#pragma once

#include "world/BlockCollisions.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace kestrel {

struct MotionVector {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    MotionVector operator+(const MotionVector& other) const
    {
        return { x + other.x, y + other.y, z + other.z };
    }

    MotionVector operator-(const MotionVector& other) const
    {
        return { x - other.x, y - other.y, z - other.z };
    }

    MotionVector scaled(float factor) const
    {
        return { x * factor, y * factor, z * factor };
    }

    float lengthSquared() const
    {
        return x * x + y * y + z * z;
    }

    float horizontalLengthSquared() const
    {
        return x * x + z * z;
    }
};

/**
 * The two block layers of one position, as collision states; a null state is
 * air.
 */
struct MotionCell {
    const world::CollisionState* primary = nullptr;
    const world::CollisionState* extra = nullptr;
};

/**
 * What the player asks for during one tick: the movement keys as a vector
 * (x sideways with left positive, y forward), the held keys and where they
 * look, in degrees, with the worn gear that changes how the player moves.
 */
struct MotionInput {
    float sideways = 0.0f;
    float forward = 0.0f;
    bool jump = false;
    bool sneak = false;
    bool sprint = false;
    bool usingItem = false;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool elytra = false;
    int32_t depthStrider = 0;
    int32_t soulSpeed = 0;
    int32_t swiftSneak = 0;
    int32_t riptide = 0;
    bool raining = false;
};

/**
 * Everything the server needs to replay one tick, read back after the tick
 * ran.
 */
struct MotionTick {
    MotionVector position;
    MotionVector velocity;
    MotionVector movement;
    float moveSideways = 0.0f;
    float moveForward = 0.0f;
    MotionVector knockback;
    bool knockedBack = false;
    bool onGround = false;
    bool horizontalCollision = false;
    bool verticalCollision = false;
    bool startedJump = false;
    bool jumping = false;
    bool startGliding = false;
    bool stopGliding = false;
    bool startCrawling = false;
    bool stopCrawling = false;
    bool gliding = false;
    bool crawling = false;
    bool forcedSneak = false;
    bool startSpinAttack = false;
    bool stopSpinAttack = false;
    bool startSprinting = false;
    bool stopSprinting = false;
    bool startSneaking = false;
    bool stopSneaking = false;
    bool startFlying = false;
    bool stopFlying = false;
    bool startSwimming = false;
    bool stopSwimming = false;
    bool sneaking = false;
    bool sprinting = false;
    bool flying = false;
    bool swimming = false;
};

/**
 * The local player's movement, simulated one tick at a time in single
 * precision with the same rules, the same constants and the same order of
 * operations the server replays each input with, so a tick the client sends
 * is always the tick the server predicts.
 */
class PlayerMotion {
public:
    using CellLookup = std::function<MotionCell(int32_t x, int32_t y, int32_t z)>;

    PlayerMotion();

    void reset(const MotionVector& feet);
    void teleport(const MotionVector& feet);
    void knockback(const MotionVector& motion);

    /**
     * Moves the feet to where the server reads them back from the sent eye
     * position, so both sides start the next tick from the same float.
     */
    void anchor(const MotionVector& position);
    void correct(const MotionVector& position, const MotionVector& motion, bool grounded);

    /**
     * Holds the player still for a tick the world around it has not loaded
     * yet: the velocity is dropped so nothing falls through missing ground.
     */
    void hold();

    /**
     * Carries a knockback the live state received but has not run yet over
     * to this replayed state.
     */
    void keepPendingKnockback(const PlayerMotion& live);

    /**
     * Takes what the server sets rather than what the simulation moves from
     * another state: movement speed, gravity, scale, abilities, game type,
     * effects and hunger, so a replayed tick runs with the settings it first
     * ran with.
     */
    void takeSettings(const PlayerMotion& other);

    void setMovementSpeed(float current, float base);
    void setServerSprint(bool sprinting);
    void setGravity(bool affected);
    void setImmobile(bool value);
    void setScale(float value);
    void setAbilities(bool mayFly, bool flying, bool noClip, float flySpeed, float verticalFlySpeed);
    void setGameType(int32_t gameType);
    void setEffects(int32_t jumpBoost, int32_t levitation, bool slowFalling, bool weaving);
    void setHunger(float hunger);

    MotionTick step(const MotionInput& input, const CellLookup& lookup);

    const MotionVector& position() const
    {
        return feet;
    }

    const MotionVector& currentVelocity() const
    {
        return velocity;
    }

    bool grounded() const
    {
        return onGround;
    }

    bool sneaking() const
    {
        return isSneaking;
    }

    float speed() const
    {
        return movementSpeed;
    }

    bool initialized() const
    {
        return ready;
    }

    bool swimming() const
    {
        return isSwimming;
    }

private:
    struct Fluid {
        bool water = false;
        bool lava = false;
        int bubbleDirection = 0;
        bool bubbleSurface = false;
    };

    struct Resolution {
        world::CollisionBox box;
        MotionVector movement;
        bool penetrated = false;
    };

    world::CollisionBox boundingBox() const;
    std::vector<world::CollisionBox> collisionBoxes(const world::CollisionBox& area) const;
    const world::CollisionState* cellState(int32_t x, int32_t y, int32_t z) const;
    MotionCell cell(int32_t x, int32_t y, int32_t z) const;
    const world::CollisionState* blockView(int32_t x, int32_t y, int32_t z) const;
    bool named(const world::CollisionState* state, std::string_view name) const;
    float friction(const world::CollisionState* state) const;
    bool climbable(int32_t x, int32_t y, int32_t z) const;
    Fluid fluidState(const world::CollisionBox& area) const;

    /**
     * The liquid in a block, from either block layer: whether it is lava, its
     * depth (0 for a source, 8 and up while falling) and whether there is one.
     */
    bool liquidAt(int32_t x, int32_t y, int32_t z, bool& lava, int32_t& depth) const;
    std::vector<std::array<int32_t, 3>> touchingLiquid(bool lava) const;
    MotionVector liquidFlow(int32_t x, int32_t y, int32_t z, bool lava, int32_t depth) const;
    bool closesFlow(int32_t x, int32_t y, int32_t z) const;
    void applyLiquidFlow(const std::vector<std::array<int32_t, 3>>& blocks, bool lava);
    void updateSwimming(bool inWater, MotionTick& tick);
    void updateSwimTravel();
    void applyBubbleColumn(const Fluid& fluid);
    bool insideBlockNamed(std::string_view name) const;
    const world::CollisionState* blockUnder(float distance) const;
    float jumpPreventionMultiplier() const;
    bool canClimbOut(float boxBottom) const;
    bool findSupportingBlock(const world::CollisionBox& area, std::array<int32_t, 3>& found) const;

    bool poseFits(float poseHeight) const;
    void updatePose(const MotionInput& input, MotionTick& tick);
    void updateGliding(const MotionInput& input, bool jumpPressed, MotionTick& tick);
    void updateInput(const MotionInput& input, MotionTick& tick);
    bool exposedToRain() const;

    /**
     * Throws the player along the look direction the way releasing a riptide
     * trident does while wet, lifting it off the ground first.
     */
    void launchRiptide(int32_t level, MotionTick& tick);
    void updateSpinAttack(MotionTick& tick);
    void simulate();
    void runGroundAndAir();
    void runWater(const Fluid& fluid, bool touchingWater);
    void runLava();
    void runFlight(const MotionInput& input);
    void runGlide();
    void moveRelative(float speed);
    void applyKnockback();
    void applyJump();
    void applyClimbable();
    void applyPowderSnowTraversal();
    void applyHoneyWallSlide();
    void walkOnBlock(const world::CollisionState* block);
    void postCollisionMotion(const MotionVector& oldVelocity, bool oldOnGround, const world::CollisionState* blockUnderFeet);
    void move();
    MotionVector avoidEdge(const world::CollisionBox& box, MotionVector movement) const;
    void updateSupportingBlock(const MotionVector& requested);

    const world::BlockCollisions* table = nullptr;
    const CellLookup* lookup = nullptr;

    MotionVector feet;
    MotionVector velocity;
    MotionVector pendingKnockback;
    bool hasKnockback = false;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool isSwimming = false;
    float swimAmount = 0.0f;
    bool stoppedSwimmingThisTick = false;
    float impulseSideways = 0.0f;
    float impulseForward = 0.0f;
    float width = 0.6f;
    float height = 1.8f;
    float scale = 1.0f;
    float gravity = 0.08f;
    float jumpHeight = 0.42f;
    float movementSpeed = 0.1f;
    float baseMovementSpeed = 0.1f;
    float airSpeed = 0.02f;
    float flySpeed = 0.05f;
    float verticalFlySpeed = 1.0f;
    float hunger = 20.0f;
    int32_t jumpDelay = 0;
    int32_t jumpBoostLevel = 0;
    int32_t levitationLevel = 0;
    int32_t gameType = 0;
    bool slowFalling = false;
    bool weaving = false;
    bool isSprinting = false;
    bool isSneaking = false;
    bool pressingSneak = false;
    bool pressingJump = false;
    bool jumping = false;
    bool serverSprint = false;
    bool serverSprintApplied = true;
    bool collideX = false;
    bool collideY = false;
    bool collideZ = false;
    bool onGround = false;
    bool penetratedLastFrame = false;
    bool stuckInCollider = false;
    bool affectedByGravity = true;
    bool immobile = false;
    bool mayFly = false;
    bool isFlying = false;
    bool noClip = false;
    bool hasSupportingBlock = false;
    std::array<int32_t, 3> supportingBlock {};
    bool teleported = false;
    bool ready = false;
    bool jumpWasHeld = false;
    bool jumped = false;
    bool jumpArc = false;
    bool isGliding = false;
    bool isCrawling = false;
    bool forcedSneak = false;
    bool wearsElytra = false;
    int32_t depthStriderLevel = 0;
    int32_t soulSpeedLevel = 0;
    int32_t swiftSneakLevel = 0;
    int32_t flyToggleTicks = 0;
    int32_t spinAttackTicks = 0;
};

}
