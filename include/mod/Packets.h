#pragma once

#include "mod/Types.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace kestrel::mod {

namespace packets {

/**
 * The input the client sends every tick. position is where the client says
 * the player is, at eye height; delta is the movement of the tick; moveX and
 * moveY are the movement keys from -1 to 1. Angles are in degrees. inputs has
 * one entry per input flag of the protocol, indexed by its number (8 is
 * sneaking, 10 to 13 the four movement directions and so on).
 */
struct AuthInput {
    static constexpr int Id = 144;

    Vec3 position;
    Vec3 delta;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float headYaw = 0.0f;
    float moveX = 0.0f;
    float moveY = 0.0f;
    std::vector<bool> inputs;
    uint64_t tick = 0;
};

/**
 * A chat line, raw or to translate. type is the protocol's text type:
 * 0 raw, 1 chat, 2 translation, 3 popup, 4 jukebox popup, 5 tip, 6 system,
 * 7 whisper, 8 announcement, 9 whisper JSON, 10 JSON, 11 announcement JSON.
 */
struct Text {
    static constexpr int Id = 9;

    int type = 0;
    bool needsTranslation = false;
    std::string source;
    std::string message;
    std::vector<std::string> parameters;
    std::string xuid;
    std::string filteredMessage;
};

/**
 * A player moved. mode is 0 normal, 1 respawn, 2 teleport, 3 head rotation
 * only. position is at eye height, as the protocol sends it.
 */
struct MovePlayer {
    static constexpr int Id = 19;

    uint64_t runtimeId = 0;
    Vec3 position;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float headYaw = 0.0f;
    int mode = 0;
    bool onGround = false;
    uint64_t ridingRuntimeId = 0;
    uint64_t tick = 0;
};

/**
 * A block set by the server. runtimeId is the block's network id as the
 * server numbers them; layer 1 holds the water of waterlogged blocks.
 */
struct UpdateBlock {
    static constexpr int Id = 21;

    BlockPos position;
    uint32_t runtimeId = 0;
    uint32_t flags = 0;
    uint32_t layer = 0;
};

/**
 * Something the player did, numbered as the protocol's player action type
 * (0 start break, 1 abort break, 2 stop break and so on). face counts like
 * RaycastHit::face.
 */
struct PlayerAction {
    static constexpr int Id = 36;

    uint64_t runtimeId = 0;
    int action = 0;
    BlockPos position;
    BlockPos resultPosition;
    int face = 0;
};

/**
 * An entity, other than a player or a dropped item, appeared. Its metadata,
 * attributes, links and properties are not part of this view and pass
 * through unchanged.
 */
struct AddActor {
    static constexpr int Id = 13;

    int64_t uniqueId = 0;
    uint64_t runtimeId = 0;
    std::string identifier;
    Vec3 position;
    Vec3 motion;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float headYaw = 0.0f;
    float bodyYaw = 0.0f;
};

/**
 * A use of an item. transactionType is 0 normal, 1 mismatch, 2 item use (on
 * a block or the air), 3 use on an entity, 4 release. actionType is what the
 * transaction does within its type, such as 0 click block and 1 click air for
 * an item use, or 0 interact and 1 attack on an entity. itemInHand is only
 * shown: changing it changes nothing, and the legacy inventory actions of
 * the transaction pass through unchanged.
 */
struct InventoryTransaction {
    static constexpr int Id = 30;

    int transactionType = 0;
    int actionType = 0;
    uint64_t runtimeId = 0;
    BlockPos position;
    int face = 0;
    int hotbarSlot = 0;
    Vec3 playerPosition;
    Vec3 clickPosition;
    Vec3 headPosition;
    ItemStack itemInHand;
};

}

/**
 * A game packet as a TypedPacketFilter sees it: its id and, for the packets
 * this API knows, its decoded fields; any other packet holds std::monostate.
 * Changing the fields changes the packet, which is encoded again with the
 * client's own codec. Changing id or the kind of packet held is ignored.
 */
struct PacketView {
    int id = 0;
    std::variant<std::monostate, packets::AuthInput, packets::Text, packets::MovePlayer, packets::UpdateBlock, packets::PlayerAction,
        packets::AddActor, packets::InventoryTransaction>
        data;

    template <class Kind>
    Kind* as()
    {
        return std::get_if<Kind>(&data);
    }

    template <class Kind>
    const Kind* as() const
    {
        return std::get_if<Kind>(&data);
    }
};

}
