#pragma once

#include <cstdint>

namespace kestrel::world {

inline constexpr uint8_t DoorUpper = 16;

// Direction and opening belong to the lower half; the upper half owns the hinge.
inline constexpr uint8_t resolveDoorState(uint8_t own, uint8_t other)
{
    return (own & DoorUpper) ? uint8_t((own & 24) | (other & 7)) : uint8_t((own & 7) | (other & 8));
}

}
