#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace kestrel {

class ActorRiders {
public:
    std::optional<int64_t> update(int64_t vehicle, int64_t rider, bool remove)
    {
        auto old = vehicleByRider.find(rider);
        if (remove && (old == vehicleByRider.end() || old->second != vehicle)) return std::nullopt;
        std::optional<int64_t> previous;
        if (old != vehicleByRider.end()) {
            previous = old->second;
            auto group = ridersByVehicle.find(*previous);
            if (group != ridersByVehicle.end() && group->second.erase(rider) && group->second.empty()) ridersByVehicle.erase(group);
            vehicleByRider.erase(old);
        }
        if (!remove) {
            vehicleByRider[rider] = vehicle;
            ridersByVehicle[vehicle].insert(rider);
        }
        return previous;
    }

    bool hasRider(int64_t vehicle) const { return ridersByVehicle.contains(vehicle); }

    std::optional<int64_t> erase(int64_t actor)
    {
        std::optional<int64_t> previous;
        if (auto found = vehicleByRider.find(actor); found != vehicleByRider.end()) previous = update(found->second, actor, true);
        if (auto group = ridersByVehicle.find(actor); group != ridersByVehicle.end()) {
            for (int64_t rider : group->second) vehicleByRider.erase(rider);
            ridersByVehicle.erase(group);
        }
        return previous;
    }

    void clear() { vehicleByRider.clear(); ridersByVehicle.clear(); }

private:
    std::unordered_map<int64_t, int64_t> vehicleByRider;
    std::unordered_map<int64_t, std::unordered_set<int64_t>> ridersByVehicle;
};

}
