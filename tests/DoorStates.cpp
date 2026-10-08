#include "world/BlockModels.h"
#include "world/DoorState.h"
#include "client/RayBox.h"

#include <cstdio>
#include <cstdlib>

using namespace kestrel;
using namespace kestrel::world;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main()
{
    for (uint8_t lower = 0; lower < 16; ++lower) {
        for (uint8_t upper = 16; upper < 32; ++upper) {
            uint8_t bottom = resolveDoorState(lower, upper);
            uint8_t top = resolveDoorState(upper, lower);
            require((bottom & 15) == (top & 15), "Both door halves must share their effective geometry");
            require((bottom & 7) == (lower & 7), "Opening and direction must come from the lower half");
            require((bottom & 8) == (upper & 8), "The hinge must come from the upper half");
            require(!(bottom & DoorUpper) && (top & DoorUpper), "Half identity must be preserved");
        }
    }
    auto closed = models::doorBounds(1, false, false);
    require(closed.first[2] == 0 && closed.second[2] == 48, "South-facing closed doors must occupy the north edge");
    auto opened = models::doorBounds(1, true, false);
    require(opened.first[0] == 208 && opened.second[0] == 256, "An open door must rotate to its hinge edge");
    auto opposite = models::doorBounds(1, true, true);
    require(opposite.first[0] == 0 && opposite.second[0] == 48, "The opposite hinge must rotate to the other edge");
    for (double y : { 0.5, 1.5 }) {
        double half = y > 1 ? 1 : 0;
        std::array<double, 3> low { 0, half, 0 };
        std::array<double, 3> high { 1, half + 1, 3.0 / 16 };
        require(enterBox({ 0.5, y, -2 }, { 0, 0, 1 }, low, high).has_value(), "Closed doors must be targetable on either half");
        require(!enterBox({ 0.5, y, -2 }, { 0, 0, 1 }, { 13.0 / 16, half, 0 }, { 1, half + 1, 1 }), "The open doorway must leave the look ray clear");
    }
}
