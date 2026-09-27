#include "menu/Menu.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"

#include <algorithm>
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

constexpr float HeaderHeight = 48.0f;
constexpr float ColumnWidth = 631.0f;
constexpr float TabHeight = 37.0f;
constexpr float RowHeight = 23.33f;
constexpr float ButtonHeight = 22.0f;

struct SettingsEntry {
    SettingsPage page;
    const char* key;
    const char* label;
    const char* icon;
    const char* groupKey;
    const char* group;
};

constexpr SettingsEntry SettingsEntries[] = {
    { SettingsPage::Accessibility, "menu.accessibility.tab.title", "Accessibility", "hbui/accessibility", nullptr, nullptr },
    { SettingsPage::Keyboard, "menu.keyboardAndMouse.tab.title", "Keyboard & Mouse", "hbui/keyboard-mouse", "options.group.input", "Controls" },
    { SettingsPage::Controller, "menu.controller.tab.title", "Controller", "hbui/cursor_gamepad_brackets", nullptr, nullptr },
    { SettingsPage::Touch, "menu.touch.tab.title", "Touch", "hbui/touch", nullptr, nullptr },
    { SettingsPage::Party, "options.party", "Party", "hbui/party", "options.social", "Social" },
    { SettingsPage::General, "menu.general.tab.title", "General", "hbui/general-icon", "options.general", "General" },
    { SettingsPage::Video, "menu.video.tab.title", "Video", "hbui/World", nullptr, nullptr },
    { SettingsPage::Audio, "menu.audio.tab.title", "Audio", "hbui/sound-block", nullptr, nullptr },
    { SettingsPage::Account, "menu.account.tab.title", "Account", "hbui/account", nullptr, nullptr },
    { SettingsPage::Subscriptions, "options.viewSubscriptions", "Subscriptions", "hbui/subscriptions", nullptr, nullptr },
    { SettingsPage::GlobalResources, "menu.globalpacks", "Global Resources", "hbui/resource-packs-icon", nullptr, nullptr },
    { SettingsPage::Storage, "menu.storage.tab.title", "Storage", "hbui/storage", nullptr, nullptr },
    { SettingsPage::Language, "menu.language.tab.title", "Language", "hbui/language", nullptr, nullptr },
    { SettingsPage::Creator, "menu.creator.tab.title", "Creator", "hbui/Settings", nullptr, nullptr },
};

std::string upperCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

Rect column(float width, float top, float bottom)
{
    float w = std::min(ColumnWidth, width - 16.0f);
    return { std::round((width - w) * 0.5f), top, w, bottom - top };
}

void divider(Context& ui, float x, float y, float width)
{
    ui.fill({ x, y, width, css(2.0f) }, Divider);
}

// The pill the HTML menus put under the label of the active tab.
void tabUnderline(Context& ui, const Rect& tab)
{
    ui.fill({ std::round(tab.x + tab.w * 0.5f - 6.0f), tab.bottom() - 5.0f, 12.0f, 1.0f }, White);
}

}

std::vector<Menu::ServerRow> Menu::featuredRows() const
{
    std::vector<ServerRow> rows;
    for (size_t i = 0; i < std::size(Featured); ++i) {
        rows.push_back({ true, i, Featured[i].name, Featured[i].address, Featured[i].genre });
    }
    return rows;
}

std::vector<Menu::ServerRow> Menu::savedRows() const
{
    std::vector<ServerRow> rows;
    const std::vector<SavedServer>& saved = store.servers();
    for (size_t i = 0; i < saved.size(); ++i) {
        rows.push_back({ false, i, saved[i].name, saved[i].address, {} });
    }
    return rows;
}

std::optional<Menu::ServerRow> Menu::selectedRow() const
{
    if (!selection) {
        return std::nullopt;
    }
    std::vector<ServerRow> rows = selection->featured ? featuredRows() : savedRows();
    if (selection->index >= rows.size()) {
        return std::nullopt;
    }
    return rows[selection->index];
}

float Menu::header(Context& ui, float width, std::string_view heading, bool social)
{
    ui.fill({ 0.0f, 0.0f, width, HeaderHeight - 2.0f }, HeaderBar);
    ui.fill({ 0.0f, HeaderHeight - 2.0f, width, 2.0f }, HeaderEdge);

    Rect back { 0.0f, 0.0f, 106.0f, HeaderHeight - 2.0f };
    Interaction backState = ui.interact("header:back", back);
    ui.sprite({ 48.0f, std::round((HeaderHeight - 2.0f - 14.0f) * 0.5f), 7.0f, 14.0f }, "hbui/arrowBack", backState.hovered ? Color { 90, 90, 90, 255 } : InkDark);
    if (backState.clicked) {
        goBack();
    }

    ui.textCentered(heading, TextStyle::Heading, { 0.0f, 0.0f, width, HeaderHeight - 2.0f }, InkDark);

    if (social) {
        float left = width - 120.0f;
        ui.fill({ left, 0.0f, 1.0f, HeaderHeight - 2.0f }, White);
        Rect button { left + 1.0f, 0.0f, width - left - 1.0f, HeaderHeight - 2.0f };
        Interaction state = ui.interact("header:social", button);
        if (state.hovered) {
            ui.fill(button, { 0, 0, 0, 20 });
        }
        std::string Label = tr("options.social", "Social") + " (0)";
        float textWidth = ui.measure(Label, TextStyle::Ui);
        float x = std::round(button.x + (button.w - textWidth - 9.0f) * 0.5f);
        ui.sprite({ x - 3.0f, std::round((HeaderHeight - 2.0f - 11.0f) * 0.5f), 11.0f, 11.0f }, "hbui/friends");
        ui.text(Label, TextStyle::Ui, x + 11.0f, std::round((HeaderHeight - 2.0f - ui.lineHeight(TextStyle::Ui)) * 0.5f), InkDark);
        if (state.clicked) {
            socialOpen = true;
            socialParty = false;
        }
    }
    return HeaderHeight;
}

