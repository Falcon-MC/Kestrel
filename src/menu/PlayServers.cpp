#include "menu/Menu.h"
#include "menu/PlayLayout.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;
using namespace play;

namespace {

constexpr float ListWidth = 197.0f;
constexpr float ServerIconSize = css(40.0f);
constexpr float PingIconSize = css(20.0f);
constexpr float PlayerIconSize = css(24.0f);
constexpr float ImageBarHeight = css(60.0f);
constexpr float ImageBarPadding = css(24.0f);
constexpr Color ImageBarBackground { 0, 0, 0, 179 };
constexpr float PlayBarPaddingX = css(24.0f);
constexpr float PlayBarPaddingY = css(8.0f);
constexpr float PlayMaxWidth = css(320.0f);
constexpr float ShowButtonMaxWidth = css(120.0f);
constexpr float ShowButtonMargin = css(16.0f);
constexpr float EditPaddingX = css(64.0f);
constexpr float EditPaddingY = css(16.0f);
constexpr float ActivityImageSize = css(152.0f);
constexpr float TagHeight = css(20.0f);
constexpr float TagPadding = css(4.0f);
constexpr float BorderWidth = css(2.0f);
constexpr float PingFrameWidth = 16.0f;
constexpr float PingFrameHeight = 20.0f;
constexpr int PingFrames = 6;
constexpr int PingAnimationMs = 700;
constexpr int MediumPingMs = 150;
constexpr int HighPingMs = 300;

/**
 * The game's spacing component: every step is 0.4rem.
 */
constexpr float spacing(int size)
{
    return css(4.0f) * static_cast<float>(size);
}

/**
 * The game only knows low, medium and high from its native pinger; anything
 * else, including a server that never answered, shows the loading animation.
 */
enum class PingStatus {
    Loading,
    Low,
    Medium,
    High,
};

/**
 * The thresholds are Kestrel's own: the game computes the status natively
 * and its values are not visible from the UI bundle.
 */
PingStatus pingStatusOf(const ServerStatus* status)
{
    if (!status || !status->online || status->latencyMs < 0) {
        return PingStatus::Loading;
    }
    if (status->latencyMs < MediumPingMs) {
        return PingStatus::Low;
    }
    return status->latencyMs < HighPingMs ? PingStatus::Medium : PingStatus::High;
}

std::string pingLabel(PingStatus status)
{
    switch (status) {
    case PingStatus::Low:
        return tr("hbui.PlayScreen.ServerPing.lowPing", "Low ping");
    case PingStatus::Medium:
        return tr("hbui.PlayScreen.ServerPing.mediumPing", "Medium ping");
    case PingStatus::High:
        return tr("hbui.PlayScreen.ServerPing.highPing", "High ping");
    case PingStatus::Loading:
        break;
    }
    return tr("hbui.PlayScreen.ServerPing.loadingPing", "Loading ping");
}

/**
 * The ping icon, or the six frame loading strip, followed by its label.
 * Returns the width it took.
 */
float pingIndicator(Context& ui, float x, float centerY, PingStatus status)
{
    float top = std::round(centerY - PingIconSize * 0.5f);
    if (status == PingStatus::Loading) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        int frame = static_cast<int>((elapsed % PingAnimationMs) * PingFrames / PingAnimationMs);
        Rect strip { x + BorderWidth, top, css(PingFrameWidth), css(PingFrameHeight) };
        ui.spriteRegion(strip, "hbui/pingAnimation", { static_cast<float>(frame) * PingFrameWidth, 0.0f, PingFrameWidth, PingFrameHeight });
    } else {
        const char* icon = status == PingStatus::Low ? "hbui/pingGreen" : status == PingStatus::Medium ? "hbui/pingYellow" : "hbui/pingRed";
        ui.sprite({ x, top, PingIconSize, PingIconSize }, icon);
    }
    std::string label = pingLabel(status);
    float textX = x + PingIconSize + spacing(1);
    ui.text(label, TextStyle::Ui, textX, std::round(centerY - ui.lineHeight(TextStyle::Ui) * 0.5f), Muted1);
    return textX + ui.measure(label, TextStyle::Ui) - x;
}

/**
 * The player icon and count, with the red Full tag once the server reports
 * as many players as its capacity.
 */
