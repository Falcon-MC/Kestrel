#include "client/motion/MotionMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
    glideTicks = 0;
    hasPreviousRotation = false;
    isCrawling = false;
    forcedSneak = false;
    ready = true;
}

void PlayerMotion::hold()
{
    velocity = {};
}

PlayerMotion PlayerMotion::detached() const
{
    PlayerMotion copy = *this;
    copy.lookup = nullptr;
    copy.scratch = std::make_shared<Scratch>();
    return copy;
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

void PlayerMotion::setUnderwaterSpeed(float value)
{
    if (std::isfinite(value) && value >= 0.0f) {
        underwaterSpeed = value;
    }
}

void PlayerMotion::setLavaSpeed(float value)
{
    if (std::isfinite(value) && value >= 0.0f) {
        lavaSpeed = value;
    }
}

void PlayerMotion::setUniformAirDrag(bool value)
{
    uniformAirDrag = value;
}

void PlayerMotion::setAirDragModifier(float value)
{
    if (std::isfinite(value)) {
        airDragModifier = value;
    }
}

/**
 * The share of vertical speed kept by a drag that keeps `retention` by
 * default. The server's air drag modifier scales the share lost, which is
 * clamped so the drag never reverses or adds speed.
 */
float PlayerMotion::verticalRetention(float retention) const
{
    if (airDragModifier == 1.0f) {
        return retention;
    }
    float lost = (1.0f - retention) * airDragModifier;
    if (lost > 1.0f) {
        return 0.0f;
    }
    if (lost < 0.0f) {
        return 1.0f;
    }
    return 1.0f - lost;
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
    underwaterSpeed = other.underwaterSpeed;
    lavaSpeed = other.lavaSpeed;
    uniformAirDrag = other.uniformAirDrag;
    airDragModifier = other.airDragModifier;
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
}

/**
 * Opens the elytra on a jump press in the air, rising or falling, and closes
 * it once the player lands, enters water, starts flying, takes it off or
 * breaks it, reaches climbable feet, or presses jump again after eleven
 * gliding ticks. Lava neither prevents nor ends a glide.
 */
void PlayerMotion::updateGliding(const MotionInput& input, bool jumpPressed, MotionTick& tick)
{
    bool was = isGliding;
    int32_t x = floorInt(feet.x);
    int32_t y = floorInt(feet.y);
    int32_t z = floorInt(feet.z);
    bool feetEnd = climbable(x, y, z) || (input.leatherBoots && named(blockView(x, y, z), "powder_snow"));
    bool usable = input.elytra && !onGround && !isFlying && !noClip && !touchesLiquid(false) && !feetEnd;
    bool held = was ? !jumpPressed || glideTicks < GlideCancelTicks : jumpPressed;
    isGliding = usable && held;
    glideTicks = isGliding ? (was ? glideTicks + 1 : 1) : 0;
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

    bool wantSprint = (input.sprint || isSprinting) && input.forward > 0.0f && !input.sneak && !forcedSneak && !isCrawling && !isGliding && !input.usingItem && (gameType == GameCreative || gameType == GameSpectator || hunger > 6.0f);
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
    bool flightRequested = (input.startFlying && !isFlying) || (input.stopFlying && isFlying);
    if (!toggledFlight && flightRequested && mayFly && gameType != GameSpectator) {
        isFlying = !isFlying;
        tick.startFlying = isFlying;
        tick.stopFlying = !isFlying;
        toggledFlight = true;
    }
    bool glidePressed = (jumpPressed && !toggledFlight) || (input.startGlide && !isGliding) || (input.stopGlide && isGliding);
    updateGliding(input, glidePressed, tick);

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
    if (isSneaking || isCrawling) {
        float pose = std::min(1.0f, SneakInput + SwiftSneakPerLevel * static_cast<float>(swiftSneakLevel));
        sideways *= pose;
        forward *= pose;
    }
    if (input.usingItem) {
        sideways *= ItemUseInput * ItemUseInput;
        forward *= ItemUseInput * ItemUseInput;
    }
    tick.moveSideways = sideways;
    tick.moveForward = forward;
    moveSideways = sideways;
    moveForward = forward;
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
    if (effective.autoJump && onGround && !effective.sneak && !immobile && !isFlying && (collideX || collideZ)
        && (effective.forward != 0 || effective.sideways != 0)) {
        float angle = effective.yaw * 3.14159265f / 180.0f;
        float dx = -std::sin(angle) * effective.forward - std::cos(angle) * effective.sideways;
        float dz = std::cos(angle) * effective.forward - std::sin(angle) * effective.sideways;
        float length = std::hypot(dx, dz);
        dx *= 0.7f / length; dz *= 0.7f / length;
        auto target = boundingBox();
        target.minX += dx; target.maxX += dx;
        target.minZ += dz; target.maxZ += dz;
        target.minY += 1; target.maxY += 1;
        auto boxes = collisionBoxes(target);
        if (std::none_of(boxes.begin(), boxes.end(), [&](const auto& box) { return target.intersects(box); })) effective.jump = true;
    }
    if (immobile) {
        effective.forward = 0.0f;
        effective.sideways = 0.0f;
        effective.jump = false;
        if (isFlying || noClip) effective.sneak = false;
    }
    glideBoost = effective.glideBoost;
    dolphinBoost = effective.dolphinBoost;
    updateInput(effective, tick);
    updateSwimming(touchesLiquid(false), tick);
    updateSpinAttack(tick);
    if (effective.riptide > 0 && !isFlying && !noClip && (touchesLiquid(false) || (effective.raining && exposedToRain()))) {
        launchRiptide(effective.riptide, tick);
    }

    MotionVector start = feet;
    descendingScaffold = false;
    stuckInBlock = false;
    startedInWater = touchesLiquid(false);
    if (teleported) {
        teleported = false;
        velocity = {};
        jumpDelay = 0;
        if (!isFlying) {
            applyJump();
        }
    } else if (isFlying || noClip) {
        runFlight(effective);
    } else if (isGliding && touchingLiquid(true).empty()) {
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
    previousYaw = yaw;
    previousPitch = pitch;
    hasPreviousRotation = true;
    if (isGliding && onGround) {
        isGliding = false;
        glideTicks = 0;
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
    Fluid after = fluidState(boundingBox());
    tick.inWater = after.water;
    tick.inLava = after.lava;
    tick.onClimbable = climbable(floorInt(feet.x), floorInt(feet.y), floorInt(feet.z));
    if (onGround || after.water || after.lava || tick.onClimbable || isFlying || isGliding || noClip) {
        fallen = 0.0f;
    } else if (tick.movement.y < 0.0f) {
        fallen -= tick.movement.y;
    }
    tick.fallDistance = fallen;
    tick.width = width * scale;
    tick.height = height * scale;
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
        runWater();
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

void PlayerMotion::applyClimbable(bool scaffoldJump)
{
    if (!climbable(floorInt(feet.x), floorInt(feet.y), floorInt(feet.z))) {
        return;
    }
    float y = std::max(velocity.y, -ClimbSpeed);
    if ((pressingJump && !scaffoldJump) || collideX || collideZ) {
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

/**
 * After the move and its friction, the effects of the blocks the body is
 * inside, cell by cell in x, y, z order: every bubble column cell pushes the
 * player up or down, harder at its surface, unless flying; then, unless the
 * tick started in water, every honey cell slows the player and caps the fall.
 */
void PlayerMotion::applyInsideBlocks()
{
    std::array<int32_t, 3> low {};
    std::array<int32_t, 3> high {};
    insideCells(boundingBox(), low, high);
    int32_t honeyCells = 0;
    float vertical = velocity.y;
    for (int32_t x = low[0]; x <= high[0]; ++x) {
        for (int32_t y = low[1]; y <= high[1]; ++y) {
            for (int32_t z = low[2]; z <= high[2]; ++z) {
                const world::CollisionState* state = cellState(x, y, z);
                if (named(state, "honey_block")) {
                    ++honeyCells;
                    continue;
                }
                if (isFlying || !named(state, "bubble_column")) {
                    continue;
                }
                bool surface = cellState(x, y + 1, z) == nullptr;
                if (state->face > 0) {
                    vertical = std::max(vertical - 0.03f, surface ? -0.9f : -0.3f);
                } else {
                    vertical = std::min(vertical + (surface ? 0.1f : 0.06f), surface ? 1.8f : 0.7f);
                }
            }
        }
    }
    velocity.y = vertical;
    if (startedInWater) {
        return;
    }
    for (int32_t index = 0; index < honeyCells; ++index) {
        velocity = { velocity.x * 0.4f, std::max(velocity.y, -0.12f), velocity.z * 0.4f };
    }
}

/**
 * Standing on slime or honey damps the horizontal speed after friction,
 * unless the player sneaks or rises.
 */
void PlayerMotion::standOnBlock(const world::CollisionState* block)
{
    if (!onGround || isSneaking || velocity.y >= 0.1f) {
        return;
    }
    if (!named(block, "slime") && !named(block, "honey_block")) {
        return;
    }
    float multiplier = std::abs(velocity.y) * 0.2f + 0.4f;
    velocity = { velocity.x * multiplier, velocity.y, velocity.z * multiplier };
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
    const world::CollisionState* under = blockUnder(0.1f);
    float frictionFactor = AirFriction;
    float acceleration = airSpeed;
    if (onGround) {
        float speed = movementSpeed;
        bool soulBlock = named(under, "soul_sand") || named(under, "soul_soil");
        if (soulSpeedLevel > 0 && soulBlock) {
            speed *= 1.0f + SoulSpeedPerLevel * static_cast<float>(soulSpeedLevel);
        }
        frictionFactor *= friction(under);
        float accelerationFriction = frictionFactor;
        if (soulSpeedLevel <= 0 && named(under, "soul_sand")) {
            accelerationFriction *= SoulSandFriction;
        }
        float cubed = accelerationFriction * accelerationFriction;
        cubed = cubed * accelerationFriction;
        acceleration = speed * (0.16277136f / cubed);
    }

    applyKnockback();
    moveRelative(acceleration);
    ScaffoldContact scaffold = scaffoldContact();
    descendingScaffold = isSneaking && scaffold.overSupported;
    if (descendingScaffold) {
        velocity.y = -ScaffoldingSpeed;
    }
    bool scaffoldJump = jumping && !descendingScaffold && scaffold.inside;
    if (scaffoldJump) {
        velocity.y = ScaffoldingSpeed;
        jumpDelay = JumpDelayTicks;
    } else {
        applyJump();
    }
    bool scaffoldGlide = descendingScaffold && (scaffold.inside || scaffold.over);
    applyPowderSnowTraversal();
    applyClimbable(scaffoldJump);

    MotionVector oldVelocity = velocity;
    bool oldOnGround = onGround;
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

    postCollisionMotion(oldVelocity, oldOnGround, under);

    if (!oldOnGround && onGround) {
        jumpDelay = 0;
    }
    if (stuckInBlock) {
        velocity = {};
    }

    float x = velocity.x;
    float y = velocity.y;
    float z = velocity.z;
    if (!scaffoldGlide && levitationLevel > 0) {
        float levitationSpeed = LevitationMultiplier * static_cast<float>(levitationLevel);
        y += (levitationSpeed - y) * 0.2f;
    } else if (affectedByGravity && !scaffoldGlide) {
        y -= y < 0.0f ? gravity : NormalGravity;
    }
    if (uniformAirDrag) {
        y *= verticalRetention(AirFriction);
    } else if (affectedByGravity) {
        y *= verticalRetention(GravityMultiplier);
    }
    x *= frictionFactor;
    z *= frictionFactor;
    velocity = { x, y, z };
    standOnBlock(under);
    applyInsideBlocks();
}

void PlayerMotion::runWater()
{
    if (isSwimming) {
        float level = std::clamp(float(depthStriderLevel), 0.0f, float(DepthStriderMaxLevel));
        if (dolphinBoost) {
            moveRelative(underwaterSpeed * DolphinSwimMultiplier * ((level / float(DepthStriderMaxLevel)) * 0.3f + 0.7f));
        } else {
            float halved = onGround ? level : level * 0.5f;
            moveRelative(underwaterSpeed + ((baseMovementSpeed - underwaterSpeed) * halved) / float(DepthStriderMaxLevel));
        }
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
        applyInsideBlocks();
        return;
    }
    if (jumping) {
        velocity.y += WaterAscent;
    }
    float drag = WaterDrag;
    float acceleration = underwaterSpeed;
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
    standOnBlock(blockUnder(0.2f));
    applyInsideBlocks();
}

void PlayerMotion::runLava()
{
    if (jumping) {
        velocity.y = velocity.y + WaterAscent;
    }
    float boxBottom = boundingBox().minY;
    moveRelative(lavaSpeed);
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
    standOnBlock(blockUnder(0.2f));
    applyInsideBlocks();
}

/**
 * Ability flight. Without movement input the horizontal drag strengthens,
 * and in creative the vertical speed also drops before the move; jump and
 * sneak push up or down by fixed impulses and cancel each other to a stop.
 * Taking off from the ground keeps the ground's friction for that tick.
 */
void PlayerMotion::runFlight(const MotionInput& input)
{
    applyKnockback();
    bool creative = gameType == GameCreative;
    bool hovering = std::max(std::abs(moveSideways), std::abs(moveForward)) < FlightHoverInput;
    float groundFriction = onGround ? friction(blockUnder(0.1f)) : 1.0f;
    float speed = flySpeed * (isSprinting ? 2.0f : 1.0f);
    moveRelative(speed);
    if (input.jump && input.sneak) {
        velocity.y = 0.0f;
    } else {
        float previous = velocity.y;
        if (hovering && creative && !input.jump && !input.sneak) {
            previous *= CreativeHoverDrag;
        }
        float push = input.jump ? FlightAscend : input.sneak ? FlightDescend : 0.0f;
        velocity.y = push * verticalFlySpeed + previous;
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
    float hoverDrag = !hovering ? 1.0f : creative ? CreativeHoverDrag : OtherHoverDrag;
    float horizontal = groundFriction * hoverDrag * AirFriction;
    auto damp = [&](float value) {
        return std::abs(value) <= std::numeric_limits<float>::epsilon() ? 0.0f : value * horizontal;
    };
    velocity = { damp(velocity.x), velocity.y * verticalRetention(FlightVerticalRetention), damp(velocity.z) };
    if (!noClip) {
        applyInsideBlocks();
    }
}

/**
 * Elytra flight: gravity is partly lifted the flatter the player looks,
 * falling speed turns into forward speed, looking up trades speed for
 * height, the horizontal speed turns toward where the player looks and a
 * firework boost pulls toward the look direction. Lift reads the current
 * pitch, while the look direction comes from the previous rotation advanced
 * by the wrapped difference to the current one, which the game computes in
 * that order.
 */
void PlayerMotion::runGlide()
{
    auto wrapDegrees = [](float degrees) {
        float wrapped = std::fmod(degrees + 180.0f, 360.0f);
        if (wrapped < 0.0f) {
            wrapped += 360.0f;
        }
        return wrapped + -180.0f;
    };
    float fromYaw = hasPreviousRotation ? previousYaw : yaw;
    float fromPitch = hasPreviousRotation ? previousPitch : pitch;
    constexpr float Radians = Pi / 180.0f;
    float lookYaw = (wrapDegrees(yaw - fromYaw) + fromYaw) * -Radians + -Pi;
    float lookPitch = (wrapDegrees(pitch - fromPitch) + fromPitch) * -Radians;
    float horizontal = -cosine(lookPitch);
    MotionVector look { horizontal * sine(lookYaw), sine(lookPitch), cosine(lookYaw) * horizontal };
    float lookHorizontalSquared = look.x * look.x + look.z * look.z;
    float lookHorizontal = std::sqrt(lookHorizontalSquared);
    float lookLength = std::sqrt(look.y * look.y + look.x * look.x + look.z * look.z) / GlideLookLengthDivisor;
    float pitchAngle = pitch * Radians;
    float pitchCosine = cosine(pitchAngle);
    float lift = std::min(lookLength, 1.0f) * pitchCosine * pitchCosine;

    float x = velocity.x;
    float y = velocity.y;
    float z = velocity.z;
    float speedHorizontal = std::sqrt(x * x + z * z);
    if (affectedByGravity) {
        float fall = slowFalling ? -SlowFallingGravity : -NormalGravity;
        y -= (GlideLift * lift + -1.0f) * fall;
    }
    if (lookHorizontalSquared > 0.0f && y < 0.0f) {
        float converted = lift * -GlideFallConversion * y;
        x += (look.x * converted) / lookHorizontal;
        y += converted;
        z += (look.z * converted) / lookHorizontal;
    }
    if (pitchAngle < 0.0f && lookHorizontalSquared > 0.0f) {
        float converted = sine(pitchAngle) * speedHorizontal * -GlideClimbConversion;
        x -= (converted * look.x) / lookHorizontal;
        y += GlideClimbBoost * converted;
        z -= (converted * look.z) / lookHorizontal;
    }
    if (lookHorizontalSquared > 0.0f) {
        x += ((look.x / lookHorizontal) * speedHorizontal - x) * GlideAlignment;
        z += ((look.z / lookHorizontal) * speedHorizontal - z) * GlideAlignment;
    }
    if (glideBoost) {
        auto boost = [](float axis, float direction) {
            return axis + (GlideBoostTarget * direction - axis) * GlideBoostBlend + direction * GlideBoostPush;
        };
        x = boost(x, look.x);
        y = boost(y, look.y);
        z = boost(z, look.z);
    }
    velocity = { x * GlideHorizontalDrag, y * GlideVerticalDrag, z * GlideHorizontalDrag };
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
    applyInsideBlocks();
}

}
