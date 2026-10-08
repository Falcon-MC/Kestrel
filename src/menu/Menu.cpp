#include "menu/Menu.h"
#include "menu/TitleLayout.h"

#include "client/DebugLog.h"

#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"
#include "ui/Utf8.h"
#include "world/ItemInfo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr size_t MaxFieldLength = 96;
constexpr size_t MaxChatLength = 512;
constexpr float TitleButtonWidth = 148.0f;
constexpr float TitleButtonHeight = 30.0f;
constexpr float TitleButtonStep = 32.0f;
constexpr float CornerButtonHeight = 24.0f;
constexpr uint32_t PanoramaSize = 512;
// $transition_time_push, $transition_time_pop and $container_transition_time_push in _global_variables.json.
constexpr float ScreenTransitionSeconds = 0.4f;
// The wipe offsets in ui_common.json: a quarter of the screen sideways, containers half of it up.
constexpr float ScreenWipe = 0.25f;
constexpr float ContainerWipe = 0.5f;
constexpr Color InventoryDim { 0, 0, 0, 102 };
constexpr Color DialogInk { 0x4c, 0x4c, 0x4c, 255 };
constexpr Color Backing { 0, 0, 0, 150 };

// Draws in window coordinates while it lives, for what belongs to the whole
// window rather than the safe area, like backgrounds and the crosshair.
class WholeScreen {
public:
    WholeScreen(Context& ui, const Rect& screen)
        : ui(ui)
        , screen(screen)
    {
        ui.setOrigin(0.0f, 0.0f);
    }

    ~WholeScreen()
    {
        ui.setOrigin(-screen.x, -screen.y);
    }

    WholeScreen(const WholeScreen&) = delete;
    WholeScreen& operator=(const WholeScreen&) = delete;

private:
    Context& ui;
    Rect screen;
};

std::string megabytes(uint64_t bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

// White text on the translucent strip the title screen puts under its corner labels.
void backedLabel(Context& ui, std::string_view label, float x, float y)
{
    float width = ui.measure(label, TextStyle::Pixel);
    ui.fill({ x - 1.0f, y - 1.0f, width + 2.0f, 10.0f }, Backing);
    ui.text(label, TextStyle::Pixel, x, y, White);
}

// A classic button with a small picture in front of its label, like Profile or Social.
bool iconButton(Context& ui, std::string_view id, std::string_view label, std::string_view icon, const Rect& rect, float iconSize)
{
    Interaction state = ui.interact(id, rect);
    ui.fill(rect, { 19, 19, 19, 255 });
    ui.nineSlice(rect.inset(1.0f), state.pressed ? "ui/button_borderless_lightpressed" : state.hovered ? "ui/button_borderless_lighthover" : "ui/button_borderless_light");
    float x = rect.x + 3.0f;
    if (!icon.empty()) {
        ui.sprite({ x, rect.y + std::floor((rect.h - iconSize) * 0.5f), iconSize, iconSize }, icon);
        x += iconSize + 3.0f;
    }
    if (!label.empty()) {
        float width = ui.measure(label, TextStyle::Pixel);
        float room = rect.right() - 3.0f - x;
        ui.text(label, TextStyle::Pixel, x + std::floor((room - width) * 0.5f), rect.y + std::floor((rect.h - 8.0f) * 0.5f), state.hovered ? White : ButtonText, room);
    }
    return state.clicked;
}

// How a screen comes and goes. JSON UI screens run the ui_common.json screen_animations they
// list: the offset wipe with the fade, the fade alone, a fade out alone like progress_screen.json,
// only the wipe out when popped like store_data_driven_screen.json, or their own. OreUI routes
// take the RouteSlideTransition or RouteNoTransition routes.json gives them.
enum class Transition {
    Wipe,
    Fade,
    FadeOut,
    PopWipe,
    Own,
    Slide,
    None,
};

Transition transitionOf(Screen screen)
{
    switch (screen) {
    case Screen::Title:
        return Transition::Fade;
    case Screen::Play:
    case Screen::Settings:
    case Screen::ServerForm:
    case Screen::Profile:
        return Transition::Slide;
    case Screen::DressingRoom:
    case Screen::Marketplace:
        return Transition::PopWipe;
    default:
        return Transition::Wipe;
    }
}

Transition transitionOf(Dialog dialog)
{
    switch (dialog) {
    case Dialog::Pause:
    case Dialog::SafeArea:
        return Transition::Own;
    case Dialog::Connecting:
    case Dialog::SignIn:
    case Dialog::ConnectionError:
        return Transition::Slide;
    case Dialog::Death:
    case Dialog::Emotes:
    case Dialog::ProfileOptions:
    case Dialog::ConfirmDelete:
        return Transition::None;
    default:
        return Transition::Wipe;
    }
}

// The OreUI routes that take the place of the screen instead of opening over it.
bool replacesScreen(Dialog dialog)
{
    return dialog == Dialog::Connecting || dialog == Dialog::SignIn || dialog == Dialog::ConnectionError;
}

float outCubic(float t)
{
    float left = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - left * left * left;
}

// CSS "ease", cubic-bezier(0.25, 0.1, 0.25, 1), since the OreUI slide never names a timing function.
float cssEase(float t)
{
    auto bezier = [](float a, float b, float s) {
        float u = 1.0f - s;
        return 3.0f * u * u * s * a + 3.0f * u * s * s * b + s * s * s;
    };
    t = std::clamp(t, 0.0f, 1.0f);
    float low = 0.0f;
    float high = 1.0f;
    for (int step = 0; step < 16; ++step) {
        float mid = (low + high) * 0.5f;
        (bezier(0.25f, 0.25f, mid) < t ? low : high) = mid;
    }
    return bezier(0.1f, 1.0f, (low + high) * 0.5f);
}

float progressSince(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point since, float seconds)
{
    return std::clamp(std::chrono::duration<float>(now - since).count() / seconds, 0.0f, 1.0f);
}

// Whether kind draws anything on its way out, going forward when direction is 1 and back when -1.
bool playsExit(Transition kind, float direction)
{
    return kind != Transition::None && (kind != Transition::PopWipe || direction < 0.0f);
}

// Offsets and fades the layer the way kind comes in or goes out, progress being how far along it
// is and direction 1 going forward, -1 going back.
void transitionLayer(Context& ui, Transition kind, bool entering, float progress, float direction, float screenWidth)
{
    if (entering && kind == Transition::PopWipe) {
        ui.setLayer(0.0f, 0.0f, 1.0f);
        return;
    }
    float eased = kind == Transition::Slide ? cssEase(progress) : outCubic(progress);
    float reach = kind == Transition::Slide ? 1.0f : kind == Transition::Wipe || kind == Transition::PopWipe ? ScreenWipe : 0.0f;
    float slide = (entering ? 1.0f - eased : -eased) * reach * screenWidth * direction;
    bool fades = kind == Transition::Wipe || kind == Transition::PopWipe || kind == Transition::Fade || (kind == Transition::FadeOut && !entering);
    ui.setLayer(slide, 0.0f, !fades ? 1.0f : entering ? eased : 1.0f - eased);
}

// The server's packs, title art included, only count while we are on its way in or playing there.
bool showsServerArt(SessionStatus status)
{
    return status == SessionStatus::Resolving || status == SessionStatus::Connecting || status == SessionStatus::Joined;
}

}

Menu::Menu(ServerStore& store)
    : store(store)
{
    forms.renderer = [this](Context& ui, const std::string& renderer, const Rect& rect, float alpha, const ui::UiLookup&) {
        if (alpha <= 0.0f || rect.w <= 0.0f || rect.h <= 0.0f) {
            return;
        }
        if (renderer == "live_player_renderer" || renderer == "paper_doll_renderer") {
            constexpr float ModelPixels = 32.0f;
            float pixel = std::min(rect.h / ModelPixels, rect.w / (ModelPixels * 0.5f));
            playerModel(ui, rect.x + rect.w * 0.5f, rect.y + (rect.h - pixel * ModelPixels) * 0.5f, pixel);
        }
    };
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
    displayName = knownAccount && !account.gamertag.empty() ? account.gamertag : offlineNameValue;

    if (previous != AccountStatus::SignedIn && account.status == AccountStatus::SignedIn && dialog == Dialog::SignIn) {
        dialog = Dialog::None;
        notify("Signed in as " + displayName);
    }
}

bool Menu::signedIn() const
{
    return account.status == AccountStatus::SignedIn;
}

void Menu::beginSignIn()
{
    accountRequest = AccountRequest::SignIn;
    dialog = Dialog::SignIn;
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
    return inGame() && dialog == Dialog::None && screen == Screen::Title && !socialOpen && !inventory.active && !inventoryInputHandled && !forms.active();
}

void Menu::openForm(uint32_t id, const std::string& json)
{
    bool busy = !inGame() || session.dead || inventory.active || dialog != Dialog::None || socialOpen || screen != Screen::Title;
    if (busy) {
        forms.reject(id);
    } else {
        forms.open(id, json);
    }
}

void Menu::setInventory(const InventoryState& state, bool creative)
{
    if (state.openRevision != inventory.state.openRevision) inventory.open();
    if (state.closeRevision != inventory.state.closeRevision) inventory.active = false;
    inventory.state = state;
    inventory.creativeMode = creative;
}

void Menu::prepareInventoryInput(const InputState& input)
{
    inventoryInputHandled = false;
    if (!inGame() || session.dead || session.loadingTerrain) {
        if (inventory.active) inventory.close();
        return;
    }
    if (inventory.active) {
        inventoryInputHandled = inventory.handleKeys(input, bindings.inventory());
    } else if (capturesMouse() && input.pressedKey == bindings.inventory() && session.gameMode != "Spectator") {
        inventory.open();
        inventoryInputHandled = true;
        // Opening is the only command sent before the server identifies a window.
        inventory.requestOpen();
    } else if (capturesMouse() && input.pressedKey == bindings.emote()) {
        emoteUi.reset();
        dialog = Dialog::Emotes;
    }
}

void Menu::pauseIfPlaying()
{
    if (capturesMouse()) {
        dialog = Dialog::Pause;
    }
}

