#pragma once

#include <cmath>
#include <cstdint>
#include <memory>

namespace kestrel::ui {
class DrawList;
class Skin;
}

namespace kestrel::world {
class BlockAssets;
}

namespace kestrel {

constexpr bool fireOverlayVisible(uint64_t flags, bool firstPerson, bool attached, bool spectator)
{
    return (flags & 1) != 0 && firstPerson && attached && !spectator;
}

inline uint32_t fireOverlayFrame(double seconds, uint32_t frames, uint32_t ticksPerFrame)
{
    if (!std::isfinite(seconds) || seconds < 0 || frames == 0 || frames > 64 || ticksPerFrame == 0) return 0;
    double period = double(frames) * ticksPerFrame / 20.0;
    return uint32_t(std::fmod(seconds, period) * 20.0 / ticksPerFrame) % frames;
}

class FireOverlay {
public:
    void draw(ui::DrawList& list, ui::Skin& skin, const std::shared_ptr<const world::BlockAssets>& assets, float width, float height, double seconds);

private:
    void load(ui::Skin& skin, const std::shared_ptr<const world::BlockAssets>& assets);
    std::weak_ptr<const world::BlockAssets> source;
    uint32_t frames = 0;
    uint32_t ticksPerFrame = 1;
};

}
