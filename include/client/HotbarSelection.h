#pragma once

namespace kestrel {

struct HotbarSelection {
    int inputDelayTimer = 0;

    // IDA HudScreenController::tick rearms scrolling once per HUD render.
    void tick()
    {
        if (inputDelayTimer < 1) ++inputDelayTimer;
    }

    int scroll(int selected, float amount)
    {
        if (inputDelayTimer < 1 || amount == 0.0f) return selected;
        inputDelayTimer = 0;
        return (selected + (amount > 0.0f ? 8 : 1)) % 9;
    }
};

}
