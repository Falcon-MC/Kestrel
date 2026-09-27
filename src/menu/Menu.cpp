#include "menu/Menu.h"

#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/Theme.h"
#include "ui/Utf8.h"

#include <algorithm>
#include <span>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr size_t MaxFieldLength = 96;

struct Hue {
    Color top;
    Color bottom;
    Color ink;
};

constexpr Hue Hues[] = {
    { { 208, 180, 248, 255 }, { 150, 108, 214, 255 }, { 30, 18, 46, 255 } },
    { { 140, 230, 208, 255 }, { 70, 170, 150, 255 }, { 12, 38, 32, 255 } },
    { { 248, 170, 206, 255 }, { 206, 96, 150, 255 }, { 46, 14, 30, 255 } },
    { { 160, 190, 250, 255 }, { 92, 124, 214, 255 }, { 16, 24, 50, 255 } },
    { { 250, 204, 150, 255 }, { 214, 146, 82, 255 }, { 50, 30, 10, 255 } },
};

}

Menu::Menu(ServerStore& store)
    : store(store)
{
}

std::optional<ConnectRequest> Menu::takeConnectRequest()
{
    std::optional<ConnectRequest> request = std::move(pending);
    pending.reset();
    return request;
}

void Menu::notify(std::string message)
{
    toastMessage = std::move(message);
    toastUntil = std::chrono::steady_clock::now() + std::chrono::seconds(4);
}

void Menu::setAccount(AccountInfo info)
{
    AccountStatus previous = account.status;
    account = std::move(info);
    bool knownAccount = signedIn() || account.status == AccountStatus::Connecting;
    displayName = knownAccount && !account.gamertag.empty() ? account.gamertag : "Steve";

    if (previous != AccountStatus::SignedIn && account.status == AccountStatus::SignedIn) {
        if (sheet == Sheet::SignIn) {
            sheet = Sheet::None;
            notify("Signed in as " + displayName);
        }
    }
}

bool Menu::signedIn() const
{
    return account.status == AccountStatus::SignedIn;
}

void Menu::beginSignIn()
{
    accountRequest = AccountRequest::SignIn;
    sheet = Sheet::SignIn;
    field = Field::None;
}

bool Menu::inGame() const
{
    return session.status == SessionStatus::Joined;
}

bool Menu::worldVisible() const
{
    return inGame();
}

bool Menu::capturesMouse() const
{
    return inGame() && screen == Screen::Home && sheet == Sheet::None;
}

void Menu::setSession(SessionInfo info)
{
    SessionStatus previous = session.status;
    session = std::move(info);
    if (previous == session.status) {
        return;
    }

    switch (session.status) {
    case SessionStatus::Resolving:
    case SessionStatus::Connecting:
        sheet = Sheet::Connecting;
        field = Field::None;
        break;
    case SessionStatus::Joined:
        if (sheet == Sheet::Connecting) {
            sheet = Sheet::None;
        }
        notify("Joined " + session.name);
        break;
    case SessionStatus::Failed:
    case SessionStatus::Disconnected:
        sheet = Sheet::ConnectionError;
        break;
    case SessionStatus::Idle:
        if (sheet == Sheet::Connecting || sheet == Sheet::Pause) {
            sheet = Sheet::None;
        }
        if (previous == SessionStatus::Joined) {
            notify("Disconnected");
        }
        break;
    }
}