float Menu::captionHeight() const
{
    if (capturesMouse()) {
        return 0.0f;
    }
    // The header moves with the safe area, and the window reads the caption in window units.
    return screen == Screen::Title ? 0.0f : 48.0f - screenBounds.y;
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
        if (previous != SessionStatus::Joined && previous != SessionStatus::Resolving && previous != SessionStatus::Connecting && screen != Screen::Title) {
            gameReturnScreen = screen;
        }
        dialog = Dialog::Connecting;
        field = Field::None;
        socialOpen = false;
        break;
    case SessionStatus::Joined:
        if (dialog == Dialog::Connecting) {
            dialog = Dialog::None;
        }
        navigate(Screen::Title);
        // The progress screen hid the one we joined from, so it has nothing to play its way out of.
        leavingScreen.reset();
        break;
    case SessionStatus::Failed:
    case SessionStatus::Disconnected:
        errorDetailsShown = false;
        errorReasonScroll = errorInfoScroll = 0.0f;
        {
            auto timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm utc {};
#ifdef _WIN32
            gmtime_s(&utc, &timestamp);
#else
            gmtime_r(&timestamp, &utc);
#endif
            char date[32] {};
            std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &utc);
            errorDiagnostics = "Kestrel\nDate: " + std::string(date)
                + "\nTransport: RakNet\nServer: " + session.name
                + "\nWorldName: " + session.levelName
                + "\nPackets received: " + std::to_string(session.packetsReceived)
                + "\nDimension: " + std::to_string(session.dimension);
        }
        dialog = Dialog::ConnectionError;
        field = Field::None;
        socialOpen = false;
        if (screen == Screen::Title) {
            navigate(gameReturnScreen);
        }
        break;
    case SessionStatus::Idle:
        if (dialog == Dialog::Chat) {
            closeChat();
        }
        if (dialog == Dialog::Connecting || dialog == Dialog::Pause) {
            dialog = Dialog::None;
        }
        if (previous == SessionStatus::Joined) {
            navigate(gameReturnScreen);
        }
        break;
    }
}

void Menu::screenContent(Context& ui, float width, float height, Screen which)
{
    switch (which) {
    case Screen::Title:
        if (inGame()) {
            gameView(ui, width, height);
        } else {
            title(ui, width, height);
        }
        break;
    case Screen::Play:
        play(ui, width, height);
        break;
    case Screen::Settings:
        settings(ui, width, height);
        break;
    case Screen::ServerForm:
        serverForm(ui, width, height);
        break;
    case Screen::Marketplace:
        todoScreen(ui, width, height, tr("menu.store", "Marketplace"));
        break;
    case Screen::DressingRoom:
        dressingRoom(ui, width, height);
        break;
    case Screen::Profile:
        profile(ui, width, height);
        break;
    }
}

void Menu::frame(Context& ui, float width, float height)
{
    Rect safe = safeRect(width, height);
    screenBounds = { -safe.x, -safe.y, width, height };
    ui.setOrigin(safe.x, safe.y);
    safeFrame(ui, safe.w, safe.h);
    ui.setOrigin(0.0f, 0.0f);
}

void Menu::safeFrame(Context& ui, float width, float height)
{
    static bool timed = false;
    std::optional<StartupTimer> timer;
    if (!timed) {
        timed = true;
        timer.emplace();
    }
    auto mark = [&](const char* step) {
        if (timer) {
            timer->mark(std::string("menu: ") + step);
        }
    };
    if (!skinChoiceLoaded) {
        skinChoiceLoaded = true;
        applySkinChoice(ui);
    }
    mark("skin choice");
    if (ui.input().mousePressed) {
        field = Field::None;
        rebinding.reset();
        rebindingMod.reset();
    }
    if (inGame() && session.dead) {
        if (dialog == Dialog::None && screen == Screen::Title) {
            dialog = Dialog::Death;
        }
    } else if (dialog == Dialog::Death) {
        dialog = Dialog::None;
    }

    auto now = std::chrono::steady_clock::now();
    if (inventory.active != inventoryShown) {
        inventoryShown = inventory.active;
        inventoryChanged = now;
    }
    coverHud(inventory.active || forms.active() || dialog != Dialog::None || screen != Screen::Title, now);

    if (inventory.active) {
        ui.setBlocked(false);
        fadingHud(ui, width, height, now);
        inventoryLayer(ui, width, height, now);
        toasts.draw(ui, width, height);
        return;
    }
    if (!inGame() && forms.active()) {
        forms.closeAll();
    }
    if (forms.active()) {
        ui.setBlocked(false);
        fadingHud(ui, width, height, now);
        forms.draw(ui, width, height);
        toasts.draw(ui, width, height);
        return;
    }

    bool modal = dialog != Dialog::None || socialOpen;
    ui.setBlocked(modal || capturesMouse());

    mark("hud cover");
    if (!worldVisible()) {
        panorama(ui);
    }
    mark("panorama");

    if (dialog != shownDialog) {
        leavingDialog = shownDialog;
        shownDialog = dialog;
        dialogChanged = now;
        // The safe area screen replaces the one under it, which leaves as it comes in and comes back as it goes.
        if (dialog == Dialog::SafeArea || leavingDialog == Dialog::SafeArea) {
            leavingScreen = dialog == Dialog::SafeArea ? std::optional<Screen>(screen) : std::nullopt;
            screenChanged = now;
            screenDirection = dialog == Dialog::SafeArea ? 1.0f : -1.0f;
        }
        if (safeZoneScreen && dialog == Dialog::SafeArea) {
            safeZoneScreen->fire("screen.entrance_push");
        } else if (safeZoneScreen && leavingDialog == Dialog::SafeArea) {
            safeZoneScreen->fire("screen.exit_pop");
        }
    }
    if (socialOpen != socialShown) {
        socialShown = socialOpen;
        socialChanged = now;
    }
    // A screen on its way out takes no input and leaves the field and dialog of the one replacing it alone.
    auto leaving = [&](auto&& draw) {
        bool blocked = ui.isBlocked();
        Field keptField = field;
        Dialog keptDialog = dialog;
        ui.setBlocked(true);
        draw();
        ui.setBlocked(blocked);
        field = keptField;
        dialog = keptDialog;
    };

    bool loading = !inGame() && (dialog == Dialog::Connecting || dialog == Dialog::ConnectionError || dialog == Dialog::SignIn);
    float dialogProgress = progressSince(now, dialogChanged, ScreenTransitionSeconds);
    bool dialogMoving = dialogProgress < 1.0f;
    if (loading && dialogMoving && !replacesScreen(leavingDialog) && playsExit(transitionOf(screen), 1.0f)) {
        // A route like /progress slides the screen it replaces out ahead of it.
        transitionLayer(ui, transitionOf(screen), false, dialogProgress, 1.0f, screenBounds.w);
        leaving([&] { screenContent(ui, width, height, screen); });
        ui.clearLayer();
    }
    if (!loading) {
        float progress = progressSince(now, screenChanged, ScreenTransitionSeconds);
        if (progress < 1.0f && leavingScreen && !(inGame() && *leavingScreen == Screen::Title) && playsExit(transitionOf(*leavingScreen), screenDirection)) {
            transitionLayer(ui, transitionOf(*leavingScreen), false, progress, screenDirection, screenBounds.w);
            leaving([&] { screenContent(ui, width, height, *leavingScreen); });
        }
        if (dialog != Dialog::SafeArea) {
            if (inGame() && screen == Screen::Title) {
                ui.clearLayer();
            } else if (dialogMoving && replacesScreen(leavingDialog)) {
                // Leaving a route like /disconnected brings the screen under it back from the other side.
                transitionLayer(ui, transitionOf(screen), true, dialogProgress, -1.0f, screenBounds.w);
            } else {
                transitionLayer(ui, transitionOf(screen), true, progress, screenDirection, screenBounds.w);
            }
            screenContent(ui, width, height, screen);
        }
        ui.clearLayer();
    }
    mark("screen content");

    ui.setBlocked(false);
    auto drawSocial = [&] {
        float shown = outCubic(progressSince(now, socialChanged, ScreenTransitionSeconds));
        ui.setLayer((1.0f - shown) * 190.0f, 0.0f, shown);
        socialDrawer(ui, width, height);
        ui.clearLayer();
    };
    bool socialOverPause = dialog == Dialog::Pause;
    if (socialOpen && !loading && !socialOverPause) {
        drawSocial();
    }

    Transition leavingKind = transitionOf(leavingDialog);
    if (dialogMoving && leavingDialog != Dialog::None && playsExit(leavingKind, dialog == Dialog::None ? -1.0f : 1.0f)) {
        // Joining the world drops the OreUI routes, and the world fades in from under them.
        if (dialog == Dialog::None && inGame() && leavingKind == Transition::Slide) {
            leavingKind = Transition::FadeOut;
        }
        // Closing pops the dialog back out the way it came, one replacing it pushes it out the other side.
        transitionLayer(ui, leavingKind, false, dialogProgress, dialog == Dialog::None ? -1.0f : 1.0f, screenBounds.w);
        leaving([&] {
            bool ignored = false;
            dialogContent(ui, width, height, leavingDialog, ignored, ignored);
        });
    }
    bool confirmed = false;
    bool cancelled = false;
    transitionLayer(ui, transitionOf(dialog), true, dialogProgress, 1.0f, screenBounds.w);
    dialogContent(ui, width, height, dialog, confirmed, cancelled);
    ui.clearLayer();
    if (confirmed || cancelled) {
        dialog = Dialog::None;
    }
    if (socialOpen && !loading && socialOverPause) {
        drawSocial();
    }
    mark("social and dialogs");

    inventoryLayer(ui, width, height, now);
    // Only a closed form still playing its exit is left to draw here.
    forms.draw(ui, width, height);
    mark("inventory and forms");

    if (inGame() && session.changingDimension) {
        dimensionScreen(ui);
    }
    toast(ui, width, height);
    toasts.draw(ui, width, height);
    handleKeys(ui);
    mark("toasts and keys");
}

void Menu::dialogContent(Context& ui, float width, float height, Dialog which, bool& confirmed, bool& cancelled)
{
    switch (which) {
    case Dialog::None:
        break;
    case Dialog::Pause:
        pause(ui, width, height);
        break;
    case Dialog::Chat:
        chatScreen(ui, width, height);
        break;
    case Dialog::ProfileOptions:
        profileOptions(ui, width, height);
        break;
    case Dialog::Death:
        deathScreen(ui, width, height);
        break;
    case Dialog::SafeArea:
        safeAreaDialog(ui);
        break;
    case Dialog::Emotes:
        emoteWheel(ui, width, height);
        break;
    case Dialog::Connecting:
    case Dialog::SignIn:
        progressDialog(ui, width, height);
        break;
    case Dialog::ConnectionError:
        connectionError(ui, width, height);
        break;
    case Dialog::ConfirmDelete: {
        std::optional<ServerRow> row = selectedRow();
        messageDialog(ui, width, height, tr("selectServer.delete", "Delete Server"), tr("selectServer.deleteQuestion", "Are you sure you want to remove this server?"), tr("selectServer.deleteButton", "Delete"), tr("gui.cancel", "Cancel"), confirmed, cancelled);
        if (confirmed && row && row->group == ServerGroup::Saved) {
            store.remove(row->index);
            selection.reset();
            navigate(Screen::Play);
        }
        break;
    }
    case Dialog::ConfirmExit:
        messageDialog(ui, width, height, tr("globalPauseScreen.quit", "Quit Game"), tr("deathScreen.quit.confirm", "Are you sure you want to quit?"), tr("globalPauseScreen.quit", "Quit"), tr("gui.cancel", "Cancel"), confirmed, cancelled);
        if (confirmed) {
            quit = true;
        }
        break;
    case Dialog::RealmInvites:
        realmInvitesDialog(ui, width, height);
        break;
    case Dialog::JoinRealm:
        joinRealmDialog(ui, width, height);
        break;
    case Dialog::ConfirmRemoveFriend:
        messageDialog(ui, width, height, tr("hbui.SocialDrawer.PlayerOptionsMenu.removeFriend", "Remove friend"),
            trf("kestrel.social.removeFriendQuestion", "Remove %1$s from your friends? You will need to send a new friend request to be friends again.", { removingName }),
            tr("hbui.SocialDrawer.PlayerOptionsMenu.removeFriend", "Remove friend"), tr("gui.cancel", "Cancel"), confirmed, cancelled);
        if (confirmed && !removingXuid.empty()) {
            requestSocial(SocialAction::RemoveFriend, removingXuid);
        }
        if (confirmed || cancelled) {
            removingXuid.clear();
            removingName.clear();
            socialOpen = true;
        }
        break;
    }
}

