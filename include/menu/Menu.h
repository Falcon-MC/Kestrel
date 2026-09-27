#pragma once

#include "menu/Hud.h"
#include "menu/ServerStore.h"
#include "platform/Keys.h"
#include "ui/Font.h"
#include "ui/Image.h"
#include "ui/Types.h"

#include <chrono>
#include <map>
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
    Worlds,
    Realms,
    Servers,
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
};

enum class Field {
    None,
    ServerName,
    ServerAddress,
    ServerPort,
    SocialSearch,
};

/**
 * What a saved server answered to the last ping: still checking, reachable
 * with its message of the day and player counts, or unreachable.
 */
struct ServerStatus {
    bool checked = false;
    bool online = false;
    std::string motd;
    int players = 0;
    int maxPlayers = 0;
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

struct WorldEntry {
    std::string name;
    std::string detail;
};

struct AccountInfo {
    AccountStatus status = AccountStatus::SignedOut;
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

struct SessionInfo {
    SessionStatus status = SessionStatus::Idle;
    bool loadingTerrain = false;
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
    std::string registryInfo;
    std::string targetBlock;
    std::string error;
    bool packPrompt = false;
    size_t packCount = 0;
    uint64_t packBytes = 0;
    bool packDownloading = false;
    uint64_t packReceived = 0;
    uint64_t packTotal = 0;
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

class Menu {
public:
    explicit Menu(ServerStore& store);

    void frame(ui::Context& ui, float width, float height);

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
    void setSession(SessionInfo info);

    void setCameraInfo(std::string text)
    {
        cameraInfo = std::move(text);
    }

    void setServerStatus(std::map<std::string, ServerStatus> status)
    {
        serverStatus = std::move(status);
    }

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

    bool worldVisible() const;
    bool capturesMouse() const;
    float captionHeight() const;

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

    void setWorlds(std::vector<WorldEntry> entries)
    {
        worldEntries = std::move(entries);
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

private:
    struct ServerRow {
        bool featured = false;
        size_t index = 0;
        std::string name;
        std::string address;
        std::string detail;
    };

    struct Selection {
        bool featured = false;
        size_t index = 0;
    };

    // Screens drawn with the classic textures.
    void panorama(ui::Context& ui, float width, float height);
    void title(ui::Context& ui, float width, float height);
    void pause(ui::Context& ui, float width, float height);
    void progressDialog(ui::Context& ui, float width, float height);
    void messageDialog(ui::Context& ui, float width, float height, std::string_view heading, std::string_view body, std::string_view confirm, std::string_view cancel, bool& confirmed, bool& cancelled);
    void logo(ui::Context& ui, float centerX, float y, float maxWidth);
    void playerModel(ui::Context& ui, float centerX, float top, float pixel);
    void screenContent(ui::Context& ui, float width, float height);
    void gameView(ui::Context& ui, float width, float height);

    // Screens drawn the way the HTML menus draw them.
    float header(ui::Context& ui, float width, std::string_view heading, bool social);
    void play(ui::Context& ui, float width, float height);
    void worldsTab(ui::Context& ui, const ui::Rect& area);
    void realmsTab(ui::Context& ui, const ui::Rect& area);
    void serversTab(ui::Context& ui, const ui::Rect& area);
    void serverForm(ui::Context& ui, float width, float height);
    void settings(ui::Context& ui, float width, float height);
    void settingsPage(ui::Context& ui, const ui::Rect& area);
    void todoScreen(ui::Context& ui, float width, float height, std::string_view heading);
    void socialDrawer(ui::Context& ui, float width, float height);
    void toast(ui::Context& ui, float width, float height);

    bool textField(ui::Context& ui, std::string_view id, std::string_view placeholder, const std::string& value, const ui::Rect& rect, bool focused);
    bool toggle(ui::Context& ui, std::string_view id, const ui::Rect& rect, bool on);
    bool slider(ui::Context& ui, std::string_view id, const ui::Rect& rect, float& fraction);
    float scrollArea(ui::Context& ui, const ui::Rect& area, float& offset, float contentHeight);
    void settingsHeading(ui::Context& ui, float x, float& y, float width, std::string_view heading, std::string_view detail);
    void settingsRow(ui::Context& ui, float x, float& y, float width, std::string_view label, std::string_view detail, float controlHeight);
    void todoRow(ui::Context& ui, float x, float& y, float width, std::string_view label);

    std::vector<ServerRow> featuredRows() const;
    std::vector<ServerRow> savedRows() const;
    std::optional<ServerRow> selectedRow() const;

    void handleKeys(ui::Context& ui);
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
    PlayTab playTab = PlayTab::Worlds;
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
    Field selectedField = Field::None;
    bool selectAllPending = false;
    std::string lastFieldClick;
    std::chrono::steady_clock::time_point lastFieldClickAt {};
    std::string displayName = "Steve";
    std::string toastMessage;
    std::chrono::steady_clock::time_point toastUntil;
    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
    std::array<bool, 6> panoramaReady {};
    std::chrono::steady_clock::time_point screenChanged {};
    std::chrono::steady_clock::time_point dialogChanged {};
    std::chrono::steady_clock::time_point socialChanged {};
    float screenDirection = 1.0f;
    Screen gameReturnScreen = Screen::Play;
    Dialog shownDialog = Dialog::None;
    bool socialShown = false;
    std::optional<ConnectRequest> pending;
    ChromeInfo chrome;
    ChromeAction chromeAction = ChromeAction::None;
    AccountInfo account;
    AccountRequest accountRequest = AccountRequest::None;
    std::vector<WorldEntry> worldEntries;
    SessionInfo session;
    std::string cameraInfo;
    HudView hud;
    KeyBindings bindings;
    std::optional<size_t> rebinding;
    float listScroll = 0.0f;
    float detailScroll = 0.0f;
    float pageScroll = 0.0f;
    float sidebarScroll = 0.0f;
    float pageContent = 0.0f;
    bool disconnectRequested = false;
    std::optional<bool> packAnswer;
    float scale = 1.0f;
    int chunkDistance = DefaultRenderDistance;
    int fpsLimit = DefaultMaxFps;
    bool quit = false;
};

}