void playerCount(Context& ui, float x, float centerY, float maxWidth, const ServerStatus* status)
{
    int players = status ? status->players : 0;
    int capacity = status ? status->maxPlayers : 0;
    ui.sprite({ x, std::round(centerY - PlayerIconSize * 0.5f), PlayerIconSize, PlayerIconSize }, "hbui/player-online-icon");
    std::string count = std::to_string(players);
    float textX = x + PlayerIconSize + spacing(1);
    float textY = std::round(centerY - ui.lineHeight(TextStyle::Ui) * 0.5f);
    ui.text(count, TextStyle::Ui, textX, textY, Muted1, std::max(1.0f, maxWidth - (textX - x)));
    if (players == 0 || players != capacity) {
        return;
    }
    std::string full = tr("hbui.PlayScreen.serverCapacity.fullLabel", "Full");
    float tagHeight = TagHeight + BorderWidth * 2.0f;
    Rect tag { textX + ui.measure(count, TextStyle::Ui) + spacing(2), std::round(centerY - tagHeight * 0.5f), ui.measure(full, TextStyle::Body) + (TagPadding + BorderWidth) * 2.0f, tagHeight };
    if (tag.right() > x + maxWidth) {
        return;
    }
    ui.fill(tag, DestructiveTint);
    ui.textCentered(full, TextStyle::Body, tag, InkDark);
}

/**
 * The yellow notice banner the game puts above a tab's content.
 */
float noticeBanner(Context& ui, float x, float y, float width, std::string_view text)
{
    float height = ui.paragraphHeight(text, TextStyle::Body, width - 16.0f) + 8.0f;
    ui.fill({ x, y, width, height }, NoticeTint);
    ui.paragraph(text, TextStyle::Body, x + 8.0f, y + 4.0f, width - 16.0f, InkDark);
    return height;
}

}

