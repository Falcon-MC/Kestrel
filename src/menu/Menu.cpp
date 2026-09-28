#include "menu/Menu.h"

#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"
#include "ui/Utf8.h"

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
constexpr float ScreenTransitionSeconds = 0.25f;
constexpr float DialogTransitionSeconds = 0.15f;
constexpr float ScreenSlide = 24.0f;
constexpr Color DialogInk { 0x4c, 0x4c, 0x4c, 255 };
constexpr Color Backing { 0, 0, 0, 150 };

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
    return screen == Screen::Title ? 0.0f : 48.0f;
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

void Menu::screenContent(Context& ui, float width, float height)
{
    switch (screen) {
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
        todoScreen(ui, width, height, tr("profileScreen.header", "Dressing Room"));
        break;
    case Screen::Profile:
        profile(ui, width, height);
        break;
    }
}

void Menu::frame(Context& ui, float width, float height)
{
    if (ui.input().mousePressed) {
        field = Field::None;
        rebinding.reset();
    }
    if (inGame() && session.dead) {
        if (dialog == Dialog::None && screen == Screen::Title) {
            dialog = Dialog::Death;
        }
    } else if (dialog == Dialog::Death) {
        dialog = Dialog::None;
    }

    if (inventory.active) {
        ui.setBlocked(false);
        inventory.draw(ui, width, height, [&](float x, float y, float pixel) { playerModel(ui, x, y, pixel, true); });
        return;
    }
    if (!inGame() && forms.active()) {
        forms.closeAll();
    }
    if (forms.active()) {
        ui.setBlocked(false);
        forms.draw(ui, width, height);
        return;
    }

    bool modal =dialog != Dialog::None || socialOpen;
    ui.setBlocked(modal || capturesMouse());

    if (!worldVisible()) {
        panorama(ui, width, height);
    }

    auto now = std::chrono::steady_clock::now();
    if (dialog != shownDialog) {
        shownDialog = dialog;
        dialogChanged = now;
    }
    if (socialOpen != socialShown) {
        socialShown = socialOpen;
        socialChanged = now;
    }
    auto eased = [&](std::chrono::steady_clock::time_point since, float seconds) {
        float t = std::clamp(std::chrono::duration<float>(now - since).count() / seconds, 0.0f, 1.0f);
        return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    };

    bool loading = !inGame() && (dialog == Dialog::Connecting || dialog == Dialog::ConnectionError || dialog == Dialog::SignIn);
    if (!loading) {
        float shown = inGame() && screen == Screen::Title ? 1.0f : eased(screenChanged, ScreenTransitionSeconds);
        ui.setLayer((1.0f - shown) * ScreenSlide * screenDirection, 0.0f, shown);
        screenContent(ui, width, height);
        ui.clearLayer();
    }

    ui.setBlocked(false);
    if (socialOpen && !loading) {
        float shown = eased(socialChanged, ScreenTransitionSeconds);
        ui.setLayer((1.0f - shown) * 190.0f, 0.0f, shown);
        socialDrawer(ui, width, height);
        ui.clearLayer();
    }

    bool confirmed = false;
    bool cancelled = false;
    float dialogShown = eased(dialogChanged, DialogTransitionSeconds);
    ui.setLayer(0.0f, (1.0f - dialogShown) * 8.0f, dialogShown);
    switch (dialog) {
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
    }
    ui.clearLayer();
    if (confirmed || cancelled) {
        dialog = Dialog::None;
    }

    if (inGame() && session.changingDimension) {
        dimensionScreen(ui, width, height);
    }
    toast(ui, width, height);
    handleKeys(ui);
}

void Menu::panorama(Context& ui, float width, float height)
{
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
}

