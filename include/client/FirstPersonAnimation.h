#pragma once

#include "world/EntityAnimation.h"

namespace kestrel {

inline void cameraLocalHandPose(world::AnimationInput& input)
{
    // The view matrix already carries these rotations; pack driver variables remain intact.
    input.yaw = 0.0f;
    input.headYaw = 0.0f;
    input.pitch = 0.0f;
}

}
