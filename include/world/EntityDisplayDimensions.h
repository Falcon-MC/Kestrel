#pragma once

#include "EntityDisplayDimensionsData.h"

#include <algorithm>
#include <iterator>

namespace kestrel::world {

inline const EntityDisplaySize* entityDisplaySize(std::string_view identifier)
{
    const auto found = std::lower_bound(std::begin(EntityDisplaySizes), std::end(EntityDisplaySizes), identifier,
        [](const EntityDisplaySize& size, std::string_view name) { return size.name < name; });
    return found != std::end(EntityDisplaySizes) && found->name == identifier ? found : nullptr;
}

}