float Menu::scrollArea(Context& ui, const Rect& area, float& offset, float contentHeight)
{
    float limit = std::max(0.0f, contentHeight - area.h);
    if (ui.hovered(area) && ui.input().wheel != 0.0f) {
        offset -= ui.input().wheel * 24.0f;
    }
    offset = std::clamp(offset, 0.0f, limit);
    if (limit > 0.0f) {
        Rect track { area.right() - 3.0f, area.y, 3.0f, area.h };
        ui.fill(track, { 0, 0, 0, 120 });
        float thumbHeight = std::max(12.0f, area.h * area.h / contentHeight);
        float thumbY = area.y + (area.h - thumbHeight) * (offset / limit);
        ui.fill({ track.x, thumbY, track.w, thumbHeight }, { 0xef, 0xf0, 0xf1, 255 });
    }
    return offset;
}

bool Menu::textField(Context& ui, std::string_view id, std::string_view placeholder, const std::string& value, const Rect& rect, bool focused)
{
    Interaction state = ui.interact(id, rect);
    if (state.clicked) {
        auto now = std::chrono::steady_clock::now();
        if (lastFieldClick == id && now - lastFieldClickAt < std::chrono::milliseconds(400)) {
            selectAllPending = true;
            lastFieldClick.clear();
        } else {
            lastFieldClick = std::string(id);
            lastFieldClickAt = now;
            selectedField = Field::None;
        }
    }
    ui.border(rect, "baseTextField", focused ? "Focused" : state.hovered ? "Hovered" : "Default");
    float x = rect.x + css(14.0f);
    float y = std::round(rect.y + (rect.h - ui.lineHeight(TextStyle::Ui)) * 0.5f + css(2.0f));
    float room = rect.w - css(26.0f);
    if (value.empty() && !focused) {
        ui.text(placeholder, TextStyle::Ui, x, y, Muted1, room);
    } else {
        bool selected = focused && selectedField != Field::None && selectedField == field && !value.empty();
        if (selected) {
            ui.fill({ x - 1.0f, y - 1.0f, std::min(ui.measure(value, TextStyle::Ui), room) + 2.0f, ui.lineHeight(TextStyle::Ui) + 1.0f }, { 0x3c, 0x8a, 0xd6, 255 });
        }
        ui.text(value, TextStyle::Ui, x, y, White, room);
        if (focused && !selected) {
            float caret = x + std::min(ui.measure(value, TextStyle::Ui), room) + 0.5f;
            ui.fill({ caret, y, 1.0f, ui.lineHeight(TextStyle::Ui) - 1.0f }, Caret);
        }
    }
    return state.clicked;
}

bool Menu::toggle(Context& ui, std::string_view id, const Rect& rect, bool on)
{
    Interaction state = ui.interact(id, rect);
    ui.fill(rect, Divider);
    Rect inner = rect.inset(1.0f);
    ui.fill(inner, on ? Primary : PanelDark);
    Rect knob { on ? inner.right() - inner.h : inner.x, inner.y, inner.h, inner.h };
    ui.fill(knob, state.hovered ? White : Secondary);
    ui.fill({ knob.x, knob.bottom() - 1.0f, knob.w, 1.0f }, { 0x8c, 0x8d, 0x90, 255 });
    if (on) {
        ui.fill({ std::round(inner.x + (inner.w - inner.h) * 0.5f), inner.y + 3.0f, 1.0f, inner.h - 6.0f }, White);
    } else {
        Rect ring { std::round(inner.x + inner.h + (inner.w - inner.h * 2.0f) * 0.5f + inner.h * 0.5f - 2.5f), inner.y + 3.5f, 5.0f, inner.h - 7.0f };
        ui.outline(ring, Muted1);
    }
    return state.clicked;
}

bool Menu::slider(Context& ui, std::string_view id, const Rect& rect, float& fraction)
{
    Interaction state = ui.interact(id, rect);
    float before = fraction;
    constexpr float Knob = 14.0f;
    if (state.pressed || (state.hovered && ui.input().mouseDown)) {
        fraction = std::clamp((ui.mouseX() - rect.x - Knob * 0.5f) / std::max(rect.w - Knob, 1.0f), 0.0f, 1.0f);
    }
    float travel = rect.w - Knob;
    Rect track { rect.x, std::round(rect.y + rect.h * 0.5f - 2.0f), rect.w, 4.0f };
    ui.fill(track, Divider);
    ui.fill(track.inset(1.0f), { 0x8c, 0x8d, 0x90, 255 });
    ui.fill({ track.x + 1.0f, track.y + 1.0f, travel * fraction + Knob * 0.5f, 2.0f }, Primary);
    Rect knob { std::round(rect.x + travel * fraction), std::round(rect.y + (rect.h - Knob) * 0.5f), Knob, Knob };
    ui.fill(knob, Divider);
    ui.fill(knob.inset(1.0f), state.hovered ? White : Secondary);
    ui.fill({ knob.x + 1.0f, knob.bottom() - 2.0f, knob.w - 2.0f, 1.0f }, { 0x8c, 0x8d, 0x90, 255 });
    return fraction != before;
}

