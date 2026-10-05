#pragma once

#include "mod/Types.h"

namespace kestrel::mod {

/**
 * The view the world is drawn from. A detached camera draws from where the
 * mod puts it while the player stays where it is: the mouse still turns the
 * player, so a free camera reads Player::rotation and moves itself, and
 * cancels the player's movement through MovementEvent if it wants it still.
 */
class Camera {
public:
    virtual ~Camera() = default;

    /**
     * Where the view was drawn from last frame and which way it looked.
     */
    virtual Vec3 position() const = 0;
    virtual Rotation rotation() const = 0;

    /**
     * Draws the world from this position and rotation until attach, with
     * the player shown as in third person and no hand in view. Call it every
     * frame to move the camera.
     */
    virtual void detach(const Vec3& position, Rotation rotation) = 0;
    virtual void attach() = 0;
    virtual bool detached() const = 0;

    /**
     * Multiplies the field of view: below 1 zooms in, above 1 widens it.
     */
    virtual void setFovScale(float scale) = 0;

    // Added in API 3 and kept last so older mods still find everything above.
    // The vertical field of view the world was drawn with last frame, in degrees.
    virtual float fieldOfView() const = 0;
};

}
