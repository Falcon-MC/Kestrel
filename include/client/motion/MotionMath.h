#pragma once

#include "client/PlayerMotion.h"

#include <cstdint>
#include <vector>

namespace kestrel::motion {

inline constexpr float AirFriction = 0.91f;
inline constexpr float NormalGravity = 0.08f;
inline constexpr float SlowFallingGravity = 0.01f;
inline constexpr float GravityMultiplier = 0.98f;
inline constexpr float LevitationMultiplier = 0.05f;
inline constexpr float StepHeight = 0.5625f;
inline constexpr float ClimbSpeed = 0.2f;
inline constexpr float HoneyJumpFactor = 0.5f;
inline constexpr float JumpVelocity = 0.42f;
inline constexpr float JumpBoostStep = 0.1f;
inline constexpr float SprintJumpImpulse = 0.2f;
inline constexpr float WalkAirSpeed = 0.02f;
inline constexpr float SprintAirSpeed = 0.026f;
inline constexpr float SprintSpeedMultiplier = 1.3f;
inline constexpr float SoulSandSpeed = 0.543f;
inline constexpr float SoulSpeedPerLevel = 0.105f;
inline constexpr float DepthStriderDrag = 0.546f;
inline constexpr int32_t DepthStriderMaxLevel = 3;
inline constexpr float SwiftSneakPerLevel = 0.15f;
inline constexpr float BedBounce = 0.66f;
inline constexpr float BedBounceLimit = 1.0f;
inline constexpr float StandingHeight = 1.8f;
inline constexpr float SneakingHeight = 1.5f;
inline constexpr float LowPoseHeight = 0.6f;
inline constexpr float PoseFitInset = 1.0E-3f;
inline constexpr float GlideLift = 0.75f;
inline constexpr float GlideFallConversion = 0.1f;
inline constexpr float GlideClimbConversion = 0.04f;
inline constexpr float GlideClimbBoost = 3.2f;
inline constexpr float GlideAlignment = 0.1f;
inline constexpr float GlideHorizontalDrag = 0.99f;
inline constexpr float GlideVerticalDrag = 0.98f;
inline constexpr float SneakInput = 0.3f;
// How much of the movement input is left while an item is held in use, like a drawn bow.
inline constexpr float ItemUseInput = 0.35f;
inline constexpr int32_t JumpDelayTicks = 10;
inline constexpr float CollisionEpsilon = 1.0E-5f;
inline constexpr float PenetrationEpsilonSquared = 1.0E-11f;
inline constexpr float EdgeInset = 0.025f;
inline constexpr float EdgeStep = 0.05f;
inline constexpr float WaterAcceleration = 0.02f;
inline constexpr float WaterDrag = 0.8f;
inline constexpr float WaterFastDrag = 0.9f;
inline constexpr float WaterAscent = 0.04f;
inline constexpr float WaterGravity = 0.02f;
inline constexpr float SwimlessWaterGravity = 0.005f;
inline constexpr float LedgeClimb = 0.3f;
inline constexpr float LavaDrag = 0.5f;
inline constexpr float FluidHorizontalInset = 0.001f;
inline constexpr float FluidVerticalInset = 0.40099999f;
inline constexpr int32_t FlyToggleWindow = 7;
inline constexpr float RiptideLift = 1.1999999f;
inline constexpr int32_t SpinAttackTicks = 20;
inline constexpr int32_t SkyCheckTop = 320;
inline constexpr float Pi = 3.1415927f;
inline constexpr int32_t GameSpectator = 6;

/**
 * Warms the 65536 entry sine table the movement rules read their angles
 * from.
 */
void prepareSineTable();
float sine(float value);
float cosine(float value);

bool boundedQuery(const world::CollisionBox& box);
int32_t floorInt(float value);
world::CollisionBox offset(const world::CollisionBox& box, const MotionVector& vector);
world::CollisionBox grow(const world::CollisionBox& box, float x, float y, float z);

/**
 * The box stretched along the movement, so it covers everything the box
 * sweeps through.
 */
world::CollisionBox extend(const world::CollisionBox& box, const MotionVector& vector);
MotionVector feetOf(const world::CollisionBox& box);

/**
 * The movement left after sliding the moving box against every stationary
 * box, walked from last to first. A box already overlapping pushes the mover
 * out along its shallowest axis unless oneWay, and the deepest overlap per
 * axis is kept in penetration when given.
 */
MotionVector clipAll(const std::vector<world::CollisionBox>& collisions, const world::CollisionBox& moving, MotionVector movement, bool oneWay, float* penetration);

}