void Menu::frame(Context& ui, float width, float height)
{
    if (ui.input().mousePressed) {
        field = Field::None;
        rebinding.reset();
    }

    ui.setBlocked(sheet != Sheet::None || capturesMouse());
    if (!worldVisible()) {
        background(ui, width, height);
    } else if (screen != Screen::Home) {
        ui.fill({ 0.0f, 0.0f, width, height }, { 12, 10, 18, 215 });
    }

    float contentWidth = std::min(width - PadXl * 2.0f, ContentMaxWidth);
    Area area {
        (width - contentWidth) * 0.5f,
        HeaderHeight + PadXl,
        contentWidth,
        height - HeaderHeight - PadXl * 2.0f,
    };

    ui.setExcluded({ 0.0f, 0.0f, width, HeaderHeight });
    switch (inGame() && screen != Screen::Settings ? Screen::Home : screen) {
    case Screen::Home:
        if (inGame()) {
            gameView(ui, area);
        } else {
            home(ui, area);
        }
        break;
    case Screen::Servers:
        servers(ui, area);
        break;
    case Screen::Worlds:
        worlds(ui, area);
        break;
    case Screen::Friends:
        friends(ui, area);
        break;
    case Screen::Settings:
        settings(ui, area);
        break;
    }

    ui.clearExcluded();
    bool headerOnTop = sheet == Sheet::Pause;
    if (!headerOnTop) {
        header(ui, width);
    }

    ui.setBlocked(false);
    if (headerOnTop) {
        ui.setExcluded({ 0.0f, 0.0f, width, HeaderHeight });
    }
    switch (sheet) {
    case Sheet::None:
        break;
    case Sheet::ServerEditor:
        serverEditor(ui, width, height);
        break;
    case Sheet::ConfirmDelete: {
        std::string name = selection && !selection->featured && selection->index < store.servers().size()
            ? store.servers()[selection->index].name
            : std::string("this server");
        confirm(ui, width, height, "Delete server?", "\"" + name + "\" will be removed from your list.", "Delete");
        break;
    }
    case Sheet::ConfirmExit:
        confirm(ui, width, height, "Quit Kestrel?", "Your servers and settings are saved on this device.", "Quit");
        break;
    case Sheet::Pause:
        pause(ui, width, height);
        break;
    case Sheet::SignIn:
        signInSheet(ui, width, height);
        break;
    case Sheet::Connecting:
        connectingSheet(ui, width, height);
        break;
    case Sheet::ConnectionError:
        connectionErrorSheet(ui, width, height);
        break;
    }

    if (headerOnTop) {
        ui.clearExcluded();
        Screen before = screen;
        header(ui, width);
        if (screen != before && sheet == Sheet::Pause) {
            sheet = Sheet::None;
        }
    }

    toast(ui, width, height);
    handleKeys(ui);
}

void Menu::background(Context& ui, float width, float height)
{
    ui.gradient({ 0.0f, 0.0f, width, height }, CanvasTop, Canvas);
    ui.glow(width * 0.18f, HeaderHeight + 40.0f, 760.0f, { 150, 100, 230, 34 });
    ui.glow(width * 0.92f, height * 0.9f, 680.0f, { 90, 200, 180, 18 });
}

void Menu::badge(Context& ui, const Rect& rect, std::string_view name, TextStyle style)
{
    uint32_t hash = 2166136261u;
    for (char c : name) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    const Hue& hue = Hues[hash % std::size(Hues)];
    float radius = rect.w * 0.28f;
    ui.gradient(rect, hue.top, hue.bottom, radius);
    ui.fill({ rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h * 0.45f }, { 255, 255, 255, 22 }, radius - 1.0f);

    std::string initial;
    if (!name.empty()) {
        char first = name.front();
        initial.push_back(first >= 'a' && first <= 'z' ? static_cast<char>(first - 32) : first);
    }
    ui.textCentered(initial, style, rect, hue.ink);
}

void Menu::profileBadge(Context& ui, const Rect& rect, TextStyle style)
{
    if (avatar.valid && displayName == account.gamertag) {
        ui.image(rect, avatar, rect.w * 0.28f);
        return;
    }
    badge(ui, rect, displayName, style);
}

