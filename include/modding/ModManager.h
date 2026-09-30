#pragma once

#include "client/Session.h"
#include "menu/Menu.h"
#include "mod/Types.h"
#include "render/Renderer.h"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kestrel {
struct InputState;
namespace menu {
class Menu;
}
namespace ui {
class Context;
}
}

namespace kestrel::modding {

struct HostState;
class ModSlot;
class ChatService;

/**
 * What one mod asked of the camera: to draw from its own place, and how
 * wide to make the view.
 */
struct CameraRequest {
    bool detached = false;
    mod::Vec3 position;
    mod::Rotation rotation;
    float fovScale = 1.0f;
};

/**
 * What only the client knows how to do, handed to the mods' services.
 */
struct ClientBridge {
    std::function<void(std::string title, std::string subtitle)> showTitle;
    std::function<void(std::string text)> showActionbar;
    std::function<mod::Vec3()> eyePosition;
    std::function<mod::Rotation()> rotation;
    std::function<void(mod::Rotation rotation)> setRotation;
    std::function<void(std::string name, std::string address)> connect;
};

/**
 * Loads every mod library in the mods folder and sits between the client and
 * them: the client reports what happens each frame, the mods get their events
 * and act through their context. Every mod found is enabled; taking a mod out
 * of the folder is how it gets turned off.
 */
class ModManager {
public:
    ModManager(Session& session, menu::Menu& menu, Renderer& renderer, ClientBridge bridge);
    ~ModManager();

    ModManager(const ModManager&) = delete;
    ModManager& operator=(const ModManager&) = delete;

    void loadFolder(const std::filesystem::path& folder);

    bool empty() const
    {
        return slots.empty();
    }

    /**
     * Key and click events for this frame's input, before anything else reads
     * it; a cancelled press is taken out of input. uiScale turns the pixel
     * mouse position into the interface units mods draw in.
     */
    void handleInput(InputState& input, bool inGame, float uiScale);

    /**
     * Whether a mod has freed the mouse, so play should give the cursor back.
     */
    bool wantsCursor() const;

    /**
     * Where a mod wants the world drawn from this frame, if one detached the
     * camera, and the field of view scale all mods asked for together.
     */
    std::optional<CameraRequest> cameraView() const;
    float fovScale() const;

    /**
     * Tells the mods where the view was drawn from, for Camera::position.
     */
    void setView(const mod::Vec3& position, mod::Rotation rotation);

    /**
     * Every block name the mods hide, when the list changed since the last
     * call.
     */
    std::optional<std::set<std::string>> takeHiddenBlocks();
    void observe(const SessionSnapshot& snapshot);
    void update(float deltaSeconds);
    void adjustMovement(MotionInput& input);

    // False means a mod hid it or, for sendChat, used it up as a command.
    bool receiveChat(const ChatMessage& message, std::string& text);
    bool sendChat(std::string& text);
    bool filterTitle(TitleRequest& request);
    bool filterActionbar(ActionbarText& actionbar);
    bool filterToast(ToastRequest& toast);
    bool filterForm(const FormRequest& form);

    std::vector<menu::ModKeyBind> listedKeyBinds() const;
    bool setKeyBind(const std::string& id, Key key);

    void drawHud(ui::Context& context, float width, float height, bool screenOpen);

    /**
     * Collects the mods' world draws and draws them; viewProjection works on
     * positions relative to camera.
     */
    void drawWorld(const std::array<float, 16>& viewProjection, const mod::Vec3& camera);

    /**
     * Runs the mods' post processing passes over the world just drawn.
     */
    void drawPost(const std::array<float, 16>& viewProjection);

    void setEnvironment(const mod::Environment& environment);

    /**
     * Draws the HUD layer's shader draws queued by drawHud, under or over the
     * interface.
     */
    void drawScreen(CustomLayer layer);

private:
    void reportError(size_t owner, std::string_view what);
    void registerBuiltins();
    void drainPackets();
    std::string modName(size_t owner) const;

    std::unique_ptr<HostState> host;
    std::unique_ptr<ChatService> hostChat;
    std::vector<std::unique_ptr<ModSlot>> slots;
    std::set<size_t> warned;
    double started = 0.0;
    double tickClock = 0.0;
    uint64_t ticks = 0;
    float hudWidth = 0.0f;
    float hudHeight = 0.0f;
    SessionState lastState = SessionState::Idle;
    uint64_t lastJoin = 0;
    bool lastDead = false;
    int lastDimension = 0;
};

}