/**
 * The inventory the way inventory_screen_common in ui_common.json moves it:
 * the panel rises from half a screen down while it fades in and sinks back
 * while it fades out, and the dim behind it fades linearly on its own.
 */
void Menu::inventoryLayer(Context& ui, float width, float height, std::chrono::steady_clock::time_point now)
{
    float progress = progressSince(now, inventoryChanged, ScreenTransitionSeconds);
    if (!inventory.active && (progress >= 1.0f || !inGame())) {
        return;
    }
    float shown = inventory.active ? outCubic(progress) : 1.0f - outCubic(progress);
    bool blocked = ui.isBlocked();
    ui.setBlocked(!inventory.active);
    {
        WholeScreen whole(ui, screenBounds);
        ui.clearClip();
        ui.setLayer(0.0f, 0.0f, inventory.active ? progress : 1.0f - progress);
        ui.fill({ 0.0f, 0.0f, screenBounds.w, screenBounds.h }, InventoryDim);
        ui.clearLayer();
    }
    ui.setLayer(0.0f, (1.0f - shown) * ContainerWipe * screenBounds.h, shown);
    inventory.draw(ui, width, height, [&](float x, float y, float pixel) { playerModel(ui, x, y, pixel, true); });
    ui.clearLayer();
    ui.setBlocked(blocked);
}

/**
 * hud_screen.json fades the HUD out as a screen is pushed over it and back in
 * once that screen pops.
 */
void Menu::coverHud(bool covered, std::chrono::steady_clock::time_point now)
{
    if (covered == hudCovered) {
        return;
    }
    hudCovered = covered;
    hudChanged = now;
    if (!hudScreen) {
        return;
    }
    if (!covered) {
        // Otherwise the loading bars base_screen starts on exit_push would still be up.
        hudScreen->fire("screen_animation_reset");
    }
    hudScreen->fire(covered ? "screen.exit_push" : "screen.entrance_pop");
}

void Menu::fadingHud(Context& ui, float width, float height, std::chrono::steady_clock::time_point now)
{
    if (inGame() && hudCovered && progressSince(now, hudChanged, ScreenTransitionSeconds) < 1.0f) {
        drawHudScreen(ui, width, height);
    }
}