float Menu::captionButtons(Context& ui, float width)
{
    if (!chrome.captionButtons) {
        return width;
    }

    constexpr float buttonWidth = 48.0f;
    constexpr float buttonHeight = 36.0f;
    float x = width - buttonWidth * 3.0f;

    Rect minimize { x, 0.0f, buttonWidth, buttonHeight };
    Interaction minimizeState = ui.interact("chrome:minimize", minimize);
    if (minimizeState.hovered) {
        ui.fill(minimize, GhostHover);
    }
    ui.fill({ minimize.x + 19.0f, minimize.y + 18.0f, 10.0f, 1.0f }, minimizeState.hovered ? Text : Muted);

    Rect maximize { x + buttonWidth, 0.0f, buttonWidth, buttonHeight };
    Interaction maximizeState = ui.interact("chrome:maximize", maximize);
    if (maximizeState.hovered) {
        ui.fill(maximize, GhostHover);
    }
    Color maximizeInk = maximizeState.hovered ? Text : Muted;
    if (chrome.maximized) {
        ui.outline({ maximize.x + 21.0f, maximize.y + 11.0f, 9.0f, 9.0f }, maximizeInk);
        ui.fill({ maximize.x + 18.0f, maximize.y + 14.0f, 9.0f, 9.0f }, Canvas);
        ui.outline({ maximize.x + 18.0f, maximize.y + 14.0f, 9.0f, 9.0f }, maximizeInk);
    } else {
        ui.outline({ maximize.x + 19.0f, maximize.y + 13.0f, 10.0f, 10.0f }, maximizeInk);
    }

    Rect close { x + buttonWidth * 2.0f, 0.0f, buttonWidth, buttonHeight };
    Interaction closeState = ui.interact("chrome:close", close);
    if (closeState.hovered) {
        ui.fill(close, CloseHover);
    }
    ui.textCentered("\xC3\x97", TextStyle::Heading, { close.x, close.y - 1.0f, close.w, close.h }, closeState.hovered ? Text : Muted);

    if (minimizeState.clicked) {
        chromeAction = ChromeAction::Minimize;
    }
    if (maximizeState.clicked) {
        chromeAction = ChromeAction::Maximize;
    }
    if (closeState.clicked) {
        chromeAction = ChromeAction::Close;
    }
    return x;
}

void Menu::header(Context& ui, float width)
{
    ui.fill({ 0.0f, 0.0f, width, HeaderHeight }, Header);
    ui.fill({ 0.0f, HeaderHeight - 1.0f, width, 1.0f }, Line);

    float right = captionButtons(ui, width) - Pad;
    float left = PadLg + chrome.insetLeft;

    Rect logo { left, (HeaderHeight - 34.0f) * 0.5f, 34.0f, 34.0f };
    ui.shadow({ logo.x, logo.y + 4.0f, logo.w, logo.h }, 11.0f, 16.0f, AccentGlow);
    ui.gradient(logo, AccentHover, AccentDeep, 11.0f);
    ui.fill({ logo.x + 9.0f, logo.y + 10.0f, 16.0f, 4.0f }, OnAccent, 2.0f);
    ui.fill({ logo.x + 9.0f, logo.y + 16.0f, 11.0f, 4.0f }, OnAccent, 2.0f);
    ui.fill({ logo.x + 9.0f, logo.y + 22.0f, 6.0f, 4.0f }, OnAccent, 2.0f);
    ui.text("Kestrel", TextStyle::Heading, logo.right() + 12.0f, (HeaderHeight - ui.lineHeight(TextStyle::Heading)) * 0.5f, Text);

    struct Tab {
        Screen target;
        const char* label;
    };
    constexpr Tab tabs[] = {
        { Screen::Home, "Home" },
        { Screen::Servers, "Servers" },
        { Screen::Worlds, "Worlds" },
        { Screen::Friends, "Friends" },
    };

    constexpr Tab gameTabs[] = {
        { Screen::Home, "Game" },
    };

    float x = logo.right() + 12.0f + ui.measure("Kestrel", TextStyle::Heading) + PadXl;
    for (const Tab& item : inGame() ? std::span<const Tab>(gameTabs) : std::span<const Tab>(tabs)) {
        float tabWidth = ui.measure(item.label, TextStyle::Label) + 32.0f;
        if (ui.tab(std::string("tab:") + item.label, item.label, { x, (HeaderHeight - 36.0f) * 0.5f, tabWidth, 36.0f }, screen == item.target)) {
            navigate(item.target);
        }
        x += tabWidth + 4.0f;
    }

    float chipWidth = ui.measure(displayName, TextStyle::Label) + 64.0f;
    Rect chip { right - chipWidth, (HeaderHeight - 42.0f) * 0.5f, chipWidth, 42.0f };
    Interaction account = ui.interact("header:account", chip);
    ui.card(chip, account.hovered ? Hover : SurfaceAlt, account.hovered ? LineStrong : Line, 21.0f);
    Rect avatar { chip.x + 5.0f, chip.y + 5.0f, 32.0f, 32.0f };
    profileBadge(ui, avatar, TextStyle::Label);
    ui.fill({ avatar.right() - 8.0f, avatar.bottom() - 8.0f, 10.0f, 10.0f }, Header, 5.0f);
    ui.fill({ avatar.right() - 6.0f, avatar.bottom() - 6.0f, 6.0f, 6.0f }, signedIn() ? Secondary : Subtle, 3.0f);
    ui.text(displayName, TextStyle::Label, avatar.right() + 10.0f, chip.y + (chip.h - ui.lineHeight(TextStyle::Label)) * 0.5f, Text);
    if (account.clicked) {
        navigate(Screen::Settings);
    }
}

