#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace kestrel {

struct ActorMoveTarget {
    std::array<double, 3> position {};
    std::array<float, 3> turn {};
};

struct ActorMoveQueue {
    // Bound the backlog even if a server sends moves faster than simulation ticks.
    std::array<ActorMoveTarget, 64> samples {};
    size_t first = 0;
    size_t count = 0;

    void push(const std::array<double, 3>& position, const std::array<float, 3>& turn)
    {
        if (count == samples.size()) {
            first = (first + 1) % samples.size();
            --count;
        }
        samples[(first + count) % samples.size()] = { position, turn };
        ++count;
    }

    bool pop(ActorMoveTarget& sample)
    {
        if (!count) return false;
        sample = samples[first];
        first = (first + 1) % samples.size();
        --count;
        return true;
    }
};

struct ActorInterpolation {
    std::array<double, 3> previous {}, current {}, target {};
    std::array<float, 3> previousTurn {}, currentTurn {}, targetTurn {};
    double tickTime = 0.0;
    uint8_t steps = 0;
    bool initialized = false;

    void retarget(const std::array<double, 3>& position, const std::array<float, 3>& turn, bool teleport)
    {
        target = position;
        targetTurn = turn;
        if (teleport || !initialized) {
            previous = current = target;
            previousTurn = currentTurn = targetTurn;
            steps = 0;
            initialized = true;
        } else {
            // Mob::lerpTo replaces the target; packets do not advance the simulation.
            steps = 3;
        }
    }

    void tick(double stamp)
    {
        if (!initialized) return;
        previous = current;
        previousTurn = currentTurn;
        tickTime = stamp;
        if (!steps) return;
        if (steps == 1) {
            current = target;
            currentTurn = targetTurn;
        } else {
            for (size_t axis = 0; axis < 3; ++axis) {
                current[axis] += (target[axis] - current[axis]) / steps;
                currentTurn[axis] += wrapDegrees(targetTurn[axis] - currentTurn[axis]) / steps;
            }
        }
        --steps;
    }

    double alpha(double now) const
    {
        return tickTime > 0.0 ? std::clamp((now - tickTime) * 20.0, 0.0, 1.0) : 1.0;
    }

    std::array<double, 3> position(double now) const
    {
        auto result = previous;
        double blend = alpha(now);
        for (size_t axis = 0; axis < 3; ++axis) result[axis] += (current[axis] - previous[axis]) * blend;
        return result;
    }

    std::array<float, 3> rotation(double now) const
    {
        auto result = previousTurn;
        float blend = static_cast<float>(alpha(now));
        for (size_t axis = 0; axis < 3; ++axis) result[axis] += wrapDegrees(currentTurn[axis] - previousTurn[axis]) * blend;
        return result;
    }

private:
    static float wrapDegrees(float degrees)
    {
        float wrapped = std::fmod(degrees + 180.0f, 360.0f);
        return (wrapped < 0.0f ? wrapped + 360.0f : wrapped) - 180.0f;
    }
};

}
