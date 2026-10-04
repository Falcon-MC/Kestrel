#include "client/HealthFeedback.h"

#include <cstdio>
#include <cstdlib>

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main()
{
    kestrel::HealthFeedback feedback;
    require(!feedback.change(20, 18, 1, false), "Initial synchronization must not hurt");
    require(feedback.previousHealth == 18, "Initial previous health");
    require(!feedback.change(18.9f, 18.1f, 2, true), "Fractional changes within one heart point must not hurt");
    require(feedback.change(18, 14, 3, true), "First damage must start hurt feedback");
    require(feedback.previousHealth == 18 && feedback.flashing(3), "Flash must use health before damage");
    require(feedback.remainingTicks(3.05) == 9 && !feedback.flashing(3.05), "Native ten-tick default blink threshold");
    require(!feedback.change(14, 12, 3.1, true), "Repeated damage during invulnerability must not restart");
    require(feedback.previousHealth == 18 && feedback.remainingTicks(3.1) == 8, "Repeated hit must preserve timer and old health");
    require(feedback.change(12, 6, 3.25, true), "Damage at half duration must restart");
    require(feedback.previousHealth == 12, "New hurt cycle must capture previous health");
    require(feedback.change(6, 4, 3.3, true, true), "Legacy SetHealth must force a new hurt cycle");
    require(feedback.previousHealth == 6 && feedback.remainingTicks(3.3) == 10, "Forced cycle must reset previous health and timer");
    require(!feedback.change(4, 8, 4, true), "Healing must not animate hurt");
    require(feedback.remainingTicks(4) == 5 && !feedback.flashing(4), "Healing must extend immunity to half duration");
    require(feedback.remainingTicks(4.25) == 0, "Timer must expire on a simulation tick");
    std::puts("Health feedback timing passed");
}