Rect Menu::sheetFrame(Context& ui, float width, float height, float sheetWidth, float sheetHeight)
{
    ui.fill({ 0.0f, 0.0f, width, height }, Scrim);
    float w = std::min(sheetWidth, width - PadXl);
    Rect frame { (width - w) * 0.5f, (height - sheetHeight) * 0.5f, w, sheetHeight };
    ui.shadow({ frame.x, frame.y + 16.0f, frame.w, frame.h }, 20.0f, 48.0f, Shadow);
    ui.fill(frame, LineStrong, 20.0f);
    ui.gradient(frame.inset(1.0f), SurfaceTop, Surface, 19.0f);
    return frame;
}

void Menu::serverEditor(Context& ui, float width, float height)
{
    Rect frame = sheetFrame(ui, width, height, 480.0f, 330.0f);
    float x = frame.x + PadXl;
    float w = frame.w - PadXl * 2.0f;
    ui.text(editing ? "Edit server" : "Add a server", TextStyle::Heading, x, frame.y + PadXl, Text);
    ui.text("Saved on this device.", TextStyle::Body, x, frame.y + PadXl + 32.0f, Muted);

    float y = frame.y + 104.0f;
    ui.text("NAME", TextStyle::Caption, x, y, Subtle);
    if (ui.field("editor:name", "My server", editName, { x, y + 20.0f, w, 44.0f }, field == Field::EditName)) {
        field = Field::EditName;
    }
    y += 78.0f;
    ui.text("ADDRESS", TextStyle::Caption, x, y, Subtle);
    if (ui.field("editor:address", "play.example.net:19132", editAddress, { x, y + 20.0f, w, 44.0f }, field == Field::EditAddress)) {
        field = Field::EditAddress;
    }

    float buttonsY = frame.bottom() - PadLg - ControlHeight;
    if (ui.button("editor:save", "Save", { frame.right() - PadXl - 112.0f, buttonsY, 112.0f, ControlHeight }, ButtonKind::Primary)) {
        saveEditor();
    }
    if (ui.button("editor:cancel", "Cancel", { frame.right() - PadXl - 112.0f - Gap - 100.0f, buttonsY, 100.0f, ControlHeight }, ButtonKind::Ghost)) {
        sheet = Sheet::None;
        field = Field::None;
    }
}

void Menu::confirm(Context& ui, float width, float height, std::string_view title, std::string_view body, std::string_view action)
{
    Rect frame = sheetFrame(ui, width, height, 440.0f, 200.0f);
    float x = frame.x + PadXl;
    float w = frame.w - PadXl * 2.0f;
    ui.text(title, TextStyle::Heading, x, frame.y + PadXl, Text, w);
    ui.paragraph(body, TextStyle::Body, x, frame.y + PadXl + 38.0f, w, Muted);

    float buttonsY = frame.bottom() - PadLg - ControlHeight;
    if (ui.button("confirm:ok", action, { frame.right() - PadXl - 112.0f, buttonsY, 112.0f, ControlHeight }, ButtonKind::Danger)) {
        confirmSheet();
    }
    if (ui.button("confirm:cancel", "Cancel", { frame.right() - PadXl - 112.0f - Gap - 100.0f, buttonsY, 100.0f, ControlHeight }, ButtonKind::Ghost)) {
        sheet = Sheet::None;
    }
}

