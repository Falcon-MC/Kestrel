#pragma once

#include "client/Session.h"
#include "mod/Shaders.h"

namespace kestrel {

/**
 * Draws the debug shapes a server sent through PrimitiveShapes as wireframes
 * and text in the world, skipping the expired ones, the ones of another
 * dimension and the ones past their render distance.
 */
void drawDebugShapes(mod::WorldPainter& painter, const SessionSnapshot& snapshot, double now);

}
