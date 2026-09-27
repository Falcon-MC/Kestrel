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
inline constexpr float PreventedJumpMultiplier = 0.6f;
inline constexpr float SneakInput = 0.3f;
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
inline constexpr float LedgeClimb = 0.3f;
inline constexpr float LavaDrag = 0.5f;
inline constexpr float FluidHorizontalInset = 0.001f;
inline constexpr float FluidVerticalInset = 0.40099999f;
inline constexpr int32_t FlyToggleWindow = 7;
inline constexpr float Pi = 3.1415927f;
inline constexpr int32_t GameSpectator = 6;

/**
 * Warms the 65536 entry sine table the movement rules read their angles
 * from.
 */
void prepareSineTable();
float sine(float value);
float cosine(float value);

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
