#pragma once

#include "mod/Event.h"

#include <optional>
#include <string>
#include <string_view>

namespace kestrel::mod {

/**
 * An emote a mod adds to the emote wheel. The animation is a resource pack
 * animation file, the same JSON as a pack's animations/*.json, played on the
 * player's body in third person; clip names the animation in it to play, or
 * the first one when left empty. Loop or hold the clip with its own
 * "loop" property; a clip that plays once ends the emote when it finishes.
 * The icon is a texture path in the loaded packs, like
 * "textures/ui/emote_wave"; without one the wheel shows the name.
 */
struct EmoteSpec {
    std::string id;
    std::string name;
    std::string animation;
    std::string clip;
    std::string icon;
};

/**
 * The emotes the wheel offers. Emotes play on the local player only and stop
 * as soon as the player moves, jumps, sneaks, attacks or uses an item.
 */
class Emotes {
public:
    virtual ~Emotes() = default;

    /**
     * Adds an emote until the subscription is cancelled or the mod unloads.
     * An emote whose id is taken or whose animation does not parse is
     * refused with an error in the log and an inactive subscription.
     */
    virtual Subscription add(EmoteSpec spec) = 0;

    /**
     * Starts any emote added by any mod, by id.
     */
    virtual void play(std::string_view id) = 0;
    virtual void stop() = 0;

    /**
     * The id of the emote playing, if one is.
     */
    virtual std::optional<std::string> playing() const = 0;
};

}
