#include "client/Client.h"

#include "platform/Paths.h"
#include "render/Renderer.h"

namespace kestrel {

/**
 * Loads whatever sits in the mods folder. The bridge is the handful of
 * things only the client can do for them: titles and the action bar, which
 * live in its HUD state, and the camera.
 */
void Client::startMods()
{
    modding::ClientBridge bridge;
    bridge.textures = [this](const std::string& name, const mod::Image* image) {
        if (image) skin.setDynamic(name, { image->width, image->height, image->pixels });
        else skin.clearDynamic(name);
    };
    bridge.effects = [this](mod::detail::EffectRequest& request) {
        using Request = mod::detail::EffectRequest;
        request.result = false;
        if (request.action == Request::Action::Supported) {
            request.result = true;
            return;
        }
        const std::array<double, 3> position { request.position.x, request.position.y, request.position.z };
        if (request.kind == Request::Kind::Particle) {
            switch (request.action) {
            case Request::Action::Create: {
                if (!worldShown) return;
                world::ParticleSpawn spawn;
                spawn.identifier = request.particle.identifier;
                spawn.position = { request.particle.position.x, request.particle.position.y, request.particle.position.z };
                spawn.direction = { float(request.particle.direction.x), float(request.particle.direction.y), float(request.particle.direction.z) };
                spawn.attachedActor = request.particle.attachedEntity;
                spawn.variables = request.particle.variables;
                request.handle = particleSystem.spawnTracked(spawn);
                request.result = request.handle != 0;
                break;
            }
            case Request::Action::Active: request.result = particleSystem.active(request.handle); break;
            case Request::Action::Move: request.result = particleSystem.move(request.handle, position); break;
            case Request::Action::Remove: request.result = particleSystem.remove(request.handle); break;
            default: break;
            }
            return;
        }
        if (!soundEngine || !soundEngine->ready()) return;
        switch (request.action) {
        case Request::Action::Create: {
            const auto& sound = request.sound;
            std::array<double, 3> source {};
            if (sound.position) source = { sound.position->x, sound.position->y, sound.position->z };
            request.handle = soundEngine->playTracked(sound.name, source, sound.position.has_value(), sound.volume, sound.pitch, sound.loop);
            request.result = request.handle != 0;
            break;
        }
        case Request::Action::Active: request.result = soundEngine->playing(request.handle); break;
        case Request::Action::Move: request.result = soundEngine->moveVoice(request.handle, position); break;
        case Request::Action::Volume: request.result = soundEngine->setVoiceVolume(request.handle, request.volume); break;
        case Request::Action::Remove: request.result = soundEngine->stopVoice(request.handle); break;
        default: break;
        }
    };
    bridge.showTitle = [this](std::string title, std::string subtitle) {
        // A subtitle waits for the next title, so it goes first.
        if (!subtitle.empty()) {
            TitleRequest request;
            request.kind = TitleRequest::Kind::Subtitle;
            request.text = std::move(subtitle);
            applyTitle(std::move(request));
        }
        TitleRequest request;
        request.kind = TitleRequest::Kind::Title;
        request.text = std::move(title);
        applyTitle(std::move(request));
    };
    bridge.showActionbar = [this](std::string text) {
        actionbarMessage = { std::move(text), secondsNow() };
    };
    bridge.eyePosition = [this] {
        return mod::Vec3 { eyePosition[0], eyePosition[1], eyePosition[2] };
    };
    bridge.rotation = [this] {
        return mod::Rotation { camera.minecraftYaw(), camera.minecraftPitch() };
    };
    bridge.setRotation = [this](mod::Rotation rotation) {
        camera.setRotation(rotation.yaw, rotation.pitch);
    };
    bridge.connect = [this](std::string name, std::string address) {
        menu.connectTo(std::move(name), std::move(address));
    };
    mods = std::make_unique<modding::ModManager>(session, menu, *renderer, std::move(bridge));
    mods->loadFolder(platform::dataDirectory() / "mods");
    menu.setModKeyBindHandler([this](const std::string& id, Key key) {
        if (mods) {
            mods->setKeyBind(id, key);
        }
    });
}

}