void Menu::play(Context& ui, float width, float height)
{
    header(ui, width, upperCase(tr("menu.play", "Play")), true);
    Rect body = column(width, 52.0f, height);

    struct TabInfo {
        PlayTab tab;
        std::string label;
        const char* icon;
    };
    TabInfo tabs[] = {
        { PlayTab::Realms, tr("menu.realms", "Realms"), "hbui/Realms" },
        { PlayTab::Servers, tr("menu.servers", "Servers"), "hbui/server" },
    };
    float tabWidth = body.w / static_cast<float>(std::size(tabs));
    for (size_t i = 0; i < std::size(tabs); ++i) {
        Rect tab { std::round(body.x + tabWidth * static_cast<float>(i)), body.y, std::round(tabWidth), TabHeight };
        bool active = playTab == tabs[i].tab;
        Interaction state = ui.pressable("tab:" + std::to_string(i), "tabBarNeutral", tab, true, active);
        float textWidth = ui.measure(tabs[i].label, TextStyle::Ui);
        constexpr float IconSize = 10.0f;
        float x = std::round(tab.x + (tab.w - textWidth - IconSize - 4.0f) * 0.5f);
        float y = std::round(tab.y + (tab.h - css(4.0f) - ui.lineHeight(TextStyle::Ui)) * 0.5f) + (active ? 1.0f : 0.0f);
        ui.sprite({ x, std::round(y + (ui.lineHeight(TextStyle::Ui) - IconSize) * 0.5f), IconSize, IconSize }, tabs[i].icon);
        ui.text(tabs[i].label, TextStyle::Ui, x + IconSize + 4.0f, y, White);
        if (active) {
            tabUnderline(ui, tab);
        }
        if (state.clicked && !active) {
            playTab = tabs[i].tab;
            listScroll = 0.0f;
            detailScroll = 0.0f;
        }
    }

    Rect area { body.x, body.y + TabHeight + 1.0f, body.w, height - body.y - TabHeight - 1.0f };
    switch (playTab) {
    case PlayTab::Realms:
        realmsTab(ui, area);
        break;
    case PlayTab::Servers:
        serversTab(ui, area);
        break;
    }
}

void Menu::realmsTab(Context& ui, const Rect& area)
{
    ui.fill(area, { 0, 0, 0, 200 });
    Rect banner { area.x, area.y + 5.0f, area.w - 5.0f, 12.0f };
    ui.fill(banner, { 0x28, 0x5f, 0xe0, 255 });
    ui.textCentered("Realms subscriptions can be purchased in the full version of Minecraft.", TextStyle::UiSmall, banner, White);

    float y = banner.bottom() + 5.0f;
    float center = std::round(area.x + area.w * 0.5f);
    if (ui.pressableButton("realms:invites", "pressableElevatedSecondary", tr("realmsInvitationScreen.title", "Invitations"), { center - 128.0f, y, 126.0f, ButtonHeight })) {
        notify("TODO: Realm invitations");
    }
    if (ui.pressableButton("realms:join", "pressableElevatedSecondary", tr("networkWorld.joinByCode", "Join a Realm"), { center + 2.0f, y, 126.0f, ButtonHeight })) {
        notify("TODO: Joining a Realm by code");
    }
    y += ButtonHeight + 8.0f;

    Rect list { area.x, y, area.w - 5.0f, area.bottom() - y };
    if (!signedIn()) {
        ui.textCentered("Sign in with a Microsoft account to see your Realms", TextStyle::Ui, { list.x, list.y + 10.0f, list.w, 20.0f }, Muted0);
        if (ui.pressableButton("realms:signin", "pressableElevatedPrimary", tr("gui.signIn", "Sign In"), { center - 63.0f, list.y + 36.0f, 126.0f, ButtonHeight })) {
            beginSignIn();
        }
        return;
    }
    if (account.realmsLoading || !account.realmsError.empty() || account.realms.empty()) {
        std::string_view message = account.realmsLoading ? "Loading your Realms..." : !account.realmsError.empty() ? std::string_view(account.realmsError) : "You aren't a member of any Realms yet";
        ui.textCentered(message, TextStyle::Ui, { list.x, list.y + 10.0f, list.w, 20.0f }, Muted0);
        return;
    }

    constexpr float Row = 36.0f;
    float content = static_cast<float>(account.realms.size()) * (Row + 4.0f);
    scrollArea(ui, list, listScroll, content);
    ui.setClip(list);
    for (size_t i = 0; i < account.realms.size(); ++i) {
        const RealmEntry& realm = account.realms[i];
        Rect row { list.x, list.y - listScroll + static_cast<float>(i) * (Row + 4.0f), list.w - 4.0f, Row };
        ui.fill(row, Panel);
        ui.sprite({ row.x + 6.0f, row.y + 6.0f, 24.0f, 24.0f }, "hbui/Realms");
        ui.text(realm.name, TextStyle::Ui, row.x + 36.0f, row.y + 7.0f, White, row.w - 140.0f);
        std::string state = realm.expired ? tr("playscreen.realmExpired", "Expired") : realm.open ? realm.detail : tr("realmsSettingsScreen.dropdown.closed.tts", "Closed");
        ui.text(state, TextStyle::UiSmall, row.x + 36.0f, row.y + 20.0f, Muted0, row.w - 140.0f);
        bool joinable = realm.open && !realm.expired;
        if (ui.pressableButton("realm:" + std::to_string(realm.id), "pressableElevatedPrimary", tr("menu.play", "Play"), { row.right() - 86.0f, row.y + 7.0f, 80.0f, ButtonHeight }, TextStyle::HeadingSmall, joinable)) {
            pending = ConnectRequest { realm.name, "realm_id/" + std::to_string(realm.id) };
        }
    }
    ui.clearClip();
}

