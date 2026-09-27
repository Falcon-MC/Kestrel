#include "menu/Menu.h"

#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/Theme.h"
#include "ui/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr size_t MaxFieldLength = 96;
constexpr float TitleButtonWidth = 148.0f;
constexpr float TitleButtonHeight = 30.0f;
constexpr float TitleButtonStep = 32.0f;
constexpr float CornerButtonHeight = 24.0f;
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
    return inGame() && dialog == Dialog::None && screen == Screen::Title && !socialOpen;
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
        dialog = Dialog::ConnectionError;
        break;
    case SessionStatus::Idle:
        if (dialog == Dialog::Connecting || dialog == Dialog::Pause) {
            dialog = Dialog::None;
        }
        if (previous == SessionStatus::Joined) {
            navigate(Screen::Title);
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

    bool modal = dialog != Dialog::None || socialOpen;
    ui.setBlocked(modal || capturesMouse());

    if (!worldVisible()) {
        panorama(ui, width, height);
    }

    switch (screen) {
    case Screen::Title:
        if (inGame()) {
            gameView(ui, width, height);
        } else if (dialog != Dialog::Connecting && dialog != Dialog::ConnectionError) {
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
        todoScreen(ui, width, height, "Marketplace");
        break;
    case Screen::DressingRoom:
        todoScreen(ui, width, height, "Dressing Room");
        break;
    case Screen::Profile:
        todoScreen(ui, width, height, "Profile");
        break;
    }

    ui.setBlocked(false);
    if (socialOpen) {
        socialDrawer(ui, width, height);
    }

    bool confirmed = false;
    bool cancelled = false;
    switch (dialog) {
    case Dialog::None:
        break;
    case Dialog::Pause:
        pause(ui, width, height);
        break;
    case Dialog::Connecting:
    case Dialog::ConnectionError:
    case Dialog::SignIn:
        progressDialog(ui, width, height);
        break;
    case Dialog::ConfirmDelete: {
        std::optional<ServerRow> row = selectedRow();
        messageDialog(ui, width, height, "Delete Server", "Are you sure you want to delete " + (row ? "\"" + row->name + "\"" : std::string("this server")) + "?", "Delete", "Cancel", confirmed, cancelled);
        if (confirmed && row && !row->featured) {
            store.remove(row->index);
            selection.reset();
            navigate(Screen::Play);
        }
        break;
    }
    case Dialog::ConfirmExit:
        messageDialog(ui, width, height, "Quit Game", "Are you sure you want to quit Kestrel?", "Quit", "Cancel", confirmed, cancelled);
        if (confirmed) {
            quit = true;
        }
        break;
    }
    if (confirmed || cancelled) {
        dialog = Dialog::None;
    }

    toast(ui, width, height);
    handleKeys(ui);
}

void Menu::panorama(Context& ui, float width, float height)
{
    // The classic title background is a cube map turning slowly. Its four side faces laid out
    // in a row and scrolled sideways give the same drift without a 3D pass.
    float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt).count();
    float face = std::max(width, height);
    float strip = face * 4.0f;
    float offset = std::fmod(seconds * face / 60.0f, strip);
    float y = (height - face) * 0.5f;
    for (int i = 0; i < 5; ++i) {
        int index = i % 4;
        float x = static_cast<float>(i) * face - offset;
        if (x < width && x + face > 0.0f) {
            ui.sprite({ x, y, face, face }, "ui/panorama_" + std::to_string(index));
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
    const Sprite& word = ui.skin().sprite("ui/title");
    if (!word.valid) {
        return;
    }
    float w = std::min(maxWidth, 378.5f);
    ui.sprite({ centerX - w * 0.5f, y, w, w * word.height / word.width }, "ui/title");
}

// Kestrel doesn't have the player's skin, so the default Steve stands in, seen from the front.
// Each part is its front face from the 64x64 skin layout, overlay layer on top.
void Menu::playerModel(Context& ui, float centerX, float top, float pixel)
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
    constexpr std::string_view Skin = "textures/entity/steve";
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

    float x = std::floor((width - TitleButtonWidth) * 0.5f);
    float y = std::round(height * 0.5f + 13.67f);
    if (ui.classicButton("title:play", "Play", { x, y, TitleButtonWidth, TitleButtonHeight })) {
        navigate(Screen::Play);
    }
    if (ui.classicButton("title:settings", "Settings", { x, y + TitleButtonStep, TitleButtonWidth, TitleButtonHeight })) {
        returnScreen = Screen::Title;
        navigate(Screen::Settings);
    }
    if (ui.classicButton("title:marketplace", "Marketplace", { x, y + TitleButtonStep * 2.0f, TitleButtonWidth, TitleButtonHeight })) {
        navigate(Screen::Marketplace);
    }

    if (iconButton(ui, "title:social", "Social (0)", "ui/FriendsIcon", { width - 48.0f - 80.0f, 29.0f, 80.0f, CornerButtonHeight }, 9.0f)) {
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
    if (iconButton(ui, "title:profile", "Profile", avatar.valid ? "dynamic/avatar" : "ui/profile_glyph_color", { cornerLeft + 31.0f, bottom, 63.0f, CornerButtonHeight }, 18.0f)) {
        navigate(Screen::Profile);
    }

    float dressingX = width - 158.0f;
    float dressingY = height - 96.33f;
    if (ui.classicButton("title:dressing", "Dressing Room", { dressingX, dressingY, 82.0f, CornerButtonHeight })) {
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

    if (ui.classicButton("pause:resume", "Resume Game", { x, y, ButtonWidth, ButtonHeight })) {
        dialog = Dialog::None;
    }
    if (ui.classicButton("pause:settings", "Settings", { x, y + Step, ButtonWidth, ButtonHeight })) {
        dialog = Dialog::None;
        returnScreen = Screen::Title;
        navigate(Screen::Settings);
    }
    if (ui.classicButton("pause:quit", "Save & Quit", { x, y + Step * 2.0f, ButtonWidth, ButtonHeight })) {
        dialog = Dialog::None;
        disconnectRequested = true;
    }

    if (iconButton(ui, "pause:social", "Social (0)", "ui/FriendsIcon", { width - 48.0f - 80.0f, 29.0f, 80.0f, CornerButtonHeight }, 9.0f)) {
        socialOpen = true;
    }
    float dressingX = width - 158.0f;
    if (ui.classicButton("pause:dressing", "Dressing Room", { dressingX, y + 119.0f, 82.0f, CornerButtonHeight })) {
        notify("TODO: Dressing Room");
    }
    float nameWidth = ui.measure(displayName, TextStyle::Pixel);
    backedLabel(ui, displayName, std::floor(dressingX + 41.0f - nameWidth * 0.5f), y - 25.67f);
    playerModel(ui, dressingX + 41.0f, y - 13.0f, 4.06f);
}

void Menu::progressDialog(Context& ui, float width, float height)
{
    constexpr float DialogWidth = 286.67f;
    constexpr float DialogHeight = 97.33f;
    Rect frame { std::round((width - DialogWidth) * 0.5f), std::round(height * 0.5f - 49.0f), DialogWidth, DialogHeight };
    logo(ui, width * 0.5f, frame.y - 145.33f, width - 32.0f);

    std::string heading;
    std::string body;
    std::string button = "Cancel";
    bool progress = false;
    float fraction = -1.0f;
    bool twoButtons = false;

    if (dialog == Dialog::SignIn) {
        switch (account.status) {
        case AccountStatus::AwaitingCode:
            heading = "Sign in with a Microsoft account";
            body = "Go to " + account.verificationUri + " and enter the code " + account.userCode;
            twoButtons = true;
            break;
        case AccountStatus::Failed:
            heading = "Sign-in failed";
            body = account.error;
            button = "Try Again";
            twoButtons = true;
            break;
        default:
            heading = "Signing in";
            progress = true;
            break;
        }
    } else if (dialog == Dialog::ConnectionError) {
        heading = session.status == SessionStatus::Disconnected ? "Disconnected from Server" : "Unable to connect to world";
        body = session.error.empty() ? "The connection was closed." : session.error;
        button = "OK";
    } else if (session.packPrompt) {
        heading = "Resource Packs Required";
        std::string count = session.packCount == 1 ? "1 resource pack" : std::to_string(session.packCount) + " resource packs";
        body = "This server uses " + count + " (" + megabytes(session.packBytes) + "). Download them?";
        button = "Download";
        twoButtons = true;
    } else if (session.packDownloading) {
        heading = "Downloading packs (" + megabytes(session.packReceived) + " / " + megabytes(session.packTotal) + ")";
        fraction = session.packTotal ? std::clamp(static_cast<float>(session.packReceived) / static_cast<float>(session.packTotal), 0.0f, 1.0f) : 0.0f;
    } else {
        heading = session.status == SessionStatus::Resolving ? "Locating server" : session.loadingTerrain ? "Generating world" : "Connecting to online experience";
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
    if (progress) {
        const Sprite& bar = ui.skin().sprite("ui/loading_bar");
        if (bar.valid) {
            float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt).count();
            int frames = std::max(1, static_cast<int>(bar.width / 64.0f));
            int current = static_cast<int>(seconds * 10.0f) % frames;
            ui.spriteRegion({ std::floor(well.x + (well.w - 64.0f) * 0.5f), buttonY - 14.0f, 64.0f, 8.0f }, "ui/loading_bar", { current * 64.0f, 0.0f, 64.0f, bar.height });
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
        if (ui.classicButton("signin:cancel", "Cancel", cancel)) {
            if (account.status != AccountStatus::Failed) {
                accountRequest = AccountRequest::Cancel;
            }
            dialog = Dialog::None;
        }
        return;
    }
    if (dialog == Dialog::ConnectionError) {
        if (ui.classicButton("error:ok", button, primary)) {
            dialog = Dialog::None;
        }
        return;
    }
    if (session.packPrompt) {
        if (ui.classicButton("packs:download", button, primary)) {
            packAnswer = true;
        }
        if (ui.classicButton("packs:skip", "Skip", secondary)) {
            packAnswer = false;
        }
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

void Menu::gameView(Context& ui, float width, float height)
{
    std::vector<std::string> lines {
        session.levelName.empty() ? session.name : session.levelName,
        cameraInfo,
        "Looking at " + (session.targetBlock.empty() ? std::string("nothing") : session.targetBlock),
        "Chunks " + std::to_string(session.columns) + ", sub-chunks " + std::to_string(session.subChunks) + ", pending " + std::to_string(session.pendingSubChunks),
        "Meshes " + std::to_string(session.meshes) + ", quads " + std::to_string(session.meshQuads) + ", jobs " + std::to_string(session.meshJobs),
        "Textures " + std::to_string(session.textureLayers) + ", decode errors " + std::to_string(session.worldErrors),
        session.registryInfo,
    };
    if (!session.assetsError.empty()) {
        lines.push_back(session.assetsError);
    }
    if (!session.lastWorldError.empty()) {
        lines.push_back(session.lastWorldError);
    }
    float y = 2.0f;
    for (const std::string& line : lines) {
        if (!line.empty()) {
            ui.fill({ 1.0f, y - 1.0f, ui.measure(line, TextStyle::Pixel) + 2.0f, 10.0f }, { 0, 0, 0, 110 });
            ui.text(line, TextStyle::Pixel, 2.0f, y, White);
        }
        y += 10.0f;
    }

    drawHud(ui, hud, 0.0f, 0.0f, width, height);

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
    if (!input.text.empty()) {
        type(input.text);
    }
    if (input.backspace) {
        if (std::string* target = focusedText()) {
            popUtf8(*target);
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
    } else if (dialog == Dialog::Pause) {
        dialog = Dialog::None;
    } else if (dialog == Dialog::SignIn) {
        accountRequest = AccountRequest::Cancel;
        dialog = Dialog::None;
    } else if (dialog == Dialog::Connecting) {
        disconnectRequested = true;
        dialog = Dialog::None;
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
    for (char32_t cp : text) {
        if (target->size() >= MaxFieldLength) {
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
    case Field::None:
        break;
    }
    return nullptr;
}

void Menu::navigate(Screen target)
{
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
}

void Menu::connect(const ServerRow& row)
{
    if (!row.featured) {
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
    selection = Selection { false, index };
    playTab = PlayTab::Servers;
    navigate(Screen::Play);
    if (andPlay) {
        const SavedServer& server = store.servers()[index];
        connect({ false, index, server.name, server.address, {} });
    }
    return true;
}

}