void Menu::pause(Context& ui, float width, float height)
{
    Rect frame = sheetFrame(ui, width, height, 380.0f, 272.0f);
    float x = frame.x + PadXl;
    float w = frame.w - PadXl * 2.0f;
    ui.text("Paused", TextStyle::Heading, x, frame.y + PadXl, Text);

    float y = frame.y + 90.0f;
    if (ui.button("pause:resume", "Back to game", { x, y, w, ControlHeight }, ButtonKind::Primary)) {
        sheet = Sheet::None;
        navigate(Screen::Home);
    }
    y += ControlHeight + Gap;
    if (ui.button("pause:settings", "Settings", { x, y, w, ControlHeight })) {
        sheet = Sheet::None;
        navigate(Screen::Settings);
    }
    y += ControlHeight + Gap;
    if (ui.button("pause:disconnect", "Disconnect", { x, y, w, ControlHeight }, ButtonKind::Ghost)) {
        sheet = Sheet::None;
        disconnectRequested = true;
    }
}

void Menu::signInSheet(Context& ui, float width, float height)
{
    Rect frame = sheetFrame(ui, width, height, 500.0f, 380.0f);
    float x = frame.x + PadXl;
    float w = frame.w - PadXl * 2.0f;

    Rect logo { x, frame.y + PadXl, 40.0f, 40.0f };
    float cell = 9.0f;
    ui.fill({ logo.x + 10.0f, logo.y + 10.0f, cell, cell }, { 242, 80, 34, 255 }, 1.0f);
    ui.fill({ logo.x + 21.0f, logo.y + 10.0f, cell, cell }, { 127, 186, 0, 255 }, 1.0f);
    ui.fill({ logo.x + 10.0f, logo.y + 21.0f, cell, cell }, { 0, 164, 239, 255 }, 1.0f);
    ui.fill({ logo.x + 21.0f, logo.y + 21.0f, cell, cell }, { 255, 185, 0, 255 }, 1.0f);
    ui.text("Sign in with Microsoft", TextStyle::Heading, logo.right() + 14.0f, logo.y + (logo.h - ui.lineHeight(TextStyle::Heading)) * 0.5f, Text, w - 54.0f);

    float y = logo.bottom() + PadLg;
    int dots = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 400 % 4);
    std::string ellipsis(static_cast<size_t>(dots), '.');

    float buttonsY = frame.bottom() - PadLg - ControlHeight;
    bool closeSheet = false;

    switch (account.status) {
    case AccountStatus::SignedOut:
    case AccountStatus::Connecting:
    case AccountStatus::SignedIn:
        ui.paragraph("Contacting Microsoft to get a sign-in code" + ellipsis, TextStyle::Body, x, y, w, Muted);
        break;
    case AccountStatus::AwaitingCode: {
        ui.paragraph("Open the page below on any device, then enter this code to link your Xbox account.", TextStyle::Body, x, y, w, Muted);
        Rect codeBox { x, y + 52.0f, w, 84.0f };
        ui.card(codeBox, theme::Field, AccentLine, 14.0f);
        ui.textCentered(account.userCode, TextStyle::Display, codeBox, Accent);

        Rect link { x, codeBox.bottom() + 12.0f, w, 24.0f };
        Interaction linkState = ui.interact("signin:link", link);
        ui.textCentered(account.verificationUri, TextStyle::Label, link, linkState.hovered ? AccentHover : Secondary);
        if (linkState.clicked) {
            platform::openUrl(account.verificationUri);
        }

        ui.text("Waiting for you to finish" + ellipsis, TextStyle::Caption, x, buttonsY - 30.0f, Subtle);
        if (ui.button("signin:open", "Open page", { frame.right() - PadXl - 132.0f, buttonsY, 132.0f, ControlHeight }, ButtonKind::Primary)) {
            platform::copyText(account.userCode);
            platform::openUrl(account.verificationUri);
            notify("Code copied, paste it on the page");
        }
        if (ui.button("signin:copy", "Copy code", { frame.right() - PadXl - 132.0f - Gap - 120.0f, buttonsY, 120.0f, ControlHeight })) {
            if (platform::copyText(account.userCode)) {
                notify("Code copied");
            }
        }
        break;
    }
    case AccountStatus::Failed:
        ui.text("Sign-in failed", TextStyle::Label, x, y, Danger, w);
        ui.paragraph(account.error, TextStyle::Caption, x, y + 28.0f, w, Muted);
        if (ui.button("signin:retry", "Try again", { frame.right() - PadXl - 120.0f, buttonsY, 120.0f, ControlHeight }, ButtonKind::Primary)) {
            beginSignIn();
        }
        break;
    }

    if (ui.button("signin:cancel", account.status == AccountStatus::Failed ? "Close" : "Cancel", { x - 8.0f, buttonsY, 100.0f, ControlHeight }, ButtonKind::Ghost)) {
        closeSheet = true;
    }
    if (closeSheet) {
        if (account.status != AccountStatus::Failed) {
            accountRequest = AccountRequest::Cancel;
        }
        sheet = Sheet::None;
    }
}