void Menu::serversTab(Context& ui, const Rect& area)
{
    Rect list { area.x, area.y, 197.0f, area.h };
    Rect detail { list.right() + 13.0f, area.y, area.right() - list.right() - 20.0f, area.h };
    ui.fill(list, { 0, 0, 0, 220 });

    std::vector<ServerRow> featured = featuredRows();
    std::vector<ServerRow> saved = savedRows();
    if (!selection) {
        selection = saved.empty() ? Selection { true, 0 } : Selection { false, 0 };
    }

    float content = 34.0f + 12.0f + RowHeight * static_cast<float>(featured.size()) + 14.0f + 12.0f + RowHeight * static_cast<float>(saved.size());
    Rect view { list.x, list.y + 4.0f, list.w, list.h - 4.0f };
    scrollArea(ui, view, listScroll, content);
    ui.setClip(view);
    float y = view.y - listScroll;

    std::string addLabel = tr("selectServer.add", "Add server");
    if (ui.pressableButton("servers:add", "pressableElevatedSecondary", addLabel, { list.x + 8.67f, y + 5.0f, 174.67f, 20.67f })) {
        openServerForm(std::nullopt);
    }
    ui.sprite({ list.x + 8.67f + 87.0f - ui.measure(addLabel, TextStyle::Ui) * 0.5f - 9.0f, y + 11.0f, 6.0f, 6.0f }, "hbui/Plus", InkDark);
    y += 34.0f;

    auto section = [&](std::string_view label, const std::vector<ServerRow>& rows) {
        ui.text(label, TextStyle::UiSmall, list.x + 7.33f, y, Muted0);
        y += 12.0f;
        for (const ServerRow& row : rows) {
            Rect bounds { list.x, y, list.w - 4.0f, RowHeight };
            bool selected = selection && selection->featured == row.featured && selection->index == row.index;
            Interaction state = ui.interact(std::string(row.featured ? "server:f:" : "server:s:") + std::to_string(row.index), bounds);
            if (selected) {
                ui.fill(bounds, Panel);
            } else if (state.hovered) {
                ui.fill(bounds, { 0x48, 0x49, 0x4a, 140 });
            }
            float textX = bounds.x + 7.33f;
            if (row.featured) {
                ui.sprite({ bounds.x + 8.67f, bounds.y + 5.67f, 12.0f, 12.0f }, "hbui/server");
                textX = bounds.x + 25.0f;
            }
            if (row.detail.empty()) {
                ui.text(row.name, TextStyle::Ui, textX, std::round(bounds.y + (bounds.h - ui.lineHeight(TextStyle::Ui)) * 0.5f), White, bounds.right() - textX - 4.0f);
            } else {
                ui.text(row.name, TextStyle::Ui, textX, bounds.y + 3.0f, White, bounds.right() - textX - 4.0f);
                ui.text(row.detail, TextStyle::UiSmall, textX, bounds.y + 12.67f, Muted0, bounds.right() - textX - 4.0f);
            }
            if (state.clicked) {
                selection = Selection { row.featured, row.index };
                detailScroll = 0.0f;
            }
            y += RowHeight;
        }
    };
    section(tr("thirdPartyWorld.Featured", "Featured experiences") + " (" + std::to_string(featured.size()) + ")", featured);
    y += 6.0f;
    divider(ui, list.x, y, list.w - 4.0f);
    y += 8.0f;
    std::vector<ServerRow> savedWithDetail = saved;
    for (ServerRow& row : savedWithDetail) {
        auto status = serverStatus.find(row.address);
        if (status == serverStatus.end() || !status->second.checked) {
            row.detail = tr("connect.connecting", "Checking connection...");
        } else if (!status->second.online) {
            row.detail = "\xC2\xA7" "c" + tr("disconnectionScreen.title.unableToConnect", "Unable to connect to world");
        } else {
            row.detail = status->second.motd;
        }
    }
    section(tr("thirdPartyWorld.Additional", "Other Servers") + " (" + std::to_string(saved.size()) + ")", savedWithDetail);
    ui.clearClip();

    std::optional<ServerRow> row = selectedRow();
    if (!row) {
        return;
    }
    ui.fill(detail, PanelDark);
    Rect top { detail.x, detail.y, detail.w, 32.0f };
    ui.fill(top, Divider);
    ui.fill(top.inset(1.0f), PanelDark);
    ui.text(row->featured ? tr("thirdPartyWorld.Featured", "Featured server") : tr("menu.servers", "Saved server"), TextStyle::UiSmall, top.x + 8.0f, top.y + 12.0f, Muted0);
    if (ui.pressableButton("server:play", "pressableElevatedPrimary", upperCase(tr("menu.play", "Play")), { top.right() - 13.33f - 157.33f, top.y + 5.0f, 157.33f, 20.0f }, TextStyle::HeadingSmall)) {
        connect(*row);
    }

    float rowY = top.bottom();
    auto value = [&](std::string_view text, std::string_view label) {
        Rect bounds { detail.x, rowY, detail.w, 36.0f };
        ui.text(text, TextStyle::Ui, bounds.x + 8.0f, bounds.y + 8.0f, White, bounds.w - 16.0f);
        ui.text(label, TextStyle::UiSmall, bounds.x + 8.0f, bounds.y + 21.0f, Muted0, bounds.w - 16.0f);
        divider(ui, bounds.x, bounds.bottom() - css(2.0f), bounds.w);
        rowY = bounds.bottom();
    };
    std::string host = row->address;
    std::string port = "19132";
    size_t colon = host.rfind(':');
    if (colon != std::string::npos && host.find(':') == colon) {
        port = host.substr(colon + 1);
        host.resize(colon);
    }
    value(row->name, tr("addServer.enterName", "Server name"));
    if (row->featured) {
        value(row->detail, tr("store.mashup.description", "Description"));
        return;
    }
    value(host, tr("addServer.enterIp", "Server address"));
    value(port, tr("externalServerScreen.serverPortInput", "Server port"));
    Rect bar { detail.x, rowY, detail.w, 36.0f };
    ui.fill(bar, Panel);
    float buttonsX = std::round(bar.x + bar.w * 0.5f - 159.0f);
    if (ui.pressableButton("server:edit", "pressableElevatedSecondary", tr("accessibility.play.editServer", "Edit server"), { buttonsX, bar.y + 7.0f, 156.0f, ButtonHeight })) {
        openServerForm(row->index);
    }
    if (ui.pressableButton("server:delete", "pressableElevatedDestructive", tr("selectServer.delete", "Delete server"), { buttonsX + 162.0f, bar.y + 7.0f, 156.0f, ButtonHeight })) {
        dialog = Dialog::ConfirmDelete;
    }
}

