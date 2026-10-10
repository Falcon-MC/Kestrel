#pragma once
#include "world/GlobalResources.h"

#include "client/SocialModel.h"
#include "menu/ChatCommands.h"
#include "menu/FormScreen.h"
#include "menu/Hud.h"
#include "menu/InventoryScreen.h"
#include "menu/ServerStore.h"
#include "menu/ToastQueue.h"
#include "platform/Keys.h"
#include "ui/Font.h"
#include "ui/Image.h"
#include "ui/Types.h"

#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kestrel::ui {
class Context;
}

namespace kestrel::menu {

inline constexpr int MinRenderDistance = 2;
inline constexpr int MaxRenderDistance = 32;
inline constexpr int DefaultRenderDistance = 16;
inline constexpr int MinMaxFps = 30;
inline constexpr int MaxMaxFps = 240;
inline constexpr int MaxFpsStep = 10;
inline constexpr int UnlimitedFps = 0;
inline constexpr int DefaultMaxFps = 120;
inline constexpr int MinFov = 30;
inline constexpr int MaxFov = 110;
inline constexpr int DefaultFov = 90;
inline constexpr float MinSafeArea = 0.9f;
inline constexpr float MaxSafeArea = 1.0f;
inline constexpr size_t VolumeChannelCount = 10;
inline constexpr int MinBrightness = 0;
inline constexpr int MaxBrightness = 100;
inline constexpr int DefaultBrightness = 0;
inline constexpr float MaxBrightnessLift = 0.5f;
inline constexpr int MinChatFontSize = 6;
inline constexpr int MaxChatFontSize = 12;
inline constexpr int DefaultChatFontSize = 8;
inline constexpr float MinChatLineSpacing = 1.0f;
inline constexpr float MaxChatLineSpacing = 2.0f;
inline constexpr float ChatLineSpacingStep = 0.1f;
inline constexpr int ChatColorCount = 7;
inline constexpr int DefaultChatColor = 0;
inline constexpr int DefaultMentionsColor = 2;

/**
 * Where a chat line came from, so the chat settings can mute it: the server
 * or a command, a player talking, or a player's emote.
 */
enum class ChatSource {
    System,
    Player,
    Emote,
};

/**
 * What the chat settings screen sets: muting, text to speech, the chat font
 * (Noto Sans when smooth) with its size and line spacing, and the colors of
 * chat lines and of lines that mention the player, as indexes into the
 * screen's color list.
 */
struct ChatSettings {
    bool muteAll = false;
    bool muteEmotes = false;
    bool textToSpeech = false;
    bool smoothFont = false;
    int fontSize = DefaultChatFontSize;
    float lineSpacing = MinChatLineSpacing;
    int chatColor = DefaultChatColor;
    int mentionsColor = DefaultMentionsColor;
    bool operator==(const ChatSettings&) const = default;
};

/**
 * How far the brightness setting lifts dark places toward full light, the way
 * night vision does: nothing at the lowest setting, MaxBrightnessLift at the
 * highest.
 */
inline float brightnessLift(int percent)
{
    int clamped = percent < MinBrightness ? MinBrightness : percent > MaxBrightness ? MaxBrightness : percent;
    return MaxBrightnessLift * static_cast<float>(clamped) / static_cast<float>(MaxBrightness);
}

/**
 * The skin sprite a person's profile picture is registered under.
 */
inline std::string socialAvatarSprite(const std::string& xuid)
{
    return "social/" + xuid;
}

enum class Screen {
    Title,
    Play,
    Settings,
    ServerForm,
    Marketplace,
    DressingRoom,
    Profile,
};

enum class PlayTab {
    Realms,
    Servers,
};

struct ModKeyBind {
    std::string id;
    std::string label;
    Key key = Key::None;
};

/**
 * One library in the mods folder as the Mods settings page shows it: what it
 * says it is, whether the player turned it on, whether it is running, why it
 * failed when it did, and its saved settings.
 */
struct ModEntry {
    std::string file;
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    bool enabled = true;
    bool loaded = false;
    std::string error;
    std::vector<std::pair<std::string, std::string>> config;
    // Whether the mod built a settings page with Ui::addSettings.
    bool hasSettings = false;
};

/**
 * What the player asked of the mods from the Mods settings page, carried out
 * by the client between frames.
 */
struct ModAction {
    enum class Kind {
        Enable,
        Disable,
        Reload,
        Remove,
        SetConfig,
        Rescan,
        OpenFolder,
        ReloadConfigs,
        OpenSettings,
    };

