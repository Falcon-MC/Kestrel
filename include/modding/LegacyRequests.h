#pragma once

#include "mod/Canvas.h"
#include "mod/Effects.h"
#include "mod/Event.h"
#include "mod/Textures.h"

#include <filesystem>
#include <span>
#include <string>

/**
 * The request events mods built against API 3 post on their event bus. Their
 * layout must stay exactly as those mods compiled it, since the mod fills the
 * struct and the host reads it back.
 */
namespace kestrel::mod::detail {

struct EffectRequest : Event {
    KESTREL_EVENT("kestrel:local_effect_request/v1")
    enum class Kind { Particle, Sound };
    enum class Action { Supported, Create, Active, Move, Volume, Remove, Clear };
    Kind kind = Kind::Particle;
    Action action = Action::Supported;
    uint64_t handle = 0;
    ParticleOptions particle;
    SoundOptions sound;
    Vec3 position;
    float volume = 1.0f;
    bool result = false;
};

struct UiRequest : Event {
    KESTREL_EVENT("kestrel:ui_request/v1")
    enum class Action { Supported, Open, Close, IsOpen };
    Action action = Action::Supported;
    std::string id;
    bool result = false;
};

struct TextureRequest : Event {
    KESTREL_EVENT("kestrel:texture_request/v1")
    enum class Action { Supported, Load, Decode, Create, Info, Read, Update, Patch, Draw, Destroy, Clear };
    Action action = Action::Supported;
    TextureHandle handle = 0;
    std::filesystem::path path;
    std::span<const uint8_t> encoded;
    Image image;
    uint32_t x = 0, y = 0;
    Canvas* canvas = nullptr;
    Rect rect;
    Color tint { 255, 255, 255, 255 };
    bool result = false;
};

}