void Menu::serverForm(Context& ui, float width, float height)
{
    (void)height;
    header(ui, width, upperCase(editing ? tr("accessibility.play.editServer", "Edit server") : tr("externalServerScreen.addServer", "Add a new server")), false);
    constexpr float PanelWidth = 522.67f;
    constexpr float RowStep = 50.0f;
    Rect panel { std::round((width - PanelWidth) * 0.5f), 53.33f, PanelWidth, 191.33f };
    ui.fill(panel, Panel);

    struct Entry {
        Field field;
        const char* id;
        std::string label;
        std::string placeholder;
        const std::string* value;
    };
    Entry entries[] = {
        { Field::ServerName, "name", tr("addServer.enterName", "Server name"), tr("addExternalServerScreen.nameTextBoxLabel", "Example server name"), &editName },
        { Field::ServerAddress, "address", tr("addServer.enterIp", "Server address"), "IP (1.0.0.1) or URL (www.example.com)", &editAddress },
        { Field::ServerPort, "port", tr("addExternalServerScreen.portTextBoxLabel", "Port"), "19132", &editPort },
    };
    float y = panel.y;
    for (const Entry& entry : entries) {
        ui.text(entry.label, TextStyle::Ui, panel.x + 12.0f, y + 6.0f, White);
        Rect input { panel.x + 12.0f, y + 18.67f, panel.w - 24.0f, 22.67f };
        if (textField(ui, std::string("form:") + entry.id, entry.placeholder, *entry.value, input, field == entry.field)) {
            field = entry.field;
        }
        y += RowStep;
        ui.fill({ panel.x, y - 3.0f, panel.w, 3.0f }, PanelDark);
    }

    Rect left { panel.x + 4.33f, panel.y + 159.33f, panel.w * 0.5f - 7.0f, ButtonHeight };
    Rect right { panel.x + panel.w * 0.5f + 2.67f, left.y, left.w, ButtonHeight };
    if (editing) {
        if (ui.pressableButton("form:delete", "pressableElevatedDestructive", tr("selectServer.delete", "Delete server"), left)) {
            selection = Selection { false, *editing };
            dialog = Dialog::ConfirmDelete;
        }
        if (ui.pressableButton("form:save", "pressableElevatedPrimary", tr("addExternalServerScreen.saveButtonLabel", "Save changes"), right)) {
            saveServerForm(false);
        }
    } else {
        if (ui.pressableButton("form:add", "pressableElevatedSecondary", tr("selectServer.add", "Add server"), left)) {
            saveServerForm(false);
        }
        if (ui.pressableButton("form:play", "pressableElevatedPrimary", tr("addExternalServerScreen.playButtonLabel", "Add and play"), right)) {
            saveServerForm(true);
        }
    }
}

void Menu::settings(Context& ui, float width, float height)
{
    header(ui, width, upperCase(tr("menu.settings", "Settings")), false);
    Rect sidebar { std::round(width * 0.5f - 314.67f), 53.33f, 196.0f, std::min(402.0f, height - 53.33f - 8.0f) };
    Rect page { sidebar.right() + 17.33f, 53.33f, 408.0f, height - 53.33f - 8.0f };

    ui.fill(sidebar, PanelDark);
    float listHeight = 0.0f;
    for (const SettingsEntry& entry : SettingsEntries) {
        listHeight += entry.group ? 48.0f : 24.0f;
    }
    scrollArea(ui, sidebar, sidebarScroll, listHeight);
    ui.setClip(sidebar);
    float y = sidebar.y - sidebarScroll;
    for (const SettingsEntry& entry : SettingsEntries) {
        if (entry.group) {
            ui.text(tr(entry.groupKey, entry.group), TextStyle::UiSmall, sidebar.x + 8.0f, y + 9.0f, Muted0);
            y += 24.0f;
            divider(ui, sidebar.x, y - css(2.0f), sidebar.w);
        }
        Rect row { sidebar.x, y, sidebar.w - 4.0f, 24.0f };
        bool active = settingsSection == entry.page;
        bool shown = row.bottom() > sidebar.y && row.y < sidebar.bottom();
        Interaction state = shown ? ui.interact(std::string("settings:") + entry.label, row) : Interaction {};
        if (active || state.hovered) {
            ui.fill(row, active ? Panel : Color { 0x48, 0x49, 0x4a, 150 });
        }
        ui.sprite({ row.x + 8.67f, row.y + 8.33f, 7.5f, 7.5f }, entry.icon);
        ui.text(tr(entry.key, entry.label), TextStyle::Ui, row.x + 20.0f, std::round(row.y + (row.h - ui.lineHeight(TextStyle::Ui)) * 0.5f), White, row.w - 24.0f);
        divider(ui, row.x, row.bottom() - css(2.0f), row.w);
        if (state.clicked && !active) {
            settingsSection = entry.page;
            pageScroll = 0.0f;
            rebinding.reset();
        }
        y += 24.0f;
    }
    ui.clearClip();

    ui.fill({ sidebar.right() + 3.33f, sidebar.y, 3.0f, sidebar.h }, { 0xef, 0xf0, 0xf1, 255 });
    settingsPage(ui, page);
}

void Menu::settingsHeading(Context& ui, float x, float& y, float width, std::string_view heading, std::string_view detail)
{
    y += 14.0f;
    std::string upper = upperCase(std::string(heading));
    ui.text(upper, TextStyle::HeadingSmall, x + 12.0f, y, White, width - 24.0f);
    y += std::max(10.0f, ui.lineHeight(TextStyle::HeadingSmall)) + 3.0f;
    if (!detail.empty()) {
        ui.text(detail, TextStyle::UiSmall, x + 12.0f, y, Muted0, width - 24.0f);
        y += 12.0f;
    }
    y += 6.0f;
    divider(ui, x, y - css(2.0f), width);
}

