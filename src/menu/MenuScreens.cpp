#include "menu/Menu.h"

#include "ui/Context.h"
#include "ui/Theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;

namespace {

struct FeaturedServer {
    const char* name;
    const char* genre;
    const char* address;
};

constexpr FeaturedServer Featured[] = {
    { "The Hive", "Minigames", "geo.hivebedrock.network:19132" },
    { "CubeCraft", "Minigames", "mco.cubecraft.net:19132" },
    { "Lifeboat", "Survival & minigames", "mco.lbsg.net:19132" },
    { "Mineville", "Roleplay & minigames", "play.inpvp.net:19132" },
    { "Galaxite", "Minigames", "play.galaxite.net:19132" },
};

constexpr float InterfaceScales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
constexpr const char* InterfaceScaleLabels[] = { "100%", "125%", "150%", "200%" };

std::string joinedLabel(int64_t timestamp)
{
    if (timestamp <= 0) {
        return "Never joined";
    }
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t elapsed = std::max<int64_t>(0, now - timestamp);
    if (elapsed < 60) {
        return "Joined just now";
    }
    if (elapsed < 3600) {
        return "Joined " + std::to_string(elapsed / 60) + " min ago";
    }
    if (elapsed < 86400) {
        return "Joined " + std::to_string(elapsed / 3600) + " h ago";
    }
    int64_t days = elapsed / 86400;
    return "Joined " + std::to_string(days) + (days == 1 ? " day ago" : " days ago");
}

void panel(Context& ui, const Rect& rect)
{
    ui.shadow({ rect.x, rect.y + 10.0f, rect.w, rect.h }, Radius, 30.0f, { 0, 0, 0, 70 });
    ui.fill(rect, Line, Radius);
    ui.gradient(rect.inset(1.0f), SurfaceTop, Surface, Radius - 1.0f);
}

void pill(Context& ui, std::string_view label, float x, float y, Color background, Color ink)
{
    float width = ui.measure(label, TextStyle::Caption) + 20.0f;
    Rect rect { x, y, width, 24.0f };
    ui.fill(rect, background, 12.0f);
    ui.textCentered(label, TextStyle::Caption, rect, ink);
}

}

std::vector<Menu::Row> Menu::featuredRows() const
{
    std::vector<Row> rows;
    for (size_t i = 0; i < std::size(Featured); ++i) {
        rows.push_back({ true, i, Featured[i].name, Featured[i].address, Featured[i].genre });
    }
    return rows;
}

std::vector<Menu::Row> Menu::rowsFor(ServerFilter source) const
{
    if (source == ServerFilter::Featured) {
        return featuredRows();
    }
    std::vector<Row> rows;
    const std::vector<SavedServer>& saved = store.servers();
    for (size_t i = 0; i < saved.size(); ++i) {
        if (source == ServerFilter::Favorites && !saved[i].favorite) {
            continue;
        }
        rows.push_back({ false, i, saved[i].name, saved[i].address, joinedLabel(saved[i].lastJoined) });
    }
    return rows;
}

std::vector<Menu::Row> Menu::recentRows(size_t limit) const
{
    std::vector<size_t> order;
    const std::vector<SavedServer>& saved = store.servers();
    for (size_t i = 0; i < saved.size(); ++i) {
        if (saved[i].lastJoined > 0) {
            order.push_back(i);
        }
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return saved[a].lastJoined > saved[b].lastJoined;
    });
    if (order.size() > limit) {
        order.resize(limit);
    }

    std::vector<Row> rows;
    for (size_t i : order) {
        rows.push_back({ false, i, saved[i].name, saved[i].address, joinedLabel(saved[i].lastJoined) });
    }
    return rows;
}

void Menu::heading(Context& ui, std::string_view title, float x, float y)
{
    ui.text(title, TextStyle::Heading, x, y, Text);
}