void Menu::connectingSheet(Context& ui, float width, float height)
{
    Rect frame = sheetFrame(ui, width, height, 460.0f, 250.0f);
    float x = frame.x + PadXl;
    float w = frame.w - PadXl * 2.0f;

    ui.text(session.status == SessionStatus::Resolving ? "Finding the Realm" : "Joining server", TextStyle::Heading, x, frame.y + PadXl, Text, w);
    ui.text(session.name, TextStyle::Label, x, frame.y + PadXl + 36.0f, Accent, w);
    ui.paragraph(session.status == SessionStatus::Resolving
            ? "Asking Realms for the address. A sleeping Realm can take a moment to start."
            : "Signing the login, negotiating encryption and resource packs, then waiting for the world.",
        TextStyle::Body, x, frame.y + PadXl + 64.0f, w, Muted);

    Rect track { x, frame.bottom() - PadLg - ControlHeight - 26.0f, w, 6.0f };
    ui.fill(track, theme::Field, 3.0f);
    float phase = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() % 1400) / 1400.0f;
    float barWidth = w * 0.3f;
    float barX = track.x + (w + barWidth) * phase - barWidth;
    float left = std::max(barX, track.x);
    float right = std::min(barX + barWidth, track.right());
    if (right > left) {
        ui.gradient({ left, track.y, right - left, track.h }, AccentHover, AccentDeep, 3.0f);
    }

    float buttonsY = frame.bottom() - PadLg - ControlHeight;
    if (ui.button("connecting:cancel", "Cancel", { frame.right() - PadXl - 110.0f, buttonsY, 110.0f, ControlHeight }, ButtonKind::Ghost)) {
        disconnectRequested = true;
        sheet = Sheet::None;
    }
}

void Menu::connectionErrorSheet(Context& ui, float width, float height)
{
    Rect frame = sheetFrame(ui, width, height, 480.0f, 260.0f);
    float x = frame.x + PadXl;
    float w = frame.w - PadXl * 2.0f;
    bool lost = session.status == SessionStatus::Disconnected;

    ui.text(lost ? "Disconnected" : "Couldn't connect", TextStyle::Heading, x, frame.y + PadXl, Text, w);
    ui.text(session.name, TextStyle::Label, x, frame.y + PadXl + 36.0f, Danger, w);
    ui.paragraph(session.error.empty() ? "The connection was closed." : session.error, TextStyle::Body, x, frame.y + PadXl + 64.0f, w, Muted);

    float buttonsY = frame.bottom() - PadLg - ControlHeight;
    if (ui.button("error:back", "Back", { frame.right() - PadXl - 110.0f, buttonsY, 110.0f, ControlHeight }, ButtonKind::Primary)) {
        sheet = Sheet::None;
    }
}