void Menu::serversTab(Context& ui, const Rect& area)
{
    bool locked = !signedIn();
    Rect body = area;
    if (locked) {
        std::string message = tr("hbui.notLoggedInWarning.playScreen.message.servers", "You need a Microsoft account to play on servers.");
        std::string link = tr("hbui.notLoggedInWarning.loginLink", "Log in or sign up");
        float textHeight = ui.paragraphHeight(message, TextStyle::Body, area.w - 16.0f);
        float linkHeight = ui.lineHeight(TextStyle::Body);
        Rect banner { area.x, area.y + spacing(2), area.w, textHeight + linkHeight + 10.0f };
        ui.fill(banner, NoticeTint);
        ui.paragraph(message, TextStyle::Body, banner.x + 8.0f, banner.y + 4.0f, banner.w - 16.0f, InkDark);
        Rect linkBounds { banner.x + 8.0f, banner.y + 6.0f + textHeight, ui.measure(link, TextStyle::Body), linkHeight };
        Interaction linkState = ui.interact("servers:signin", linkBounds);
        ui.text(link, TextStyle::Body, linkBounds.x, linkBounds.y, InkDark);
        ui.fill({ linkBounds.x, linkBounds.bottom() - BorderWidth, linkBounds.w, BorderWidth }, InkDark);
        if (linkState.clicked) {
            beginSignIn();
        }
        body.y = banner.bottom() + spacing(2);
        body.h = std::max(0.0f, area.bottom() - body.y);
    }

    Rect list { body.x, body.y, ListWidth, body.h };
    Rect detail { list.right() + 13.0f, body.y, body.right() - list.right() - 20.0f, body.h };
    ui.fill(list, NeutralAlpha60);

    std::vector<ServerRow> featuredList = featuredRows(ServerGroup::Featured);
    std::vector<ServerRow> creatorList = featuredRows(ServerGroup::Creator);
    std::vector<ServerRow> saved = savedRows();
    bool loading = featuredLoading && featured.empty();
    if (!selectedRow()) {
        const std::vector<ServerRow>* first = !featuredList.empty() ? &featuredList : !creatorList.empty() ? &creatorList : loading ? nullptr : &saved;
        selection = !first || first->empty() ? std::nullopt : std::optional<Selection>(Selection { first->front().group, first->front().index });
    }

    struct Section {
        std::string label;
        const std::vector<ServerRow>* rows;
    };
    std::vector<Section> sections;
    if (!featuredList.empty()) {
        sections.push_back({ trf("hbui.PlayScreen.serverTab.featuredServer", "Featured experiences (%1$s)", { std::to_string(featuredList.size()) }), &featuredList });
    }
    if (!creatorList.empty()) {
        sections.push_back({ trf("hbui.PlayScreen.serverTab.creatorServer", "Creator experiences (%1$s)", { std::to_string(creatorList.size()) }), &creatorList });
    }
    if (!saved.empty()) {
        sections.push_back({ trf("hbui.PlayScreen.serverTab.externalServer", "Other Server (%1$s)", { std::to_string(saved.size()) }), &saved });
    }

    constexpr int SkeletonRows = 3;
    float content = 34.0f + (loading ? RowHeight * SkeletonRows : 0.0f) + spacing(4);
    for (size_t i = 0; i < sections.size(); ++i) {
        content += (i > 0 || loading ? SectionGap : 0.0f) + 12.0f + RowHeight * static_cast<float>(sections[i].rows->size());
    }
    Rect view { list.x, list.y + 4.0f, list.w, list.h - 4.0f };
    scrollArea(ui, view, listScroll, content);
    ui.setClip(view);
    float y = view.y - listScroll;

    std::string addLabel = tr("hbui.PlayScreen.serverTab.addServer", "Add server");
    Rect add { list.x + 8.67f, y + 5.0f, 174.67f, 20.67f };
    if (ui.pressableButton("servers:add", "pressableElevatedSecondary", addLabel, add, TextStyle::Ui, !locked)) {
        openServerForm(std::nullopt);
    }
    ui.sprite({ std::round(add.x + add.w * 0.5f - ui.measure(addLabel, TextStyle::Ui) * 0.5f - 9.0f), y + 11.0f, 6.0f, 6.0f }, "hbui/Plus", locked ? Disabled : InkDark);
    y += 34.0f;

    if (loading) {
        for (int i = 0; i < SkeletonRows; ++i) {
            Rect icon { list.x + 6.0f, std::round(y + (RowHeight - ServerIconSize) * 0.5f), ServerIconSize, ServerIconSize };
            ui.fill(icon, Neutral80);
            ui.fill({ icon.right() + 6.0f, y + 5.0f, 90.0f, 5.0f }, Neutral80);
            ui.fill({ icon.right() + 6.0f, y + 13.0f, 60.0f, 4.0f }, Neutral80);
            y += RowHeight;
        }
    }

    for (size_t i = 0; i < sections.size(); ++i) {
        if (i > 0 || loading) {
            y += SectionGap;
        }
        ui.text(sections[i].label, TextStyle::UiSmall, list.x + 7.33f, y, Muted0);
        y += 12.0f;
        for (const ServerRow& row : *sections[i].rows) {
            Rect bounds { list.x, y, list.w - 4.0f, RowHeight };
            bool selected = selection && selection->group == row.group && selection->index == row.index;
            Interaction state = ui.interact("server:" + std::to_string(static_cast<int>(row.group)) + ":" + std::to_string(row.index), bounds);
            if (selected) {
                ui.fill(bounds, Panel);
            } else if (state.hovered) {
                ui.fill(bounds, { 0x48, 0x49, 0x4a, 140 });
            }
            float textX = bounds.x + 7.33f;
            if (!row.icon.empty()) {
                Rect icon { bounds.x + 6.0f, std::round(bounds.y + (bounds.h - ServerIconSize) * 0.5f), ServerIconSize, ServerIconSize };
                ui.sprite(icon, row.icon);
                ui.outline(icon, Divider, BorderWidth);
                textX = icon.right() + 6.0f;
            }
            float textWidth = bounds.right() - textX - 4.0f;
            if (row.detail.empty()) {
                ui.text(row.name, TextStyle::Ui, textX, std::round(bounds.y + (bounds.h - ui.lineHeight(TextStyle::Ui)) * 0.5f), White, textWidth);
            } else {
                ui.text(row.name, TextStyle::Ui, textX, bounds.y + 3.0f, White, textWidth);
                ui.text(row.detail, TextStyle::UiSmall, textX, bounds.y + 12.67f, Muted1, textWidth);
            }
            if (state.clicked && !selected) {
                selection = Selection { row.group, row.index };
                serverAddressShown = false;
                detailScroll = 0.0f;
            }
            y += RowHeight;
        }
    }
    ui.clearClip();

    std::optional<ServerRow> row = selectedRow();
    if (!row) {
        return;
    }
    const std::string& address = row->group == ServerGroup::Saved ? row->address : featured[row->index].address;
    auto found = serverStatus.find(address);
    const ServerStatus* status = address.empty() || found == serverStatus.end() ? nullptr : &found->second;
    PingStatus ping = pingStatusOf(status);
    if (ping == PingStatus::High) {
        float warning = noticeBanner(ui, detail.x, detail.y, detail.w, tr("hbui.PlayScreen.serverTab.ServerNotifications.highPingWarning", "You don't have a strong connection to the chosen server. Your experience may be impacted."));
        detail.y += warning + spacing(2);
        detail.h = std::max(0.0f, detail.h - warning - spacing(2));
    }
    if (row->group != ServerGroup::Saved) {
        featuredDetail(ui, detail, featured[row->index]);
        return;
    }

    constexpr float FieldHeight = 36.0f;
    float barHeight = ButtonHeight + PlayBarPaddingY * 2.0f;
    float footerHeight = ButtonHeight + EditPaddingY * 2.0f;
    scrollArea(ui, detail, detailScroll, barHeight + FieldHeight * 3.0f + footerHeight);
    ui.setClip(detail);
    float topY = detail.y - detailScroll;

    Rect bar { detail.x, topY, detail.w, barHeight };
    ui.fill(bar, Neutral100);
    float barCenter = bar.y + bar.h * 0.5f;
    float playWidth = std::min(PlayMaxWidth, bar.w * 0.42f);
    Rect play { bar.right() - PlayBarPaddingX - playWidth, bar.y + PlayBarPaddingY, playWidth, ButtonHeight };
    float playersX = bar.x + PlayBarPaddingX + pingIndicator(ui, bar.x + PlayBarPaddingX, barCenter, ping) + spacing(6);
    playerCount(ui, playersX, barCenter, play.x - spacing(4) - playersX, status);
    if (ui.pressableButton("server:play", "pressableElevatedPrimary", tr("hbui.PlayScreen.serverTab.Play", "Play"), play, TextStyle::HeadingSmall, !locked)) {
        connect(*row);
    }

    float rowY = bar.bottom();
    auto field = [&](std::string_view text, std::string_view label, bool address) {
        Rect bounds { detail.x, rowY, detail.w, FieldHeight };
        ui.fill(bounds, Neutral80);
        float buttonWidth = std::min(ShowButtonMaxWidth, bounds.w * 0.3f);
        float textWidth = bounds.w - 24.0f - (address ? buttonWidth + ShowButtonMargin : 0.0f);
        ui.text(text, TextStyle::Pixel, bounds.x + 12.0f, bounds.y + 8.0f, White, textWidth);
        ui.text(label, TextStyle::Ui, bounds.x + 12.0f, bounds.y + 8.0f + spacing(2) + ui.lineHeight(TextStyle::Pixel), Muted1, textWidth);
        if (address) {
            Rect button { bounds.right() - 12.0f - buttonWidth, std::round(bounds.y + (bounds.h - ButtonHeight) * 0.5f), buttonWidth, ButtonHeight };
            std::string toggle = serverAddressShown ? tr("hbui.PlayScreen.serverTab.externalServerDetails.hideButton", "Hide") : tr("hbui.PlayScreen.serverTab.externalServerDetails.showButton", "Show");
            if (ui.pressableButton("server:address", "pressableElevatedSecondary", toggle, button)) {
                if (serverAddressShown) {
                    serverAddressShown = false;
                } else {
                    dialog = Dialog::RevealServerAddress;
                }
            }
        }
        divider(ui, bounds.x, bounds.bottom() - BorderWidth, bounds.w);
        rowY = bounds.bottom();
    };
    std::string host = row->address;
    std::string port = "19132";
    size_t colon = host.rfind(':');
    if (colon != std::string::npos && host.find(':') == colon) {
        port = host.substr(colon + 1);
        host.resize(colon);
    }
    field(row->name, tr("hbui.PlayScreen.serverTab.externalServerDetails.name", "Server name"), false);
    field(serverAddressShown ? host : tr("hbui.PlayScreen.serverTab.externalServerDetails.placeholder", "XX.XXX.XXX.XXX"), tr("hbui.PlayScreen.serverTab.externalServerDetails.address", "Server address"), true);
    field(port, tr("hbui.PlayScreen.serverTab.externalServerDetails.port", "Server port"), false);

    Rect footer { detail.x, rowY, detail.w, footerHeight };
    ui.fill(footer, Divider);
    ui.fill({ footer.x + BorderWidth, footer.y, footer.w - BorderWidth * 2.0f, footer.h - BorderWidth }, Panel);
    float editWidth = std::min(PlayMaxWidth, footer.w - EditPaddingX * 2.0f);
    Rect edit { std::round(footer.x + (footer.w - editWidth) * 0.5f), footer.y + EditPaddingY, editWidth, ButtonHeight };
    if (ui.pressableButton("server:edit", "pressableElevatedSecondary", tr("hbui.PlayScreen.serverTab.externalServerDetails.editButton", "Edit server"), edit, TextStyle::Ui, !locked)) {
        openServerForm(row->index);
    }
    ui.clearClip();
}

