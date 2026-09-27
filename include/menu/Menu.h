#pragma once

#include "menu/ServerStore.h"
#include "platform/Keys.h"
#include "ui/Font.h"
#include "ui/Image.h"
#include "ui/Types.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel::ui {
class Context;
}

namespace kestrel::menu {

enum class Screen {
    Home,
    Servers,
    Worlds,
    Friends,
    Settings,
};

enum class ServerFilter {
    All,
    Favorites,
    Featured,
};

enum class Sheet {
    None,
    ServerEditor,
    ConfirmDelete,
    ConfirmExit,
    Pause,
    SignIn,
    Connecting,
    ConnectionError,
};

enum class Field {
    None,
    QuickAddress,
    EditName,
    EditAddress,
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
};

struct ChromeInfo {
    bool captionButtons = false;
    float insetLeft = 0.0f;
    bool maximized = false;
};

enum class ChromeAction {
    None,
    Minimize,
    Maximize,
    Close,
};

struct Area {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
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

    bool takeDisconnectRequest()
    {
        bool requested = disconnectRequested;
        disconnectRequested = false;
        return requested;
    }

    void setWorlds(std::vector<WorldEntry> entries)
    {
        worldEntries = std::move(entries);
    }

    void setInterfaceScale(float value)
    {
        scale = value;
    }

    void setAvatar(ui::ImageRef image)
    {
        avatar = image;
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
    struct Selection {
        bool featured = false;
        size_t index = 0;
    };

    struct Row {
        bool featured = false;
        size_t index = 0;
        std::string name;
        std::string address;
        std::string detail;
    };

    void background(ui::Context& ui, float width, float height);
    void header(ui::Context& ui, float width);
    float captionButtons(ui::Context& ui, float width);
    void badge(ui::Context& ui, const ui::Rect& rect, std::string_view name, ui::TextStyle style);
    void profileBadge(ui::Context& ui, const ui::Rect& rect, ui::TextStyle style);
    void home(ui::Context& ui, const Area& area);
    void servers(ui::Context& ui, const Area& area);
    void worlds(ui::Context& ui, const Area& area);
    void friends(ui::Context& ui, const Area& area);
    void settings(ui::Context& ui, const Area& area);

    void serverEditor(ui::Context& ui, float width, float height);
    void confirm(ui::Context& ui, float width, float height, std::string_view title, std::string_view body, std::string_view action);
    void pause(ui::Context& ui, float width, float height);
    void signInSheet(ui::Context& ui, float width, float height);
    void connectingSheet(ui::Context& ui, float width, float height);
    void connectionErrorSheet(ui::Context& ui, float width, float height);
    void gameView(ui::Context& ui, const Area& area);
    void beginSignIn();
    bool signedIn() const;
    bool inGame() const;
    void toast(ui::Context& ui, float width, float height);
    ui::Rect sheetFrame(ui::Context& ui, float width, float height, float sheetWidth, float sheetHeight);

    float tileGrid(ui::Context& ui, const std::vector<Row>& rows, float x, float y, float width, size_t limit);
    bool tile(ui::Context& ui, std::string_view id, const ui::Rect& rect, std::string_view title, std::string_view subtitle, std::string_view tag, bool secondaryTag, bool enabled);
    ui::Rect gridCell(size_t index, float x, float y, float width, float height) const;
    void heading(ui::Context& ui, std::string_view title, float x, float y);
    std::vector<Row> rowsFor(ServerFilter filter) const;
    std::vector<Row> recentRows(size_t limit) const;
    std::vector<Row> featuredRows() const;

    void handleKeys(ui::Context& ui);
    void type(std::u32string_view text);
    std::string* focusedText();
    void navigate(Screen target);
    void connect(const Row& row);
    void quickConnect();
    void openEditor(std::optional<size_t> index);
    void saveEditor();
    void confirmSheet();

    ServerStore& store;
    Screen screen = Screen::Home;
    ServerFilter filter = ServerFilter::All;
    Sheet sheet = Sheet::None;
    Field field = Field::None;
    std::optional<Selection> selection;
    std::optional<size_t> editing;
    size_t scrollRow = 0;
    float worldsScroll = 0.0f;
    std::string quickAddress;
    std::string editName;
    std::string editAddress;
    std::string displayName = "Steve";
    std::string toastMessage;
    std::chrono::steady_clock::time_point toastUntil;
    std::chrono::steady_clock::time_point lastRowClick;
    std::optional<Selection> lastRowClicked;
    std::optional<ConnectRequest> pending;
    ChromeInfo chrome;
    ChromeAction chromeAction = ChromeAction::None;
    AccountInfo account;
    AccountRequest accountRequest = AccountRequest::None;
    std::vector<WorldEntry> worldEntries;
    SessionInfo session;
    ui::ImageRef avatar;
    std::string cameraInfo;
    KeyBindings bindings;
    std::optional<size_t> rebinding;
    float settingsScroll = 0.0f;
    bool disconnectRequested = false;
    float scale = 1.0f;
    bool quit = false;
};

}