void Menu::panorama(Context& ui)
{
    WholeScreen whole(ui, screenBounds);
    float width = screenBounds.w;
    float height = screenBounds.h;
    using Vec = std::array<float, 3>;
    struct CubeFace {
        Vec center;
        Vec right;
        Vec up;
    };
    constexpr CubeFace Faces[6] = {
        { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
        { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
    };
    constexpr int Cells = 12;
    constexpr float Degrees = 3.14159265f / 180.0f;
    constexpr float Near = 0.02f;

    float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt).count();
    float yaw = seconds * 2.0f * Degrees;
    float pitch = 8.0f * Degrees;
    float cy = std::cos(yaw);
    float sy = std::sin(yaw);
    float cp = std::cos(pitch);
    float sp = std::sin(pitch);
    float focal = 1.0f / std::tan(42.5f * Degrees) * height * 0.5f;

    ui.fill({ 0.0f, 0.0f, width, height }, { 0, 0, 0, 255 });
    std::optional<StartupTimer> timer;
    if (std::find(panoramaReady.begin(), panoramaReady.end(), false) != panoramaReady.end()) {
        timer.emplace();
        std::vector<std::string> faces;
        for (int index = 0; index < 6; ++index) {
            faces.push_back("ui/panorama_alternate_" + std::to_string(index));
        }
        ui.skin().preload(faces);
        timer->mark("panorama: preload");
    }
    for (int index = 0; index < 6; ++index) {
        const CubeFace& face = Faces[index];
        std::string name = "dynamic/panorama_" + std::to_string(index);
        if (!panoramaReady[size_t(index)]) {
            panoramaReady[size_t(index)] = true;
            std::string source = "ui/panorama_alternate_" + std::to_string(index);
            if (const Bitmap* full = ui.skin().bitmap(source)) {
                uint32_t step = std::max<uint32_t>(1, full->width / PanoramaSize);
                Bitmap reduced { full->width / step, full->height / step, {} };
                reduced.rgba.resize(size_t(reduced.width) * reduced.height * 4);
                for (uint32_t y = 0; y < reduced.height; ++y) {
                    for (uint32_t x = 0; x < reduced.width; ++x) {
                        const uint8_t* texel = full->rgba.data() + (size_t(y * step) * full->width + x * step) * 4;
                        std::copy(texel, texel + 4, reduced.rgba.data() + (size_t(y) * reduced.width + x) * 4);
                    }
                }
                ui.skin().setDynamic(name, std::move(reduced));
            }
            ui.skin().clearDynamic(source);
        }
        const Sprite& sprite = ui.skin().sprite(name);
        if (!sprite.valid) {
            continue;
        }
        auto project = [&](float u, float v, bool& visible) -> std::array<float, 2> {
            Vec point {};
            for (int axis = 0; axis < 3; ++axis) {
                point[axis] = face.center[axis] + face.right[axis] * (u * 2.0f - 1.0f) + face.up[axis] * (1.0f - v * 2.0f);
            }
            float x = point[0] * cy - point[2] * sy;
            float z = point[0] * sy + point[2] * cy;
            float y = point[1] * cp - z * sp;
            z = point[1] * sp + z * cp;
            visible = z > Near;
            float depth = std::max(z, Near);
            return { width * 0.5f + x / depth * focal, height * 0.5f - y / depth * focal };
        };
        for (int row = 0; row < Cells; ++row) {
            for (int column = 0; column < Cells; ++column) {
                float u0 = static_cast<float>(column) / Cells;
                float u1 = static_cast<float>(column + 1) / Cells;
                float v0 = static_cast<float>(row) / Cells;
                float v1 = static_cast<float>(row + 1) / Cells;
                std::array<bool, 4> visible {};
                std::array<std::array<float, 2>, 4> points {
                    project(u0, v0, visible[0]),
                    project(u1, v0, visible[1]),
                    project(u1, v1, visible[2]),
                    project(u0, v1, visible[3]),
                };
                if (!(visible[0] && visible[1] && visible[2] && visible[3])) {
                    continue;
                }
                float minX = std::min({ points[0][0], points[1][0], points[2][0], points[3][0] });
                float maxX = std::max({ points[0][0], points[1][0], points[2][0], points[3][0] });
                float minY = std::min({ points[0][1], points[1][1], points[2][1], points[3][1] });
                float maxY = std::max({ points[0][1], points[1][1], points[2][1], points[3][1] });
                if (maxX < 0.0f || minX > width || maxY < 0.0f || minY > height) {
                    continue;
                }
                std::array<std::array<float, 2>, 4> texels {
                    std::array<float, 2> { u0 * sprite.width, v0 * sprite.height },
                    { u1 * sprite.width, v0 * sprite.height },
                    { u1 * sprite.width, v1 * sprite.height },
                    { u0 * sprite.width, v1 * sprite.height },
                };
                ui.spriteQuad(points, name, texels, { 255, 255, 255, 255 });
            }
        }
    }
    if (timer) {
        timer->mark("panorama: reduce and draw");
    }
}

void Menu::logo(Context& ui, float centerX, float y, float maxWidth)
{
    // A server that ships its own title art gets it instead of the game logo, as in game.
    const Sprite& art = ui.skin().sprite("dynamic/title");
    if (art.valid && showsServerArt(session.status)) {
        float w = std::min(maxWidth, 263.0f);
        ui.sprite({ std::floor(centerX - w * 0.5f), y, w, w * art.height / art.width }, "dynamic/title");
        return;
    }
    const Sprite& word = ui.skin().sprite("kestrel/title");
    if (!word.valid) {
        return;
    }
    float w = std::min(maxWidth, 378.5f);
    ui.sprite({ centerX - w * 0.5f, y, w, w * word.height / word.width }, "kestrel/title");
}

void Menu::playerModel(Context& ui, float centerX, float top, float pixel, bool inventoryPreview, float rotation)
{
    struct Part {
        std::array<float, 3> min;
        std::array<float, 3> size;
        std::array<float, 2> uv;
        std::array<float, 2> overlay;
        bool head;
    };
    constexpr Part Parts[] = {
        { { -4.0f, 24.0f, -4.0f }, { 8.0f, 8.0f, 8.0f }, { 0.0f, 0.0f }, { 32.0f, 0.0f }, true },
        { { -4.0f, 12.0f, -2.0f }, { 8.0f, 12.0f, 4.0f }, { 16.0f, 16.0f }, { 16.0f, 32.0f }, false },
        { { -8.0f, 12.0f, -2.0f }, { 4.0f, 12.0f, 4.0f }, { 40.0f, 16.0f }, { 40.0f, 32.0f }, false },
        { { 4.0f, 12.0f, -2.0f }, { 4.0f, 12.0f, 4.0f }, { 32.0f, 48.0f }, { 48.0f, 48.0f }, false },
        { { -4.0f, 0.0f, -2.0f }, { 4.0f, 12.0f, 4.0f }, { 0.0f, 16.0f }, { 0.0f, 32.0f }, false },
        { { 0.0f, 0.0f, -2.0f }, { 4.0f, 12.0f, 4.0f }, { 16.0f, 48.0f }, { 0.0f, 48.0f }, false },
    };
    std::string_view Skin = inventoryPreview && ui.skin().sprite("dynamic/inventory_skin").valid ? std::string_view("dynamic/inventory_skin") : std::string_view(playerSkin);
    constexpr float Degrees = 3.14159265f / 180.0f;
    constexpr float NeckY = 24.0f;

    float eyeY = top + 4.0f * pixel;
    float dx = (ui.mouseX() - centerX) / pixel;
    float dy = (ui.mouseY() - eyeY) / pixel;
    float bodyYaw = std::atan(dx / 40.0f) * 20.0f * Degrees;
    float headYaw = std::atan(dx / 40.0f) * 40.0f * Degrees;
    float headPitch = std::atan(dy / 40.0f) * 20.0f * Degrees;

    using Vec = std::array<float, 3>;
    auto yaw = [](const Vec& p, float angle) -> Vec {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return { p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c };
    };
    auto pitch = [](const Vec& p, float angle) -> Vec {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return { p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c };
    };
    auto roll = [](const Vec& p, float angle) -> Vec {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return { p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2] };
    };
    // A pose turns each part about its joint the way the player geometry does, the head and
    // arms following the body; Bedrock's Y and Z turn the other way round from this view.
    constexpr std::array<Vec, 6> Joints { { { 0.0f, 24.0f, 0.0f }, { 0.0f, 24.0f, 0.0f }, { -5.0f, 22.0f, 0.0f }, { 5.0f, 22.0f, 0.0f }, { -2.0f, 12.0f, 0.0f }, { 2.0f, 12.0f, 0.0f } } };
    auto turn = [&](Vec p, size_t index) -> Vec {
        const std::array<float, 3>& rotation = (*modelPose)[index];
        const Vec& joint = Joints[index];
        for (size_t axis = 0; axis < 3; ++axis) {
            p[axis] -= joint[axis];
        }
        p = roll(yaw(pitch(p, rotation[0] * Degrees), -rotation[1] * Degrees), -rotation[2] * Degrees);
        for (size_t axis = 0; axis < 3; ++axis) {
            p[axis] += joint[axis];
        }
        return p;
    };
    auto transform = [&](Vec p, const Part& part) -> Vec {
        bool head = part.head;
        size_t index = static_cast<size_t>(&part - Parts);
        if (modelPose && index < Joints.size()) {
            p = turn(p, index);
            if (index == 0 || index == 2 || index == 3) {
                p = turn(p, 1);
            }
        }
        if (head) {
            p[1] -= NeckY;
            p = yaw(pitch(p, headPitch), headYaw - bodyYaw);
            p[1] += NeckY;
        }
        return yaw(p, bodyYaw + rotation);
    };

    struct Face {
        std::array<std::array<float, 2>, 4> points;
        std::array<std::array<float, 2>, 4> texels;
        float depth;
        float light;
        std::string_view texture;
    };
    std::vector<Face> faces;
    auto addBox = [&](const Part& part, const std::array<float, 2>& uv, float inflate, std::string_view texture, bool mirror) {
        float x0 = part.min[0] - inflate;
        float y0 = part.min[1] - inflate;
        float z0 = part.min[2] - inflate;
        float x1 = part.min[0] + part.size[0] + inflate;
        float y1 = part.min[1] + part.size[1] + inflate;
        float z1 = part.min[2] + part.size[2] + inflate;
        float w = part.size[0];
        float h = part.size[1];
        float d = part.size[2];
        float u = uv[0];
        float v = uv[1];
        struct Side {
            std::array<Vec, 4> corners;
            std::array<float, 4> region;
            Vec normal;
        };
        Side sides[6] = {
            { { { { x0, y1, z1 }, { x1, y1, z1 }, { x1, y0, z1 }, { x0, y0, z1 } } }, { u + d, v + d, w, h }, { 0.0f, 0.0f, 1.0f } },
            { { { { x1, y1, z0 }, { x0, y1, z0 }, { x0, y0, z0 }, { x1, y0, z0 } } }, { u + d + w + d, v + d, w, h }, { 0.0f, 0.0f, -1.0f } },
            { { { { x0, y1, z0 }, { x0, y1, z1 }, { x0, y0, z1 }, { x0, y0, z0 } } }, { u, v + d, d, h }, { -1.0f, 0.0f, 0.0f } },
            { { { { x1, y1, z1 }, { x1, y1, z0 }, { x1, y0, z0 }, { x1, y0, z1 } } }, { u + d + w, v + d, d, h }, { 1.0f, 0.0f, 0.0f } },
            { { { { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } } }, { u + d, v, w, d }, { 0.0f, 1.0f, 0.0f } },
            { { { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y0, z0 }, { x0, y0, z0 } } }, { u + d + w, v, w, d }, { 0.0f, -1.0f, 0.0f } },
        };
        if (mirror) {
            std::swap(sides[2].region, sides[3].region);
            for (Side& side : sides) {
                side.region[0] += side.region[2];
                side.region[2] = -side.region[2];
            }
        }
        for (const Side& side : sides) {
            Vec normal = transform(side.normal, part);
            Vec origin = transform({ 0.0f, 0.0f, 0.0f }, part);
            Vec facing { normal[0] - origin[0], normal[1] - origin[1], normal[2] - origin[2] };
            if (facing[2] <= 0.0f) {
                continue;
            }
            Face face;
            float depth = 0.0f;
            for (size_t corner = 0; corner < 4; ++corner) {
                Vec p = transform(side.corners[corner], part);
                face.points[corner] = { centerX + p[0] * pixel, top + (32.0f - p[1]) * pixel };
                depth += p[2];
            }
            const std::array<float, 4>& r = side.region;
            face.texels = { { { r[0], r[1] }, { r[0] + r[2], r[1] }, { r[0] + r[2], r[1] + r[3] }, { r[0], r[1] + r[3] } } };
            face.depth = depth * 0.25f + inflate;
            face.light = 0.6f + 0.4f * std::clamp(facing[2] * 0.8f + facing[1] * 0.4f, 0.0f, 1.0f);
            face.texture = texture;
            faces.push_back(face);
        }
    };
    for (const Part& part : Parts) {
        addBox(part, part.uv, 0.0f, Skin, false);
        addBox(part, part.overlay, part.head ? 0.5f : 0.25f, Skin, false);
    }

    // The vanilla armor models: each piece inflates some of the parts above
    // and wraps them in its 64x32 armor texture, left limbs mirrored.
    struct ArmorBox {
        size_t slot;
        size_t part;
        std::array<float, 2> uv;
        float inflate;
        bool mirror;
    };
    constexpr ArmorBox ArmorBoxes[] = {
        { 0, 0, { 0.0f, 0.0f }, 1.0f, false },
        { 0, 0, { 32.0f, 0.0f }, 1.5f, false },
        { 1, 1, { 16.0f, 16.0f }, 1.01f, false },
        { 1, 2, { 40.0f, 16.0f }, 1.0f, false },
        { 1, 3, { 40.0f, 16.0f }, 1.0f, true },
        { 2, 1, { 16.0f, 16.0f }, 0.5f, false },
        { 2, 4, { 0.0f, 16.0f }, 0.5f, false },
        { 2, 5, { 0.0f, 16.0f }, 0.5f, true },
        { 3, 4, { 0.0f, 16.0f }, 1.0f, false },
        { 3, 5, { 0.0f, 16.0f }, 1.0f, true },
    };
    // Only the inventory's player preview dresses the model; the pause screen shows the plain skin.
    std::array<std::string, 4> armor;
    if (inGame() && inventoryPreview) {
        for (size_t slot = 0; slot < armor.size(); ++slot) {
            const HudItem& piece = inventory.state.slots[inventory::Armor + slot];
            if (!piece.empty()) {
                armor[slot] = world::itemArmorTexture(piece.identifier, slot);
            }
            if (!armor[slot].empty() && !ui.skin().sprite(armor[slot]).valid) {
                armor[slot].clear();
            }
        }
    }
    for (const ArmorBox& box : ArmorBoxes) {
        if (!armor[box.slot].empty()) {
            addBox(Parts[box.part], box.uv, box.inflate, armor[box.slot], box.mirror);
        }
    }
    std::stable_sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) {
        return a.depth < b.depth;
    });
    for (const Face& face : faces) {
        uint8_t shade = static_cast<uint8_t>(std::clamp(face.light, 0.0f, 1.0f) * 255.0f);
        ui.spriteQuad(face.points, face.texture, face.texels, { shade, shade, shade, 255 });
    }
}

