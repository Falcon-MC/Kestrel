#include "client/motion/MotionMath.h"

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
    jumpArc = false;
    isGliding = false;
    isCrawling = false;
    forcedSneak = false;
    ready = true;
}

void PlayerMotion::hold()
{
    velocity = {};
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

/**
 * Takes the movement attribute the server computed, effects included. The
 * server counts the sprint modifier in it while the player sprints, so that
 * part is taken back out; each tick puts it on again while sprinting, which
 * keeps Speed and Slowness through sprint changes.
 */
void PlayerMotion::setMovementSpeed(float current, float base)
{
    float value = std::isfinite(current) && current >= 0.0f ? current : base;
    if (!std::isfinite(value) || value < 0.0f) {
        return;
    }
    baseMovementSpeed = isSprinting ? value / SprintSpeedMultiplier : value;
    movementSpeed = isSprinting ? baseMovementSpeed * SprintSpeedMultiplier : baseMovementSpeed;
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
    mayFly = canFly || gameType == GameSpectator;
    isFlying = (flying && canFly) || gameType == GameSpectator;
    noClip = clip || gameType == GameSpectator;
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
    mayFly = gameType == GameCreative || gameType == GameSpectator;
    noClip = gameType == GameSpectator;
    isFlying = noClip || (mayFly && isFlying);
}

void PlayerMotion::setEffects(int32_t jumpBoost, int32_t levitation, bool slow, bool weave)
{
    weaving = weave;
    jumpBoostLevel = jumpBoost;
    levitationLevel = levitation;
    slowFalling = slow;
}

void PlayerMotion::takeSettings(const PlayerMotion& other)
{
    movementSpeed = other.movementSpeed;
    baseMovementSpeed = other.baseMovementSpeed;
    affectedByGravity = other.affectedByGravity;
    immobile = other.immobile;
    scale = other.scale;
    mayFly = other.mayFly;
    noClip = other.noClip;
    flySpeed = other.flySpeed;
    verticalFlySpeed = other.verticalFlySpeed;
    gameType = other.gameType;
    jumpBoostLevel = other.jumpBoostLevel;
    levitationLevel = other.levitationLevel;
    slowFalling = other.slowFalling;
    weaving = other.weaving;
    hunger = other.hunger;
}

void PlayerMotion::setHunger(float value)
{
    hunger = value;
}

/**
 * Whether the player box would fit at the feet with the given pose height,
 * touching solids counting as fitting.
 */
bool PlayerMotion::poseFits(float poseHeight) const
{
    world::CollisionBox box = boundingBox();
    box.maxY = box.minY + poseHeight * scale - PoseFitInset;
    box.minY = box.minY + PoseFitInset;
    return !anyCollision(box);
}

/**
 * Forces the lower poses when the standing box no longer fits: sneaking when
 * the sneaking box fits, and crawling for a player already crawling or just
 * out of a swim when only the low box fits.
 */
void PlayerMotion::updatePose(const MotionInput& input, MotionTick& tick)
{
    bool wasCrawling = isCrawling;
    forcedSneak = false;
    bool crawl = false;
    if (!isFlying && !isGliding && !isSwimming && !noClip && !poseFits(StandingHeight)) {
        if (poseFits(SneakingHeight)) {
            forcedSneak = !input.sneak;
        } else if ((wasCrawling || swimAmount > 0.0f) && poseFits(LowPoseHeight)) {
            crawl = true;
        }
    }
    isCrawling = crawl;
    tick.startCrawling = crawl && !wasCrawling;
    tick.stopCrawling = !crawl && wasCrawling;
    tick.crawling = crawl;
    tick.forcedSneak = forcedSneak;
}

/**
 * Opens the elytra on a jump press in the air and closes it once the player
 * lands, enters a liquid, starts flying or takes it off.
 */
void PlayerMotion::updateGliding(const MotionInput& input, bool jumpPressed, MotionTick& tick)
{
    bool was = isGliding;
    bool usable = input.elytra && !onGround && !isFlying && !noClip && levitationLevel <= 0 && !touchesLiquid(false) && !touchesLiquid(true);
    isGliding = usable && (was || jumpPressed);
    tick.startGliding = isGliding && !was;
    tick.stopGliding = !isGliding && was;
}

void PlayerMotion::updateInput(const MotionInput& input, MotionTick& tick)
{
    yaw = input.yaw;
    pitch = input.pitch;
    pressingSneak = input.sneak;
    wearsElytra = input.elytra;
    depthStriderLevel = std::clamp(input.depthStrider, 0, DepthStriderMaxLevel);
    soulSpeedLevel = std::max(input.soulSpeed, 0);
    swiftSneakLevel = std::max(input.swiftSneak, 0);
    updatePose(input, tick);

    bool wantSprint = (input.sprint || isSprinting) && input.forward > 0.0f && !input.sneak && !forcedSneak && !isCrawling && !isGliding && !input.usingItem && (gameType == GameCreative || hunger > 6.0f);
    bool startSprint = wantSprint && !isSprinting;
    bool stopSprint = !wantSprint && isSprinting;
    if (!startSprint && !stopSprint && !serverSprintApplied && serverSprint != isSprinting) {
        isSprinting = serverSprint;
    } else if (startSprint) {
        isSprinting = true;
    } else if (stopSprint) {
        isSprinting = false;
    }
    serverSprintApplied = true;
    airSpeed = isSprinting ? SprintAirSpeed : WalkAirSpeed;
    movementSpeed = isSprinting ? baseMovementSpeed * SprintSpeedMultiplier : baseMovementSpeed;
    tick.startSprinting = startSprint;
    tick.stopSprinting = stopSprint;

    if (flyToggleTicks > 0) {
        --flyToggleTicks;
    }
    bool jumpPressed = input.jump && !jumpWasHeld;
    jumpWasHeld = input.jump;
    bool toggledFlight = false;
    if (jumpPressed && mayFly && gameType != GameSpectator) {
        if (flyToggleTicks > 0) {
            isFlying = !isFlying;
            tick.startFlying = isFlying;
            tick.stopFlying = !isFlying;
            flyToggleTicks = 0;
            toggledFlight = true;
        } else {
            flyToggleTicks = FlyToggleWindow;
        }
    }
    updateGliding(input, jumpPressed && !toggledFlight, tick);

    bool wantSneak = (input.sneak || forcedSneak) && !isFlying && !isGliding;
    tick.startSneaking = wantSneak && !isSneaking;
    tick.stopSneaking = !wantSneak && isSneaking;
    isSneaking = wantSneak;
    if (isCrawling || isGliding) {
        height = LowPoseHeight;
    } else {
        height = isSneaking ? SneakingHeight : StandingHeight;
    }

    float sideways = std::clamp(input.sideways, -1.0f, 1.0f);
    float forward = std::clamp(input.forward, -1.0f, 1.0f);
    float lengthSquared = sideways * sideways + forward * forward;
    if (lengthSquared > 1.0f) {
        float inverse = 1.0f / std::sqrt(lengthSquared);
        sideways *= inverse;
        forward *= inverse;
    }
    float factor = input.usingItem ? ItemUseInput : 1.0f;
    if (isSneaking || isCrawling) {
        factor *= std::min(1.0f, SneakInput + SwiftSneakPerLevel * static_cast<float>(swiftSneakLevel));
    }
    tick.moveSideways = sideways * factor;
    tick.moveForward = forward * factor;
    impulseSideways = tick.moveSideways * 0.98f;
    impulseForward = tick.moveForward * 0.98f;

    jumping = input.jump;
    pressingJump = input.jump;
    jumpHeight = JumpVelocity;
    if (!pressingJump) {
        jumpDelay = 0;
    }
    gravity = slowFalling ? SlowFallingGravity : NormalGravity;
}

/**
 * Rain reaches the player when nothing stands in the column above the head.
 */
bool PlayerMotion::exposedToRain() const
{
    world::CollisionBox box = boundingBox();
    int32_t x = floorInt(feet.x);
    int32_t z = floorInt(feet.z);
    for (int32_t y = floorInt(box.maxY); y < SkyCheckTop; ++y) {
        if (cell(x, y, z).primary) {
            return false;
        }
    }
    return true;
}

void PlayerMotion::launchRiptide(int32_t level, MotionTick& tick)
{
    float yawAngle = yaw * Pi / 180.0f;
    float pitchAngle = pitch * Pi / 180.0f;
    MotionVector push { -sine(yawAngle) * cosine(pitchAngle), -sine(pitchAngle), cosine(yawAngle) * cosine(pitchAngle) };
    float length = std::sqrt(push.lengthSquared());
    if (length <= 0.0f) {
        return;
    }
    float strength = 3.0f * (1.0f + static_cast<float>(level)) / 4.0f;
    velocity = velocity + push.scaled(strength / length);
    if (onGround) {
        world::CollisionBox lifted = offset(boundingBox(), { 0.0f, RiptideLift, 0.0f });
        if (!anyCollision(lifted)) {
            feet.y += RiptideLift;
            onGround = false;
        }
    }
    tick.startSpinAttack = spinAttackTicks == 0;
    spinAttackTicks = SpinAttackTicks;
}

void PlayerMotion::updateSpinAttack(MotionTick& tick)
{
    if (spinAttackTicks <= 0) {
        return;
    }
    --spinAttackTicks;
    if (spinAttackTicks == 0) {
        tick.stopSpinAttack = true;
    }
}

MotionTick PlayerMotion::step(const MotionInput& input, const CellLookup& cells)
{
    lookup = &cells;
    for (auto& entry : scratch->cells) entry.valid = false;
    scratch->liquidsValid = false;
    scratch->liquidKnown = {};
    if (!boundedQuery(boundingBox())) {
        velocity = {};
        lookup = nullptr;
        MotionTick held;
        held.position = feet;
        held.onGround = onGround;
        return held;
    }
    MotionTick tick;
    jumped = false;
    bool knockbackPending = hasKnockback;
    tick.knockback = pendingKnockback;
    MotionInput effective = input;
    if (immobile) {
        effective.forward = 0.0f;
        effective.sideways = 0.0f;
        effective.jump = false;
        if (isFlying || noClip) effective.sneak = false;
    }
    updateInput(effective, tick);
    updateSwimming(touchesLiquid(false), tick);
    updateSpinAttack(tick);
    if (effective.riptide > 0 && !isFlying && !noClip && (touchesLiquid(false) || (effective.raining && exposedToRain()))) {
        launchRiptide(effective.riptide, tick);
    }

    MotionVector start = feet;
    if (teleported) {
        teleported = false;
        velocity = {};
        jumpDelay = 0;
        if (!isFlying) {
            applyJump();
        }
    } else if (isFlying || noClip) {
        runFlight(effective);
    } else if (isGliding) {
        applyKnockback();
        runGlide();
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
    if (isGliding && onGround) {
        isGliding = false;
        bool startedNow = tick.startGliding;
        tick.startGliding = false;
        tick.stopGliding = !startedNow;
        height = isSneaking ? SneakingHeight : StandingHeight;
    }
    tick.startedJump = jumped;
    tick.knockedBack = knockbackPending && !hasKnockback;
    if (jumped) {
        jumpArc = true;
    } else if (onGround || isFlying || isGliding) {
        jumpArc = false;
    }

    if (jumpDelay > 0) {
        --jumpDelay;
    }
    tick.position = feet;
    tick.movement = feet - start;
    tick.velocity = velocity;
    tick.onGround = onGround;
    tick.horizontalCollision = collideX || collideZ;
    tick.verticalCollision = collideY;
    tick.sneaking = isSneaking;
    tick.sprinting = isSprinting;
    tick.flying = isFlying;
    tick.swimming = isSwimming;
    tick.gliding = isGliding;
    tick.jumping = jumpArc;
    lookup = nullptr;
    return tick;
}

void PlayerMotion::simulate()
{
    Fluid fluid = fluidState(boundingBox());
    const auto& water = touchingLiquid(false);
    const auto& lava = touchingLiquid(true);
    if (!water.empty() || (isSwimming && lava.empty() && fluid.water)) {
        applyKnockback();
        applyLiquidFlow(water, false);
        runWater(fluid);
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
    float y = std::max(velocity.y, jumpHeight * jumpPreventionMultiplier() + JumpBoostStep * static_cast<float>(jumpBoostLevel));
    float z = velocity.z;
    jumpDelay = JumpDelayTicks;
    jumped = true;
    if (isSprinting) {
        float direction = yaw * 0.017453292f;
        x -= sine(direction) * SprintJumpImpulse;
        z += cosine(direction) * SprintJumpImpulse;
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
            y = std::min(-BedBounce * oldVelocity.y, BedBounceLimit);
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
        float soulSpeed = 1.0f + SoulSpeedPerLevel * static_cast<float>(soulSpeedLevel);
        if (named(under, "soul_sand")) {
            speed *= soulSpeedLevel > 0 ? std::max(SoulSandSpeed, soulSpeed) : SoulSandSpeed;
        } else if (soulSpeedLevel > 0 && named(under, "soul_soil")) {
            speed *= soulSpeed;
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

void PlayerMotion::runWater(const Fluid& fluid)
{
    if (isSwimming) {
        float strider = std::clamp(float(depthStriderLevel), 0.0f, float(DepthStriderMaxLevel))
            / float(DepthStriderMaxLevel) * (onGround ? 1.0f : 0.5f);
        moveRelative(WaterAcceleration + (baseMovementSpeed - WaterAcceleration) * strider);
        updateSwimTravel();
        move();
        if (collideX) velocity.x = 0.0f;
        if (collideY) velocity.y = 0.0f;
        if (collideZ) velocity.z = 0.0f;
        float horizontalDrag = isSprinting ? WaterFastDrag : WaterDrag;
        velocity = { velocity.x * horizontalDrag, velocity.y * WaterDrag, velocity.z * horizontalDrag };
        if (levitationLevel > 0) {
            float target = float(levitationLevel) * LevitationMultiplier;
            velocity.y += (target - velocity.y) * 0.2f;
        }
        return;
    }
    if (jumping) {
        velocity.y += WaterAscent;
    }
    float drag = WaterDrag;
    float acceleration = WaterAcceleration;
    float strider = std::clamp(float(depthStriderLevel), 0.0f, float(DepthStriderMaxLevel));
    if (!onGround) {
        strider *= 0.5f;
    }
    if (strider > 0.0f) {
        float blend = strider / static_cast<float>(DepthStriderMaxLevel);
        drag += (DepthStriderDrag - drag) * blend;
        acceleration += (baseMovementSpeed - acceleration) * blend;
    }
    if (onGround) {
        float groundFriction = AirFriction * friction(blockUnder(0.5f));
        acceleration = movementSpeed * (0.16277136f / (groundFriction * groundFriction * groundFriction));
    }
    moveRelative(acceleration);

    float boxBottom = boundingBox().minY;
    move();

    if (collideX) velocity.x = 0.0f;
    if (collideY) velocity.y = 0.0f;
    if (collideZ) velocity.z = 0.0f;
    velocity = velocity.scaled(drag);
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
    if (gameType == GameCreative && impulseSideways == 0.0f && impulseForward == 0.0f && !input.jump && !input.sneak) {
        velocity.y *= 0.375f;
    }
    float vertical = flySpeed * (input.sneak && !input.jump ? 4.4f : 3.0f) * verticalFlySpeed;
    if (input.jump) {
        velocity.y += vertical;
    }
    if (input.sneak) {
        velocity.y -= vertical;
    }
    if (noClip) {
        if (!boundedQuery(extend(boundingBox(), velocity))) { velocity = {}; return; }
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

/**
 * Elytra flight: gravity is partly lifted the flatter the player looks,
 * falling speed turns into forward speed, looking up trades speed for
 * height, and the horizontal speed turns toward where the player looks.
 */
void PlayerMotion::runGlide()
{
    float pitchAngle = pitch * Pi / 180.0f;
    float yawAngle = yaw * Pi / 180.0f;
    float pitchCosine = cosine(pitchAngle);
    MotionVector look { -sine(yawAngle) * pitchCosine, -sine(pitchAngle), cosine(yawAngle) * pitchCosine };
    float lookHorizontal = std::sqrt(look.x * look.x + look.z * look.z);
    float speedHorizontal = std::sqrt(velocity.horizontalLengthSquared());
    float lift = pitchCosine * pitchCosine;
    float fall = slowFalling && velocity.y <= 0.0f ? SlowFallingGravity : NormalGravity;
    if (affectedByGravity) {
        velocity.y += fall * (-1.0f + lift * GlideLift);
    }
    if (lookHorizontal > 0.0f) {
        if (velocity.y < 0.0f) {
            float converted = velocity.y * -GlideFallConversion * lift;
            velocity = { velocity.x + look.x * converted / lookHorizontal, velocity.y + converted, velocity.z + look.z * converted / lookHorizontal };
        }
        if (pitchAngle < 0.0f) {
            float converted = speedHorizontal * -sine(pitchAngle) * GlideClimbConversion;
            velocity = { velocity.x - look.x * converted / lookHorizontal, velocity.y + converted * GlideClimbBoost, velocity.z - look.z * converted / lookHorizontal };
        }
        velocity.x += (look.x / lookHorizontal * speedHorizontal - velocity.x) * GlideAlignment;
        velocity.z += (look.z / lookHorizontal * speedHorizontal - velocity.z) * GlideAlignment;
    }
    velocity = { velocity.x * GlideHorizontalDrag, velocity.y * GlideVerticalDrag, velocity.z * GlideHorizontalDrag };
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

}
