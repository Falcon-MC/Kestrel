#pragma once

#include "mod/Types.h"

#include <string>
#include <vector>

namespace kestrel::mod {

/**
 * The local player. Values are from the last frame and read as defaults
 * while not in a world.
 */
class Player {
public:
    static constexpr int InventorySize = 36;
    static constexpr int HotbarSize = 9;

    virtual ~Player() = default;

    virtual bool inWorld() const = 0;
    virtual std::string name() const = 0;
    virtual uint64_t runtimeId() const = 0;

    // Feet position, smoothed between ticks the way the camera sees it.
    virtual Vec3 position() const = 0;
    virtual Vec3 eyePosition() const = 0;
    virtual Rotation rotation() const = 0;
    virtual void setRotation(Rotation rotation) = 0;

    virtual float health() const = 0;
    virtual float maxHealth() const = 0;
    virtual float absorption() const = 0;
    virtual float hunger() const = 0;
    virtual float saturation() const = 0;
    virtual int level() const = 0;
    // Progress toward the next level, 0 to 1.
    virtual float experience() const = 0;
    virtual int air() const = 0;
    virtual std::string gameMode() const = 0;
    virtual int dimension() const = 0;
    virtual bool dead() const = 0;

    virtual bool onGround() const = 0;
    virtual bool sneaking() const = 0;
    virtual bool sprinting() const = 0;
    virtual bool swimming() const = 0;
    virtual bool flying() const = 0;

    virtual int selectedSlot() const = 0;
    virtual void selectSlot(int slot) = 0;
    // Slots 0 to 8 are the hotbar.
    virtual ItemStack inventory(int slot) const = 0;
    // Helmet, chestplate, leggings, boots.
    virtual ItemStack armor(int slot) const = 0;
    virtual ItemStack offhand() const = 0;
    virtual std::vector<StatusEffect> effects() const = 0;

    ItemStack heldItem() const
    {
        return inventory(selectedSlot());
    }

    virtual void attack() = 0;
    virtual void use() = 0;
    virtual void pickBlock(bool withData = false) = 0;
    virtual void dropHeld(bool wholeStack = false) = 0;
    virtual void respawn() = 0;
};

}