void Menu::settingsRow(Context& ui, float x, float& y, float width, std::string_view label, std::string_view detail, float controlHeight)
{
    float labelY = detail.empty() && controlHeight > 24.0f ? std::round(y + (controlHeight - ui.lineHeight(TextStyle::Ui)) * 0.5f) : y + 7.0f;
    ui.text(label, TextStyle::Ui, x + 12.0f, labelY, White, width - 90.0f);
    float height = 18.0f;
    if (!detail.empty()) {
        height += ui.paragraph(detail, TextStyle::UiSmall, x + 12.0f, y + 17.0f, width - 90.0f, Muted0);
    }
    y += std::max(height + 6.0f, controlHeight);
    divider(ui, x, y - css(2.0f), width);
}

void Menu::todoRow(Context& ui, float x, float& y, float width, std::string_view label)
{
    settingsRow(ui, x, y, width, label, "TODO", 31.33f);
}

void Menu::settingsPage(Context& ui, const Rect& area)
{
    Rect view { area.x, area.y, area.w + 7.0f, area.h };
    ui.fill(area, Panel);
    scrollArea(ui, view, pageScroll, pageContent);
    ui.setClip(area);
    float x = area.x;
    float w = area.w;
    float y = area.y - pageScroll;

    const SettingsEntry* entry = nullptr;
    for (const SettingsEntry& candidate : SettingsEntries) {
        if (candidate.page == settingsSection) {
            entry = &candidate;
        }
    }

    switch (settingsSection) {
    case SettingsPage::Keyboard: {
        settingsHeading(ui, x, y, w, tr("menu.keyboardAndMouse.tab.title", "Keyboard & Mouse"), tr("menu.keyboardAndMouse.tab.description", "Input options and key mapping for keyboard and mouse"));
        settingsHeading(ui, x, y, w, tr("menu.keyboardAndMouse.tab.mappings.title", "Keyboard & Mouse Mappings"), tr("menu.keyboardAndMouse.tab.mappings.description", "Click a key, then press the new one. Esc cancels."));
        for (size_t i = 0; i < KeyBindings::Count; ++i) {
            float rowY = y;
            bool waiting = rebinding && *rebinding == i;
            settingsRow(ui, x, y, w, tr(KeyBindings::translationKey(i), KeyBindings::label(i)), {}, 31.33f);
            std::string label = waiting ? tr("options.pressKey", "Press a key...") : std::string(keyName(bindings.keys[i]));
            if (ui.pressableButton(std::string("bind:") + KeyBindings::id(i), waiting ? "pressableElevatedPrimary" : "pressableElevatedSecondary", label, { x + w - 12.0f - 68.0f, rowY + 5.0f, 68.0f, 20.0f })) {
                rebinding = i;
            }
        }
        float rowY = y;
        settingsRow(ui, x, y, w, tr("options.keyboardAndMouse.resetSettings", "Reset settings to default"), tr("options.keyboardAndMouse.resetSettings.description", "Restore all the above keyboard & mouse actions to their original values"), 31.33f);
        if (ui.pressableButton("bind:reset", "pressableElevatedSecondary", tr("options.keyboardAndMouse.resetSettings.buttonLabel", "Reset"), { x + w - 12.0f - 68.0f, rowY + 5.0f, 68.0f, 20.0f })) {
            bindings = KeyBindings {};
            rebinding.reset();
        }
        break;
    }
    case SettingsPage::Video: {
        settingsHeading(ui, x, y, w, tr("menu.video.tab.title", "Video"), tr("menu.video.tab.description", "Adjust graphics and visual quality"));
        float rowY = y;
        settingsRow(ui, x, y, w, tr("options.renderDistance", "Render distance"), tr("options.renderDistance.description", "How far away chunks are drawn"), 44.0f);
        std::string distance = trf("options.raytracing.renderDistanceFormat", "%s chunks", { std::to_string(chunkDistance) });
        ui.text(distance, TextStyle::Ui, x + w - 12.0f - ui.measure(distance, TextStyle::Ui), rowY + 7.0f, White);
        float distanceFraction = float(chunkDistance - MinRenderDistance) / float(MaxRenderDistance - MinRenderDistance);
        if (slider(ui, "video:distance", { x + 12.0f, rowY + 26.0f, w - 24.0f, 14.0f }, distanceFraction)) {
            chunkDistance = MinRenderDistance + int(std::lround(distanceFraction * float(MaxRenderDistance - MinRenderDistance)));
        }

        rowY = y;
        settingsRow(ui, x, y, w, tr("options.fov.name", "Field of view"), tr("options.fov.description", "How wide the view is, in degrees"), 44.0f);
        std::string degrees = trf("options.fov.format", "%s", { std::to_string(fieldOfView) });
        ui.text(degrees, TextStyle::Ui, x + w - 12.0f - ui.measure(degrees, TextStyle::Ui), rowY + 7.0f, White);
        float fovFraction = float(fieldOfView - MinFov) / float(MaxFov - MinFov);
        if (slider(ui, "video:fov", { x + 12.0f, rowY + 26.0f, w - 24.0f, 14.0f }, fovFraction)) {
            fieldOfView = MinFov + int(std::lround(fovFraction * float(MaxFov - MinFov)));
        }

        rowY = y;
        settingsRow(ui, x, y, w, tr("options.framerateLimit", "Max framerate"), "Caps how many frames are drawn each second", 44.0f);
        int fpsSteps = (MaxMaxFps - MinMaxFps) / MaxFpsStep + 1;
        int fpsStep = fpsLimit == UnlimitedFps ? fpsSteps : (std::clamp(fpsLimit, MinMaxFps, MaxMaxFps) - MinMaxFps) / MaxFpsStep;
        std::string fps = fpsLimit == UnlimitedFps ? tr("options.framerateLimit.max", "Unlimited") : std::to_string(fpsLimit);
        ui.text(fps, TextStyle::Ui, x + w - 12.0f - ui.measure(fps, TextStyle::Ui), rowY + 7.0f, White);
        float fpsFraction = float(fpsStep) / float(fpsSteps);
        if (slider(ui, "video:fps", { x + 12.0f, rowY + 26.0f, w - 24.0f, 14.0f }, fpsFraction)) {
            int step = int(std::lround(fpsFraction * float(fpsSteps)));
            fpsLimit = step >= fpsSteps ? UnlimitedFps : MinMaxFps + step * MaxFpsStep;
        }

        rowY = y;
        settingsRow(ui, x, y, w, tr("options.guiScale.optionName", "GUI scale modifier"), "Makes every menu larger on top of the automatic scale", 60.0f);
        float segment = std::floor((w - 24.0f) / static_cast<float>(std::size(InterfaceScales)));
        for (size_t i = 0; i < std::size(InterfaceScales); ++i) {
            Rect option { x + 12.0f + segment * static_cast<float>(i), rowY + 33.0f, segment, 20.0f };
            bool active = scale == InterfaceScales[i];
            if (ui.pressableButton(std::string("scale:") + InterfaceScaleLabels[i], active ? "pressableElevatedPrimary" : "pressableElevatedSecondary", InterfaceScaleLabels[i], option)) {
                scale = InterfaceScales[i];
            }
        }
        todoRow(ui, x, y, w, tr("options.gamma", "Brightness"));
        break;
    }
    case SettingsPage::Audio: {
        settingsHeading(ui, x, y, w, tr("menu.audio.tab.title", "Audio"), tr("menu.audio.tab.description", "Adjust the volume of each kind of sound"));
        struct Channel {
            const char* key;
            const char* label;
            const char* detail;
        };
        static constexpr Channel Channels[VolumeChannelCount] = {
            { "soundCategory.main", "Main volume", "Every sound" },
            { "soundCategory.music", "Music", "Menu and game music" },
            { "soundCategory.ambient", "Ambient & environment", "Caves, biomes and underwater" },
            { "soundCategory.weather", "Weather", "Rain and thunder" },
            { "soundCategory.block", "Blocks", "Breaking, placing and using blocks" },
            { "soundCategory.hostile", "Hostile creatures", "Monsters" },
            { "soundCategory.neutral", "Friendly creatures", "Animals and villagers" },
            { "soundCategory.player", "Players", "Steps, hits and other players" },
            { "soundCategory.record", "Jukebox & note blocks", "Records and notes" },
            { "soundCategory.ui", "Interface", "Buttons and menus" },
        };
        for (size_t i = 0; i < VolumeChannelCount; ++i) {
            float rowY = y;
            std::string key = Channels[i].key;
            settingsRow(ui, x, y, w, tr(key, Channels[i].label), tr(key + ".description", Channels[i].detail), 44.0f);
            std::string percent = std::to_string(volumes[i]) + "%";
            ui.text(percent, TextStyle::Ui, x + w - 12.0f - ui.measure(percent, TextStyle::Ui), rowY + 7.0f, White);
            float fraction = float(volumes[i]) / 100.0f;
            if (slider(ui, "audio:" + std::to_string(i), { x + 12.0f, rowY + 26.0f, w - 24.0f, 14.0f }, fraction)) {
                volumes[i] = int(std::lround(fraction * 100.0f));
            }
        }
        break;
    }
    case SettingsPage::Account: {
        settingsHeading(ui, x, y, w, tr("menu.account.tab.title", "Account"), tr("menu.account.tab.description", "Access your account information"));
        float rowY = y;
        settingsRow(ui, x, y, w, signedIn() ? tr("menu.account.gamertag.title", "Gamertag") + ": " + displayName : tr("authentication.signInRequired", "Not signed in"), {}, 27.33f);
        ui.sprite({ x + w - 12.0f - 16.0f, rowY + 6.0f, 16.0f, 16.0f }, ui.skin().sprite("dynamic/avatar").valid ? "dynamic/avatar" : "ui/profile_glyph_color");
        rowY = y;
        if (signedIn()) {
            settingsRow(ui, x, y, w, tr("menu.account.signOutOfMicrosoft.title", "Sign out of your Microsoft account"), {}, 36.0f);
            if (ui.pressableButton("account:signout", "pressableElevatedSecondary", tr("menu.account.signOutOfMicrosoft.buttonLabel", "Sign Out"), { x + w - 12.0f - 68.0f, rowY + 8.0f, 68.0f, 20.0f })) {
                accountRequest = AccountRequest::SignOut;
                notify("Signed out");
            }
        } else {
            settingsRow(ui, x, y, w, tr("menu.account.signIn.title", "Sign in with a Microsoft account"), "Needed for online servers and Realms", 36.0f);
            if (ui.pressableButton("account:signin", "pressableElevatedPrimary", tr("menu.account.signIn.buttonLabel", "Sign In"), { x + w - 12.0f - 68.0f, rowY + 8.0f, 68.0f, 20.0f })) {
                beginSignIn();
            }
        }
        rowY = y;
        settingsRow(ui, x, y, w, "Quit Kestrel", {}, 36.0f);
        if (ui.pressableButton("account:quit", "pressableElevatedDestructive", tr("globalPauseScreen.quit", "Quit"), { x + w - 12.0f - 68.0f, rowY + 8.0f, 68.0f, 20.0f })) {
            dialog = Dialog::ConfirmExit;
        }
        break;
    }
    case SettingsPage::Language: {
        settingsHeading(ui, x, y, w, tr("menu.language.tab.title", "Language"), tr("menu.language.tab.description", "Select your preferred language for Minecraft"));
        for (const LanguageInfo& language : Localization::shared().languages()) {
            Rect row { x + 12.0f, y + 3.0f, w - 24.0f, 22.0f };
            bool active = language.code == languageCode;
            if (ui.pressableButton("language:" + language.code, active ? "pressableElevatedPrimary" : "pressableElevatedSecondary", language.name, row)) {
                languageCode = language.code;
            }
            y += 26.0f;
        }
        y += 6.0f;
        break;
    }
    default:
        settingsHeading(ui, x, y, w, entry ? tr(entry->key, entry->label) : tr("menu.settings", "Settings"), {});
        ui.textCentered("TODO", TextStyle::Heading, { x, y + 20.0f, w, 20.0f }, Muted0);
        y += 40.0f;
        break;
    }
    ui.clearClip();
    pageContent = y + pageScroll - area.y + 8.0f;
}