void Menu::logo(Context& ui, float centerX, float y, float maxWidth)
{
    // A server that ships its own title art gets it instead of the game logo, as in game.
    const Sprite& art = ui.skin().sprite("dynamic/title");
    if (art.valid && session.status != SessionStatus::Idle) {
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

// Kestrel doesn't have the player's skin, so the default Steve stands in, seen from the front.
// Each part is its front face from the 64x64 skin layout, overlay layer on top.
void Menu::playerModel(Context& ui, float centerX, float top, float pixel, bool inventoryPreview)
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
    std::string_view Skin = inventoryPreview && ui.skin().sprite("dynamic/inventory_skin").valid ? "dynamic/inventory_skin" : "textures/entity/steve";
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
    auto transform = [&](Vec p, bool head) -> Vec {
        if (head) {
            p[1] -= NeckY;
            p = yaw(pitch(p, headPitch), headYaw - bodyYaw);
            p[1] += NeckY;
        }
        return yaw(p, bodyYaw);
    };

    struct Face {
        std::array<std::array<float, 2>, 4> points;
        std::array<std::array<float, 2>, 4> texels;
        float depth;
        float light;
    };
    std::vector<Face> faces;
    auto addBox = [&](const Part& part, const std::array<float, 2>& uv, float inflate) {
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
        const Side sides[6] = {
            { { { { x0, y1, z1 }, { x1, y1, z1 }, { x1, y0, z1 }, { x0, y0, z1 } } }, { u + d, v + d, w, h }, { 0.0f, 0.0f, 1.0f } },
            { { { { x1, y1, z0 }, { x0, y1, z0 }, { x0, y0, z0 }, { x1, y0, z0 } } }, { u + d + w + d, v + d, w, h }, { 0.0f, 0.0f, -1.0f } },
            { { { { x0, y1, z0 }, { x0, y1, z1 }, { x0, y0, z1 }, { x0, y0, z0 } } }, { u, v + d, d, h }, { -1.0f, 0.0f, 0.0f } },
            { { { { x1, y1, z1 }, { x1, y1, z0 }, { x1, y0, z0 }, { x1, y0, z1 } } }, { u + d + w, v + d, d, h }, { 1.0f, 0.0f, 0.0f } },
            { { { { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } } }, { u + d, v, w, d }, { 0.0f, 1.0f, 0.0f } },
            { { { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y0, z0 }, { x0, y0, z0 } } }, { u + d + w, v, w, d }, { 0.0f, -1.0f, 0.0f } },
        };
        for (const Side& side : sides) {
            Vec normal = transform(side.normal, part.head);
            Vec origin = transform({ 0.0f, 0.0f, 0.0f }, part.head);
            Vec facing { normal[0] - origin[0], normal[1] - origin[1], normal[2] - origin[2] };
            if (facing[2] <= 0.0f) {
                continue;
            }
            Face face;
            float depth = 0.0f;
            for (size_t corner = 0; corner < 4; ++corner) {
                Vec p = transform(side.corners[corner], part.head);
                face.points[corner] = { centerX + p[0] * pixel, top + (32.0f - p[1]) * pixel };
                depth += p[2];
            }
            const std::array<float, 4>& r = side.region;
            face.texels = { { { r[0], r[1] }, { r[0] + r[2], r[1] }, { r[0] + r[2], r[1] + r[3] }, { r[0], r[1] + r[3] } } };
            face.depth = depth * 0.25f + inflate;
            face.light = 0.6f + 0.4f * std::clamp(facing[2] * 0.8f + facing[1] * 0.4f, 0.0f, 1.0f);
            faces.push_back(face);
        }
    };
    for (const Part& part : Parts) {
        addBox(part, part.uv, 0.0f);
        addBox(part, part.overlay, part.head ? 0.5f : 0.25f);
    }
    std::stable_sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) {
        return a.depth < b.depth;
    });
    for (const Face& face : faces) {
        uint8_t shade = static_cast<uint8_t>(std::clamp(face.light, 0.0f, 1.0f) * 255.0f);
        ui.spriteQuad(face.points, Skin, face.texels, { shade, shade, shade, 255 });
    }
}

