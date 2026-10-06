#pragma once

#include "Protocol/Packets/SyncWorldClocksPacket.h"

#include <optional>

namespace kestrel {

class WorldClockSync {
public:
    struct State {
        int32_t time;
        bool paused;
    };

    std::optional<State> apply(const SyncWorldClocksPacket& packet)
    {
        if (packet.mType == SyncWorldClocksType::InitializeRegistry) {
            clockId.reset();
            for (const auto& clock : packet.mClocks) {
                if (clock.mName != "minecraft:overworld") continue;
                clockId = clock.mId;
                return State { clock.mTime, clock.mPaused };
            }
        } else if (packet.mType == SyncWorldClocksType::SyncState && clockId) {
            for (const auto& clock : packet.mClockStates) {
                if (clock.mClockId == *clockId) return State { clock.mTime, clock.mPaused };
            }
        }
        return std::nullopt;
    }

private:
    std::optional<uint64_t> clockId;
};

inline double worldTimeAt(int64_t ticks, double stamp, bool daylightCycle, bool paused, double now)
{
    return static_cast<double>(ticks) + (daylightCycle && !paused ? (now - stamp) * 20.0 : 0.0);
}

}