void Menu::todoScreen(Context& ui, float width, float height, std::string_view heading)
{
    header(ui, width, upperCase(std::string(heading)), heading != tr("profileScreen.header", "Dressing Room"));
    Rect panel = column(width, 53.33f, height - 8.0f);
    ui.fill(panel, PanelDark);
    ui.textCentered("TODO", TextStyle::HeadingLarge, { panel.x, panel.y + panel.h * 0.4f, panel.w, 20.0f }, White);
    ui.textCentered(std::string(heading) + " isn't implemented in Kestrel yet", TextStyle::Ui, { panel.x, panel.y + panel.h * 0.4f + 24.0f, panel.w, 12.0f }, Muted0);
}

void Menu::socialDrawer(Context& ui, float width, float height)
{
    ui.fill({ 0.0f, 0.0f, width, height }, { 0, 0, 0, 150 });
    Rect panel { width - 1.0f - 186.67f, 28.0f, 186.67f, height - 29.0f };
    if (socialArmed && dialog == Dialog::None && ui.input().mousePressed && !panel.contains(ui.mouseX(), ui.mouseY())) {
        socialOpen = false;
        socialArmed = false;
        return;
    }
    socialArmed = !ui.input().mouseDown;
    ui.fill(panel, Divider);
    Rect inner = panel.inset(2.0f);
    ui.fill(inner, PanelDark);

    Rect search { inner.x + 2.0f, inner.y + 2.0f, inner.w - 29.0f, 22.67f };
    if (textField(ui, "social:search", tr("store.search.button", "Search for people"), socialSearch, search, field == Field::SocialSearch)) {
        field = Field::SocialSearch;
    }
    Rect close { search.right() + 2.0f, search.y, 23.0f, 22.67f };
    if (ui.pressable("social:close", "pressableElevatedSecondary", close).clicked) {
        socialOpen = false;
    }
    ui.sprite({ close.x + 8.0f, close.y + 7.0f, 7.0f, 7.0f }, "hbui/Close", InkDark);

    Rect tabs { inner.x + 2.0f, search.bottom() + 3.0f, inner.w - 4.0f, 21.0f };
    Rect peopleTab { tabs.x, tabs.y, std::round(tabs.w * 0.5f), tabs.h };
    Rect partyTab { peopleTab.right(), tabs.y, tabs.w - peopleTab.w, tabs.h };
    if (ui.pressable("social:people", "tabBarNeutral", peopleTab, true, !socialParty).clicked) {
        socialParty = false;
    }
    ui.sprite({ peopleTab.x + peopleTab.w * 0.5f - 4.0f, peopleTab.y + 5.0f, 8.0f, 8.0f }, "hbui/friends");
    if (ui.pressable("social:party", "tabBarNeutral", partyTab, true, socialParty).clicked) {
        socialParty = true;
    }
    ui.sprite({ partyTab.x + partyTab.w * 0.5f - 4.0f, partyTab.y + 5.0f, 8.0f, 8.0f }, "hbui/party");
    tabUnderline(ui, socialParty ? partyTab : peopleTab);

    float y = tabs.bottom() + 4.0f;
    ui.textCentered(upperCase(socialParty ? tr("options.party", "Party") : tr("networkWorld.friends_label", "People")), TextStyle::Heading, { inner.x, y, inner.w, 12.0f }, White);
    y += 16.0f;
    if (socialParty) {
        ui.textCentered("No parties available", TextStyle::Ui, { inner.x, inner.y + inner.h * 0.4f, inner.w, 12.0f }, White);
        if (ui.pressableButton("social:create", "pressableElevatedSecondary", "Create party", { inner.x + 8.0f, inner.y + inner.h * 0.4f + 42.0f, inner.w - 16.0f, ButtonHeight })) {
            notify("TODO: Parties");
        }
        return;
    }

    Rect you { inner.x + 4.0f, y, inner.w - 8.0f, 32.0f };
    ui.fill(you, Panel);
    bool hasAvatar = ui.skin().sprite("dynamic/avatar").valid;
    ui.sprite({ you.x + 4.0f, you.y + 4.0f, 24.0f, 24.0f }, hasAvatar ? "dynamic/avatar" : "ui/profile_glyph_color");
    ui.text(displayName + " (You)", TextStyle::Ui, you.x + 32.0f, you.y + 7.0f, White, you.w - 36.0f);
    ui.text(inGame() ? tr("menu.servers", "Playing on a server") : tr("accessibility.screenName.start", "In the Minecraft Menus"), TextStyle::UiSmall, you.x + 32.0f, you.y + 18.0f, Muted0, you.w - 36.0f);
    y = you.bottom() + 4.0f;

    if (ui.pressableButton("social:requests", "pressableElevatedSecondary", "Friend requests", { you.x, y, you.w, ButtonHeight })) {
        notify("TODO: Friend requests");
    }
    y += ButtonHeight + 6.0f;
    ui.fill({ you.x, y, you.w, 10.0f }, Primary);
    ui.text(tr("invite.OnlineFriends", "Online") + " (0)", TextStyle::UiSmall, you.x + 3.0f, y + 1.0f, White);
    y += 12.0f;
    ui.textCentered("TODO: Friends list", TextStyle::Ui, { you.x, y + 4.0f, you.w, 12.0f }, Muted0);
}

}