void Menu::featuredDetail(Context& ui, const Rect& area, const FeaturedEntry& entry)
{
    scrollArea(ui, area, detailScroll, detailContent);
    ui.setClip(area);
    ui.fill(area, Neutral80);
    float x = area.x + 12.0f;
    float width = area.w - 24.0f;
    float y = area.y - detailScroll;

    Rect image { area.x, y, area.w, std::round(area.w * 0.3f) };
    ui.fill(image, InkDark);
    if (!entry.showcase.empty()) {
        const std::string& name = entry.showcase.front();
        const Sprite& picture = ui.skin().sprite(name);
        if (picture.valid && picture.width > 0.0f && picture.height > 0.0f) {
            float fit = std::max(image.w / picture.width, image.h / picture.height);
            float spanX = image.w / fit;
            float spanY = image.h / fit;
            ui.spriteRegion(image, name, { (picture.width - spanX) * 0.5f, (picture.height - spanY) * 0.5f, spanX, spanY });
        }
    }
    ui.outline(image, Neutral80, BorderWidth);
    if (!entry.address.empty()) {
        auto found = serverStatus.find(entry.address);
        const ServerStatus* status = found == serverStatus.end() ? nullptr : &found->second;
        Rect bar { image.x, image.bottom() - ImageBarHeight, image.w, ImageBarHeight };
        ui.fill(bar, ImageBarBackground);
        float center = bar.y + bar.h * 0.5f;
        float playersX = bar.x + ImageBarPadding + pingIndicator(ui, bar.x + ImageBarPadding, center, pingStatusOf(status)) + spacing(6);
        playerCount(ui, playersX, center, bar.right() - ImageBarPadding - playersX, status);
    }

    y = image.bottom();
    float buttonWidth = std::min(PlayMaxWidth, width * 0.42f);
    Rect play { area.right() - 12.0f - buttonWidth, y + 6.0f, buttonWidth, ButtonHeight };
    ui.text(entry.name, TextStyle::Pixel, x, y + 14.0f, White, std::max(1.0f, play.x - x - spacing(1)));
    if (ui.pressableButton("server:play", "pressableElevatedPrimary", tr("hbui.PlayScreen.serverTab.Play", "Play"), play, TextStyle::HeadingSmall, signedIn())) {
        if (std::optional<ServerRow> row = selectedRow()) {
            connect(*row);
        }
    }
    y += 36.0f;

    if (!entry.description.empty()) {
        divider(ui, area.x, y, area.w);
        y += 8.0f;
        ui.text(tr("hbui.PlayScreen.serverTab.ServerDescription.title", "Description"), TextStyle::Pixel, x, y, White);
        y += ui.lineHeight(TextStyle::Pixel) + spacing(2);
        y += ui.paragraph(entry.description, TextStyle::Pixel, x, y, width, Muted0) + 8.0f;
    }
    if (!entry.games.empty()) {
        divider(ui, area.x, y, area.w);
        y += spacing(3);
        ui.text(tr("hbui.PlayScreen.serverTab.Activities", "Activities"), TextStyle::Pixel, x, y, White);
        y += ui.lineHeight(TextStyle::Pixel) + spacing(3);
        for (const FeaturedGameEntry& game : entry.games) {
            float textX = x;
            float imageBottom = y;
            if (!game.image.empty()) {
                Rect picture { x, y + spacing(3), ActivityImageSize, ActivityImageSize };
                ui.fill(picture, InkDark);
                ui.sprite(picture, game.image);
                ui.outline(picture, Divider, BorderWidth);
                imageBottom = picture.bottom() + spacing(3);
                textX = picture.right() + spacing(3) * 2.0f;
            }
            float textWidth = x + width - textX;
            float textY = y + spacing(3);
            ui.text(game.title, TextStyle::Pixel, textX, textY, White, textWidth);
            textY += ui.lineHeight(TextStyle::Pixel);
            if (!game.subtitle.empty()) {
                ui.text(game.subtitle, TextStyle::UiSmall, textX, textY, Muted1, textWidth);
                textY += ui.lineHeight(TextStyle::UiSmall);
            }
            textY += spacing(2);
            textY += ui.paragraph(game.description, TextStyle::BodySmall, textX, textY, textWidth, Muted0) + spacing(3);
            y = std::max(imageBottom, textY);
        }
    }
    if (!entry.newsTitle.empty() || !entry.news.empty()) {
        divider(ui, area.x, y, area.w);
        y += spacing(3);
        ui.text(tr("hbui.PlayScreen.serverTab.newsTitle", "News"), TextStyle::Pixel, x, y, White);
        y += ui.lineHeight(TextStyle::Pixel) + spacing(2);
        if (!entry.newsTitle.empty()) {
            y += ui.paragraph(entry.newsTitle, TextStyle::UiSmall, x, y, width, Muted1);
        }
        y += spacing(3);
        if (!entry.news.empty()) {
            y += ui.paragraph(entry.news, TextStyle::Pixel, x, y, width, Muted0);
        }
        y += spacing(6);
    }
    ui.clearClip();
    detailContent = y + detailScroll - area.y;
}

