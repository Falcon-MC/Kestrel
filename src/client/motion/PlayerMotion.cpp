#include "MotionMath.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

using namespace motion;

PlayerMotion::PlayerMotion()
    : table(&world::BlockCollisions::shared())
{
    prepareSineTable();
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

void PlayerMotion::keepPendingKnockback(const PlayerMotion& live)
{
    if (live.hasKnockback) {
        pendingKnockback = live.pendingKnockback;
        hasKnockback = true;
    }
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

void PlayerMotion::updateInput(const MotionInput& input, MotionTick& tick)
{
    yaw = input.yaw;
    pitch = input.pitch;
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
    updateSwimming(!touchingLiquid(false).empty(), tick);

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
    tick.swimming = isSwimming;
    lookup = nullptr;
    return tick;
}

void PlayerMotion::simulate()
{
    Fluid fluid = fluidState(boundingBox());
    std::vector<std::array<int32_t, 3>> water = touchingLiquid(false);
    std::vector<std::array<int32_t, 3>> lava = touchingLiquid(true);
    if (!water.empty() || (isSwimming && lava.empty() && fluid.water)) {
        applyKnockback();
        applyLiquidFlow(water, false);
        runWater(fluid, !water.empty());
        return;
    }
    if (!lava.empty()) {
        applyKnockback();
        applyLiquidFlow(lava, true);
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
        y -= y < 0.0f ? gravity : NormalGravity;
        y *= GravityMultiplier;
    }
    x *= frictionFactor;
    z *= frictionFactor;
    velocity = { x, y, z };
    applyHoneyWallSlide();
}

void PlayerMotion::runWater(const Fluid& fluid, bool touchingWater)
{
    if (pressingSneak) {
        velocity.y -= WaterAscent;
    }
    updateSwimTravel();
    if (jumping) {
        if ((swimAmount > 0.0f && swimAmount < 1.0f) || (isSwimming && !touchingWater)) {
            velocity.y = 0.0f;
        } else {
            velocity.y += WaterAscent;
        }
    }
    moveRelative(WaterAcceleration);

    float boxBottom = boundingBox().minY;
    move();

    float drag = isSprinting || stoppedSwimmingThisTick ? WaterFastDrag : WaterDrag;
    velocity = { velocity.x * drag, velocity.y * WaterDrag, velocity.z * drag };
    if (levitationLevel > 0) {
        float target = static_cast<float>(levitationLevel) * LevitationMultiplier;
        velocity.y = velocity.y + (target - velocity.y) * 0.2f;
    } else if (affectedByGravity && !isSwimming) {
        velocity.y = velocity.y - SwimlessWaterGravity;
    }

    if ((collideX || collideZ) && canClimbOut(boxBottom)) {
        velocity.y = LedgeClimb;
    }
    applyBubbleColumn(fluid);
}

void PlayerMotion::runLava()
{
    if (jumping) {
        velocity.y = velocity.y + WaterAscent;
    }
    float boxBottom = boundingBox().minY;
    moveRelative(WaterAcceleration);
    move();

    velocity = velocity.scaled(LavaDrag);
    if (levitationLevel > 0) {
        float target = static_cast<float>(levitationLevel) * LevitationMultiplier;
        velocity.y = velocity.y + (target - velocity.y) * 0.2f;
    } else if (affectedByGravity) {
        velocity.y = velocity.y - WaterGravity;
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

}
