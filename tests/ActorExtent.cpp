#include "client/ActorExtent.h"
#include <cstdio>
#include <limits>

int main()
{
    using kestrel::actorExtent;
    if (actorExtent(0.8f, 1.8f, 0.5f) != 0.4f) { std::fprintf(stderr, "Baby hitbox height must scale with the mob\n"); return 1; }
    if (actorExtent(2.0f, 0.6f, 3.0f) != 6.0f) return 1;
    if (actorExtent(0, 1.8f, 0.5f) != 0.9f) return 1;
    if (actorExtent(0.8f, 1.8f, 0) != 0) return 1;
    if (actorExtent(std::numeric_limits<float>::infinity(), 1.8f, 0.5f) != 0.9f) return 1;
    if (actorExtent(0.8f, 1.8f, std::numeric_limits<float>::quiet_NaN()) != 0.8f) return 1;
}
