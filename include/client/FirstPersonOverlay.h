#pragma once

#include <string_view>

namespace kestrel {

constexpr bool pumpkinMaskVisible(std::string_view headItem, bool firstPerson, bool attachedCamera, bool spectator)
{
    return headItem == "minecraft:carved_pumpkin" && firstPerson && attachedCamera && !spectator;
}

}
