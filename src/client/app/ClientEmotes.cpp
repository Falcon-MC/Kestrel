#include "client/Client.h"

#include "Core/Json/Json.h"
#include "modding/ModManager.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace kestrel {

/**
 * The emote halfway through its clip, the pose the wheel shows the player
 * in, for the parts the menu's player model draws.
 */
menu::ModelPose Client::emotePose(const std::string& clipName)
{
    constexpr std::array<std::string_view, 6> Parts { "head", "body", "rightarm", "leftarm", "rightleg", "leftleg" };
    menu::ModelPose pose {};
    const world::AnimationClip* clip = emoteAnimations.clip(clipName);
    if (!clip) {
        return pose;
    }
    world::EntityAnimator animator;
    for (const auto& [bone, rotation] : animator.rotationsAt(*clip, clip->length * 0.5)) {
        std::string name = bone;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (size_t part = 0; part < Parts.size(); ++part) {
            if (name == Parts[part]) {
                pose[part] = rotation;
            }
        }
    }
    return pose;
}

/**
 * Plays an emote the mods added on the local player, from its start; asking
 * for the one already playing starts it over.
 */
void Client::startEmote(const std::string& id, double now)
{
    const modding::EmoteRegistry::Entry* entry = mods->emotes().find(id);
    if (!entry) {
        return;
    }
    const world::AnimationClip* clip = emoteAnimations.clip(entry->clip);
    if (!clip) {
        return;
    }
    bool restart = activeEmote && activeEmote->clip == entry->clip;
    activeEmote = ActiveEmote { id, entry->clip, now, clip->length, clip->loop == world::LoopMode::Once, restart ? 2 : 0 };
}

/**
 * Keeps the emotes in step with the mods, starts the one the wheel or a mod
 * asked for, and stops the playing one when it ends or the player moves,
 * jumps, sneaks, attacks or uses an item, as the game does.
 */
void Client::updateEmotes(double now)
{
    modding::EmoteRegistry& registry = mods->emotes();
    if (registry.revision() != emoteRevision) {
        emoteRevision = registry.revision();
        emoteAnimations = world::AnimationLibrary {};
        std::vector<menu::EmoteOption> options;
        for (const auto& entry : registry.list()) {
            if (std::unique_ptr<json::Value> document = json::parse(entry->spec.animation)) {
                emoteAnimations.parse(*document);
            }
            options.push_back({ entry->spec.id, entry->spec.name, entry->spec.icon, emotePose(entry->clip) });
        }
        menu.setEmotes(std::move(options));
        if (activeEmote && !registry.find(activeEmote->id)) {
            activeEmote.reset();
        }
    }
    if (menu.takeEmoteSlotsChanged()) {
        saveSettings();
    }
    if (std::optional<std::string> picked = menu.takeEmoteRequest()) {
        startEmote(*picked, now);
    }
    if (std::optional<std::string> requested = registry.takeRequest()) {
        if (requested->empty()) {
            activeEmote.reset();
        } else {
            startEmote(*requested, now);
        }
    }
    if (activeEmote) {
        const InputState& input = window->input();
        const KeyBindings& keys = menu.keyBindings();
        bool playing = menu.capturesMouse();
        bool moved = playing && (input.isHeld(keys.forward()) || input.isHeld(keys.back()) || input.isHeld(keys.left()) || input.isHeld(keys.right())
            || input.isHeld(keys.up()) || input.isHeld(keys.down()) || input.mouseDown || input.rightMouseDown);
        bool away = !seenSessionSnapshot || seenSessionSnapshot->state != SessionState::Joined || seenSessionSnapshot->dead
            || seenSessionSnapshot->changingDimension;
        bool ended = activeEmote->once && activeEmote->length > 0.0 && now - activeEmote->started >= activeEmote->length;
        if (moved || away || ended) {
            activeEmote.reset();
        }
    }
    registry.playing = activeEmote ? std::optional<std::string>(activeEmote->id) : std::nullopt;
}

}