void Menu::title(Context& ui, float width, float height)
{
    const auto layout = titleLayout(width, height);
    logo(ui, width * 0.5f, layout.logoTop, layout.logoWidth);
    const auto& splashes = Localization::shared().splashes();
    if (splashText.empty() && !splashes.empty()) {
        std::mt19937 random(std::random_device {}());
        splashText = splashes[std::uniform_int_distribution<size_t>(0, splashes.size() - 1)(random)];
    }
    if (!splashText.empty()) {
        std::string key = splashText.front() == '%' ? splashText.substr(1) : splashText;
        std::string text = tr(key, splashText);
        float widest = 0.0f;
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            while (ui.measure(std::string_view(text).substr(start, end - start), TextStyle::Pixel) > 130.0f) {
                size_t split = text.rfind(' ', end - 1);
                if (split == std::string::npos || split <= start) break;
                end = split;
            }
            widest = std::max(widest, ui.measure(std::string_view(text).substr(start, end - start), TextStyle::Pixel));
            if (end < text.size()) text[end] = '\n';
            start = end + 1;
        }
        float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt).count();
        float pulse = 1.0f - 0.05f * std::abs(std::sin(seconds * 6.2831853f));
        float magnify = pulse * std::min(1.0f, 130.0f / std::max(widest, 1.0f));
        const Sprite& serverArt = ui.skin().sprite("dynamic/title");
        bool customLogo = serverArt.valid && showsServerArt(session.status);
        float logoWidth = std::min(layout.logoWidth, customLogo ? 263.0f : 378.5f);
        const Sprite& art = customLogo ? serverArt : ui.skin().sprite("kestrel/title");
        float logoHeight = art.valid ? logoWidth * art.height / art.width : 40.0f;
        float textHeight = (1.0f + static_cast<float>(std::count(text.begin(), text.end(), '\n'))) * 9.0f;
        float halfWidth = ((widest + 2.0f) * 0.93969262f + (textHeight + 2.0f) * 0.34202014f) * magnify * 0.5f;
        float centerX = std::min(width * 0.5f + logoWidth * 0.46f, width - 8.0f - halfWidth);
        ui.rotatedPixelText(text, centerX, layout.logoTop + logoHeight * 0.65f, magnify, -0.34906585f, { 255, 255, 0, 255 });
    }

    float x = std::floor((width - TitleButtonWidth) * 0.5f);
    float y = std::round(height * 0.5f + 13.67f);
    if (ui.classicButton("title:play", tr("menu.play", "Play"), { x, y, TitleButtonWidth, TitleButtonHeight })) {
        playTab = PlayTab::Servers;
        navigate(Screen::Play);
    }
    if (ui.classicButton("title:settings", tr("menu.settings", "Settings"), { x, y + TitleButtonStep, TitleButtonWidth, TitleButtonHeight })) {
        returnScreen = Screen::Title;
        navigate(Screen::Settings);
    }
    if (ui.classicButton("title:marketplace", tr("menu.store", "Marketplace"), { x, y + TitleButtonStep * 2.0f, TitleButtonWidth, TitleButtonHeight })) {
        navigate(Screen::Marketplace);
    }

    if (iconButton(ui, "title:social", tr("options.social", "Social") + " (" + std::to_string(onlineCount(social.friends)) + ")", "ui/FriendsIcon", { width - 1.0f - 80.0f, 1.0f, 80.0f, CornerButtonHeight }, 9.0f)) {
        socialOpen = true;
        socialParty = false;
    }

    constexpr float CornerMargin = 2.0f;
    float labelY = std::floor(height - CornerMargin - 9.0f);
    float cornerLeft = CornerMargin - 1.0f;
    float bottom = std::floor(labelY - 1.0f - CornerMargin - CornerButtonHeight);
    titlePromo(ui, cornerLeft, bottom - 4.0f);
    if (iconButton(ui, "title:inbox", "", "ui/mail_icon", { cornerLeft, bottom, 23.0f, CornerButtonHeight }, 15.0f)) {
        dialog = Dialog::RealmInvites;
        requestSocial(SocialAction::RefreshInvites);
    }
    const Sprite& avatar = ui.skin().sprite("dynamic/avatar");
    std::string profileLabel = signedIn() ? tr("menu.profile", "Profile") : tr("menu.account.signIn.buttonLabel", "Sign In");
    if (iconButton(ui, "title:profile", profileLabel, avatar.valid ? "dynamic/avatar" : "ui/profile_glyph_color", { cornerLeft + 31.0f, bottom, 63.0f, CornerButtonHeight }, 18.0f)) {
        if (signedIn()) {
            navigate(Screen::Profile);
        } else {
            beginSignIn();
        }
    }

    float dressingX = layout.dressingCenter - 41.0f;
    float dressingY = layout.dressingTop;
    if (ui.classicButton("title:dressing", tr("profileScreen.header", "Dressing Room"), { dressingX, dressingY, 82.0f, CornerButtonHeight })) {
        returnScreen = Screen::Title;
        navigate(Screen::DressingRoom);
    }
    float nameWidth = ui.measure(displayName, TextStyle::Pixel);
    backedLabel(ui, displayName, std::floor(dressingX + 41.0f - nameWidth * 0.5f), std::floor(dressingY - 97.67f));
    constexpr float ModelPixel = 2.23f;
    float modelX = dressingX + 41.0f;
    float modelY = dressingY - 81.67f;
    auto model = ui.interact("title:player_model", { modelX - 9.0f * ModelPixel, modelY, 18.0f * ModelPixel, 33.0f * ModelPixel });
    const InputState& input = ui.input();
    if (!input.mouseDown || ui.isBlocked()) {
        titleModelDragging = false;
    } else if (input.mousePressed && model.pressed) {
        titleModelDragging = true;
        titleModelDragX = ui.mouseX();
    }
    if (titleModelDragging) {
        constexpr float Degrees = 3.14159265f / 180.0f;
        titleModelRotation = std::remainder(titleModelRotation + (ui.mouseX() - titleModelDragX) * Degrees, 2.0f * 3.14159265f);
        titleModelDragX = ui.mouseX();
    }
    playerModel(ui, modelX, modelY, ModelPixel, false, titleModelRotation);

    backedLabel(ui, "Kestrel, not affiliated with Mojang", CornerMargin, labelY);
    constexpr std::string_view Version = "v1.26.51";
    backedLabel(ui, Version, std::floor(width - CornerMargin - ui.measure(Version, TextStyle::Pixel)), labelY);
}

/**
 * The pause menu drawn by hand, for when the game's UI files are missing.
 */
void Menu::classicPause(Context& ui, float width, float height)
{
    ui.fill(screenBounds, { 0, 0, 0, 90 });
    constexpr float ButtonWidth = 276.0f;
    constexpr float ButtonHeight = 28.0f;
    constexpr float Step = 31.0f;
    float x = std::round(width * 0.5f - 296.33f);
    float y = std::round(height * 0.5f - 47.67f);

    logo(ui, x + ButtonWidth * 0.5f, y - 59.0f, ButtonWidth);

    if (ui.classicButton("pause:resume", tr("menu.returnToGame", "Resume Game"), { x, y, ButtonWidth, ButtonHeight })) {
        dialog = Dialog::None;
    }
    if (ui.classicButton("pause:settings", tr("menu.settings", "Settings"), { x, y + Step, ButtonWidth, ButtonHeight })) {
        dialog = Dialog::None;
        returnScreen = Screen::Title;
        navigate(Screen::Settings);
    }
    if (ui.classicButton("pause:quit", tr("menu.quit", "Save & Quit"), { x, y + Step * 2.0f, ButtonWidth, ButtonHeight })) {
        dialog = Dialog::None;
        disconnectRequested = true;
    }

    if (iconButton(ui, "pause:social", tr("options.social", "Social") + " (" + std::to_string(onlineCount(social.friends)) + ")", "ui/FriendsIcon", { width - 1.0f - 80.0f, 1.0f, 80.0f, CornerButtonHeight }, 9.0f)) {
        socialOpen = true;
    }
    float dressingX = width - 158.0f;
    if (ui.classicButton("pause:dressing", tr("profileScreen.header", "Dressing Room"), { dressingX, y + 119.0f, 82.0f, CornerButtonHeight })) {
        dialog = Dialog::None;
        returnScreen = Screen::Title;
        navigate(Screen::DressingRoom);
    }
    float nameWidth = ui.measure(displayName, TextStyle::Pixel);
    backedLabel(ui, displayName, std::floor(dressingX + 41.0f - nameWidth * 0.5f), y - 25.67f);
    playerModel(ui, dressingX + 41.0f, y - 13.0f, 4.06f);
}

/**
 * The screen that hides the world while the player travels to another
 * dimension, until the server has sent the new terrain: the dimension's own
 * backdrop, its name, and the terrain building bar.
 */
void Menu::dimensionScreen(Context& ui)
{
    WholeScreen whole(ui, screenBounds);
    float width = screenBounds.w;
    float height = screenBounds.h;
    static constexpr const char* Backdrops[] = { "textures/blocks/dirt", "textures/blocks/netherrack", "textures/blocks/end_stone" };
    size_t index = static_cast<size_t>(std::clamp(session.dimension, 0, 2));
    ui.fill({ 0.0f, 0.0f, width, height }, Black);
    const Sprite& tile = ui.skin().sprite(Backdrops[index]);
    if (tile.valid) {
        constexpr float Tile = 16.0f;
        for (float y = 0.0f; y < height; y += Tile) {
            for (float x = 0.0f; x < width; x += Tile) {
                ui.sprite({ x, y, Tile, Tile }, Backdrops[index], { 64, 64, 64, 255 });
            }
        }
    }

    constexpr float DialogWidth = 286.67f;
    constexpr float DialogHeight = 97.33f;
    Rect frame { std::round((width - DialogWidth) * 0.5f), std::round(height * 0.5f - 49.0f), DialogWidth, DialogHeight };
    const Sprite& word = ui.skin().sprite("kestrel/title");
    float logoWidth = std::min(width - 32.0f, 378.5f);
    float logoHeight = word.valid && word.width > 0.0f ? logoWidth * word.height / word.width : 64.0f;
    float logoTop = frame.y - 24.0f - logoHeight;
    if (logoTop < 8.0f) {
        logoTop = 8.0f;
        logoWidth = std::max(120.0f, (frame.y - 32.0f) * logoWidth / std::max(logoHeight, 1.0f));
    }
    logo(ui, width * 0.5f, logoTop, logoWidth);

    ui.nineSlice(frame, "ui/dialog_background_opaque");
    std::string heading = tr("progressScreen.generating", "Generating World");
    float headingWidth = ui.measure(heading, TextStyle::Pixel);
    ui.text(heading, TextStyle::Pixel, frame.x + std::floor((frame.w - std::min(headingWidth, frame.w - 12.0f)) * 0.5f), frame.y + 9.0f, DialogInk, frame.w - 12.0f);

    Rect well { frame.x + 5.67f, frame.y + 21.33f, frame.w - 11.33f, frame.h - 27.0f };
    ui.fill(well, { 85, 85, 85, 255 });
    ui.fill(well.inset(1.0f), { 0, 0, 0, 220 });
    std::string stage = tr("progressScreen.message.building", "Building terrain");
    float stageWidth = ui.measure(stage, TextStyle::Pixel);
    ui.text(stage, TextStyle::Pixel, std::round(well.x + (well.w - stageWidth) * 0.5f), well.y + 5.0f, White);
}

/**
 * The screen that covers the world while the player is dead: a red veil, the
 * death title and cause, respawn, and the game menu, which comes back here
 * when resumed.
 */
void Menu::deathScreen(Context& ui, float width, float height)
{
    ui.fill(screenBounds, { 110, 0, 0, 120 });
    float top = std::round(height * 0.265f);
    ui.textCentered(upperCase(tr("hbui.gameplay.DeathScreen.youDied", "You Died!")), TextStyle::HeadingLarge, { 0.0f, top - 12.0f, width, 24.0f }, White);
    if (!session.deathMessage.empty()) {
        ui.textCentered(session.deathMessage, TextStyle::Ui, { 0.0f, top + 10.0f, width, 12.0f }, White);
    }

    constexpr float ButtonWidth = 158.0f;
    constexpr float ButtonHeight = 20.0f;
    float x = std::round((width - ButtonWidth) * 0.5f);
    float y = std::round(height * 0.713f);
    bool waiting = respawnClicked != std::chrono::steady_clock::time_point {} && std::chrono::steady_clock::now() - respawnClicked < std::chrono::seconds(5);
    std::string respawnLabel = upperCase(waiting ? tr("hbui.gameplay.DeathScreen.respawning", "Respawning...") : tr("hbui.gameplay.DeathScreen.respawn", "Respawn"));
    if (ui.pressableButton("death:respawn", "pressableElevatedPrimary", respawnLabel, { x, y, ButtonWidth, ButtonHeight }, TextStyle::HeadingSmall, !waiting)) {
        respawnRequested = true;
        respawnClicked = std::chrono::steady_clock::now();
    }
    if (ui.pressableButton("death:menu", "pressableElevatedSecondary", tr("hbui.gameplay.DeathScreen.gameMenu", "Game Menu"), { x, y + ButtonHeight + 6.0f, ButtonWidth, ButtonHeight })) {
        dialog = Dialog::Pause;
    }
}

