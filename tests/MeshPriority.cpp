#include "world/MeshPriority.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using kestrel::world::MeshViewPriority;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main()
{
    MeshViewPriority startup({ 8, 8, 8 }, { 1, 0, 0 }, true);
    require(startup.rank(20, 0, 0, true) < startup.rank(0, 0, 0), "Urgent edits precede startup terrain");
    require(startup.rank(1, -8, 1) < startup.rank(2, 0, 0), "All startup column sections precede ordinary terrain");
    require(startup.isStartup(-1, -1) && startup.isStartup(1, 1) && !startup.isStartup(2, 0),
        "Startup terrain is the same 3x3 neighbourhood required for readiness");

    MeshViewPriority forward({ 8, 8, 8 }, { 1, 0, 0 });
    MeshViewPriority backward({ 8, 8, 8 }, { -1, 0, 0 });
    require(forward.rank(4, 0, 0) < forward.rank(0, 0, 0, false, true), "First terrain precedes neighbour refreshes");
    require(forward.rank(0, 0, 0, true, true) < forward.rank(4, 0, 0), "Urgent edits still precede first terrain");
    require(forward.rank(1, 0, 0) < forward.rank(-1, 0, 0), "Prefer terrain ahead at comparable distance");
    require(backward.rank(-1, 0, 0) < backward.rank(1, 0, 0), "Turning reprioritizes queued terrain");
    require(forward.rank(-1, 0, 0) < forward.rank(2, 0, 0), "Closer bands precede farther terrain ahead");
    require(forward.rank(-1, 0, 0, true) < forward.rank(2, 0, 0, true), "Urgent edits retain exact proximity order");
    require(forward.rank(2, 0, 0) < forward.rank(1, -8, 1), "Startup preference ends after terrain is ready");

    MeshViewPriority upward({ 8, 8, 8 }, { 0, 1, 0 });
    MeshViewPriority downward({ 8, 8, 8 }, { 0, -1, 0 });
    require(upward.rank(0, 1, 0) < upward.rank(0, -1, 0), "Camera pitch affects priority");
    require(downward.rank(0, -1, 0) < downward.rank(0, 1, 0), "Looking down favours terrain below");
    MeshViewPriority negative({ -0.5, 8, -16.5 }, {}, true);
    require(negative.isStartup(-2, -3) && negative.isStartup(0, -1) && !negative.isStartup(1, -2),
        "Negative coordinates use floor rather than truncation");

    MeshViewPriority noDirection({ 8, 8, 8 });
    MeshViewPriority invalidDirection({ 8, 8, 8 }, { std::numeric_limits<float>::quiet_NaN(), 0, 0 });
    require(noDirection.rank(1, 0, 0) < noDirection.rank(1, 1, 0), "Missing direction preserves exact proximity");
    require(invalidDirection.rank(1, 0, 0) < invalidDirection.rank(1, 1, 0), "Invalid direction preserves exact proximity");
    auto centered = forward.rank(0, 0, 0);
    require(std::isfinite(centered.facing) && centered < forward.rank(1, 0, 0), "Camera at section centre stays finite");

    std::vector<kestrel::world::MeshPriority> ranks;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -1; y <= 1; ++y) {
            ranks.push_back(startup.rank(x, y, 0));
            ranks.push_back(startup.rank(x, y, 0, true));
        }
    }
    for (const auto& a : ranks) {
        require(!(a < a), "Priority is irreflexive");
        for (const auto& b : ranks) {
            require(!(a < b && b < a), "Priority is asymmetric");
            for (const auto& c : ranks) {
                if (a < b && b < c) require(a < c, "Priority is transitive");
            }
        }
    }
}
