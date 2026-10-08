#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace kestrel {

/**
 * The block hit during one breaking tick: its cell, the face hit (down, up,
 * north, south, west, east), its network block value and the point hit.
 */
struct BreakHit {
    std::array<int32_t, 3> cell {};
    int32_t face = 0;
    uint32_t value = 0;
    std::array<double, 3> point {};
};

/**
 * One block action a breaking tick sends, in the order it goes out. Destroy
 * is the predicted destroy that ends a break.
 */
struct BreakAction {
    enum class Kind {
        Start,
        Continue,
        Abort,
        Destroy,
    };

    Kind kind = Kind::Start;
    std::array<int32_t, 3> cell {};
    int32_t face = 0;
};

/**
 * How far the breaking of one block got during a tick, from 0 to 1, and
 * whether it broke or was given up.
 */
struct BreakProgress {
    std::array<int32_t, 3> cell {};
    float progress = 0.0f;
    bool finished = false;
    bool aborted = false;
};

/**
 * What one breaking tick did: the actions it sends, the progress it made,
 * whether the arm swings at a block, the hit sound is due and the face
 * chips.
 */
struct BreakStep {
    std::vector<BreakAction> actions;
    std::vector<BreakProgress> progress;
    bool swung = false;
    bool hitSound = false;
    bool crack = false;
};

/**
 * The local player's breaking between ticks, stepped once a tick with the
 * block hit, whoever chose it: the crosshair or a mod.
 */
class BlockBreaker {
public:
    static constexpr int32_t DestroyDelayTicks = 5;
    static constexpr uint32_t HitSoundTicks = 4;

    /**
     * One tick of holding the attack button over hit, which breaks speed of
     * the block each tick. creative waits out the destroy delay after an
     * instant break too; blocked keeps a new break from starting.
     */
    BreakStep step(bool held, const std::optional<BreakHit>& hit, float speed, bool creative, bool blocked)
    {
        BreakStep result;
        bool heldBefore = attackHeldBefore;
        attackHeldBefore = held;
        if (destroyDelay > 0) {
            --destroyDelay;
        }
        if (active && (!hit || hit->cell != cell)) {
            if (!hit) {
                result.actions.push_back({ BreakAction::Kind::Abort, cell, face });
            }
            result.progress.push_back({ cell, std::fmin(progress, 1.0f), false, true });
            switched = hit.has_value();
            active = false;
        }
        if (!hit) {
            return result;
        }
        result.swung = true;
        if (active) {
            face = hit->face;
            result.hitSound = ticks++ % HitSoundTicks == 0;
            result.crack = true;
            progress += speed;
            if (progress < 1.0f) {
                result.progress.push_back({ cell, progress, false, false });
                return result;
            }
            result.actions.push_back({ BreakAction::Kind::Destroy, cell, face });
            result.progress.push_back({ cell, 1.0f, true, false });
            active = false;
            destroyDelay = DestroyDelayTicks;
            return result;
        }
        if (destroyDelay != 0 || blocked) {
            return result;
        }
        result.actions.push_back({ heldBefore && switched ? BreakAction::Kind::Continue : BreakAction::Kind::Start, hit->cell, hit->face });
        switched = false;
        active = true;
        cell = hit->cell;
        face = hit->face;
        value = hit->value;
        progress = 0.0f;
        ticks = 0;
        if (speed < 1.0f) {
            result.progress.push_back({ cell, 0.0f, false, false });
            return result;
        }
        result.actions.push_back({ BreakAction::Kind::Destroy, cell, face });
        result.progress.push_back({ cell, 1.0f, true, false });
        active = false;
        if (creative) {
            destroyDelay = DestroyDelayTicks;
        }
        return result;
    }

    bool active = false;
    std::array<int32_t, 3> cell {};
    int32_t face = 0;
    uint32_t value = 0;
    float progress = 0.0f;
    uint32_t ticks = 0;
    bool attackHeldBefore = false;
    bool switched = false;
    int32_t destroyDelay = 0;
};

/**
 * The centre of one face of a box from low to high, faces counted down, up,
 * north, south, west, east.
 */
inline std::array<double, 3> faceCentre(const std::array<double, 3>& low, const std::array<double, 3>& high, int32_t face)
{
    std::array<double, 3> centre { (low[0] + high[0]) * 0.5, (low[1] + high[1]) * 0.5, (low[2] + high[2]) * 0.5 };
    size_t axis = face < 2 ? 1 : face < 4 ? 2 : 0;
    centre[axis] = face % 2 == 0 ? low[axis] : high[axis];
    return centre;
}

/**
 * The yaw and pitch, in degrees as the game measures them, that look from
 * eye toward point.
 */
inline std::array<float, 2> lookRotation(const std::array<double, 3>& eye, const std::array<double, 3>& point)
{
    constexpr double Degrees = 180.0 / 3.141592653589793;
    double dx = point[0] - eye[0];
    double dy = point[1] - eye[1];
    double dz = point[2] - eye[2];
    double horizontal = std::sqrt(dx * dx + dz * dz);
    return { float(std::atan2(-dx, dz) * Degrees), float(std::atan2(-dy, horizontal) * Degrees) };
}

}
