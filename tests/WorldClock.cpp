#include "client/WorldClock.h"

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
    kestrel::WorldClockSync clock;
    SyncWorldClocksPacket packet;
    packet.mClockStates.push_back({});
    packet.mClockStates.back().mClockId = 42;
    packet.mClockStates.back().mTime = 18000;
    require(!clock.apply(packet), "Unregistered clocks must not change the sky");

    packet.mType = SyncWorldClocksType::InitializeRegistry;
    WorldClockData custom;
    custom.mId = 7;
    custom.mName = "audit:other";
    custom.mTime = 123;
    WorldClockData overworld;
    overworld.mId = 42;
    overworld.mName = "minecraft:overworld";
    overworld.mTime = 6000;
    packet.mClocks = { custom, overworld };
    auto state = clock.apply(packet);
    require(state && state->time == 6000 && !state->paused, "Registry must select the named Overworld clock");

    packet.mType = SyncWorldClocksType::SyncState;
    SyncWorldClockStateData unrelated;
    unrelated.mClockId = 7;
    unrelated.mTime = 1000;
    packet.mClockStates.insert(packet.mClockStates.begin(), unrelated);
    state = clock.apply(packet);
    require(state && state->time == 18000, "Sync must use the registered ID rather than the first clock");
    packet.mClockStates.back().mPaused = true;
    state = clock.apply(packet);
    require(state && state->paused, "Clock pause must be applied independently of game rules");
    require(kestrel::worldTimeAt(state->time, 10, true, state->paused, 15) == 18000, "Paused clock must not advance");
    require(kestrel::worldTimeAt(6000, 10, true, false, 15) == 6100, "Unpaused clock must advance at 20 ticks/s");
    require(kestrel::worldTimeAt(6000, 10, false, false, 15) == 6000, "Disabled daylight cycle must still freeze time");

    packet.mType = SyncWorldClocksType::AddTimeMarker;
    require(!clock.apply(packet), "Time marker changes must not reset time");
    packet.mType = SyncWorldClocksType::InitializeRegistry;
    packet.mClocks = { custom };
    require(!clock.apply(packet), "Registry without Overworld must not select a custom clock");
    packet.mType = SyncWorldClocksType::SyncState;
    require(!clock.apply(packet), "Registry replacement must forget the previous clock ID");
    clock = {};
    require(!clock.apply(packet), "New sessions must not reuse old clock IDs");
}