void Menu::gameView(Context& ui, const Area& area)
{
    std::vector<std::string> lines {
        session.levelName.empty() ? session.name : session.levelName,
        cameraInfo,
        "Looking at " + (session.targetBlock.empty() ? std::string("nothing") : session.targetBlock),
        "Chunks " + std::to_string(session.columns) + "  \xC2\xB7  sub-chunks " + std::to_string(session.subChunks) + "  \xC2\xB7  pending " + std::to_string(session.pendingSubChunks),
        "Meshes " + std::to_string(session.meshes) + "  \xC2\xB7  quads " + std::to_string(session.meshQuads) + "  \xC2\xB7  jobs " + std::to_string(session.meshJobs),
        "Textures " + std::to_string(session.textureLayers) + "  \xC2\xB7  decode errors " + std::to_string(session.worldErrors),
        session.registryInfo,
    };
    if (!session.assetsError.empty()) {
        lines.push_back(session.assetsError);
    }
    if (!session.lastWorldError.empty()) {
        lines.push_back(session.lastWorldError);
    }

    float lineHeight = ui.lineHeight(TextStyle::Caption) + 4.0f;
    Rect panel { Pad, HeaderHeight + Pad, 520.0f, 20.0f + lineHeight * static_cast<float>(lines.size()) };
    ui.fill(panel, { 10, 8, 14, 150 }, 12.0f);
    for (size_t i = 0; i < lines.size(); ++i) {
        Color color = i == 0 ? Text : i >= 7 ? Danger : Muted;
        ui.text(lines[i], i == 0 ? TextStyle::Label : TextStyle::Caption, panel.x + 12.0f, panel.y + 10.0f + lineHeight * static_cast<float>(i), color, panel.w - 24.0f);
    }

    float centerX = area.x + area.w * 0.5f;
    float centerY = (HeaderHeight + area.y + area.h + PadXl) * 0.5f;
    ui.fill({ centerX - 8.0f, centerY - 1.0f, 16.0f, 2.0f }, { 255, 255, 255, 200 });
    ui.fill({ centerX - 1.0f, centerY - 8.0f, 2.0f, 16.0f }, { 255, 255, 255, 200 });
}

void Menu::toast(Context& ui, float width, float height)
{
    if (toastMessage.empty() || std::chrono::steady_clock::now() > toastUntil) {
        return;
    }
    float w = std::min(ui.measure(toastMessage, TextStyle::Label) + 64.0f, width - PadXl);
    Rect frame { (width - w) * 0.5f, height - PadXl - 48.0f, w, 48.0f };
    ui.shadow({ frame.x, frame.y + 10.0f, frame.w, frame.h }, 24.0f, 30.0f, Shadow);
    ui.card(frame, Raised, LineStrong, 24.0f);
    ui.fill({ frame.x + 20.0f, frame.y + 20.0f, 8.0f, 8.0f }, Secondary, 4.0f);
    ui.text(toastMessage, TextStyle::Label, frame.x + 40.0f, frame.y + (frame.h - ui.lineHeight(TextStyle::Label)) * 0.5f, Text, frame.w - 56.0f);
}

