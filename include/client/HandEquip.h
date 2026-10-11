#pragma once

#include "client/Inventory.h"

#include <algorithm>
#include <cmath>

namespace kestrel {

/** ItemInHandRenderer::tick advances height by at most 0.4 per game tick. */
struct HandEquip {
    HudItem item;
    int slot = 0;
    float height = 0.0f;
    float oldHeight = 0.0f;
    double tickTime = -1.0;
    static constexpr double TickSeconds = 0.05;

    static bool matches(const HudItem& a, const HudItem& b)
    {
        if (a.empty() || b.empty()) return a.empty() && b.empty();
        return a.identifier == b.identifier && a.count == b.count && a.aux == b.aux
            && a.blockRuntimeId == b.blockRuntimeId && a.userData == b.userData
            && a.canPlace == b.canPlace && a.canBreak == b.canBreak;
    }

    void tick(const HudItem& selected, int selectedSlot)
    {
        oldHeight = height;
        bool changing = !matches(item, selected);
        height += std::clamp((changing ? 0.0f : 1.0f) - height, -0.4f, 0.4f);
        if (height <= 0.1f) {
            item = selected;
            slot = selectedSlot;
        }
    }

    void update(const HudItem& selected, int selectedSlot, double now)
    {
        if (tickTime < 0.0) {
            tickTime = now;
            tick(selected, selectedSlot);
        }
        double ticks = std::floor((now - tickTime) / TickSeconds + 1.0e-9);
        if (ticks >= 1.0) {
            // Six ticks settle any transition; excess elapsed ticks hold that pose.
            int count = static_cast<int>(std::min(ticks, 6.0));
            for (int i = 0; i < count; ++i) tick(selected, selectedSlot);
            if (ticks > 6.0) oldHeight = height;
            tickTime += ticks * TickSeconds;
        }
    }

    float sample(double now) const
    {
        float alpha = static_cast<float>(std::clamp((now - tickTime) / TickSeconds, 0.0, 1.0));
        return oldHeight + (height - oldHeight) * alpha;
    }
};

}
