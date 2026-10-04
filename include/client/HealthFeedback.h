#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace kestrel {

struct HealthFeedback {
    int previousHealth = 20;
    int lastDamage = 0;
    int timerTicks = 0;
    double timerStarted = 0.0;

    // Mob::hurtEffects uses 10 ticks when the level duration is unset.
    static constexpr int InvulnerableTicks = 10;

    int remainingTicks(double now) const
    {
        int elapsed = static_cast<int>(std::floor(std::max(now - timerStarted, 0.0) * 20.0 + 1.0e-7));
        return std::max(timerTicks - elapsed, 0);
    }

    bool flashing(double now) const
    {
        int ticks = remainingTicks(now);
        return ticks >= 10 && (ticks / 3) % 2 == 1;
    }

    bool change(float before, float after, double now, bool known, bool setHealth = false)
    {
        int oldHealth = static_cast<int>(std::floor(before + std::numeric_limits<float>::epsilon()));
        int newHealth = static_cast<int>(std::floor(after + std::numeric_limits<float>::epsilon()));
        if (!known) {
            previousHealth = newHealth;
            return false;
        }
        if (newHealth == oldHealth) return false;
        int remaining = remainingTicks(now);
        if (newHealth > oldHealth) {
            if (remaining < InvulnerableTicks / 2) {
                timerTicks = InvulnerableTicks / 2;
                timerStarted = now;
            }
            return false;
        }
        int damage = oldHealth - newHealth;
        if (setHealth || remaining <= InvulnerableTicks / 2) {
            previousHealth = oldHealth;
            lastDamage = damage;
            timerTicks = InvulnerableTicks;
            timerStarted = now;
            return true;
        }
        lastDamage = std::max(lastDamage, damage);
        return false;
    }
};

}