void Menu::handleKeys(Context& ui)
{
    const InputState& input = ui.input();

    if (rebinding) {
        if (input.pressedKey == Key::Escape) {
            rebinding.reset();
        } else if (input.pressedKey != Key::None) {
            bindings.keys[*rebinding] = input.pressedKey;
            rebinding.reset();
        }
        return;
    }

    if (!input.text.empty()) {
        type(input.text);
    }
    if (input.backspace) {
        if (std::string* target = focusedText()) {
            popUtf8(*target);
        }
    }
    if (input.tab && sheet == Sheet::ServerEditor) {
        field = field == Field::EditName ? Field::EditAddress : Field::EditName;
    }
    if (input.enter) {
        if (sheet == Sheet::ServerEditor) {
            saveEditor();
        } else if (sheet == Sheet::ConfirmDelete || sheet == Sheet::ConfirmExit) {
            confirmSheet();
        } else if (field == Field::QuickAddress) {
            quickConnect();
        } else if (screen == Screen::Servers && selection) {
            for (const Row& row : rowsFor(filter)) {
                if (row.featured == selection->featured && row.index == selection->index) {
                    connect(row);
                    break;
                }
            }
        }
    }
    if (input.escape) {
        if (sheet == Sheet::Pause) {
            sheet = Sheet::None;
        } else if (sheet == Sheet::SignIn) {
            accountRequest = AccountRequest::Cancel;
            sheet = Sheet::None;
        } else if (sheet == Sheet::Connecting) {
            disconnectRequested = true;
            sheet = Sheet::None;
        } else if (sheet != Sheet::None) {
            sheet = Sheet::None;
            field = Field::None;
        } else if (field != Field::None) {
            field = Field::None;
        } else if (inGame() && screen != Screen::Home) {
            navigate(Screen::Home);
        } else if (inGame()) {
            sheet = Sheet::Pause;
        } else if (screen != Screen::Home) {
            navigate(Screen::Home);
        } else {
            sheet = Sheet::ConfirmExit;
        }
    }
}

void Menu::type(std::u32string_view text)
{
    std::string* target = focusedText();
    if (!target) {
        return;
    }
    for (char32_t cp : text) {
        if (target->size() >= MaxFieldLength) {
            break;
        }
        appendUtf8(*target, cp);
    }
}

std::string* Menu::focusedText()
{
    switch (field) {
    case Field::QuickAddress:
        return &quickAddress;
    case Field::EditName:
        return &editName;
    case Field::EditAddress:
        return &editAddress;
    case Field::None:
        break;
    }
    return nullptr;
}

void Menu::navigate(Screen target)
{
    screen = target;
    field = Field::None;
    scrollRow = 0;
    worldsScroll = 0.0f;
    settingsScroll = 0.0f;
    rebinding.reset();
}

void Menu::connect(const Row& row)
{
    if (!row.featured) {
        store.markJoined(row.index);
    }
    pending = ConnectRequest { row.name, row.address };
}

void Menu::quickConnect()
{
    std::string address = normalizeAddress(quickAddress);
    if (address.empty()) {
        notify("Enter a server address first");
        field = Field::QuickAddress;
        return;
    }

    for (size_t i = 0; i < store.servers().size(); ++i) {
        if (store.servers()[i].address == address) {
            connect({ false, i, store.servers()[i].name, address, {} });
            quickAddress.clear();
            return;
        }
    }
    size_t index = store.add(address, address);
    connect({ false, index, address, address, {} });
    quickAddress.clear();
}

void Menu::openEditor(std::optional<size_t> index)
{
    editing = index;
    if (index && *index < store.servers().size()) {
        editName = store.servers()[*index].name;
        editAddress = store.servers()[*index].address;
    } else {
        editing.reset();
        editName.clear();
        editAddress.clear();
    }
    sheet = Sheet::ServerEditor;
    field = Field::EditName;
}

void Menu::saveEditor()
{
    if (normalizeAddress(editAddress).empty()) {
        notify("The address can't be empty");
        field = Field::EditAddress;
        return;
    }
    if (editing) {
        store.update(*editing, editName, editAddress);
        selection = Selection { false, *editing };
    } else {
        size_t index = store.add(editName, editAddress);
        selection = Selection { false, index };
        if (filter == ServerFilter::Featured) {
            filter = ServerFilter::All;
        }
    }
    sheet = Sheet::None;
    field = Field::None;
}

void Menu::confirmSheet()
{
    if (sheet == Sheet::ConfirmExit) {
        quit = true;
    } else if (sheet == Sheet::ConfirmDelete && selection && !selection->featured) {
        store.remove(selection->index);
        selection.reset();
        notify("Server deleted");
    }
    sheet = Sheet::None;
}

}