void Menu::connectionError(Context& ui, float width, float height)
{
    const std::string& server = session.name.empty() ? session.levelName : session.name;
    std::string reason = session.error.empty() ? tr("disconnect.closed", "The connection was closed.")
        : Localization::shared().translateMessage(session.error);
    if (!session.packetError.empty()) {
        reason += "\n\n" + session.packetError;
    }
    auto textPanel = [&](const Rect& area, std::string_view text, float& scroll, bool centered) {
        float contentHeight = ui.paragraphHeight(text, TextStyle::ErrorBody, area.w - 6.0f);
        scrollArea(ui, area, scroll, contentHeight);
        ui.setClip(area);
        float y = area.y + (centered ? std::max(0.0f, (area.h - contentHeight) * 0.5f) : 0.0f);
        ui.paragraph(text, TextStyle::ErrorBody, area.x, y - scroll, area.w - 6.0f, White);
        ui.clearClip();
    };
    if (!errorDetailsShown) {
        float panelWidth = std::min(524.0f, width - 24.0f);
        Rect panel { std::floor((width - panelWidth) * 0.5f), std::floor((height - 108.0f) * 0.5f), panelWidth, 108.0f };
        ui.fill(panel, { 24, 24, 24, 255 });
        ui.fill(panel.inset(1.0f), { 48, 48, 48, 255 });
        std::string title = session.status == SessionStatus::Failed
            ? tr("disconnectionScreen.title.unableToConnect", "Unable to connect.")
            : server.empty() ? tr("hbui.ConnectionErrorRoute.server.disconnectedFromServer", "Disconnected from server")
            : trf("hbui.ConnectionErrorRoute.server.disconnectedFrom", "Disconnected from %1$s", { server });
        ui.setClip({ panel.x + 9.0f, panel.y + 1.0f, panel.w - 18.0f, 26.0f });
        ui.textCentered(title, TextStyle::Pixel, { panel.x + 9.0f, panel.y + 1.0f, panel.w - 18.0f, 26.0f }, White);
        ui.clearClip();
        Rect well { panel.x + 9.0f, panel.y + 27.0f, panel.w - 18.0f, 40.0f };
        ui.fill(well, { 34, 34, 34, 255 });
        textPanel(well.inset(8.0f), reason, errorReasonScroll, true);
        float buttonWidth = std::min(126.0f, (panel.w - 26.0f) * 0.5f);
        float x = panel.x + (panel.w - buttonWidth * 2.0f - 8.0f) * 0.5f;
        if (ui.classicButton("error:menu", tr("hbui.ConnectionErrorRoute.server.backToMenu", "Back to menu"),
                { x, panel.y + 75.0f, buttonWidth, 24.0f })) {
            dialog = Dialog::None;
        }
        if (ui.classicButton("error:details", tr("hbui.ConnectionErrorRoute.server.showDetails", "Show details"),
                { x + buttonWidth + 8.0f, panel.y + 75.0f, buttonWidth, 24.0f })) {
            errorDetailsShown = true;
            errorReasonScroll = 0.0f;
        }
        return;
    }

    ui.fill({ 0.0f, 0.0f, width, 24.0f }, HeaderEdge);
    ui.fill({ 0.0f, 0.0f, width, 23.0f }, HeaderBar);
    ui.textCentered(tr("hbui.ConnectionErrorRoute.details.header", "Error details"), TextStyle::Heading,
        { 30.0f, 0.0f, width - 60.0f, 23.0f }, InkDark);
    Interaction back = ui.interact("error:back", { 0.0f, 0.0f, 30.0f, 24.0f });
    ui.sprite({ 10.0f, 6.0f, 6.0f, 12.0f }, "hbui/arrowBack", back.hovered ? Muted1 : InkDark);
    if (back.clicked) {
        errorDetailsShown = false;
        errorReasonScroll = 0.0f;
    }

    float totalWidth = std::min(632.0f, width - 32.0f);
    bool stacked = totalWidth < 460.0f;
    float panelHeight = std::min(144.0f, stacked ? (height - 64.0f) * 0.5f : height - 56.0f);
    float totalHeight = stacked ? panelHeight * 2.0f + 8.0f : panelHeight;
    float x = std::floor((width - totalWidth) * 0.5f);
    float y = std::floor(24.0f + (height - 24.0f - totalHeight) * 0.5f);
    float leftWidth = stacked ? totalWidth : std::floor((totalWidth - 8.0f) * 0.416f);
    Rect left { x, y + 10.0f, leftWidth, panelHeight - 10.0f };
    Rect right { stacked ? x : left.right() + 8.0f, stacked ? y + panelHeight + 8.0f : y,
        stacked ? totalWidth : totalWidth - leftWidth - 8.0f, panelHeight };
    ui.fill(left, { 112, 112, 112, 255 });
    ui.fill(left.inset(1.0f), { 48, 48, 48, 255 });
    ui.fill(right, { 112, 112, 112, 255 });
    ui.fill(right.inset(1.0f), { 48, 48, 48, 255 });
    std::string source = server.empty() ? tr("hbui.ConnectionErrorRoute.details.fromServerNoName", "From server")
        : trf("hbui.ConnectionErrorRoute.details.fromServer", "From %1$s", { server });
    float tabWidth = std::min(left.w, ui.measure(source, TextStyle::ErrorTab) + 8.0f);
    ui.fill({ left.x, y, tabWidth, 13.0f }, { 65, 120, 177, 255 });
    ui.fill({ left.x, left.y, left.w, 2.0f }, { 101, 164, 226, 255 });
    ui.setClip({ left.x, y, tabWidth, 13.0f });
    ui.text(source, TextStyle::ErrorTab, left.x + 4.0f, y + 1.0f, White);
    ui.clearClip();
    textPanel({ left.x + 9.0f, left.y + 20.0f, left.w - 18.0f, left.h - 29.0f }, reason, errorReasonScroll, false);
    textPanel(right.inset(9.0f), errorDiagnostics, errorInfoScroll, false);
}

void Menu::progressDialog(Context& ui, float width, float height)
{
    constexpr float DialogWidth = 286.67f;
    constexpr float DialogHeight = 97.33f;
    Rect frame { std::round((width - DialogWidth) * 0.5f), std::round(height * 0.5f - 49.0f), DialogWidth, DialogHeight };
    const Sprite& word = ui.skin().sprite("kestrel/title");
    float logoWidth = std::min(width - 32.0f, 378.5f);
    float logoHeight = word.valid && word.width > 0.0f ? logoWidth * word.height / word.width : 64.0f;
    float logoTop = frame.y - 56.0f - logoHeight;
    if (logoTop < 8.0f) {
        logoTop = 8.0f;
        logoWidth = std::max(120.0f, (frame.y - 64.0f) * logoWidth / std::max(logoHeight, 1.0f));
    }
    logo(ui, width * 0.5f, logoTop, logoWidth);

    std::string heading;
    std::string body;
    std::string button = tr("gui.cancel", "Cancel");
    bool progress = false;
    float fraction = -1.0f;
    bool twoButtons = false;

    if (dialog == Dialog::SignIn) {
        switch (account.status) {
        case AccountStatus::AwaitingCode:
            heading = tr("menu.account.signIn.title", "Sign in with a Microsoft account");
            body = "Go to " + account.verificationUri + " and enter the code " + account.userCode;
            twoButtons = true;
            break;
        case AccountStatus::Failed:
            heading = "Sign-in failed";
            body = account.error;
            button = tr("gui.tryAgain", "Try Again");
            twoButtons = true;
            break;
        default:
            heading = tr("authentication.loggingin", "Signing in...");
            progress = true;
            break;
        }
    } else if (session.packPrompt) {
        heading = tr("progressScreen.dialog.title.resourcePack", "Download Resource Packs?");
        body = session.packSkippable ? tr("progressScreen.dialog.message.resourcePack.optional", "This world has optional Resource Packs applied to it. Would you like to download them before you join?") : tr("progressScreen.dialog.message.resourcePack.serverRequired", "The owner of this world requires players to download all Resource Packs applied to it. Would you like to download them and join?");
        button = tr("selectTemplate.download", "Download");
        twoButtons = session.packSkippable;
    } else if (session.packDownloading) {
        heading = trf("progressScreen.title.downloading", "Downloading packs %1", { "(" + megabytes(session.packReceived) + " / " + megabytes(session.packTotal) + ")" });
        fraction = session.packTotal ? std::clamp(static_cast<float>(session.packReceived) / static_cast<float>(session.packTotal), 0.0f, 1.0f) : 0.0f;
    } else {
        bool generating = session.packsResolved || session.loadingTerrain;
        heading = generating ? tr("progressScreen.generating", "Generating World") : tr("progressScreen.title.connectingExternal", "Connecting to external server");
        progress = true;
    }

    ui.nineSlice(frame, "ui/dialog_background_opaque");
    float headingWidth = ui.measure(heading, TextStyle::Pixel);
    ui.text(heading, TextStyle::Pixel, frame.x + std::floor((frame.w - std::min(headingWidth, frame.w - 12.0f)) * 0.5f), frame.y + 9.0f, DialogInk, frame.w - 12.0f);

    Rect well { frame.x + 5.67f, frame.y + 21.33f, frame.w - 11.33f, frame.h - 27.0f };
    ui.fill(well, { 85, 85, 85, 255 });
    ui.fill(well.inset(1.0f), { 0, 0, 0, 220 });

    float buttonY = well.bottom() - 8.0f - 24.0f;
    if (!body.empty()) {
        ui.paragraph(body, TextStyle::Pixel, well.x + 6.0f, well.y + 6.0f, well.w - 12.0f, White);
    }
    if (progress && dialog == Dialog::Connecting && (session.packsResolved || session.loadingTerrain)) {
        std::string stage = session.loadingTerrain ? tr("progressScreen.message.building", "Building terrain") : tr("progressScreen.message.locating", "Locating server");
        float stageWidth = ui.measure(stage, TextStyle::Pixel);
        float buttonCenter = std::floor(well.x + (well.w - 64.0f) * 0.5f) + 32.0f;
        ui.text(stage, TextStyle::Pixel, std::round(buttonCenter - stageWidth * 0.5f), buttonY - 33.0f, White);
    }
    if (progress && !session.loadingTerrain) {
        const Sprite& bar = ui.skin().sprite("ui/loading_bar");
        if (bar.valid) {
            float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt).count();
            int frames = std::max(1, static_cast<int>(bar.width / 64.0f));
            int cycle = std::max(1, frames * 2 - 2);
            int step = static_cast<int>(seconds * 10.0f) % cycle;
            int current = step < frames ? step : cycle - step;
            ui.spriteRegion({ std::floor(well.x + (well.w - 64.0f) * 0.5f), buttonY - 14.0f, 64.0f, 8.0f }, "ui/loading_bar", { current * 64.0f, 0.0f, 64.0f, bar.height }, { 178, 178, 178, 255 });
        }
    }
    if (fraction >= 0.0f) {
        Rect track { well.x + 20.0f, buttonY - 14.0f, well.w - 40.0f, 5.0f };
        ui.nineSlice(track, "ui/empty_progress_bar");
        if (fraction > 0.0f) {
            ui.nineSlice({ track.x, track.y, std::max(track.w * fraction, 8.0f), track.h }, "ui/filled_progress_bar");
        }
    }

    constexpr float ButtonWidth = 64.0f;
    Rect primary { std::floor(well.x + (well.w - ButtonWidth) * 0.5f), buttonY, ButtonWidth, 24.0f };
    Rect secondary {};
    if (twoButtons) {
        primary.x = std::floor(well.x + well.w * 0.5f - ButtonWidth - 2.0f);
        secondary = { primary.right() + 4.0f, buttonY, ButtonWidth, 24.0f };
    }

    if (dialog == Dialog::SignIn) {
        if (account.status == AccountStatus::AwaitingCode) {
            if (ui.classicButton("signin:open", "Open Page", primary)) {
                platform::copyText(account.userCode);
                platform::openUrl(account.verificationUri);
                notify("Code copied, paste it on the page");
            }
        } else if (account.status == AccountStatus::Failed) {
            if (ui.classicButton("signin:retry", button, primary)) {
                beginSignIn();
            }
        }
        Rect cancel = twoButtons ? secondary : primary;
        if (ui.classicButton("signin:cancel", tr("gui.cancel", "Cancel"), cancel)) {
            if (account.status != AccountStatus::Failed) {
                accountRequest = AccountRequest::Cancel;
            }
            dialog = Dialog::None;
        }
        return;
    }
    if (session.packPrompt) {
        if (ui.classicButton("packs:download", button, primary)) {
            packAnswer = true;
        }
        if (session.packSkippable && ui.classicButton("packs:skip", tr("gui.skip", "Skip"), secondary)) {
            packAnswer = false;
        }
        return;
    }
    if (session.loadingTerrain) {
        return;
    }
    if (ui.classicButton("connecting:cancel", button, primary)) {
        disconnectRequested = true;
        dialog = Dialog::None;
    }
}