    Kind kind = Kind::Rescan;
    std::string file;
    std::string key;
    std::string value;
};

enum class SettingsPage {
    Accessibility,
    Keyboard,
    Controller,
    Touch,
    Party,
    General,
    Video,
    Audio,
    Account,
    Mods,
    GlobalResources,
    Storage,
    Language,
    Creator,
    Count,
};

enum class Dialog {
    None,
    ConfirmDelete,
    ConfirmExit,
    Pause,
    SignIn,
    Connecting,
    ConnectionError,
    Chat,
    ProfileOptions,
    Death,
    SafeArea,
    RealmInvites,
    JoinRealm,
    ConfirmRemoveFriend,
    Emotes,
};

enum class Field {
    None,
    ServerName,
    ServerAddress,
    ServerPort,
    SocialSearch,
    DressingSearch,
    Chat,
    RealmCode,
    ModConfig,
};

/**
 * What the People tab of the social drawer shows: the friends list, the
 * friend requests, or the players a search found.
 */
enum class SocialPage {
    Friends,
    Requests,
    Search,
};

/**
 * What a saved server answered to the last ping: still checking, reachable
 * with its message of the day, or unreachable.
 */
struct ServerStatus {
    bool checked = false;
    bool online = false;
    std::string motd;
    int players = 0;
    int latencyMs = -1;
};

struct FeaturedGameEntry {
    std::string title;
    std::string subtitle;
    std::string description;
    std::string image;
};

/**
 * A partner server from the game's discovery service, with the skin sprites
 * of its icon and of the showcase screenshots that finished downloading.
 */
struct FeaturedEntry {
    std::string id;
    std::string name;
    std::string creator;
    std::string description;
    std::string newsTitle;
    std::string news;
    std::string address;
    std::string icon;
    std::vector<std::string> showcase;
    size_t showcaseCount = 0;
    std::vector<FeaturedGameEntry> games;
};

struct ConnectRequest {
    std::string name;
    std::string address;
};

enum class AccountStatus {
    SignedOut,
    Connecting,
    AwaitingCode,
    SignedIn,
    Failed,
};

struct RealmEntry {
    int64_t id = 0;
    std::string name;
    std::string detail;
    bool open = false;
    bool expired = false;
};

/**
 * The text with its ASCII letters in capitals, the way menu headings print.
 */
inline std::string upperCase(std::string text)
{
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

struct ProfileAchievement {
    std::string name;
    std::string description;
    std::string icon;
    int gamerscore = 0;
    bool achieved = false;
};

/**
 * What the profile page shows: achievement and gamerscore totals, suggested
 * and recently earned achievements, and play statistics.
 */
/**
 * An emote the wheel can hold: its id, the name shown and an optional icon
 * texture.
 */
// Degrees per part, in the order head, body, right arm, left arm, right leg, left leg.
using ModelPose = std::array<std::array<float, 3>, 6>;

struct EmoteOption {
    std::string id;
    std::string name;
    std::string icon;
    // The emote halfway through, for the wheel's preview of the player.
    ModelPose pose {};
};

inline constexpr size_t EmoteSlotCount = 4;

/**
 * A piece of a dressing room page as the grid shows it: its offer, title,
 * rarity, creator, price, whether it is owned, and its thumbnail sprite.
 */
struct DressingPiece {
    std::string id;
    std::string title;
    std::string rarity;
    std::string creator;
    bool owned = false;
    std::string sprite;
    std::string packType;
    int coins = 0;
    int bonus = 0;
    std::string header;
    std::string coinText;
    std::string footer;
};

struct DressingPageView {
    bool loading = false;
    bool loaded = false;
    std::string error;
    std::vector<DressingPiece> owned;
    std::vector<DressingPiece> others;
    int balance = -1;
};

using DressingRoomView = std::map<std::string, DressingPageView>;

enum class DressingSection {
    Characters,
    Creator,
    ClassicSkins,
    Emotes,
    Capes,
    Category,
    Colors,
    Fullscreen,
    Coins,
};

enum class DressingDialog {
    None,
    DeleteCharacter,
    Differences,
    SkinModel,
};

/**
 * Where the dressing room is: the section shown and the ones before it, the
 * category page, the piece chosen and the one equipped, the chosen color,
 * the sidebar, the creator's open groups, the dialog and the grid scroll.
 */
struct DressingState {
    DressingSection section = DressingSection::Characters;
    std::vector<DressingSection> trail;
    std::string page;
    std::string pageTitle;
    std::string selected;
    std::string equipped;
    int color = -1;
    bool sidebarOpen = false;
    bool bodyOpen = true;
    bool styleOpen = false;
    DressingDialog dialog = DressingDialog::None;
    float scroll = 0.0f;
    std::string search;
};

struct ProfileInfo {
    bool achievementsLoaded = false;
    int achieved = 0;
    int total = 0;
    int gamerscore = 0;
    int totalGamerscore = 0;
    std::vector<ProfileAchievement> suggested;
    std::vector<ProfileAchievement> recent;
    bool statsLoaded = false;
    int64_t minutesPlayed = 0;
    int64_t blocksBroken = 0;
    int64_t mobsDefeated = 0;
    int64_t distanceTravelled = 0;
};

struct AccountInfo {
    AccountStatus status = AccountStatus::SignedOut;
    ProfileInfo profile;
    std::string verificationUri;
    std::string userCode;
    std::string gamertag;
    std::string error;
    std::vector<RealmEntry> realms;
    bool realmsLoading = false;
    std::string realmsError;
};

enum class AccountRequest {
    None,
    SignIn,
    Cancel,
    SignOut,
};

enum class SessionStatus {
    Idle,
    Resolving,
    Connecting,
    Joined,
    Disconnected,
    Failed,
};

/**
 * The F3 screen laid out like Java Edition's: lines pinned to the top left
 * and lines pinned to the top right. Empty strings leave a gap.
 */
struct DebugView {
    std::vector<std::string> left;
    std::vector<std::string> right;
};

struct SessionInfo {
    SessionStatus status = SessionStatus::Idle;
    bool loadingTerrain = false;
    bool dead = false;
    std::string deathMessage;
    bool changingDimension = false;
    std::string name;
    std::string displayName;
    std::string levelName;
    std::string gameMode;
    std::string position;

    /**
     * The block the player stands in and the one they look at, for the
     * chat's copy coordinates controls.
     */
    std::optional<std::array<int, 3>> playerBlock;
    std::optional<std::array<int, 3>> facingBlock;
    int dimension = 0;
    int chunkRadius = 0;
    uint64_t packetsReceived = 0;
    size_t columns = 0;
    size_t subChunks = 0;
    size_t pendingSubChunks = 0;
    uint64_t blockUpdates = 0;
    uint64_t worldErrors = 0;
    std::string lastWorldError;
    size_t meshes = 0;
    size_t meshQuads = 0;
    size_t meshJobs = 0;
    size_t textureLayers = 0;
    size_t materials = 0;
    size_t diagnosticVisuals = 0;
    std::string assetsError;
    std::string error;
    std::string packetError;
    bool packPrompt = false;
    size_t packCount = 0;
    bool packSkippable = true;
    uint64_t packBytes = 0;
    bool packDownloading = false;
    uint64_t packReceived = 0;
    uint64_t packTotal = 0;
    bool packsResolved = false;
};

struct ChromeInfo {
    bool captionButtons = false;
    float insetLeft = 0.0f;
    bool maximized = false;
    bool fullscreen = false;
};

enum class ChromeAction {
    None,
    Minimize,
    Maximize,
    Fullscreen,
    Close,
};

/**
 * The promotion the game's treatment packs put on the start screen: an
 * animated badge cut from a strip of frames, a caption on a flyout and a
 * button that opens its page.
 */
struct TitlePromo {
    bool loaded = false;
    bool valid = false;
    uint32_t frames = 1;
    float fps = 1.0f;
    float frameWidth = 0.0f;
    float frameHeight = 0.0f;
    std::string caption;
};

class Menu {
public:
    explicit Menu(ServerStore& store);

    InventoryScreen& inventoryPanel() { return inventory; }
    FormScreen& formPanel() { return forms; }

    /**
     * Shows a server form, or answers that the player is busy when another
     * screen is open, the way the game does.
     */
    void openForm(uint32_t id, const std::string& json);
    void prepareInventoryInput(const InputState& input);
    void prepareInput(const InputState& input);
    void setInventory(const InventoryState& state, bool creative);

    void frame(ui::Context& ui, float width, float height);

    /**
     * The part of a width by height window the interface keeps to.
     */
    ui::Rect safeRect(float width, float height) const;

    bool quitRequested() const
    {
        return quit;
    }

    float interfaceScale() const
    {
        return scale;
    }

    int renderDistance() const
    {
        return chunkDistance;
    }

    int maxFps() const
    {
        return fpsLimit;
    }

    int fov() const
    {
        return fieldOfView;
    }

    void setFov(int degrees)
    {
        fieldOfView = degrees;
    }

    bool paperDollHidden() const
    {
        return hidePaperDoll;
    }

    void setPaperDollHidden(bool hidden)
    {
        hidePaperDoll = hidden;
    }

    bool vsync() const
    {
        return verticalSync;
    }

    void setVsync(bool enabled)
    {
        verticalSync = enabled;
    }

    /**
     * Whether sprinting, speed and the like may widen or narrow the view.
     */
    bool gameplayFov() const
    {
        return fovAlteredByGameplay;
    }

    void setGameplayFov(bool enabled)
    {
        fovAlteredByGameplay = enabled;
    }

    /**
     * The share of the screen the HUD keeps to, between MinSafeArea and
     * MaxSafeArea, the way the game stores gfx_safe_zone_all.
     */
    float safeArea() const
    {
        return safeZone;
    }

    void setSafeArea(float value)
    {
        safeZone = value;
    }

    /**
     * The brightness setting in percent, between MinBrightness and
     * MaxBrightness.
     */
    int brightness() const
    {
        return brightnessPercent;
    }

    void setBrightness(int percent)
    {
        brightnessPercent = percent < MinBrightness ? MinBrightness : percent > MaxBrightness ? MaxBrightness : percent;
    }

    /**
     * The values of the options the game's settings screen shows that no
     * other setting holds, by option name, kept between launches.
     */
    const std::map<std::string, int>& extraOptions() const
    {
        return extraOptionValues;
    }

    void setExtraOption(const std::string& name, int value)
    {
        extraOptionValues[name] = value;
    }

    /**
     * An option of the game's settings screen by name, or fallback while it
     * was never set.
     */
    int option(std::string_view name, int fallback) const
    {
        return optionValue(name, fallback);
    }

    /**
     * The name played under without a Microsoft account, kept between launches.
     */
    const std::string& offlineName() const
    {
        return offlineNameValue;
    }

    void setOfflineName(std::string name)
    {
        if (name.empty()) {
            return;
        }
        offlineNameValue = std::move(name);
        if (account.status != AccountStatus::SignedIn && account.status != AccountStatus::Connecting) {
            displayName = offlineNameValue;
        }
    }

    /**
     * The accessibility glint strength and speed in percent, 0 to 100.
     */
    int glintStrength() const
    {
        return glintStrengthPercent;
    }

    void setGlintStrength(int percent)
    {
        glintStrengthPercent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    }

    int glintSpeed() const
    {
        return glintSpeedPercent;
    }

    void setGlintSpeed(int percent)
    {
        glintSpeedPercent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    }

    void setSocial(SocialSnapshot snapshot);

    /**
     * What the social screens asked of the online services since the last
     * call, in order.
     */
    std::vector<SocialRequest> takeSocialRequests()
    {
        std::vector<SocialRequest> requests = std::move(socialRequests);
        socialRequests.clear();
        return requests;
    }

    bool takeRealmsRefreshRequest()
    {
        bool requested = realmsRefreshRequested;
        realmsRefreshRequested = false;
        return requested;
    }

    /**
     * Volume percentages: the main volume first, then music, ambient,
     * weather, blocks, hostile, friendly, players, records and interface.
     */
    const std::array<int, VolumeChannelCount>& soundVolumes() const
    {
        return volumes;
    }

    void setSoundVolume(size_t channel, int percent)
    {
        if (channel < volumes.size()) {
            volumes[channel] = percent;
        }
    }

    const std::string& language() const
    {
        return languageCode;
    }

    void setLanguage(std::string code)
    {
        languageCode = std::move(code);
    }

    const std::string& playerName() const
    {
        return displayName;
    }

    void setChrome(ChromeInfo info)
    {
        chrome = info;
    }

    ChromeAction takeChromeAction()
    {
        ChromeAction action = chromeAction;
        chromeAction = ChromeAction::None;
        return action;
    }

    void setAccount(AccountInfo info);
    void setDressingRoom(DressingRoomView view);

    /**
     * The dressing room pages the screen wants loaded since the last call.
     */
    std::vector<std::string> takeDressingRequests();
    void setSession(SessionInfo info);

    bool debugVisible() const
    {
        return debugShown;
    }

    bool hudHidden() const
    {
        return hudToggledOff;
    }

    void setDebugView(DebugView view)
    {
        debugView = std::move(view);
    }

    void setServerStatus(std::map<std::string, ServerStatus> status)
    {
        serverStatus = std::move(status);
    }

    std::optional<std::string> focusedFeatured() const;

    void setFeatured(std::vector<FeaturedEntry> entries, bool loading)
    {
        featured = std::move(entries);
        featuredLoading = loading;
    }

    /**
     * The id of the partner server open in the detail pane, whose showcase
     * is worth downloading.
     */

    /**
     * The game's JSON UI, merged with the server's packs, that the HUD and
     * server forms are drawn from.
     */
    void setJsonUi(std::shared_ptr<const ui::JsonUi> definitions);

    void setHud(HudView view)
    {
        hud = std::move(view);
    }

    const KeyBindings& keyBindings() const
    {
        return bindings;
    }

    void setKeyBindings(const KeyBindings& value)
    {
        bindings = value;
    }

    void setModKeyBinds(std::vector<ModKeyBind> binds)
    {
        modBinds = std::move(binds);
    }

    void setModKeyBindHandler(std::function<void(const std::string&, Key)> handler)
    {
        onModKeyBind = std::move(handler);
    }

    void setMods(std::vector<ModEntry> entries)
    {
        modEntries = std::move(entries);
    }

    /**
     * Where Tab and the list above the chat box get completions for a draft
     * starting with the mod command prefix.
     */
    void setModCompletions(std::function<CommandHints(std::string_view)> complete)
    {
        modCompletions = std::move(complete);
    }

    void setGlobalResources(std::vector<world::GlobalPackEntry> entries) { globalPacks = std::move(entries); globalPackScreens.clear(); }
    void setGlobalResourceStatus(bool busy, std::string status) { globalPacksBusy = busy; globalPacksStatus = std::move(status); }
    std::vector<world::GlobalPackAction> takeGlobalPackActions() { return std::exchange(globalPackActions, {}); }

    std::vector<ModAction> takeModActions()
    {
        return std::exchange(modActions, {});
    }

    bool worldVisible() const;
    bool capturesMouse() const;

    /**
     * Opens the pause menu when the player was playing: not in chat, a dialog
     * or a menu screen, and not paused already.
     */
    void pauseIfPlaying();
    float captionHeight() const;

    bool takeRespawnRequest()
    {
        bool requested = respawnRequested;
        respawnRequested = false;
        return requested;
    }

    /**
     * The emotes the wheel offers, and the ids the player put in its slots;
     * an empty or unknown slot shows the next emote not already placed.
     */
    void setEmotes(std::vector<EmoteOption> options);
    void setEmoteSlots(const std::array<std::string, EmoteSlotCount>& slots);
    const std::array<std::string, EmoteSlotCount>& emoteSlots() const
    {
        return emoteSlotIds;
    }

    /**
     * The emote the player picked on the wheel since the last call.
     */
    std::optional<std::string> takeEmoteRequest()
    {
        return std::exchange(emoteRequest, std::nullopt);
    }

    bool takeEmoteSlotsChanged()
    {
        return std::exchange(emoteSlotsChanged, false);
    }

    bool takeDisconnectRequest()
    {
        bool requested = disconnectRequested;
        disconnectRequested = false;
        return requested;
    }

    std::optional<bool> takePackAnswer()
    {
        std::optional<bool> answer = packAnswer;
        packAnswer.reset();
        return answer;
    }

    void setInterfaceScale(float value)
    {
        scale = value;
    }

    void setRenderDistance(int chunks)
    {
        chunkDistance = chunks;
    }

    void setMaxFps(int limit)
    {
        fpsLimit = limit;
    }

    AccountRequest takeAccountRequest()
    {
        AccountRequest request = accountRequest;
        accountRequest = AccountRequest::None;
        return request;
    }

    std::optional<ConnectRequest> takeConnectRequest();
    void notify(std::string message);

    Screen currentScreen() const
    {
        return screen;
    }

    Dialog currentDialog() const
    {
        return dialog;
    }

    SettingsPage currentSettingsPage() const
    {
        return settingsSection;
    }

    PlayTab currentPlayTab() const
    {
        return playTab;
    }

    bool socialDrawerOpen() const
    {
        return socialOpen;
    }

    /**
     * Goes straight to a screen the way its button would, for automation.
     */
    void openScreen(Screen target);

    void openSettingsPage(SettingsPage page)
    {
        settingsSection = page;
        pageScroll = 0.0f;
    }

    void openPlayTab(PlayTab tab)
    {
        playTab = tab;
        listScroll = 0.0f;
    }

    /**
     * Shows or dismisses a dialog for automation. Only the ones a player can
     * open on purpose are allowed; false for the rest.
     */
    bool showDialog(Dialog which);

    /**
     * Starts joining a server as if its entry had been clicked.
     */
    void connectTo(std::string name, std::string address)
    {
        pending = ConnectRequest { std::move(name), std::move(address) };
    }

    /**
     * Opens the inventory the way its key does, when the player is in game.
     */
    bool openInventory();

    /**
     * Opens the add server form, or the edit form of a saved server.
     */
    void editServer(std::optional<size_t> index)
    {
        openServerForm(index);
    }

    /**
     * The chat log, oldest line first.
     */
    std::vector<std::string> chatLog() const;

    bool inventoryOpen() const
    {
        return inventory.active;
    }

    /**
     * Queues a toast from the server, shown over every screen once the ones
     * before it are gone.
     */
    void pushToast(std::string title, std::string content)
    {
        toasts.push(std::move(title), std::move(content));
    }

    /**
     * A received line for the chat log; the HUD shows it for a while and the
     * chat screen keeps the last hundred.
     */
    void addChatLine(std::string text, ChatSource source = ChatSource::System);
    void clearChat();

    const ChatSettings& chatSettings() const
    {
        return chatOptions;
    }

    /**
     * Replaces the chat settings, clamped to what the settings screen offers.
     */
    void setChatSettings(const ChatSettings& value);

    void setCommands(std::shared_ptr<const std::vector<ChatCommand>> list)
    {
        commands = std::move(list);
    }

    void setPlayers(std::vector<std::string> names)
    {
        players = std::move(names);
    }

    /**
     * Whether the server lets the player run operator commands, which puts the
     * slash button on the chat screen.
     */
    void setOperatorCommands(bool allowed)
    {
        operatorCommands = allowed;
    }

    /**
     * Lines sent from the chat screen since the last call, commands included.
     */
    std::vector<std::string> takeChatMessages()
    {
        std::vector<std::string> messages = std::move(chatOutgoing);
        chatOutgoing.clear();
        return messages;
    }

private:
    InventoryScreen inventory;
    FormScreen forms;
    ToastQueue toasts;
    bool inventoryInputHandled = false;
    bool inputPrepared = false;
    bool inputBlocked = false;
    enum class ServerGroup {
        Featured,
        Creator,
        Saved,
    };

    struct ServerRow {
        ServerGroup group = ServerGroup::Saved;
        size_t index = 0;
        std::string name;
        std::string address;
        std::string detail;
        std::string icon;
    };

    struct Selection {
        ServerGroup group = ServerGroup::Saved;
        size_t index = 0;
    };

    struct ChatLine {
        std::string text;
        std::chrono::steady_clock::time_point arrived;
        uint64_t serial = 0;
        ChatSource source = ChatSource::System;
    };

    /**
     * One row of the list above the chat box: what it shows and, for a
     * completion, the draft that clicking it leaves.
     */
    struct ChatRow {
        std::string text;
        std::optional<std::string> pick;
        std::optional<size_t> caret;
    };

    // Screens drawn with the classic textures.
    void safeFrame(ui::Context& ui, float width, float height);
    void panorama(ui::Context& ui);
    void title(ui::Context& ui, float width, float height);
    void titlePromo(ui::Context& ui, float left, float bottom);
    void pause(ui::Context& ui, float width, float height);
    bool pauseScreen(ui::Context& ui, float width, float height);
    void emoteWheel(ui::Context& ui, float width, float height);
    std::array<const EmoteOption*, EmoteSlotCount> filledEmoteSlots() const;
    void pickEmoteSlot(size_t slot);
    void beginEmoteEquip();
    void openEmoteEquip(const std::string& id);
    void addModEmotes();
    ui::UiData pauseData() const;
    void pauseRenderer(ui::Context& ui, const std::string& renderer, const ui::Rect& rect, float alpha, const ui::UiLookup& lookup);
    void pauseButton(const std::string& id);
    void classicPause(ui::Context& ui, float width, float height);
    void progressDialog(ui::Context& ui, float width, float height);
    void connectionError(ui::Context& ui, float width, float height);
    void messageDialog(ui::Context& ui, float width, float height, std::string_view heading, std::string_view body, std::string_view confirm, std::string_view cancel, bool& confirmed, bool& cancelled);
    void logo(ui::Context& ui, float centerX, float y, float maxWidth);
    void playerModel(ui::Context& ui, float centerX, float top, float pixel, bool inventoryPreview = false, float rotation = 0.0f);
    void screenContent(ui::Context& ui, float width, float height, Screen which);
    void dialogContent(ui::Context& ui, float width, float height, Dialog which, bool& confirmed, bool& cancelled);
    void inventoryLayer(ui::Context& ui, float width, float height, std::chrono::steady_clock::time_point now);
    void coverHud(bool covered, std::chrono::steady_clock::time_point now);
    void fadingHud(ui::Context& ui, float width, float height, std::chrono::steady_clock::time_point now);
    void gameView(ui::Context& ui, float width, float height);

    // Screens drawn the way the HTML menus draw them.
    float header(ui::Context& ui, float width, std::string_view heading, bool social);
    void play(ui::Context& ui, float width, float height);
    void realmsTab(ui::Context& ui, const ui::Rect& area);
    void serversTab(ui::Context& ui, const ui::Rect& area);
    void featuredDetail(ui::Context& ui, const ui::Rect& area, const FeaturedEntry& entry);
    void serverForm(ui::Context& ui, float width, float height);
    void settings(ui::Context& ui, float width, float height);
    void settingsPage(ui::Context& ui, const ui::Rect& area);
    void profile(ui::Context& ui, float width, float height);
    void dressingRoom(ui::Context& ui, float width, float height);
    void dressingHeader(ui::Context& ui, float width, std::string_view title, bool search);
    void dressingSidebar(ui::Context& ui, float width, float height);
    void dressingCharacters(ui::Context& ui, float left, float width, float height);
    void dressingCreator(ui::Context& ui, const ui::Rect& panel);
    void dressingPieces(ui::Context& ui, const ui::Rect& panel, const std::string& pageId, std::string_view ownedTitle, std::string_view othersTitle, std::string_view noneLabel);
    void dressingColors(ui::Context& ui, const ui::Rect& panel);
    void dressingCoins(ui::Context& ui, float left, float width, float height);
    void dressingClassicSkins(ui::Context& ui, const ui::Rect& panel);
    void applySkinChoice(ui::Context& ui);
    std::string characterSprite(ui::Context& ui, const std::string& file);
    void equipCharacter(ui::Context& ui, size_t index);
    void dressingPreview(ui::Context& ui, const ui::Rect& area, bool colorable);
    void dressingDialog(ui::Context& ui, float width, float height);
    void openDressingSection(DressingSection section, const std::string& page = {}, const std::string& title = {});
    void profileCard(ui::Context& ui, const ui::Rect& card);
    void profileSummary(ui::Context& ui, const ui::Rect& area);
    void profileStats(ui::Context& ui, const ui::Rect& area);
    void profileOptions(ui::Context& ui, float width, float height);
    void deathScreen(ui::Context& ui, float width, float height);
    void dimensionScreen(ui::Context& ui);
    void todoScreen(ui::Context& ui, float width, float height, std::string_view heading);
    void safeAreaDialog(ui::Context& ui);
    void socialDrawer(ui::Context& ui, float width, float height);
    void socialFriends(ui::Context& ui, const ui::Rect& area);
    void socialRequestsPage(ui::Context& ui, const ui::Rect& area);
    void socialSearchPage(ui::Context& ui, const ui::Rect& area);
    float personRow(ui::Context& ui, const SocialPerson& person, const ui::Rect& row, bool expandable);
    bool listState(ui::Context& ui, const PeopleList& list, const ui::Rect& area, std::string_view emptyText, SocialAction retry);
    ui::Rect modalFrame(ui::Context& ui, float width, float height, float frameWidth, float frameHeight, std::string_view heading, bool& closed);
    void realmInvitesDialog(ui::Context& ui, float width, float height);
    void joinRealmDialog(ui::Context& ui, float width, float height);
    void openJoinRealm();
    void submitRealmCode();
    void requestSocial(SocialAction action, std::string target = {});
    std::string onlineErrorText(OnlineError error, int retryAfterSeconds) const;
    std::string realmCodeErrorText(OnlineError error) const;
    const SocialOperation* operation(const std::string& key) const;
    void toast(ui::Context& ui, float width, float height);
    std::vector<HudChatLine> hudChat() const;
    void drawHudScreen(ui::Context& ui, float width, float height);
    void chatScreen(ui::Context& ui, float width, float height);
    void chatSettingsScreen(ui::Context& ui, float width, float height);
    void openChatSettings();
    void closeChatSettings();
    bool chatLineShown(const ChatLine& line) const;
    std::string chatLineColor(const ChatLine& line) const;
    ChatStyle chatStyle() const;
    uint64_t chatItemSerial(const ChatLine& line) const;

    bool textField(ui::Context& ui, std::string_view id, std::string_view placeholder, const std::string& value, const ui::Rect& rect, bool focused);
    bool toggle(ui::Context& ui, std::string_view id, const ui::Rect& rect, bool on);
    bool slider(ui::Context& ui, std::string_view id, const ui::Rect& rect, float& fraction);
    float scrollArea(ui::Context& ui, const ui::Rect& area, float& offset, float contentHeight);
    void settingsHeading(ui::Context& ui, float x, float& y, float width, std::string_view heading, std::string_view detail);
    void modsPage(ui::Context& ui, float x, float& y, float width);
    void globalResourcesPage(ui::Context& ui, float x, float& y, float width);
    void settingsRow(ui::Context& ui, float x, float& y, float width, std::string_view label, std::string_view detail, float controlHeight, float controlWidth = 66.0f);

    std::vector<ServerRow> featuredRows(ServerGroup group) const;
    std::vector<ServerRow> savedRows() const;
    std::optional<ServerRow> selectedRow() const;

    void handleKeys(const InputState& input);
    bool handleChatKeys(const InputState& input);
    void openChat(std::string draft);
    void closeChat();
    void submitChat();
    void recallChat(int step);
    void completeChat(bool backwards);
    void eraseChat(bool word);
    std::vector<ChatRow> chatRows(size_t capacity) const;
    void type(std::u32string_view text);
    std::string* focusedText();
    void navigate(Screen target);
    void goBack();
    void connect(const ServerRow& row);
    void openServerForm(std::optional<size_t> index);
    bool saveServerForm(bool andPlay);
    void beginSignIn();
    bool signedIn() const;
    bool inGame() const;

    ServerStore& store;
    Screen screen = Screen::Title;
    Screen returnScreen = Screen::Title;
    PlayTab playTab = PlayTab::Servers;
    bool profileStatsTab = false;
    SettingsPage settingsSection = SettingsPage::Keyboard;
    Dialog dialog = Dialog::None;
    Field field = Field::None;
    bool socialOpen = false;
    bool socialArmed = false;
    bool socialParty = false;
    std::optional<Selection> selection;
    std::optional<size_t> editing;
    std::string editName;
    std::string editAddress;
    std::string editPort;
    std::string socialSearch;
    SocialSnapshot social;
    std::vector<SocialRequest> socialRequests;
    SocialPage socialPage = SocialPage::Friends;
    std::string socialSelected;
    float socialScroll = 0.0f;
    float socialContent = 0.0f;
    std::string removingXuid;
    std::string removingName;
    std::string realmCodeInput;
    std::string realmCodeProblem;
    float invitesScroll = 0.0f;
    float invitesContent = 0.0f;
    bool realmsRefreshRequested = false;
    std::map<std::string, std::pair<bool, std::string>> answeredInvites;
    std::map<std::string, SocialAction> personActions;
    std::map<std::string, ServerStatus> serverStatus;
    std::vector<FeaturedEntry> featured;
    bool featuredLoading = true;
    size_t showcaseIndex = 0;
    bool serverAddressShown = false;
    std::chrono::steady_clock::time_point showcaseShown = std::chrono::steady_clock::now();
    Field selectedField = Field::None;
    bool selectAllPending = false;
    std::string lastFieldClick;
    std::chrono::steady_clock::time_point lastFieldClickAt {};
    std::string displayName = "Steve";
    std::string offlineNameValue = "Steve";
    std::string toastMessage;
    std::chrono::steady_clock::time_point toastUntil;
    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
    std::string splashText;
    std::array<bool, 6> panoramaReady {};
    TitlePromo promo;
    std::chrono::steady_clock::time_point screenChanged {};
    std::chrono::steady_clock::time_point dialogChanged {};
    std::chrono::steady_clock::time_point socialChanged {};
    std::chrono::steady_clock::time_point inventoryChanged {};
    std::chrono::steady_clock::time_point hudChanged {};
    float screenDirection = 1.0f;
    std::optional<Screen> leavingScreen;
    Dialog leavingDialog = Dialog::None;
    bool inventoryShown = false;
    bool hudCovered = false;
    Screen gameReturnScreen = Screen::Play;
    Dialog shownDialog = Dialog::None;
    bool socialShown = false;
    std::optional<ConnectRequest> pending;
    ChromeInfo chrome;
    ChromeAction chromeAction = ChromeAction::None;
    AccountInfo account;
    DressingRoomView dressing;
    DressingState dressingState;
    std::string playerSkin = "textures/entity/steve";
    bool skinChoiceLoaded = false;
    std::string classicSelected;
    bool importedSlim = false;
    size_t carouselIndex = 0;
    bool carouselPlaced = false;
    std::vector<std::string> dressingRequests;
    AccountRequest accountRequest = AccountRequest::None;
    SessionInfo session;
    bool errorDetailsShown = false;
    float errorReasonScroll = 0.0f;
    float errorInfoScroll = 0.0f;
    std::string errorDiagnostics;
    DebugView debugView;
    bool debugShown = false;
    bool hudToggledOff = false;
    HudView hud;
    std::shared_ptr<const ui::JsonUi> jsonUi;
    std::unique_ptr<ui::JsonUiScreen> hudScreen;
    std::unique_ptr<ui::JsonUiScreen> safeZoneScreen;
    // The whole window in the coordinates of the safe area.
    ui::Rect screenBounds;
    uint64_t shownSubtitle = 0;
    KeyBindings bindings;
    std::vector<ModKeyBind> modBinds;
    std::function<void(const std::string&, Key)> onModKeyBind;
    std::vector<ModEntry> modEntries;
    std::function<CommandHints(std::string_view)> modCompletions;
    std::vector<ModAction> modActions;
    std::vector<world::GlobalPackEntry> globalPacks;
    std::map<std::string, std::unique_ptr<ui::JsonUiScreen>> globalPackScreens;
    std::vector<world::GlobalPackAction> globalPackActions;
    std::string globalPacksStatus, openedGlobalPack, removingGlobalPack;
    bool globalPacksBusy = false;
    bool showActiveGlobalPacks = true;
    std::unique_ptr<ui::JsonUiScreen> globalPacksUi;
    std::optional<std::pair<bool, size_t>> globalPackSelected;
    std::optional<std::pair<bool, size_t>> globalPackDetails;
    bool globalSelectedExpanded = true;
    bool globalAvailableExpanded = true;
    bool vanillaGlobalResourcesPage(ui::Context& ui, float x, float& y, float w);
    void bindGlobalResources(ui::UiData& data);
    bool globalResourcesEvent(const ui::UiEvent& event);
    std::string openedMod;
    std::string removingMod;
    std::string editModFile;
    std::string editModKey;
    std::string editModValue;
    std::optional<size_t> rebinding;
    std::optional<std::string> rebindingMod;
    float listScroll = 0.0f;
    float detailScroll = 0.0f;
    float detailContent = 0.0f;
    float pageScroll = 0.0f;
    float sidebarScroll = 0.0f;
    float pageContent = 0.0f;
    bool disconnectRequested = false;
    std::unique_ptr<ui::JsonUiScreen> emoteUi;
    std::vector<EmoteOption> emoteOptions;
    std::array<std::string, EmoteSlotCount> emoteSlotIds {};
    std::optional<std::string> emoteRequest;
    bool emoteSlotsChanged = false;
    std::string emoteEquipping;
    std::string emoteRoot;
    const ModelPose* modelPose = nullptr;
    float titleModelRotation = 0.0f;
    float titleModelDragX = 0.0f;
    bool titleModelDragging = false;
    std::string lastEmoteOffered;
    bool emoteEquipOnly = false;
    int emoteHovered = -1;
    bool respawnRequested = false;
    std::chrono::steady_clock::time_point respawnClicked {};
    std::optional<bool> packAnswer;
    std::deque<ChatLine> chatLines;
    uint64_t chatSerial = 0;
    std::string chatDraft;
    std::optional<size_t> chatCaret;
    std::vector<std::string> chatHistory;
    std::optional<size_t> chatRecall;
    std::vector<std::string> chatOutgoing;
    bool chatToBottom = false;
    std::unique_ptr<ui::JsonUiScreen> chatUi;
    std::unique_ptr<ui::JsonUiScreen> settingsUi;
    bool chatFacingCoordinates = false;
    int vanillaSettingsSection = 0;
    bool advancedGraphicsShown = false;
    std::map<std::string, int> extraOptionValues;
    bool vanillaSettings(ui::Context& ui, float width, float height);
    int optionValue(std::string_view name, int fallback) const;
    void setOptionValue(std::string_view name, int value);
    std::unique_ptr<ui::JsonUiScreen> chatSettingsUi;
    std::unique_ptr<ui::JsonUiScreen> pauseUi;
    // Whether the pause screen was open when last drawn, so opening it starts its entrance and
    // closing it plays its exit.
    bool pauseOpen = false;
    // Buttons pressed with Enter, carried out next frame so the key does not also open chat.
    std::vector<std::string> pausePressed;
    bool chatSettingsOpen = false;
    // The settings closed this frame, so the key that closed them is used up.
    bool chatSettingsClosed = false;
    ChatSettings chatOptions;
    uint64_t chatStyleRevision = 0;
    std::shared_ptr<const std::vector<ChatCommand>> commands;
    bool operatorCommands = false;
    std::vector<std::string> players;
    std::vector<CommandSuggestion> chatCycle;
    size_t chatCycleIndex = 0;
    std::string chatCycleBase;
    std::string chatCycleTail;
    size_t chatCycleCaret = 0;
    std::string chatCycleDraft;
    float scale = 1.0f;
    int chunkDistance = DefaultRenderDistance;
    int fpsLimit = DefaultMaxFps;
    int fieldOfView = DefaultFov;
    bool hidePaperDoll = false;
    bool verticalSync = false;
    bool fovAlteredByGameplay = true;
    float safeZone = MaxSafeArea;
    int brightnessPercent = DefaultBrightness;
    int glintStrengthPercent = 100;
    int glintSpeedPercent = 100;
    std::string languageCode = "en_US";
    std::array<int, VolumeChannelCount> volumes { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };
    bool quit = false;
};

}