Rect Menu::gridCell(size_t index, float x, float y, float width, float height) const
{
    size_t columns = width >= 880.0f ? 3 : width >= 540.0f ? 2 : 1;
    float cellWidth = (width - PadLg * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    return {
        x + static_cast<float>(index % columns) * (cellWidth + PadLg),
        y + static_cast<float>(index / columns) * (height + PadLg),
        cellWidth,
        height,
    };
}

bool Menu::tile(Context& ui, std::string_view id, const Rect& rect, std::string_view title, std::string_view subtitle, std::string_view tag, bool secondaryTag, bool enabled)
{
    Interaction state = ui.interact(id, rect);
    bool lifted = state.hovered && enabled;
    Rect body = lifted ? Rect { rect.x, rect.y - 2.0f, rect.w, rect.h } : rect;

    ui.shadow({ body.x, body.y + (lifted ? 14.0f : 8.0f), body.w, body.h }, Radius, lifted ? 36.0f : 24.0f, lifted ? AccentGlow : Color { 0, 0, 0, 80 });
    ui.fill(body, lifted ? AccentLine : Line, Radius);
    ui.gradient(body.inset(1.0f), lifted ? Hover : SurfaceTop, Surface, Radius - 1.0f);

    Rect icon { body.x + 20.0f, body.y + 20.0f, 52.0f, 52.0f };
    badge(ui, icon, title, TextStyle::Heading);

    float textX = icon.right() + 16.0f;
    float textWidth = body.right() - textX - 20.0f;
    ui.text(title, TextStyle::Label, textX, icon.y + 4.0f, Text, textWidth);
    ui.text(subtitle, TextStyle::Caption, textX, icon.y + 28.0f, Muted, textWidth);

    float footer = body.bottom() - 20.0f - 24.0f;
    if (!tag.empty()) {
        pill(ui, tag, body.x + 20.0f, footer, secondaryTag ? SecondarySoft : AccentSoft, secondaryTag ? Secondary : Accent);
    }
    if (lifted) {
        Rect play { body.right() - 20.0f - 72.0f, footer - 4.0f, 72.0f, 32.0f };
        ui.gradient(play, AccentHover, AccentDeep, 16.0f);
        ui.textCentered("Play", TextStyle::Label, play, OnAccent);
    }
    return state.clicked && enabled;
}

float Menu::tileGrid(Context& ui, const std::vector<Row>& rows, float x, float y, float width, size_t limit)
{
    constexpr float tileHeight = 132.0f;
    size_t count = std::min(rows.size(), limit);

    for (size_t i = 0; i < count; ++i) {
        const Row& row = rows[i];
        std::string id = std::string(row.featured ? "tile:f:" : "tile:s:") + std::to_string(row.index);
        if (tile(ui, id, gridCell(i, x, y, width, tileHeight), row.name, row.address, row.detail, row.featured, true)) {
            connect(row);
        }
    }

    size_t columns = width >= 880.0f ? 3 : width >= 540.0f ? 2 : 1;
    size_t lines = (count + columns - 1) / columns;
    return lines == 0 ? 0.0f : static_cast<float>(lines) * (tileHeight + PadLg) - PadLg;
}

void Menu::home(Context& ui, const Area& area)
{
    Rect hero { area.x, area.y, area.w, 214.0f };
    ui.shadow({ hero.x, hero.y + 18.0f, hero.w, hero.h }, 22.0f, 48.0f, { 90, 50, 160, 70 });
    ui.fill(hero, AccentLine, 22.0f);
    ui.gradient(hero.inset(1.0f), { 64, 46, 98, 255 }, { 30, 24, 44, 255 }, 21.0f);
    ui.glow(hero.right() - 120.0f, hero.y + 30.0f, 520.0f, { 200, 160, 255, 40 });
    ui.glow(hero.right() - 40.0f, hero.bottom(), 300.0f, { 124, 220, 198, 26 });

    float x = hero.x + 40.0f;
    float w = hero.w - 80.0f;
    pill(ui, "DIRECT CONNECT", x, hero.y + 32.0f, { 255, 255, 255, 18 }, AccentHover);
    ui.text("Jump into a server", TextStyle::Display, x, hero.y + 64.0f, Text, w);
    ui.text("Paste an address, or pick up right where you left off.", TextStyle::Body, x, hero.y + 114.0f, Muted, w);

    float fieldWidth = std::min(w - 140.0f - Gap, 560.0f);
    Rect input { x, hero.bottom() - 40.0f - 48.0f + 12.0f, fieldWidth, 48.0f };
    if (ui.field("home:quick", "play.example.net:19132", quickAddress, input, field == Field::QuickAddress)) {
        field = Field::QuickAddress;
    }
    if (ui.button("home:join", "Join", { input.right() + 12.0f, input.y, 128.0f, 48.0f }, ButtonKind::Primary)) {
        quickConnect();
    }

    float y = hero.bottom() + 40.0f;
    std::vector<Row> recent = recentRows(3);
    if (!recent.empty()) {
        heading(ui, "Continue playing", area.x, y);
        y += 44.0f;
        y += tileGrid(ui, recent, area.x, y, area.w, 3) + 40.0f;
    }

    heading(ui, "Featured servers", area.x, y);
    if (ui.button("home:browse", "See all", { area.x + area.w - 100.0f, y - 4.0f, 100.0f, 36.0f }, ButtonKind::Ghost)) {
        filter = ServerFilter::Featured;
        navigate(Screen::Servers);
    }
    y += 44.0f;
    size_t columns = area.w >= 880.0f ? 3 : area.w >= 540.0f ? 2 : 1;
    tileGrid(ui, featuredRows(), area.x, y, area.w, columns);
}

void Menu::servers(Context& ui, const Area& area)
{
    ui.text("Servers", TextStyle::Display, area.x, area.y - 6.0f, Text);

    struct FilterTab {
        ServerFilter value;
        const char* label;
    };
    constexpr FilterTab filters[] = {
        { ServerFilter::All, "All" },
        { ServerFilter::Favorites, "Favorites" },
        { ServerFilter::Featured, "Featured" },
    };
    float segmentWidth = 0.0f;
    for (const FilterTab& item : filters) {
        segmentWidth += ui.measure(item.label, TextStyle::Label) + 32.0f;
    }
    Rect segment { area.x + area.w - segmentWidth - 8.0f, area.y + 2.0f, segmentWidth + 8.0f, 44.0f };
    ui.card(segment, theme::Field, Line, 22.0f);
    float tabX = segment.x + 4.0f;
    for (const FilterTab& item : filters) {
        float tabWidth = ui.measure(item.label, TextStyle::Label) + 32.0f;
        if (ui.tab(std::string("filter:") + item.label, item.label, { tabX, segment.y + 4.0f, tabWidth, 36.0f }, filter == item.value)) {
            filter = item.value;
            selection.reset();
            scrollRow = 0;
        }
        tabX += tabWidth;
    }

    std::vector<Row> rows = rowsFor(filter);
    float barHeight = 48.0f + PadLg;
    Rect list { area.x, area.y + 72.0f, area.w, area.h - 72.0f - barHeight };
    panel(ui, list);

    Rect inner = list.inset(8.0f);
    size_t visible = static_cast<size_t>(std::max(1.0f, inner.h / RowHeight));
    size_t maxScroll = rows.size() > visible ? rows.size() - visible : 0;
    if (ui.hovered(list) && ui.input().wheel != 0.0f) {
        if (ui.input().wheel > 0.0f && scrollRow > 0) {
            --scrollRow;
        } else if (ui.input().wheel < 0.0f) {
            ++scrollRow;
        }
    }
    scrollRow = std::min(scrollRow, maxScroll);

    if (rows.empty()) {
        Rect empty { inner.x, inner.y + inner.h * 0.5f - 70.0f, inner.w, 140.0f };
        Rect icon { empty.x + (empty.w - 56.0f) * 0.5f, empty.y, 56.0f, 56.0f };
        ui.fill(icon, AccentSoft, 18.0f);
        ui.fill({ icon.x + 18.0f, icon.y + 18.0f, 20.0f, 20.0f }, Accent, 6.0f);
        ui.textCentered(filter == ServerFilter::Favorites ? "No favorites yet" : "No saved servers yet", TextStyle::Heading, { empty.x, icon.bottom() + 14.0f, empty.w, 30.0f }, Text);
        ui.textCentered(filter == ServerFilter::Favorites ? "Tick the box next to a server to pin it here." : "Add one below, or join from Home.", TextStyle::Body, { empty.x, icon.bottom() + 46.0f, empty.w, 24.0f }, Muted);
    }

    for (size_t i = scrollRow; i < rows.size() && i < scrollRow + visible; ++i) {
        const Row& row = rows[i];
        Rect bounds { inner.x, inner.y + static_cast<float>(i - scrollRow) * RowHeight, inner.w, RowHeight - 4.0f };
        bool selected = selection && selection->featured == row.featured && selection->index == row.index;

        Rect favoriteBox { bounds.x + 10.0f, bounds.y + (bounds.h - 36.0f) * 0.5f, 36.0f, 36.0f };
        Rect body { favoriteBox.right() + 2.0f, bounds.y, bounds.right() - favoriteBox.right() - 2.0f, bounds.h };
        std::string id = std::string(row.featured ? "row:f:" : "row:s:") + std::to_string(row.index);
        Interaction state = ui.interact(id, body);

        if (selected) {
            ui.card(bounds, { 200, 170, 242, 24 }, AccentLine, 12.0f);
        } else if (state.hovered) {
            ui.fill(bounds, GhostHover, 12.0f);
        }

        if (row.featured) {
            ui.fill({ favoriteBox.x + 14.0f, favoriteBox.y + 14.0f, 8.0f, 8.0f }, Secondary, 4.0f);
        } else if (ui.toggle(id + ":fav", favoriteBox, store.servers()[row.index].favorite)) {
            store.toggleFavorite(row.index);
        }

        Rect icon { body.x + 4.0f, bounds.y + (bounds.h - 40.0f) * 0.5f, 40.0f, 40.0f };
        badge(ui, icon, row.name, TextStyle::Label);

        float textX = icon.right() + 14.0f;
        float detailWidth = std::min(200.0f, body.w * 0.3f);
        float textWidth = body.right() - textX - detailWidth - PadLg;
        ui.text(row.name, TextStyle::Label, textX, bounds.y + 12.0f, Text, textWidth);
        ui.text(row.address, TextStyle::Caption, textX, bounds.y + 34.0f, Muted, textWidth);
        float detailWidthUsed = std::min(ui.measure(row.detail, TextStyle::Caption), detailWidth);
        ui.text(row.detail, TextStyle::Caption, body.right() - 20.0f - detailWidthUsed, bounds.y + (bounds.h - ui.lineHeight(TextStyle::Caption)) * 0.5f, row.featured ? Secondary : Subtle, detailWidth);

        if (state.clicked) {
            Selection clicked { row.featured, row.index };
            auto now = std::chrono::steady_clock::now();
            bool doubleClick = lastRowClicked && lastRowClicked->featured == clicked.featured && lastRowClicked->index == clicked.index
                && now - lastRowClick < std::chrono::milliseconds(400);
            selection = clicked;
            lastRowClicked = clicked;
            lastRowClick = now;
            if (doubleClick) {
                lastRowClicked.reset();
                connect(row);
            }
        }
    }

    const Row* selected = nullptr;
    for (const Row& row : rows) {
        if (selection && selection->featured == row.featured && selection->index == row.index) {
            selected = &row;
            break;
        }
    }
    bool savedSelected = selected && !selected->featured;

    float barY = list.bottom() + PadLg;
    float x = area.x;
    if (ui.button("servers:join", "Join server", { x, barY, 150.0f, 48.0f }, ButtonKind::Primary, selected != nullptr)) {
        connect(*selected);
    }
    x += 150.0f + 12.0f;
    if (ui.button("servers:add", "Add server", { x, barY, 136.0f, 48.0f })) {
        openEditor(std::nullopt);
    }
    x += 136.0f + 12.0f;
    if (ui.button("servers:edit", "Edit", { x, barY, 100.0f, 48.0f }, ButtonKind::Secondary, savedSelected)) {
        openEditor(selected->index);
    }
    x += 100.0f + 12.0f;
    if (ui.button("servers:delete", "Delete", { x, barY, 100.0f, 48.0f }, ButtonKind::Ghost, savedSelected)) {
        sheet = Sheet::ConfirmDelete;
    }
    if (rows.size() > visible) {
        std::string range = std::to_string(scrollRow + 1) + "\xE2\x80\x93" + std::to_string(std::min(rows.size(), scrollRow + visible)) + " of " + std::to_string(rows.size());
        float rangeWidth = ui.measure(range, TextStyle::Caption);
        ui.text(range, TextStyle::Caption, area.x + area.w - rangeWidth, barY + (48.0f - ui.lineHeight(TextStyle::Caption)) * 0.5f, Subtle);
    }
}

void Menu::worlds(Context& ui, const Area& area)
{
    constexpr float tileHeight = 132.0f;
    if (ui.input().wheel != 0.0f && !ui.hovered({ 0.0f, 0.0f, area.x + area.w, HeaderHeight })) {
        worldsScroll = std::max(0.0f, worldsScroll - ui.input().wheel * 80.0f);
    }

    float y = area.y - worldsScroll;
    ui.text("Worlds", TextStyle::Display, area.x, y - 6.0f, Text);
    y += 72.0f;

    heading(ui, "Realms", area.x, y);
    y += 44.0f;
    auto notice = [&](std::string_view title, std::string_view body) {
        Rect card { area.x, y, area.w, 96.0f };
        panel(ui, card);
        ui.text(title, TextStyle::Label, card.x + 28.0f, card.y + 24.0f, Text, card.w - 280.0f);
        ui.text(body, TextStyle::Body, card.x + 28.0f, card.y + 50.0f, Muted, card.w - 280.0f);
        return card;
    };

    if (!signedIn()) {
        Rect card = notice("Sign in to see your Realms", "Realms you own or were invited to show up here.");
        if (ui.button("worlds:signin", "Sign in with Microsoft", { card.right() - 28.0f - 220.0f, card.y + 24.0f, 220.0f, 48.0f }, ButtonKind::Primary)) {
            beginSignIn();
        }
        y = card.bottom();
    } else if (account.realmsLoading) {
        y = notice("Loading your Realms\xE2\x80\xA6", "Asking Realms for the worlds you can join.").bottom();
    } else if (!account.realmsError.empty()) {
        y = notice("Couldn't load your Realms", account.realmsError).bottom();
    } else if (account.realms.empty()) {
        y = notice("No Realms yet", "Realms you own or were invited to show up here.").bottom();
    } else {
        for (size_t i = 0; i < account.realms.size(); ++i) {
            const RealmEntry& realm = account.realms[i];
            std::string tag = realm.expired ? "EXPIRED" : realm.open ? "OPEN" : "CLOSED";
            Rect cell = gridCell(i, area.x, y, area.w, tileHeight);
            if (tile(ui, "realm:" + std::to_string(realm.id), cell, realm.name, realm.detail, tag, realm.open && !realm.expired, true)) {
                if (realm.open && !realm.expired) {
                    pending = ConnectRequest { realm.name, "realm_id/" + std::to_string(realm.id) };
                } else {
                    notify(realm.name + (realm.expired ? " has expired" : " is closed"));
                }
            }
            y = std::max(y, cell.bottom());
        }
    }
    y += 40.0f;

    heading(ui, "On this device", area.x, y);
    y += 44.0f;
    if (worldEntries.empty()) {
        y = notice("No local worlds found", "Worlds from Minecraft Bedrock on this device show up here.").bottom();
    }
    for (size_t i = 0; i < worldEntries.size(); ++i) {
        const WorldEntry& world = worldEntries[i];
        Rect cell = gridCell(i, area.x, y, area.w, tileHeight);
        if (tile(ui, "world:" + std::to_string(i), cell, world.name, world.detail, "LOCAL", false, true)) {
            notify("Playing local worlds needs the built-in server");
        }
    }
    if (!worldEntries.empty()) {
        y = gridCell(worldEntries.size() - 1, area.x, y, area.w, tileHeight).bottom();
    }

    float contentHeight = y + worldsScroll - area.y + PadXl;
    worldsScroll = std::min(worldsScroll, std::max(0.0f, contentHeight - area.h));
}

void Menu::friends(Context& ui, const Area& area)
{
    ui.text("Friends", TextStyle::Display, area.x, area.y - 6.0f, Text);
    Rect card { area.x, area.y + 72.0f, area.w, 260.0f };
    panel(ui, card);
    ui.glow(card.right() - 80.0f, card.y + 40.0f, 360.0f, { 200, 160, 255, 30 });

    Rect icon { card.x + 40.0f, card.y + 40.0f, 72.0f, 72.0f };
    profileBadge(ui, icon, TextStyle::Display);

    float x = icon.right() + 28.0f;
    float w = card.right() - x - 40.0f;
    ui.text("Play together", TextStyle::Heading, x, card.y + 74.0f, Text, w);
    if (signedIn()) {
        pill(ui, "SIGNED IN", x, card.y + 40.0f, SecondarySoft, Secondary);
        ui.paragraph("You're signed in as " + displayName + ". Your friends list and their joinable worlds will show up here.", TextStyle::Body, x, card.y + 108.0f, w, Muted);
        return;
    }
    pill(ui, "OFFLINE", x, card.y + 40.0f, { 255, 255, 255, 16 }, Muted);
    ui.paragraph("Sign in with your Microsoft account to see which friends are online and hop into their worlds and Realms.", TextStyle::Body, x, card.y + 108.0f, w, Muted);
    if (ui.button("friends:signin", "Sign in with Microsoft", { x, card.bottom() - 40.0f - 48.0f, 220.0f, 48.0f }, ButtonKind::Primary)) {
        beginSignIn();
    }
}

void Menu::settings(Context& ui, const Area& area)
{
    if (ui.input().wheel != 0.0f && ui.mouseY() > HeaderHeight) {
        settingsScroll = std::max(0.0f, settingsScroll - ui.input().wheel * 80.0f);
    }
    float top = area.y - settingsScroll;
    ui.text("Settings", TextStyle::Display, area.x, top - 6.0f, Text);
    float y = top + 72.0f;

    Rect account { area.x, y, area.w, 112.0f };
    panel(ui, account);
    Rect avatar { account.x + 28.0f, account.y + 24.0f, 64.0f, 64.0f };
    profileBadge(ui, avatar, TextStyle::Display);
    ui.text(displayName, TextStyle::Heading, avatar.right() + 20.0f, avatar.y + 6.0f, Text);
    if (signedIn()) {
        pill(ui, "Xbox Live", avatar.right() + 20.0f, avatar.y + 38.0f, SecondarySoft, Secondary);
        if (ui.button("settings:signout", "Sign out", { account.right() - 28.0f - 120.0f, account.y + (account.h - 48.0f) * 0.5f, 120.0f, 48.0f })) {
            accountRequest = AccountRequest::SignOut;
            notify("Signed out");
        }
    } else {
        pill(ui, "Offline profile", avatar.right() + 20.0f, avatar.y + 38.0f, { 255, 255, 255, 16 }, Muted);
        if (ui.button("settings:signin", "Sign in with Microsoft", { account.right() - 28.0f - 220.0f, account.y + (account.h - 48.0f) * 0.5f, 220.0f, 48.0f }, ButtonKind::Primary)) {
            beginSignIn();
        }
    }
    y = account.bottom() + PadLg;

    Rect interfacePanel { area.x, y, area.w, 144.0f };
    panel(ui, interfacePanel);
    float x = interfacePanel.x + 28.0f;
    ui.text("Interface size", TextStyle::Label, x, interfacePanel.y + 26.0f, Text);
    ui.text("Scales every menu on top of your display settings.", TextStyle::Body, x, interfacePanel.y + 50.0f, Muted, interfacePanel.w - 56.0f);

    float segmentWidth = 4.0f * 84.0f + 8.0f;
    Rect segment { x, interfacePanel.bottom() - 26.0f - 44.0f, segmentWidth, 44.0f };
    ui.card(segment, theme::Field, Line, 22.0f);
    for (size_t i = 0; i < std::size(InterfaceScales); ++i) {
        Rect option { segment.x + 4.0f + static_cast<float>(i) * 84.0f, segment.y + 4.0f, 84.0f, 36.0f };
        if (ui.tab(std::string("scale:") + InterfaceScaleLabels[i], InterfaceScaleLabels[i], option, scale == InterfaceScales[i])) {
            scale = InterfaceScales[i];
        }
    }
    y = interfacePanel.bottom() + PadLg;

    Rect video { area.x, y, area.w, 196.0f };
    panel(ui, video);
    ui.text("Video", TextStyle::Label, x, video.y + 26.0f, Text);
    float sliderWidth = std::min(video.w - 56.0f - 160.0f, 420.0f);

    ui.text("Render distance", TextStyle::Body, x, video.y + 66.0f, Muted);
    float distanceFraction = float(chunkDistance - MinRenderDistance) / float(MaxRenderDistance - MinRenderDistance);
    if (ui.slider("settings:distance", { x + 160.0f, video.y + 60.0f, sliderWidth, 32.0f }, distanceFraction)) {
        chunkDistance = MinRenderDistance + int(std::lround(distanceFraction * float(MaxRenderDistance - MinRenderDistance)));
    }
    ui.text(std::to_string(chunkDistance) + " chunks", TextStyle::Label, x + 176.0f + sliderWidth, video.y + 66.0f, Text);

    int fpsSteps = (MaxMaxFps - MinMaxFps) / MaxFpsStep + 1;
    int fpsStep = fpsLimit == UnlimitedFps ? fpsSteps : (std::clamp(fpsLimit, MinMaxFps, MaxMaxFps) - MinMaxFps) / MaxFpsStep;
    ui.text("Max FPS", TextStyle::Body, x, video.y + 126.0f, Muted);
    float fpsFraction = float(fpsStep) / float(fpsSteps);
    if (ui.slider("settings:fps", { x + 160.0f, video.y + 120.0f, sliderWidth, 32.0f }, fpsFraction)) {
        int step = int(std::lround(fpsFraction * float(fpsSteps)));
        fpsLimit = step >= fpsSteps ? UnlimitedFps : MinMaxFps + step * MaxFpsStep;
    }
    ui.text(fpsLimit == UnlimitedFps ? std::string("Unlimited") : std::to_string(fpsLimit), TextStyle::Label, x + 176.0f + sliderWidth, video.y + 126.0f, Text);
    y = video.bottom() + PadLg;

    constexpr float bindingRow = 56.0f;
    size_t bindingRows = (KeyBindings::Count + 1) / 2;
    Rect controls { area.x, y, area.w, 90.0f + bindingRow * static_cast<float>(bindingRows) };
    panel(ui, controls);
    ui.text("Controls", TextStyle::Label, controls.x + 28.0f, controls.y + 26.0f, Text);
    ui.text("Click a key, then press the new one. Esc cancels.", TextStyle::Body, controls.x + 28.0f, controls.y + 50.0f, Muted, controls.w - 56.0f);
    float columnWidth = (controls.w - 56.0f - PadLg) * 0.5f;
    for (size_t i = 0; i < KeyBindings::Count; ++i) {
        float columnX = controls.x + 28.0f + static_cast<float>(i % 2) * (columnWidth + PadLg);
        float rowY = controls.y + 84.0f + static_cast<float>(i / 2) * bindingRow;
        ui.text(KeyBindings::label(i), TextStyle::Body, columnX, rowY + (44.0f - ui.lineHeight(TextStyle::Body)) * 0.5f, Text, columnWidth - 150.0f);
        bool waiting = rebinding && *rebinding == i;
        std::string label = waiting ? std::string("Press a key\xE2\x80\xA6") : std::string(keyName(bindings.keys[i]));
        if (ui.button(std::string("bind:") + KeyBindings::id(i), label, { columnX + columnWidth - 140.0f, rowY, 140.0f, 44.0f }, waiting ? ButtonKind::Primary : ButtonKind::Secondary)) {
            rebinding = i;
        }
    }
    if (ui.button("bind:reset", "Reset", { controls.right() - 28.0f - 100.0f, controls.y + 20.0f, 100.0f, 40.0f }, ButtonKind::Ghost)) {
        bindings = KeyBindings {};
        rebinding.reset();
    }
    y = controls.bottom() + PadLg;

    Rect about { area.x, y, area.w, 84.0f };
    panel(ui, about);
    Rect logo { about.x + 28.0f, about.y + 24.0f, 36.0f, 36.0f };
    ui.gradient(logo, AccentHover, AccentDeep, 11.0f);
    ui.text("Kestrel", TextStyle::Label, logo.right() + 14.0f, about.y + 22.0f, Text);
    ui.text("Bedrock 1.26.51 \xC2\xB7 protocol 2193", TextStyle::Caption, logo.right() + 14.0f, about.y + 44.0f, Subtle);
    if (ui.button("settings:quit", "Quit Kestrel", { about.right() - 28.0f - 110.0f, about.y + 20.0f, 110.0f, 44.0f }, ButtonKind::Ghost)) {
        sheet = Sheet::ConfirmExit;
    }

    float contentHeight = about.bottom() + settingsScroll - area.y + PadXl;
    settingsScroll = std::min(settingsScroll, std::max(0.0f, contentHeight - area.h));
}

}