void Menu::messageDialog(Context& ui, float width, float height, std::string_view heading, std::string_view body, std::string_view confirm, std::string_view cancel, bool& confirmed, bool& cancelled)
{
    ui.fill(screenBounds, { 0, 0, 0, 150 });
    constexpr float DialogWidth = 200.0f;
    float bodyHeight = ui.paragraphHeight(body, TextStyle::Pixel, DialogWidth - 16.0f);
    float dialogHeight = 21.0f + bodyHeight + 16.0f + 20.0f * 2.0f + 8.0f;
    Rect frame { std::round((width - DialogWidth) * 0.5f), std::round((height - dialogHeight) * 0.5f), DialogWidth, dialogHeight };
    ui.nineSlice(frame, "ui/dialog_background_opaque");
    ui.textCentered(heading, TextStyle::Pixel, { frame.x, frame.y + 5.0f, frame.w, 12.0f }, DialogInk);
    Rect well { frame.x + 4.0f, frame.y + 20.0f, frame.w - 8.0f, frame.h - 24.0f };
    ui.fill(well, { 0, 0, 0, 230 });
    ui.paragraph(body, TextStyle::Pixel, well.x + 4.0f, well.y + 6.0f, well.w - 8.0f, White);
    float y = well.bottom() - 4.0f - 20.0f * 2.0f - 2.0f;
    confirmed = ui.classicButton("dialog:confirm", confirm, { well.x + 4.0f, y, well.w - 8.0f, 20.0f });
    cancelled = ui.classicButton("dialog:cancel", cancel, { well.x + 4.0f, y + 22.0f, well.w - 8.0f, 20.0f });
}

namespace {

void debugColumn(Context& ui, const std::vector<std::string>& lines, float width, bool alignRight)
{
    constexpr Color Backdrop { 80, 80, 80, 144 };
    constexpr Color Ink { 224, 224, 224, 255 };
    float y = 2.0f;
    for (const std::string& line : lines) {
        if (!line.empty()) {
            float w = ui.measure(line, TextStyle::Pixel);
            float x = alignRight ? width - 2.0f - w : 2.0f;
            ui.fill({ x - 1.0f, y - 1.0f, w + 2.0f, 10.0f }, Backdrop);
            ui.text(line, TextStyle::Pixel, x, y, Ink);
        }
        y += 10.0f;
    }
}

}

void Menu::gameView(Context& ui, float width, float height)
{
    if (hudToggledOff) {
        if (debugShown) {
            debugColumn(ui, debugView.left, width, false);
            debugColumn(ui, debugView.right, width, true);
        }
        return;
    }
    {
        WholeScreen whole(ui, screenBounds);
        drawNameTags(ui, hud.nameTags);
    }
    if (dialog != Dialog::None) {
        fadingHud(ui, width, height, std::chrono::steady_clock::now());
        return;
    }

    if (debugShown) {
        debugColumn(ui, debugView.left, width, false);
        debugColumn(ui, debugView.right, width, true);
    }
    drawHudScreen(ui, width, height);

    if (!hud.crosshair) {
        return;
    }
    WholeScreen whole(ui, screenBounds);
    float cx = std::floor(screenBounds.w * 0.5f - 7.5f);
    float cy = std::floor(screenBounds.h * 0.5f - 7.5f);
    ui.spriteRegion({ cx, cy, 15.0f, 15.0f }, "textures/gui/icons", { 0.0f, 0.0f, 15.0f, 15.0f }, { 255, 255, 255, 220 });
}

void Menu::setJsonUi(std::shared_ptr<const ui::JsonUi> definitions)
{
    jsonUi = std::move(definitions);
    hudScreen.reset();
    safeZoneScreen.reset();
    chatUi.reset();
    chatSettingsUi.reset();
    pauseUi.reset();
    globalPackScreens.clear();
    forms.setDefinitions(jsonUi);
    inventory.setDefinitions(jsonUi);
}

/**
 * hud_screen.json over the whole view, fed the way the game's HUD screen
 * controller feeds it, its custom renderers drawn from the HUD view.
 */
void Menu::drawHudScreen(Context& ui, float width, float height)
{
    if (!jsonUi || !hud.visible) {
        return;
    }
    if (!hudScreen) {
        hudScreen = std::make_unique<ui::JsonUiScreen>(jsonUi, "hud.hud_screen");
        hudScreen->setRenderer([this](Context& context, const std::string& renderer, const Rect& rect, float alpha, const ui::UiLookup& lookup) {
            drawHudRenderer(context, hud, renderer, rect, alpha, lookup);
        });
        shownSubtitle = hud.title.subtitleSerial;
    }
    if (hud.title.subtitleSerial != shownSubtitle) {
        shownSubtitle = hud.title.subtitleSerial;
        hudScreen->fire("anim_subtitle_text_alpha_in_play_event");
    }
    hud.chat = hudChat();
    hud.chatStyle = chatStyle();
    hud.textBackgroundOpacity = std::clamp(option("hud_text_background_opacity", 50), 0, 100) / 100.0;
    hud.actionbarBackgroundOpacity = std::clamp(option("actionbar_text_background_opacity", 50), 0, 100) / 100.0;
    hud.players = players;
    // Tab is button.scoreboard, the player list key, while nothing covers the HUD.
    hudScreen->holdButton("button.scoreboard", capturesMouse() && hud.playerList && ui.input().isHeld(Key::Tab));
    ui::UiData data = hudData(hud);
    // The HUD never takes the mouse, so the controls under it stay idle.
    bool blocked = ui.isBlocked();
    ui.setBlocked(true);
    hudScreen->draw(ui, { 0.0f, 0.0f, width, height }, data);
    hud.titleUpdates.clear();
    ui.setBlocked(blocked);
    hudScreen->takeEvents();
}

ui::Rect Menu::safeRect(float width, float height) const
{
    float insetX = std::round(width * (1.0f - safeZone) * 0.5f);
    float insetY = std::round(height * (1.0f - safeZone) * 0.5f);
    return { insetX, insetY, width - insetX * 2.0f, height - insetY * 2.0f };
}

/**
 * safe_zone_screen.json, fed the way the game's safe zone screen controller
 * feeds it. The slider reads 0.0 at the smallest area the game allows and
 * 1.0 at the whole screen, and the corners mark where the HUD will end up.
 */
void Menu::safeAreaDialog(Context& ui)
{
    if (!jsonUi) {
        dialog = Dialog::None;
        return;
    }
    if (!safeZoneScreen) {
        safeZoneScreen = std::make_unique<ui::JsonUiScreen>(jsonUi, "safe_zone.safe_zone_screen");
    }
    if (!safeZoneScreen->valid()) {
        dialog = Dialog::None;
        return;
    }
    using ui::UiValue;
    float fraction = (safeZone - MinSafeArea) / (MaxSafeArea - MinSafeArea);
    char number[16];
    std::snprintf(number, sizeof(number), "%.1f", fraction);
    WholeScreen whole(ui, screenBounds);
    float width = screenBounds.w;
    float height = screenBounds.h;
    Rect area = safeRect(width, height);

    ui::UiData data;
    ui::UiRow& globals = data.globals;
    globals["#safe_zone_all"] = UiValue::of(static_cast<double>(fraction));
    globals["#safe_zone_all_enabled"] = UiValue::of(true);
    globals["#safe_zone_all_slider_label"] = UiValue::of(tr("options.safeZone", "Safe Area") + ": " + number);
    globals["#safe_zone_all_text_value"] = UiValue::of(std::string(number));
    globals["#left_safe_zone_offset"] = UiValue::of(static_cast<double>(area.x));
    globals["#top_safe_zone_offset"] = UiValue::of(static_cast<double>(area.y));
    globals["#right_safe_zone_offset"] = UiValue::of(static_cast<double>(area.right() - width));
    globals["#bottom_safe_zone_offset"] = UiValue::of(static_cast<double>(area.bottom() - height));
    safeZoneScreen->draw(ui, { 0.0f, 0.0f, width, height }, data);

    for (const ui::UiEvent& event : safeZoneScreen->takeEvents()) {
        if (event.kind == ui::UiEvent::Kind::Slider && event.name == "safe_zone_all") {
            safeZone = MinSafeArea + static_cast<float>(event.value) * (MaxSafeArea - MinSafeArea);
        } else if (event.kind == ui::UiEvent::Kind::Button && (event.name == "button.confirm_button" || event.name == "button.menu_exit")) {
            dialog = Dialog::None;
        }
    }
}

void Menu::toast(Context& ui, float width, float height)
{
    if (toastMessage.empty() || std::chrono::steady_clock::now() > toastUntil) {
        return;
    }
    float w = std::min(ui.measure(toastMessage, TextStyle::Pixel) + 16.0f, width - 16.0f);
    Rect frame { std::round((width - w) * 0.5f), height - 40.0f, w, 20.0f };
    ui.nineSlice(frame, "ui/hud_tip_text_background", { 255, 255, 255, 230 });
    ui.textCentered(toastMessage, TextStyle::Pixel, frame, White);
}

