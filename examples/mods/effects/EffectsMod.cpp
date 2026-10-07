#include "mod/Api.h"

#include <cmath>

using namespace kestrel::mod;

class EffectsMod : public Mod {
public:
    EffectsMod() : Mod({ .id = "effects_example", .name = "Local effects", .version = "1.0.0", .author = "Kestrel", .description = "Local particles and positional sounds" }) { }

    void onEnable() override
    {
        command({ "effects", "Controls local particle and sound effects", "particle <id> | follow <id> | sound <name> | once <name> | flat <name> | move | volume <0..4> | status | stop", {} }, [this](CommandContext& command) {
            const auto& action = command.arg(0);
            if (action == "particle" || action == "follow") {
                particles().remove(particle);
                ParticleOptions options { .identifier = command.arg(1), .position = inFront() };
                if (action == "follow") options.attachedEntity = player().runtimeId();
                particle = particles().spawn(std::move(options));
                command.reply(particle ? "Particle created" : "Particle unavailable");
            } else if (action == "sound" || action == "once" || action == "flat") {
                audio().stop(sound);
                SoundOptions options { .name = command.arg(1), .volume = 0.25f, .loop = action != "once" };
                if (action != "flat") options.position = inFront();
                sound = audio().play(std::move(options));
                command.reply(sound ? "Sound playing" : "Sound unavailable");
            } else if (action == "move") {
                auto position = inFront();
                command.reply(particles().move(particle, position) ? "Particle moved" : "No active particle");
                command.reply(audio().setPosition(sound, position) ? "Sound moved" : "No positional sound");
            } else if (action == "volume") {
                command.reply(audio().setVolume(sound, static_cast<float>(command.numberArg(1))) ? "Volume updated" : "Invalid volume or inactive sound");
            } else if (action == "status") {
                command.reply(particles().active(particle) ? "Particle active" : "Particle inactive");
                command.reply(audio().playing(sound) ? "Sound active" : "Sound inactive");
            } else if (action == "stop") {
                particles().clear();
                audio().stopAll();
                command.reply("Local effects stopped");
            } else throw CommandError("Unknown effects action");
        });
    }

private:
    Vec3 inFront() const
    {
        constexpr float Radians = 3.14159265f / 180.0f;
        auto rotation = player().rotation();
        auto position = player().eyePosition();
        position.x -= 3.0 * std::sin(rotation.yaw * Radians) * std::cos(rotation.pitch * Radians);
        position.y -= 3.0 * std::sin(rotation.pitch * Radians);
        position.z += 3.0 * std::cos(rotation.yaw * Radians) * std::cos(rotation.pitch * Radians);
        return position;
    }

    ParticleHandle particle = 0;
    SoundHandle sound = 0;
};

KESTREL_MOD(EffectsMod)