void Menu::title(Context& ui, float width, float height)
{
    logo(ui, width * 0.5f, 80.0f, width - 32.0f);
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
        bool customLogo = serverArt.valid && session.status != SessionStatus::Idle;
        float logoWidth = std::min(width - 32.0f, customLogo ? 263.0f : 378.5f);
        const Sprite& art = customLogo ? serverArt : ui.skin().sprite("kestrel/title");
        float logoHeight = art.valid ? logoWidth * art.height / art.width : 40.0f;
        float textHeight = (1.0f + static_cast<float>(std::count(text.begin(), text.end(), '\n'))) * 9.0f;
        float halfWidth = ((widest + 2.0f) * 0.93969262f + (textHeight + 2.0f) * 0.34202014f) * magnify * 0.5f;
        float centerX = std::min(width * 0.5f + logoWidth * 0.46f, width - 8.0f - halfWidth);
        ui.rotatedPixelText(text, centerX, 80.0f + logoHeight * 0.65f, magnify, -0.34906585f, { 255, 255, 0, 255 });
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

    if (iconButton(ui, "title:social", tr("options.social", "Social") + " (0)", "ui/FriendsIcon", { width - 1.0f - 80.0f, 1.0f, 80.0f, CornerButtonHeight }, 9.0f)) {
        socialOpen = true;
        socialParty = false;
    }

    constexpr float CornerMargin = 2.0f;
    float labelY = std::floor(height - CornerMargin - 9.0f);
    float cornerLeft = CornerMargin - 1.0f;
    float bottom = std::floor(labelY - 1.0f - CornerMargin - CornerButtonHeight);
    if (iconButton(ui, "title:inbox", "", "ui/mail_icon", { cornerLeft, bottom, 23.0f, CornerButtonHeight }, 15.0f)) {
        notify("TODO: Inbox");
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

    float dressingX = width - 158.0f;
    float dressingY = height - 96.33f;
    if (ui.classicButton("title:dressing", tr("profileScreen.header", "Dressing Room"), { dressingX, dressingY, 82.0f, CornerButtonHeight })) {
        navigate(Screen::DressingRoom);
    }
    float nameWidth = ui.measure(displayName, TextStyle::Pixel);
    backedLabel(ui, displayName, std::floor(dressingX + 41.0f - nameWidth * 0.5f), std::floor(dressingY - 97.67f));
    playerModel(ui, dressingX + 41.0f, dressingY - 81.67f, 2.23f);

    backedLabel(ui, "Kestrel", CornerMargin, labelY);
    constexpr std::string_view Version = "v1.26.51";
    backedLabel(ui, Version, std::floor(width - CornerMargin - ui.measure(Version, TextStyle::Pixel)), labelY);
}

void Menu::pause(Context& ui, float width, float height)
{
    ui.fill({ 0.0f, 0.0f, width, height }, { 0, 0, 0, 90 });
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

    if (iconButton(ui, "pause:social", tr("options.social", "Social") + " (0)", "ui/FriendsIcon", { width - 1.0f - 80.0f, 1.0f, 80.0f, CornerButtonHeight }, 9.0f)) {
        socialOpen = true;
    }
    float dressingX = width - 158.0f;
    if (ui.classicButton("pause:dressing", tr("profileScreen.header", "Dressing Room"), { dressingX, y + 119.0f, 82.0f, CornerButtonHeight })) {
        notify("TODO: Dressing Room");
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
void Menu::dimensionScreen(Context& ui, float width, float height)
{
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
    ui.fill({ 0.0f, 0.0f, width, height }, { 110, 0, 0, 120 });
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
    ui.fill({ 0.0f, 0.0f, width, height }, { 0, 0, 0, 150 });
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

float debugColumn(Context& ui, const std::vector<std::string>& lines, float width, bool alignRight)
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
    return y;
}

}

void Menu::gameView(Context& ui, float width, float height)
{
    drawNameTags(ui, hud.nameTags);
    if (dialog != Dialog::None) {
        return;
    }

    float y = 2.0f;
    if (debugShown) {
        y = debugColumn(ui, debugView.left, width, false);
        debugColumn(ui, debugView.right, width, true);
    }

    drawHud(ui, hud, 0.0f, 0.0f, width, height);
    if (hudUi && hud.sidebarVisible) {
        hudUi->draw(ui, "scoreboard.scoreboard_sidebar", { 0.0f, 0.0f, width, height }, hud.sidebar);
    }
    chatFeed(ui, y, width, height);

    float cx = std::floor(width * 0.5f - 7.5f);
    float cy = std::floor(height * 0.5f - 7.5f);
    ui.spriteRegion({ cx, cy, 15.0f, 15.0f }, "textures/gui/icons", { 0.0f, 0.0f, 15.0f, 15.0f }, { 255, 255, 255, 220 });
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

    if (rebinding) {
        if (input.pressedKey == Key::Escape) {
            rebinding.reset();
        } else if (input.pressedKey != Key::None) {
            bindings.keys[*rebinding] = input.pressedKey;
            rebinding.reset();
        }
        return;
    }

    if (input.pressedKey == Key::F11) {
        chromeAction = ChromeAction::Fullscreen;
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
    if (std::string* target = focusedText(); target && input.isHeld(Key::Control)) {
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
            if (selected) {
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
                if (selectedField == field) {
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
    if (!input.escape) {
        return;
    }
    if (socialOpen) {
        socialOpen = false;
    } else if (dialog == Dialog::Chat) {
        closeChat();
    } else if (dialog == Dialog::Pause) {
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
        appendUtf8(*target, cp);
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
    case Field::Chat:
        return dialog == Dialog::Chat ? &chatDraft : nullptr;
    case Field::None:
        break;
    }
    return nullptr;
}

void Menu::navigate(Screen target)
{
    if (target != screen) {
        screenChanged = std::chrono::steady_clock::now();
        screenDirection = 1.0f;
    }
    screen = target;
    field = Field::None;
    listScroll = 0.0f;
    detailScroll = 0.0f;
    pageScroll = 0.0f;
    rebinding.reset();
}

void Menu::goBack()
{
    if (screen == Screen::ServerForm) {
        navigate(Screen::Play);
    } else if (screen == Screen::Settings) {
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