void Menu::handleKeys(Context& ui)
{
    const InputState& input = ui.input();

    if (rebinding || rebindingMod) {
        if (input.pressedKey == Key::Escape) {
            rebinding.reset();
            rebindingMod.reset();
        } else if (input.pressedKey != Key::None) {
            if (rebinding) {
                bindings.keys[*rebinding] = input.pressedKey;
            } else if (onModKeyBind) {
                onModKeyBind(*rebindingMod, input.pressedKey);
            }
            rebinding.reset();
            rebindingMod.reset();
        }
        return;
    }

    if (input.pressedKey == Key::F11) {
        chromeAction = ChromeAction::Fullscreen;
    }
    if (input.pressedKey == Key::F1) {
        hudToggledOff = !hudToggledOff;
    }
    if (input.pressedKey == Key::F3) {
        debugShown = !debugShown;
    }
    if (inventoryInputHandled || inventory.active) return;
    if (handleChatKeys(input)) {
        return;
    }
    if (selectAllPending) {
        selectAllPending = false;
        selectedField = field;
    }
    if (field != selectedField) {
        selectedField = Field::None;
    }
    // AltGr arrives as Control plus Alt on Windows, and it types characters such as @ on AZERTY.
    if (std::string* target = focusedText(); target && input.isHeld(Key::Control) && !input.isHeld(Key::Alt)) {
        bool selected = selectedField == field;
        if (input.pressedKey == Key::A) {
            selectedField = field;
        } else if (input.pressedKey == Key::C) {
            platform::copyText(*target);
        } else if (input.pressedKey == Key::X) {
            platform::copyText(*target);
            target->clear();
            selectedField = Field::None;
        } else if (input.pressedKey == Key::V) {
            std::string pasted = platform::pasteText();
            std::u32string codepoints;
            size_t i = 0;
            while (i < pasted.size()) {
                codepoints.push_back(nextCodepoint(pasted, i));
            }
            type(codepoints);
        } else if (input.backspace) {
            if (field == Field::Chat) {
                eraseChat(true);
            } else if (selected) {
                target->clear();
            } else {
                while (!target->empty() && target->back() == ' ') {
                    target->pop_back();
                }
                while (!target->empty() && target->back() != ' ') {
                    popUtf8(*target);
                }
            }
            selectedField = Field::None;
        }
    } else {
        if (!input.text.empty()) {
            type(input.text);
        }
        if (input.backspace) {
            if (std::string* target = focusedText()) {
                if (field == Field::Chat) {
                    eraseChat(false);
                } else if (selectedField == field) {
                    target->clear();
                    selectedField = Field::None;
                } else {
                    popUtf8(*target);
                }
            }
        }
    }
    if (input.tab && screen == Screen::ServerForm) {
        field = field == Field::ServerName ? Field::ServerAddress : field == Field::ServerAddress ? Field::ServerPort : Field::ServerName;
    }
    if (input.enter && screen == Screen::ServerForm && dialog == Dialog::None) {
        saveServerForm(false);
    }
    if (input.enter && dialog == Dialog::JoinRealm) {
        if (social.realmCode.state == RealmCodeState::Found) {
            requestSocial(SocialAction::JoinRealmCode);
        } else if (social.realmCode.state != RealmCodeState::Checking && social.realmCode.state != RealmCodeState::Joining) {
            submitRealmCode();
        }
    }
    if (input.enter && socialOpen && field == Field::SocialSearch && dialog == Dialog::None) {
        socialPage = SocialPage::Search;
        socialScroll = 0.0f;
        socialSelected.clear();
        requestSocial(SocialAction::Search, socialSearch);
    }
    if (!input.escape) {
        return;
    }
    if (dialog == Dialog::JoinRealm) {
        requestSocial(SocialAction::CancelRealmCode);
        dialog = Dialog::None;
        field = Field::None;
    } else if (dialog == Dialog::ConfirmRemoveFriend) {
        removingXuid.clear();
        removingName.clear();
        dialog = Dialog::None;
        socialOpen = true;
    } else if (socialOpen && socialPage != SocialPage::Friends) {
        socialPage = SocialPage::Friends;
        socialScroll = 0.0f;
        socialSelected.clear();
    } else if (socialOpen) {
        socialOpen = false;
    } else if (dialog == Dialog::Chat) {
        closeChat();
    } else if (dialog == Dialog::Pause || dialog == Dialog::Emotes) {
        dialog = Dialog::None;
    } else if (dialog == Dialog::Death) {
        dialog = Dialog::Pause;
    } else if (dialog == Dialog::SignIn) {
        accountRequest = AccountRequest::Cancel;
        dialog = Dialog::None;
    } else if (dialog == Dialog::Connecting) {
        disconnectRequested = true;
        dialog = Dialog::None;
    } else if (dialog == Dialog::ConnectionError && errorDetailsShown) {
        errorDetailsShown = false;
        errorReasonScroll = 0.0f;
    } else if (dialog != Dialog::None) {
        dialog = Dialog::None;
    } else if (field != Field::None) {
        field = Field::None;
    } else if (screen != Screen::Title) {
        goBack();
    } else if (inGame()) {
        dialog = Dialog::Pause;
    } else {
        dialog = Dialog::ConfirmExit;
    }
}

void Menu::type(std::u32string_view text)
{
    std::string* target = focusedText();
    if (!target) {
        return;
    }
    if (selectedField == field) {
        target->clear();
        if (field == Field::Chat) chatCaret.reset();
        selectedField = Field::None;
    }
    for (char32_t cp : text) {
        if (cp < 32 || cp == 127) {
            continue;
        }
        if (target->size() >= (field == Field::Chat ? MaxChatLength : MaxFieldLength)) {
            break;
        }
        if (field == Field::ServerPort && (cp < U'0' || cp > U'9' || target->size() >= 5)) {
            continue;
        }
        if (field == Field::Chat) {
            std::string encoded;
            appendUtf8(encoded, cp);
            if (target->size() + encoded.size() > MaxChatLength) break;
            size_t caret = std::min(chatCaret.value_or(target->size()), target->size());
            target->insert(caret, encoded);
            chatCaret = caret + encoded.size();
            chatCycle.clear();
        } else {
            appendUtf8(*target, cp);
        }
    }
}

std::string* Menu::focusedText()
{
    switch (field) {
    case Field::ServerName:
        return &editName;
    case Field::ServerAddress:
        return &editAddress;
    case Field::ServerPort:
        return &editPort;
    case Field::SocialSearch:
        return &socialSearch;
    case Field::DressingSearch:
        return &dressingState.search;
    case Field::Chat:
        return dialog == Dialog::Chat && !chatSettingsOpen ? &chatDraft : nullptr;
    case Field::RealmCode:
        return dialog == Dialog::JoinRealm ? &realmCodeInput : nullptr;
    case Field::ModConfig:
        return screen == Screen::Settings ? &editModValue : nullptr;
    case Field::None:
        break;
    }
    return nullptr;
}

void Menu::navigate(Screen target)
{
    titleModelDragging = false;
    if (target != screen) {
        leavingScreen = screen;
        screenChanged = std::chrono::steady_clock::now();
        screenDirection = 1.0f;
    }
    screen = target;
    field = Field::None;
    listScroll = 0.0f;
    detailScroll = 0.0f;
    pageScroll = 0.0f;
    rebinding.reset();
    rebindingMod.reset();
}

void Menu::openScreen(Screen target)
{
    if (dialog == Dialog::Pause || dialog == Dialog::Chat || dialog == Dialog::ConfirmExit) {
        if (dialog == Dialog::Chat) {
            closeChat();
        }
        dialog = Dialog::None;
    }
    socialOpen = false;
    switch (target) {
    case Screen::ServerForm:
        openServerForm(std::nullopt);
        return;
    case Screen::Settings:
    case Screen::DressingRoom:
        returnScreen = Screen::Title;
        break;
    default:
        break;
    }
    navigate(target);
}

bool Menu::showDialog(Dialog which)
{
    switch (which) {
    case Dialog::None:
        if (dialog == Dialog::Chat) {
            closeChat();
        }
        dialog = Dialog::None;
        socialOpen = false;
        return true;
    case Dialog::Pause:
        if (!inGame()) {
            return false;
        }
        dialog = Dialog::Pause;
        return true;
    case Dialog::Chat:
        if (!inGame() || screen != Screen::Title) {
            return false;
        }
        openChat({});
        return true;
    case Dialog::ConfirmExit:
    case Dialog::SafeArea:
    case Dialog::ProfileOptions:
        dialog = which;
        return true;
    case Dialog::RealmInvites:
        dialog = which;
        requestSocial(SocialAction::RefreshInvites);
        return true;
    case Dialog::JoinRealm:
        openJoinRealm();
        return true;
    default:
        return false;
    }
}

bool Menu::openInventory()
{
    if (!capturesMouse() || session.dead || session.loadingTerrain || session.gameMode == "Spectator") {
        return false;
    }
    inventory.open();
    inventory.requestOpen();
    return true;
}

std::vector<std::string> Menu::chatLog() const
{
    std::vector<std::string> lines;
    lines.reserve(chatLines.size());
    for (const ChatLine& line : chatLines) {
        lines.push_back(line.text);
    }
    return lines;
}

void Menu::goBack()
{
    if (screen == Screen::ServerForm) {
        navigate(Screen::Play);
    } else if (screen == Screen::Settings || screen == Screen::DressingRoom || screen == Screen::Marketplace || screen == Screen::Profile) {
        navigate(returnScreen);
        if (inGame()) {
            dialog = Dialog::Pause;
        }
    } else {
        navigate(Screen::Title);
    }
    screenDirection = -1.0f;
}

void Menu::connect(const ServerRow& row)
{
    if (row.group == ServerGroup::Saved) {
        store.markJoined(row.index);
    }
    pending = ConnectRequest { row.name, row.address };
}

void Menu::openServerForm(std::optional<size_t> index)
{
    editing = index;
    editName.clear();
    editAddress.clear();
    editPort = "19132";
    if (index && *index < store.servers().size()) {
        const SavedServer& server = store.servers()[*index];
        editName = server.name;
        editAddress = server.address;
        size_t colon = editAddress.rfind(':');
        if (colon != std::string::npos && editAddress.find(':') == colon) {
            editPort = editAddress.substr(colon + 1);
            editAddress.resize(colon);
        }
    } else {
        editing.reset();
    }
    navigate(Screen::ServerForm);
    field = Field::ServerName;
}

bool Menu::saveServerForm(bool andPlay)
{
    std::string address = editAddress;
    if (!editPort.empty() && address.find(':') == std::string::npos) {
        address += ":" + editPort;
    }
    if (normalizeAddress(address).empty()) {
        notify("Enter a server address");
        field = Field::ServerAddress;
        return false;
    }
    std::string name = editName.empty() ? editAddress : editName;
    size_t index = 0;
    if (editing) {
        store.update(*editing, name, address);
        index = *editing;
    } else {
        index = store.add(name, address);
    }
    selection = Selection { ServerGroup::Saved, index };
    playTab = PlayTab::Servers;
    navigate(Screen::Play);
    if (andPlay) {
        const SavedServer& server = store.servers()[index];
        connect({ ServerGroup::Saved, index, server.name, server.address, {}, {} });
    }
    return true;
}

}
