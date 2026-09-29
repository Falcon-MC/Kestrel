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
