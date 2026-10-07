#include "client/RayBox.h"
#include <cstdio>
#include <cstdlib>

int main()
{
    using namespace kestrel;
    const std::array<double, 3> low { -0.8, 0, -0.8 }, high { 0.8, 2.7, 0.8 };
    auto inside = actorRayDistance({ 0, 1.62, 0 }, { 0, 0, 1 }, low, high, 3, 0.1);
    if (!inside || *inside != 0) { std::fprintf(stderr, "An overlapping actor must be hittable at zero distance\n"); return 1; }
    auto outside = actorRayDistance({ 0, 1.62, -2 }, { 0, 0, 1 }, low, high, 3, 0.1);
    if (!outside || std::abs(*outside - 1.2) > 1e-6) return 1;
    if (actorRayDistance({ 2, 1.62, -2 }, { 0, 0, 1 }, low, high, 3, 0.1)) return 1;
    if (actorRayDistance({ 0, 1.62, -4 }, { 0, 0, 1 }, low, high, 3, 0.1)) return 1;
}
