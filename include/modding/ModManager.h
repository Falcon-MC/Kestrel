#pragma once

#include "client/Session.h"
#include "menu/Menu.h"
#include "mod/Hud.h"
#include "mod/Types.h"
#include "mod/Ui.h"
#include "modding/LocalEffects.h"
#include "modding/TextureStore.h"
#include "modding/EmoteRegistry.h"
#include "render/Renderer.h"

#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
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
class Font;
}
}

namespace kestrel::modding {

struct HostState;
class ModSlot;
class ChatService;
class WorldCanvas;

/**
 * Which blocks to draw as air: the listed names, or every block except them
 * when visibleOnly is set.
 */
struct BlockFilter {
    std::set<std::string> names;
    bool visibleOnly = false;
};

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
 * What one mod changed through Visuals. Anything left empty keeps the game's
 * own behaviour, so mods only fight over what they actually set.
 */
struct VisualRequest {
    std::optional<int64_t> time;
    std::optional<float> rain;
    std::optional<float> thunder;
    std::optional<float> fogScale;
    std::optional<float> brightness;
    std::optional<float> hurtCamera;
    std::optional<mod::Color> hitColor;
    std::optional<int> glintStrength;
    std::optional<int> glintSpeed;
    std::optional<bool> itemPhysics;
    std::optional<float> swingDuration;
    std::optional<mod::Vec3> heldOffset;
    std::optional<float> heldScale;
    std::optional<float> nametagScale;
    std::optional<bool> ownNametag;
    std::optional<float> interfaceScale;
};

/**
 * What only the client knows how to do, handed to the mods' services.
 */
struct ClientBridge {
    LocalEffects::Backend effects;
    TextureStore::Backend textures;
    std::function<void(std::string title, std::string subtitle)> showTitle;
    std::function<void(std::string text)> showActionbar;
    std::function<mod::Vec3()> eyePosition;
    std::function<mod::Rotation()> rotation;
    std::function<void(mod::Rotation rotation)> setRotation;
    std::function<void(std::string name, std::string address)> connect;
    // The interface font, for text mods draw into the world.
    const ui::Font* font = nullptr;
};

/**
 * Loads the mod libraries in the mods folder and sits between the client and
 * them: the client reports what happens each frame, the mods get their events
 * and act through their context. Every mod found runs unless the player
 * turned it off, which disabled.txt in the folder remembers; mods can be
 * turned on and off, reloaded or removed while the game runs.
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
    bool uiOpen() const;
    void restoreInput(InputState& input);
    void captureUiInput(InputState& input, float scale);
    void drawUi(ui::Context& context, float width, float height);
    // Whether any mod hides this element of the game's HUD.
    bool hidesHud(mod::HudElement element) const;

    /**
     * Where a mod wants the world drawn from this frame, if one detached the
     * camera, and the field of view scale all mods asked for together.
     */
    std::optional<CameraRequest> cameraView() const;

    /**
     * The emotes the mods added and what they asked the player to play.
     */
    EmoteRegistry& emotes();
    float fovScale() const;

    /**
     * Tells the mods where the view was drawn from, for Camera::position.
     */
    void setView(const mod::Vec3& position, mod::Rotation rotation, float fieldOfView);

    /**
     * Everything the mods changed through Visuals, merged so the mod loaded
     * first wins wherever two set the same thing.
     */
    VisualRequest visuals() const;

    /**
     * The blocks the mods want drawn as air, when that changed since the
     * last call: the names they hide, or, when visibleOnly is set, every
     * block but these names.
     */
    std::optional<BlockFilter> takeHiddenBlocks();
    void observe(const SessionSnapshot& snapshot);
    void update(float deltaSeconds);

    /**
     * Tells the mods the player changed settings; changed holds a
     * mod::SettingsChange bit for each.
     */
    void settingsChanged(uint32_t changed, int fov, float guiScale, int renderDistance, const std::string& language);

    /**
     * Asks the mod with this id, or every mod when it is empty, to read its
     * configuration again.
     */
    void reloadConfigs(const std::string& modId);

    /**
     * Lets the mods save before the client quits; only the first call counts.
     */
    void shutdown();
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

    /**
     * Every library in the mods folder, running or not, for the Mods
     * settings page.
     */
    std::vector<menu::ModEntry> listedMods() const;

    /**
     * Queues what the player asked from the Mods settings page; it runs at
     * the start of the next update, outside any mod callback.
     */
    void request(menu::ModAction action);

