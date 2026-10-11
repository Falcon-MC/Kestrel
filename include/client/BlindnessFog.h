#pragma once

#include "client/Sky.h"

#include <algorithm>

namespace kestrel {

inline SkyFrame blindedBy(const SkyFrame& frame)
{
    SkyFrame result = frame;
    result.fogColor = result.zenith = result.horizon = { 0.0f, 0.0f, 0.0f };
    result.fogEnd = std::min(frame.fogEnd, 5.0f);
    result.fogStart = std::min(frame.fogStart, result.fogEnd * 0.25f);
    return result;
}

}
