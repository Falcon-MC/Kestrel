#include "client/ActorRiders.h"

int main()
{
    kestrel::ActorRiders riders;
    riders.update(10, 20, false);
    riders.update(10, 20, false);
    if (!riders.hasRider(10)) return 1;
    riders.update(10, 21, false);
    riders.update(10, 20, true);
    if (!riders.hasRider(10)) return 1;
    riders.update(11, 21, false);
    if (riders.hasRider(10) || !riders.hasRider(11)) return 1;
    riders.update(10, 21, true);
    if (!riders.hasRider(11)) return 1;
    riders.erase(21);
    if (riders.hasRider(11)) return 1;
    riders.update(10, 20, false);
    riders.erase(10);
    if (riders.hasRider(10)) return 1;
    riders.update(11, 20, false);
    riders.clear();
    if (riders.hasRider(11)) return 1;
}