void Menu::revealAddressDialog(Context& ui, float width, float height, bool& closed)
{
    constexpr float DialogWidth = 300.0f;
    std::string first = tr("hbui.PlayScreen.serverTab.ShowServerAddressModal.firstParagraph", "Warning: This will reveal your server address to anyone currently viewing your screen (including via streaming). If your server address is the same as your IP address, revealing it could leave you vulnerable to cyber attack.");
    std::string second = tr("hbui.PlayScreen.serverTab.ShowServerAddressModal.secondParagraph", "Are you sure you want to show your server address?");
    float textWidth = std::min(DialogWidth, width - 16.0f) - 18.0f;
    std::vector<std::string_view> firstLines;
    std::vector<std::string_view> secondLines;
    ui.wrap(first, TextStyle::Body, textWidth, firstLines);
    ui.wrap(second, TextStyle::Body, textWidth, secondLines);
    float line = ui.lineHeight(TextStyle::Body);
    float textHeight = line * static_cast<float>(firstLines.size() + secondLines.size()) + spacing(2);
    float frameHeight = 22.0f + 8.0f + textHeight + spacing(4) + ButtonHeight * 2.0f + spacing(1) + 10.0f;
    Rect content = modalFrame(ui, width, height, DialogWidth, frameHeight, tr("hbui.PlayScreen.serverTab.ShowServerAddressModal.title", "Revealing private data"), closed);
    if (closed) {
        return;
    }
    float y = content.y;
    for (std::string_view text : firstLines) {
        ui.textCentered(text, TextStyle::Body, { content.x, y, content.w, line }, Muted0);
        y += line;
    }
    y += spacing(2);
    for (std::string_view text : secondLines) {
        ui.textCentered(text, TextStyle::Body, { content.x, y, content.w, line }, Muted0);
        y += line;
    }
    Rect secondary { content.x, content.bottom() - ButtonHeight, content.w, ButtonHeight };
    Rect primary { content.x, secondary.y - spacing(1) - ButtonHeight, content.w, ButtonHeight };
    if (ui.pressableButton("reveal:show", "pressableElevatedPrimary", tr("hbui.PlayScreen.serverTab.ShowServerAddressModal.primaryButton", "Show server address"), primary)) {
        serverAddressShown = true;
        closed = true;
    }
    if (ui.pressableButton("reveal:back", "pressableElevatedSecondary", tr("hbui.PlayScreen.serverTab.ShowServerAddressModal.secondaryButton", "Go back"), secondary)) {
        closed = true;
    }
}

}
