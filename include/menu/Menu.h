#pragma once

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

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
    Subscriptions,
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
};

enum class Field {
    None,
    ServerName,
    ServerAddress,
    ServerPort,
    SocialSearch,
    DressingSearch,
    Chat,
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
    void addChatLine(std::string text);
    void clearChat();

    void setCommands(std::shared_ptr<const std::vector<ChatCommand>> list)
    {
        commands = std::move(list);
    }

    void setPlayers(std::vector<std::string> names)
    {
        players = std::move(names);
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
    };

    // Screens drawn with the classic textures.
    void safeFrame(ui::Context& ui, float width, float height);
    void panorama(ui::Context& ui);
    void title(ui::Context& ui, float width, float height);
    void titlePromo(ui::Context& ui, float left, float bottom);
    void pause(ui::Context& ui, float width, float height);
    void progressDialog(ui::Context& ui, float width, float height);
    void connectionError(ui::Context& ui, float width, float height);
    void messageDialog(ui::Context& ui, float width, float height, std::string_view heading, std::string_view body, std::string_view confirm, std::string_view cancel, bool& confirmed, bool& cancelled);
    void logo(ui::Context& ui, float centerX, float y, float maxWidth);
    void playerModel(ui::Context& ui, float centerX, float top, float pixel, bool inventoryPreview = false);
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
    void toast(ui::Context& ui, float width, float height);
    std::vector<HudChatLine> hudChat() const;
    void drawHudScreen(ui::Context& ui, float width, float height);
    void chatScreen(ui::Context& ui, float width, float height);

    bool textField(ui::Context& ui, std::string_view id, std::string_view placeholder, const std::string& value, const ui::Rect& rect, bool focused);
    bool toggle(ui::Context& ui, std::string_view id, const ui::Rect& rect, bool on);
    bool slider(ui::Context& ui, std::string_view id, const ui::Rect& rect, float& fraction);
    float scrollArea(ui::Context& ui, const ui::Rect& area, float& offset, float contentHeight);
    void settingsHeading(ui::Context& ui, float x, float& y, float width, std::string_view heading, std::string_view detail);
    void settingsRow(ui::Context& ui, float x, float& y, float width, std::string_view label, std::string_view detail, float controlHeight, float controlWidth = 66.0f);
    void todoRow(ui::Context& ui, float x, float& y, float width, std::string_view label);

    std::vector<ServerRow> featuredRows(ServerGroup group) const;
    std::vector<ServerRow> savedRows() const;
    std::optional<ServerRow> selectedRow() const;

    void handleKeys(ui::Context& ui);
    bool handleChatKeys(const InputState& input);
    void openChat(std::string draft);
    void closeChat();
    void submitChat();
    void recallChat(int step);
    void completeChat(bool backwards);
    void commandPanel(ui::Context& ui, float width, float bottom, float top);
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
    std::optional<size_t> rebinding;
    std::optional<std::string> rebindingMod;
    float listScroll = 0.0f;
    float detailScroll = 0.0f;
    float detailContent = 0.0f;
    float pageScroll = 0.0f;
    float sidebarScroll = 0.0f;
    float pageContent = 0.0f;
    bool disconnectRequested = false;
    bool respawnRequested = false;
    std::chrono::steady_clock::time_point respawnClicked {};
    std::optional<bool> packAnswer;
    std::deque<ChatLine> chatLines;
    uint64_t chatSerial = 0;
    std::string chatDraft;
    std::vector<std::string> chatHistory;
    std::optional<size_t> chatRecall;
    std::vector<std::string> chatOutgoing;
    float chatScroll = 0.0f;
    std::shared_ptr<const std::vector<ChatCommand>> commands;
    std::vector<std::string> players;
    std::vector<CommandSuggestion> chatCycle;
    size_t chatCycleIndex = 0;
    std::string chatCycleBase;
    std::string chatCycleDraft;
    float scale = 1.0f;
    int chunkDistance = DefaultRenderDistance;
    int fpsLimit = DefaultMaxFps;
    int fieldOfView = DefaultFov;
    bool hidePaperDoll = false;
    float safeZone = MaxSafeArea;
    std::string languageCode = "en_US";
    std::array<int, VolumeChannelCount> volumes { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };
    bool quit = false;
};

}
