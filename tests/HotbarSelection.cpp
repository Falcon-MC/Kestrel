#include "client/HotbarSelection.h"
#include "client/HandEquip.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void close(float actual, float expected, const char* message)
{
    require(std::abs(actual - expected) < 0.00001f, message);
}

kestrel::HudItem item(const char* name, int count = 1)
{
    kestrel::HudItem result;
    result.identifier = name;
    result.count = count;
    return result;
}
}

int main()
{
    try {
        using namespace kestrel;
        HotbarSelection hotbar;
        require(hotbar.scroll(0, -1.0f) == 0, "scroll must wait for the first HUD update");
        hotbar.tick();
        require(hotbar.scroll(0, 0.0f) == 0, "zero scroll must leave the gate armed");
        require(hotbar.scroll(0, 12.0f) == 8, "upward scroll wraps by one slot regardless of amplitude");
        require(hotbar.scroll(8, -1.0f) == 8, "a second callback before HUD update must be ignored");
        hotbar.tick();
        require(hotbar.scroll(8, -0.001f) == 0, "the next HUD update rearms downward scrolling");

        HudItem stone = item("minecraft:stone");
        HudItem dirt = item("minecraft:dirt");
        HandEquip hand;
        hand.update(stone, 0, 0.0);
        for (int tick = 1; tick <= 4; ++tick) hand.update(stone, 0, tick * 0.05);
        close(hand.height, 1.0f, "initial item must rise to full height");
        hand.update(dirt, 1, 0.25);
        close(hand.height, 0.6f, "first switch tick must lower height by 0.4");
        close(hand.sample(0.275), 0.8f, "rendering must interpolate the two tick heights");
        require(hand.item.identifier == stone.identifier, "old item stays visible above the exchange threshold");
        hand.update(dirt, 1, 0.30);
        close(hand.height, 0.2f, "second switch tick must keep lowering the old item");
        hand.update(dirt, 1, 0.35);
        require(hand.item.identifier == dirt.identifier && hand.slot == 1, "third switch tick exchanges the item at the bottom");
        for (int tick = 8; tick <= 11; ++tick) hand.update(dirt, 1, tick * 0.05);
        hand.update(dirt, 2, 0.60);
        close(hand.height, 1.0f, "identical stacks in different slots must not lower the hand");

        HudItem changed = dirt;
        ++changed.count;
        require(!HandEquip::matches(dirt, changed), "stack count is part of the vanilla match");
        changed = dirt;
        changed.userData = Tag::ofCompound();
        changed.userData.putInt("Damage", 3);
        require(!HandEquip::matches(dirt, changed), "NBT-only changes must be detected");
        changed = dirt;
        changed.canBreak.push_back("minecraft:stone");
        require(!HandEquip::matches(dirt, changed), "stack restrictions must be compared");
        changed = dirt;
        changed.blockRuntimeId = 7;
        require(!HandEquip::matches(dirt, changed), "block variants must be compared");

        hand.update(stone, 3, 0.65);
        hand.update(dirt, 2, 0.70);
        close(hand.height, 1.0f, "returning to the displayed item before exchange must reverse the dip");
        hand.update(stone, 3, 10.0);
        require(hand.item.identifier == stone.identifier, "a stalled frame must catch up the item exchange");
        close(hand.sample(10.0), 1.0f, "a stalled frame must finish the transition");

        HandEquip slow, fast;
        for (int frame = 0; frame <= 240; ++frame) {
            double now = frame / 240.0;
            fast.update(frame < 72 ? stone : dirt, 0, now);
            if (frame % 8 == 0) {
                slow.update(frame < 72 ? stone : dirt, 0, now);
                close(slow.sample(now), fast.sample(now), "equip timing must remain independent of render FPS");
                require(slow.item == fast.item, "render FPS must not change which item is displayed");
            }
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
