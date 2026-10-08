#pragma once

#include "mod/Types.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kestrel::mod {

/**
 * One slot of an open container, counted from 0.
 */
struct ContainerSlot {
    int slot = 0;
    ItemStack item;
};

/**
 * The container screen open right now. type is the client's ContainerType
 * value, as ScreenOpenEvent gives it; position is the block it belongs to,
 * when it belongs to one.
 */
struct ContainerView {
    bool open = false;
    int type = -1;
    int windowId = 0;
    BlockPos position;
    std::vector<ContainerSlot> slots;
};

/**
 * The local player. Values are from the last frame and read as defaults
 * while not in a world.
 */
class Player {
public:
    static constexpr int InventorySize = 36;
    static constexpr int HotbarSize = 9;

    /**
     * The slot groups clickSlot names: the inventory (0 to 35), the armor
     * (0 to 3), the offhand and the cursor (slot 0 each), and the container
     * open on screen (0 up to its size).
     */
    static constexpr int SlotsInventory = 0;
    static constexpr int SlotsArmor = 1;
    static constexpr int SlotsOffhand = 2;
    static constexpr int SlotsCursor = 3;
    static constexpr int SlotsContainer = 4;

    /**
     * The buttons clickSlot presses: a left click takes or puts the whole
     * stack, a right click half of it or one item, and a shift click moves
     * the stack across.
     */
    static constexpr int ClickLeft = 0;
    static constexpr int ClickRight = 1;
    static constexpr int ClickShift = 2;

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

    // Added in API 3 and kept last so older mods still find everything above.
    /**
     * How much damage an item takes before it breaks, 0 for one that never
     * wears out. ItemStack::damage counts up toward it.
     */
    virtual int32_t maxDurability(const std::string& identifier) const = 0;

    /**
     * What the server lets the player do: whether it may fly and is flying,
     * its walking speed attribute and its flying speed.
     */
    struct Abilities {
        bool mayFly = false;
        bool flying = false;
        float walkSpeed = 0.1f;
        float flySpeed = 0.05f;
    };

    // Added in API 4 and kept last so older mods still find everything above.
    // Feet position at the last movement tick, without smoothing.
    virtual Vec3 tickPosition() const = 0;
    // Blocks per tick, as the last movement tick ended.
    virtual Vec3 velocity() const = 0;
    virtual Box boundingBox() const = 0;
    virtual float fallDistance() const = 0;
    virtual bool inWater() const = 0;
    virtual bool inLava() const = 0;
    virtual bool gliding() const = 0;
    virtual bool onClimbable() const = 0;
    virtual bool collidedHorizontally() const = 0;
    virtual bool collidedVertically() const = 0;
    virtual Abilities abilities() const = 0;

    /**
     * Clicks a slot the way the inventory screen does. container is one of
     * the Slots values at the top of this class and slot counts within it;
     * button is one of the Click values. Returns whether the click was queued; it goes to the
     * server as an item stack request, which the server may still refuse.
     */
    virtual bool clickSlot(int container, int slot, int button) = 0;

    /**
     * Moves count items, the whole stack when count is 0 or less, between two
     * inventory slots (0 to 35). The destination has to be empty or hold the
     * same item; returns whether the move was queued.
     */
    virtual bool moveItem(int fromSlot, int toSlot, int count) = 0;

    /**
     * Swaps an inventory slot (0 to 35) with a hotbar slot (0 to 8), as the
     * number keys do over the inventory screen.
     */
    virtual bool swapHotbar(int slot, int hotbarSlot) = 0;

    /**
     * The first inventory slot (0 to 35) whose item accept takes, or -1.
     * Empty slots are not offered.
     */
    virtual int findItem(const std::function<bool(const ItemStack&)>& accept) const = 0;

    /**
     * The hotbar slot whose item breaks the loaded block at position fastest,
     * the selected slot winning ties, or -1 when the block is not loaded, is
     * air or cannot be broken.
     */
    virtual int bestToolFor(const BlockPos& position) const = 0;

    /**
     * Uses the block at position with the held item, as a right click on it
     * would, which opens its screen when it has one. The block has to be
     * within reach of the player; returns whether the use was queued.
     */
    virtual bool openContainer(const BlockPos& position) = 0;

    /**
     * The container screen open right now and what it holds; open is false
     * while none is.
     */
    virtual ContainerView openContainerContents() const = 0;

    /**
     * Closes the container screen open right now, if any.
     */
    virtual void closeContainer() = 0;

    /**
     * Breaks the block at position through face (0 down, 1 up, 2 north,
     * 3 south, 4 west, 5 east) as if the crosshair rested on the centre of
     * that face and attack were held, until stopBreaking, sending the server
     * exactly what breaking it by hand would. While it lasts the player looks
     * at that face unless MovementEvent::overrideRotation says otherwise.
     * The block has to stay within reach.
     */
    virtual void startBreaking(const BlockPos& position, int face) = 0;
    virtual void stopBreaking() = 0;

    /**
     * The block the player is breaking right now, by hand or for a mod, and
     * how far along, from 0 to 1, as of the last tick.
     */
    virtual std::optional<BlockPos> breakingTarget() const = 0;
    virtual float breakingProgress() const = 0;

    /**
     * Clicks the held item on one face of a block, clickPoint being where on
     * the block it lands, from 0 to 1 on each axis, the way a right click on
     * it does. The block has to be within reach.
     */
    virtual void useOn(const BlockPos& position, int face, const Vec3& clickPoint) = 0;

    /**
     * Uses the held item in the air; one that charges, like a bow or food,
     * stays in use until releaseUse.
     */
    virtual void useItem() = 0;
    virtual void releaseUse() = 0;

    /**
     * Hits the entity, if it is within the player's reach.
     */
    virtual void attackEntity(uint64_t runtimeId) = 0;

    /**
     * Holds the attack or use button down, on top of the player's own
     * buttons, until set back to false.
     */
    virtual void setAttackHeld(bool held) = 0;
    virtual void setUseHeld(bool held) = 0;

    /**
     * Where the feet would be at each of the next ticks if the player kept
     * the input of the last movement tick, stepped on a copy of the movement
     * state against the loaded world. Empty while not in a world.
     */
    virtual std::vector<Vec3> predictPath(int ticks) const = 0;
};

}