    /**
     * What Tab can complete in a chat draft typed up to the caret that
     * starts with the mod command prefix.
     */
    menu::CommandHints completeCommand(std::string_view draft);

    void drawHud(ui::Context& context, float width, float height, bool screenOpen);

    /**
     * Collects the mods' world draws and draws them; viewProjection works on
     * positions relative to camera. client draws the client's own world
     * shapes through the same painter, before the mods.
     */
    void drawWorld(const std::array<float, 16>& viewProjection, const mod::Vec3& camera, const std::function<void(WorldCanvas&)>& client = {});

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
    /**
     * One library found in the folder: whether the player wants it running,
     * the slot running it (0 while it is not) and why it last failed.
     */
    struct Record {
        std::filesystem::path file;
        bool enabled = true;
        size_t owner = 0;
        mod::ModInfo info;
        std::string error;
        std::vector<std::string> dependencies;
    };

    /**
     * What was last seen of an entity, to report it when it goes away.
     */
    struct SeenActor {
        int64_t uniqueId = 0;
        std::string identifier;
        std::string name;
        mod::Vec3 position;
    };

    void scan();
    void loadRecord(Record& record);
    void startRecords(const std::vector<std::string>& files);
    std::unique_ptr<ModSlot> openRecord(Record& record);
    void startRecord(Record& record, std::unique_ptr<ModSlot> loaded);

    /**
     * Stops the mod and, first, every running mod that depends on it; returns
     * the files of those dependents so a reload can start them again.
     */
    std::vector<std::string> unloadRecord(Record& record);
    void stopFaulted();
    void openSettings(size_t owner);
    void drawSettings(size_t owner, mod::Canvas& canvas, mod::Controls& controls, const InputState& input, float width, float height);
    void commitSettings(size_t owner);
    void drawNotices(mod::Canvas& canvas, float width);
    void applyActions();
    void loadDisabled();
    void saveDisabled() const;
    Record* record(const std::string& file);
    ModSlot* slot(size_t owner) const;
    void reportError(size_t owner, std::string_view what);
    void registerBuiltins();
    void drainPackets();
    void trackScreen();
    void trackActors(const SessionSnapshot& snapshot);
    void forgetActors();
    void trackInventory(const SessionSnapshot& snapshot);
    void trackBlockChanges(const SessionSnapshot& snapshot);
    void trackChunks(const SessionSnapshot& snapshot);
    void forgetChunks();
    void trackPlayerTicks(const SessionSnapshot& snapshot);
    void trackBreakProgress(const SessionSnapshot& snapshot);
    void trackContainer(const SessionSnapshot& snapshot);
    void dispatchText(InputState& input);
    std::string modName(size_t owner) const;

    std::unique_ptr<HostState> host;
    std::unique_ptr<ChatService> hostChat;
    std::vector<std::unique_ptr<ModSlot>> slots;
    std::vector<Record> records;
    std::set<std::string> disabled;
    std::vector<menu::ModAction> pending;
    size_t nextOwner = 1;
    std::set<size_t> warned;
    // When each mod's recent errors happened, to turn off a mod that keeps failing.
    std::map<size_t, std::deque<double>> errorTimes;
    std::set<size_t> faulted;
    std::map<size_t, float> settingsScroll;
    std::string settingsListening;
    bool settingsUnsaved = false;
    double started = 0.0;
    double tickClock = 0.0;
    uint64_t ticks = 0;
    float hudWidth = 0.0f;
    float hudHeight = 0.0f;
    SessionState lastState = SessionState::Idle;
    float lastMouseX = -1.0f;
    float lastMouseY = -1.0f;
    uint64_t lastJoin = 0;
    bool lastDead = false;
    int lastDimension = 0;
    int lastScreen = 0;
    int lastContainerType = -1;
    bool shutDown = false;
    std::map<uint64_t, SeenActor> seenActors;
    std::array<HudItem, 36> seenInventory {};
    std::array<HudItem, 4> seenArmor {};
    HudItem seenOffhand;
    HudItem seenCursor;
    bool inventorySeen = false;
    uint64_t seenBlockChangeSerial = 0;
    uint64_t seenPlayerTickSerial = 0;
    uint64_t seenBreakProgressSerial = 0;
    uint64_t seenLoadedRevision = 0;
    int seenColumnsDimension = 0;
    std::vector<std::array<int32_t, 2>> seenColumns;
    uint64_t seenContainerOpen = 0;
    std::vector<HudItem> seenContainer;
};

}
