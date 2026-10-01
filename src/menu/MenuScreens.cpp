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

constexpr const char* ExperiencePrefix = "experience_id/";
constexpr auto ShowcaseInterval = std::chrono::seconds(6);

constexpr float InterfaceScales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
constexpr const char* InterfaceScaleLabels[] = { "100%", "125%", "150%", "200%" };

constexpr float HeaderHeight = 48.0f;
constexpr float ColumnWidth = 631.0f;
constexpr float TabHeight = 37.0f;
constexpr float RowHeight = 23.33f;
constexpr float ButtonHeight = 22.0f;
constexpr float SectionGap = 10.0f;

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
    { SettingsPage::Mods, "kestrel.settings.mods", "Mods", "hbui/resource-packs-icon", nullptr, nullptr },
    { SettingsPage::GlobalResources, "menu.globalpacks", "Global Resources", "hbui/resource-packs-icon", nullptr, nullptr },
    { SettingsPage::Storage, "menu.storage.tab.title", "Storage", "hbui/storage", nullptr, nullptr },
    { SettingsPage::Language, "menu.language.tab.title", "Language", "hbui/language", nullptr, nullptr },
    { SettingsPage::Creator, "menu.creator.tab.title", "Creator", "hbui/Settings", nullptr, nullptr },
};

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

std::vector<Menu::ServerRow> Menu::featuredRows(ServerGroup group) const
{
    std::vector<ServerRow> rows;
    for (size_t i = 0; i < featured.size(); ++i) {
        const FeaturedEntry& entry = featured[i];
        bool creator = entry.address.empty();
        if (creator != (group == ServerGroup::Creator)) {
            continue;
        }
        std::string detail = entry.creator;
        auto status = serverStatus.find(entry.address);
        if (!creator && status != serverStatus.end() && status->second.online && !status->second.motd.empty()) {
            detail = status->second.motd;
        }
        rows.push_back({ group, i, entry.name, ExperiencePrefix + entry.id, std::move(detail), entry.icon });
    }
    return rows;
}

std::vector<Menu::ServerRow> Menu::savedRows() const
{
    std::vector<ServerRow> rows;
    const std::vector<SavedServer>& saved = store.servers();
    for (size_t i = 0; i < saved.size(); ++i) {
        rows.push_back({ ServerGroup::Saved, i, saved[i].name, saved[i].address, {}, {} });
    }
    return rows;
}

std::optional<Menu::ServerRow> Menu::selectedRow() const
{
    if (!selection) {
        return std::nullopt;
    }
    std::vector<ServerRow> rows = selection->group == ServerGroup::Saved ? savedRows() : featuredRows(selection->group);
    for (ServerRow& row : rows) {
        if (row.index == selection->index) {
            return std::move(row);
        }
    }
    return std::nullopt;
}

std::optional<std::string> Menu::focusedFeatured() const
{
    if (screen != Screen::Play || playTab != PlayTab::Servers || !selection || selection->group == ServerGroup::Saved || selection->index >= featured.size()) {
        return std::nullopt;
    }
    return featured[selection->index].id;
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
        std::string Label = tr("options.social", "Social") + " (" + std::to_string(onlineCount(this->social.friends)) + ")";
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
    std::string invitesLabel = tr("hbui.JoinRealmsServerInvitationsButton.invitationsName", "Invitations");
    if (!social.invites.empty()) {
        invitesLabel += " (" + std::to_string(social.invites.size()) + ")";
    }
    if (ui.pressableButton("realms:invites", "pressableElevatedSecondary", invitesLabel, { center - 128.0f, y, 126.0f, ButtonHeight })) {
        dialog = Dialog::RealmInvites;
        requestSocial(SocialAction::RefreshInvites);
    }
    if (ui.pressableButton("realms:join", "pressableElevatedSecondary", tr("hbui.PlayScreen.realmsTab.buttonHeader.joinRealm", "Join a Realm"), { center + 2.0f, y, 126.0f, ButtonHeight })) {
        openJoinRealm();
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
    if (account.realms.empty()) {
        std::string message = account.realmsLoading ? tr("hbui.Realms.JoinRealmModals.fetchRealmInProgress", "Fetching Realm")
            : !account.realmsError.empty()          ? tr("hbui.JoinRealmsServerError.unknown.message", "Please try again later.")
                                                    : tr("kestrel.realms.noRealms", "You aren't a member of any Realms yet.");
        ui.textCentered(message, TextStyle::Ui, { list.x, list.y + 10.0f, list.w, 20.0f }, Muted0);
        if (!account.realmsLoading && !account.realmsError.empty() && ui.pressableButton("realms:retry", "pressableElevatedSecondary", tr("hbui.AddFriendError.tryAgain", "Try again"), { center - 63.0f, list.y + 36.0f, 126.0f, ButtonHeight })) {
            realmsRefreshRequested = true;
        }
        return;
    }
    if (!account.realmsError.empty() && !account.realmsLoading) {
        ui.text(tr("kestrel.realms.stale", "Couldn't update your Realms. The list may be out of date."), TextStyle::UiSmall, list.x, list.y, Muted1, list.w);
        list.y += 12.0f;
        list.h -= 12.0f;
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

    std::vector<ServerRow> featuredList = featuredRows(ServerGroup::Featured);
    std::vector<ServerRow> creatorList = featuredRows(ServerGroup::Creator);
    std::vector<ServerRow> saved = savedRows();
    if (!selectedRow()) {
        const std::vector<ServerRow>& first = !saved.empty() ? saved : !featuredList.empty() ? featuredList : creatorList;
        selection = first.empty() ? std::nullopt : std::optional<Selection>(Selection { first.front().group, first.front().index });
    }

    bool fetching = featuredLoading && featured.empty();
    float partnerRows = fetching ? 1.0f : static_cast<float>(featuredList.size() + creatorList.size());
    bool bothPartnerSections = (fetching || !featuredList.empty()) && !creatorList.empty();
    float sections = (fetching || !featuredList.empty() ? 12.0f : 0.0f) + (!creatorList.empty() ? 12.0f : 0.0f) + (bothPartnerSections ? SectionGap : 0.0f);
    float content = 34.0f + sections + RowHeight * partnerRows + 14.0f + 12.0f + RowHeight * static_cast<float>(saved.size());
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
            bool selected = selection && selection->group == row.group && selection->index == row.index;
            Interaction state = ui.interact("server:" + std::to_string(static_cast<int>(row.group)) + ":" + std::to_string(row.index), bounds);
            if (selected) {
                ui.fill(bounds, Panel);
            } else if (state.hovered) {
                ui.fill(bounds, { 0x48, 0x49, 0x4a, 140 });
            }
            float textX = bounds.x + 7.33f;
            if (row.group != ServerGroup::Saved) {
                ui.sprite({ bounds.x + 6.0f, bounds.y + 3.67f, 16.0f, 16.0f }, row.icon.empty() ? "hbui/server" : row.icon);
                textX = bounds.x + 27.0f;
            }
            if (row.detail.empty()) {
                ui.text(row.name, TextStyle::Ui, textX, std::round(bounds.y + (bounds.h - ui.lineHeight(TextStyle::Ui)) * 0.5f), White, bounds.right() - textX - 4.0f);
            } else {
                ui.text(row.name, TextStyle::Ui, textX, bounds.y + 3.0f, White, bounds.right() - textX - 4.0f);
                ui.text(row.detail, TextStyle::UiSmall, textX, bounds.y + 12.67f, Muted0, bounds.right() - textX - 4.0f);
            }
            if (state.clicked && !selected) {
                selection = Selection { row.group, row.index };
                serverAddressShown = false;
                detailScroll = 0.0f;
                showcaseIndex = 0;
                showcaseShown = std::chrono::steady_clock::now();
            }
            y += RowHeight;
        }
    };
    if (fetching) {
        ui.text(trf("hbui.PlayScreen.serverTab.featuredServer", "Featured experiences (%1$s)", { "0" }), TextStyle::UiSmall, list.x + 7.33f, y, Muted0);
        ui.text(tr("thirdPartyWorld.loadingFeaturedServers", "Fetching Servers..."), TextStyle::Ui, list.x + 7.33f, y + 12.0f + 6.0f, Muted1);
        y += 12.0f + RowHeight;
    }
    if (!featuredList.empty()) {
        section(trf("hbui.PlayScreen.serverTab.featuredServer", "Featured experiences (%1$s)", { std::to_string(featuredList.size()) }), featuredList);
    }
    if (bothPartnerSections) {
        y += SectionGap;
    }
    if (!creatorList.empty()) {
        section(trf("hbui.PlayScreen.serverTab.creatorServer", "Creator experiences (%1$s)", { std::to_string(creatorList.size()) }), creatorList);
    }
    y += 6.0f;
    divider(ui, list.x, y, list.w - 4.0f);
    y += 8.0f;
    for (ServerRow& row : saved) {
        auto status = serverStatus.find(row.address);
        if (status == serverStatus.end() || !status->second.checked) {
            row.detail = tr("connect.connecting", "Checking connection...");
        } else if (!status->second.online) {
            row.detail = "\xC2\xA7" "c" + tr("disconnectionScreen.title.unableToConnect", "Unable to connect to world");
        } else {
            row.detail = status->second.motd;
        }
    }
    section(tr("thirdPartyWorld.Additional", "Other Servers") + " (" + std::to_string(saved.size()) + ")", saved);
    ui.clearClip();

    std::optional<ServerRow> row = selectedRow();
    if (!row) {
        return;
    }
    if (row->group != ServerGroup::Saved) {
        featuredDetail(ui, detail, featured[row->index]);
        return;
    }
    auto status = serverStatus.find(row->address);
    bool online = status != serverStatus.end() && status->second.online;
    int latency = online ? status->second.latencyMs : -1;
    bool highPing = latency >= 300;
    std::string warning = tr("hbui.PlayScreen.serverTab.ServerNotifications.highPingWarning", "You don't have a strong connection to the chosen server. Your experience may be impacted.");
    float warningHeight = highPing ? ui.paragraphHeight(warning, TextStyle::Pixel, detail.w - 16.0f) + 6.0f : 0.0f;
    float contentHeight = warningHeight + (highPing ? 4.0f : 0.0f) + 32.0f + 108.0f + 41.0f;
    scrollArea(ui, detail, detailScroll, contentHeight);
    ui.setClip(detail);
    float topY = detail.y - detailScroll;
    if (highPing) {
        ui.fill({ detail.x, topY, detail.w, warningHeight }, { 255, 235, 99, 255 });
        ui.paragraph(warning, TextStyle::Pixel, detail.x + 8.0f, topY + 3.0f, detail.w - 16.0f, InkDark);
        topY += warningHeight + 4.0f;
    }
    Rect top { detail.x, topY, detail.w, 32.0f };
    ui.fill(top, { 31, 31, 31, 255 });
    float playWidth = std::min(157.33f, top.w * 0.42f);
    Rect play { top.right() - 13.0f - playWidth, top.y + 5.0f, playWidth, 22.0f };
    const char* pingKey = latency < 0 ? "unavailablePing" : latency < 150 ? "lowPing" : latency < 300 ? "mediumPing" : "highPing";
    const char* pingFallback = latency < 0 ? "Unavailable ping" : latency < 150 ? "Low ping" : latency < 300 ? "Medium ping" : "High ping";
    std::string ping = tr(std::string("hbui.PlayScreen.serverTab.ServerDescription.") + pingKey, pingFallback);
    float statusX = top.x + 12.0f;
    int bars = latency < 0 ? 0 : latency < 150 ? 3 : latency < 300 ? 2 : 1;
    Color signal = latency < 150 ? Color { 126, 214, 50, 255 } : latency < 300 ? Color { 255, 224, 0, 255 } : Color { 255, 120, 140, 255 };
    for (int i = 0; i < 3; ++i) {
        float h = 2.0f + i * 2.0f;
        ui.fill({ statusX + i * 3.0f, top.y + 19.0f - h, 2.0f, h }, i < bars ? signal : Muted0);
    }
    float pingWidth = std::min(ui.measure(ping, TextStyle::Pixel), std::max(1.0f, play.x - statusX - 52.0f));
    ui.text(ping, TextStyle::Pixel, statusX + 11.0f, top.y + 12.0f, White, pingWidth);
    float playersX = statusX + 11.0f + pingWidth + 12.0f;
    ui.sprite({ playersX, top.y + 10.0f, 12.0f, 12.0f }, "ui/FriendsIcon");
    ui.text(online ? std::to_string(status->second.players) : "--", TextStyle::Pixel, playersX + 14.0f, top.y + 12.0f, White, std::max(1.0f, play.x - playersX - 18.0f));
    if (ui.pressableButton("server:play", "pressableElevatedPrimary", upperCase(tr("menu.play", "Play")), play, TextStyle::HeadingSmall)) {
        connect(*row);
    }

    float rowY = top.bottom();
    auto value = [&](std::string_view text, std::string_view label, bool address = false) {
        Rect bounds { detail.x, rowY, detail.w, 36.0f };
        ui.fill(bounds, PanelDark);
        float textWidth = bounds.w - 24.0f - (address ? 68.0f : 0.0f);
        ui.text(text, TextStyle::Pixel, bounds.x + 12.0f, bounds.y + 8.0f, White, textWidth);
        ui.text(label, TextStyle::Ui, bounds.x + 12.0f, bounds.y + 22.0f, Muted0, textWidth);
        if (address && ui.classicButton("server:address", serverAddressShown ? tr("hbui.PlayScreen.serverTab.externalServerDetails.hideButton", "Hide") : tr("hbui.PlayScreen.serverTab.externalServerDetails.showButton", "Show"),
                { bounds.right() - 71.0f, bounds.y + 6.0f, 59.0f, 24.0f })) {
            serverAddressShown = !serverAddressShown;
        }
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
    value(row->name, tr("hbui.PlayScreen.serverTab.externalServerDetails.name", "Server name"));
    value(serverAddressShown ? host : tr("hbui.PlayScreen.serverTab.externalServerDetails.placeholder", "XX.XXX.XXX.XXX"), tr("hbui.PlayScreen.serverTab.externalServerDetails.address", "Server address"), true);
    value(port, tr("hbui.PlayScreen.serverTab.externalServerDetails.port", "Server port"));
    Rect bar { detail.x, rowY, detail.w, 41.0f };
    ui.fill(bar, Divider);
    ui.fill(bar.inset(1.0f), Panel);
    float editWidth = std::min(160.0f, bar.w - 24.0f);
    if (ui.classicButton("server:edit", tr("hbui.PlayScreen.serverTab.externalServerDetails.editButton", "Edit server"), { std::round(bar.x + (bar.w - editWidth) * 0.5f), bar.y + 8.0f, editWidth, 24.0f })) {
        openServerForm(row->index);
    }
    ui.clearClip();
}

void Menu::featuredDetail(Context& ui, const Rect& area, const FeaturedEntry& entry)
{
    scrollArea(ui, area, detailScroll, detailContent);
    ui.setClip(area);
    ui.fill(area, { 48, 48, 48, 255 });
    float x = area.x + 12.0f;
    float width = area.w - 24.0f;
    float y = area.y - detailScroll;

    Rect showcase { area.x, y, area.w, std::round(area.w * 0.3f) };
    ui.fill(showcase, InkDark);
    if (!entry.showcase.empty()) {
        auto now = std::chrono::steady_clock::now();
        if (now - showcaseShown >= ShowcaseInterval) {
            showcaseIndex = (showcaseIndex + 1) % entry.showcase.size();
            showcaseShown = now;
        }
        showcaseIndex %= entry.showcase.size();
        const std::string& name = entry.showcase[showcaseIndex];
        const Sprite& image = ui.skin().sprite(name);
        if (image.valid && image.width > 0.0f && image.height > 0.0f) {
            float fit = std::max(showcase.w / image.width, showcase.h / image.height);
            float spanX = showcase.w / fit, spanY = showcase.h / fit;
            ui.spriteRegion(showcase, name, { (image.width - spanX) * 0.5f, (image.height - spanY) * 0.5f, spanX, spanY });
        }
        if (entry.showcase.size() > 1 && ui.hovered(showcase)) {
            auto arrow = [&](const char* id, const char* icon, float arrowX, int step) {
                Rect button { arrowX, std::round(showcase.y + (showcase.h - 20.0f) * 0.5f), 20.0f, 20.0f };
                Interaction state = ui.interact(id, button);
                ui.fill(button, { 0, 0, 0, static_cast<uint8_t>(state.hovered ? 200 : 130) });
                ui.sprite(button.inset(5.0f), icon);
                if (state.clicked) {
                    size_t count = entry.showcase.size();
                    showcaseIndex = (showcaseIndex + count + static_cast<size_t>(step + static_cast<int>(count))) % count;
                    showcaseShown = std::chrono::steady_clock::now();
                }
            };
            arrow("showcase:previous", "hbui/ArrowLeft", showcase.x + 4.0f, -1);
            arrow("showcase:next", "hbui/ArrowRight", showcase.right() - 24.0f, 1);
            float dotsX = std::round(showcase.x + (showcase.w - static_cast<float>(entry.showcase.size()) * 7.0f) * 0.5f);
            for (size_t i = 0; i < entry.showcase.size(); ++i) {
                ui.fill({ dotsX + static_cast<float>(i) * 7.0f, showcase.bottom() - 8.0f, 4.0f, 4.0f }, i == showcaseIndex ? White : Color { 255, 255, 255, 110 });
            }
        }
    } else if (entry.showcaseCount > 0) {
        ui.textCentered(tr("thirdPartyWorld.loadingFeaturedServers", "Fetching Servers..."), TextStyle::Ui, showcase, Muted1);
    } else if (!entry.icon.empty()) {
        float side = std::round(showcase.h * 0.5f);
        ui.sprite({ std::round(showcase.x + (showcase.w - side) * 0.5f), std::round(showcase.y + (showcase.h - side) * 0.5f), side, side }, entry.icon);
    }
    Rect statusBar { showcase.x, showcase.bottom() - 30.0f, showcase.w, 30.0f };
    ui.fill(statusBar, { 0, 0, 0, 166 });
    auto status = serverStatus.find(entry.address);
    bool online = status != serverStatus.end() && status->second.online;
    int latency = online ? status->second.latencyMs : -1;
    const char* pingKey = latency < 0 ? "unavailablePing" : latency < 150 ? "lowPing" : latency < 300 ? "mediumPing" : "highPing";
    const char* pingFallback = latency < 0 ? "Unavailable ping" : latency < 150 ? "Low ping" : latency < 300 ? "Medium ping" : "High ping";
    std::string pingText = tr(std::string("hbui.PlayScreen.serverTab.ServerDescription.") + pingKey, pingFallback);
    Color signal = latency < 0 ? Muted0 : latency < 150 ? Color { 126, 214, 50, 255 } : latency < 300 ? Color { 255, 224, 0, 255 } : Color { 255, 80, 80, 255 };
    int bars = latency < 0 ? 0 : latency < 150 ? 4 : latency < 300 ? 3 : 1;
    for (int bar = 0; bar < 4; ++bar) {
        float h = 2.0f + bar * 2.0f;
        ui.fill({ x + bar * 3.0f, statusBar.y + 18.0f - h, 2.0f, h }, bar < bars ? signal : Color { 80, 80, 80, 255 });
    }
    float pingWidth = std::min(ui.measure(pingText, TextStyle::Pixel), std::max(0.0f, width - 77.0f));
    ui.text(pingText, TextStyle::Pixel, x + 14.0f, statusBar.y + 11.0f, White, pingWidth);
    float playersX = x + 14.0f + pingWidth + 12.0f;
    ui.sprite({ playersX, statusBar.y + 9.0f, 12.0f, 12.0f }, "ui/FriendsIcon");
    ui.text(online ? std::to_string(status->second.players) : "--", TextStyle::Pixel, playersX + 15.0f, statusBar.y + 11.0f, White, std::max(1.0f, area.right() - playersX - 23.0f));

    y = showcase.bottom();
    divider(ui, area.x, y, area.w);
    float buttonWidth = std::min(157.33f, width * 0.42f);
    Rect play { area.right() - 12.0f - buttonWidth, y + 6.0f, buttonWidth, 22.0f };
    ui.text(entry.name, TextStyle::Pixel, x, y + 14.0f, White, std::max(1.0f, play.x - x - 8.0f));
    if (ui.pressableButton("server:play", "pressableElevatedPrimary", upperCase(tr("menu.play", "Play")), play, TextStyle::HeadingSmall)) {
        if (auto row = selectedRow()) connect(*row);
    }
    y += 36.0f;

    if (!entry.description.empty()) {
        divider(ui, area.x, y, area.w);
        y += 8.0f;
        ui.text(tr("hbui.PlayScreen.serverTab.ServerDescription.title", "Description"), TextStyle::Pixel, x, y, White);
        y += 13.0f;
        y += ui.paragraph(entry.description, TextStyle::Pixel, x, y, width, Muted0) + 8.0f;
    }
    if (!entry.games.empty()) {
        divider(ui, area.x, y, area.w);
        y += 8.0f;
        ui.text(tr("hbui.PlayScreen.serverTab.Activities", "Activities"), TextStyle::UiSmall, x, y, Muted0);
        y += 13.0f;
        constexpr float CardImageWidth = 96.0f;
        constexpr float CardImageHeight = 54.0f;
        float textX = x + CardImageWidth + 8.0f;
        float textWidth = width - CardImageWidth - 8.0f;
        for (const FeaturedGameEntry& game : entry.games) {
            Rect picture { x, y, CardImageWidth, CardImageHeight };
            ui.fill(picture, InkDark);
            if (!game.image.empty()) {
                ui.sprite(picture, game.image);
            }
            float textY = y;
            ui.text(game.title, TextStyle::Ui, textX, textY, White, textWidth);
            textY += 12.0f;
            if (!game.subtitle.empty()) {
                ui.text(game.subtitle, TextStyle::UiSmall, textX, textY, Muted0, textWidth);
                textY += 11.0f;
            }
            textY += ui.paragraph(game.description, TextStyle::BodySmall, textX, textY + 2.0f, textWidth, Muted1) + 2.0f;
            y = std::max(picture.bottom(), textY) + 8.0f;
        }
    }
    if (!entry.newsTitle.empty() || !entry.news.empty()) {
        divider(ui, area.x, y, area.w);
        y += 8.0f;
        ui.text(tr("hbui.PlayScreen.serverTab.newsTitle", "News"), TextStyle::UiSmall, x, y, Muted0);
        y += 13.0f;
        if (!entry.newsTitle.empty()) {
            y += ui.paragraph(entry.newsTitle, TextStyle::BodyBold, x, y, width, White) + 2.0f;
        }
        if (!entry.news.empty()) {
            y += ui.paragraph(entry.news, TextStyle::Body, x, y, width, Muted0);
        }
        y += 8.0f;
    }
    ui.clearClip();
    detailContent = y + detailScroll - area.y;
}

void Menu::serverForm(Context& ui, float width, float height)
{
    (void)height;
    header(ui, width, upperCase(editing ? tr("accessibility.play.editServer", "Edit server") : tr("externalServerScreen.addServer", "Add a new server")), false);
    float panelWidth = std::min(522.67f, width - 16.0f);
    constexpr float RowStep = 50.0f;
    Rect panel { std::round((width - panelWidth) * 0.5f), 53.33f, panelWidth, 191.33f };
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
            selection = Selection { ServerGroup::Saved, *editing };
            dialog = Dialog::ConfirmDelete;
        }
        if (ui.pressableButton("form:save", "pressableElevatedPrimary", tr("addExternalServerScreen.saveButtonLabel", "Save changes"), right)) {
            saveServerForm(false);
        }
    } else {
        if (ui.pressableButton("form:add", "pressableElevatedSecondary", tr("selectServer.add", "Add server"), left)) {
            saveServerForm(false);
        }
        if (ui.pressableButton("form:play", "pressableElevatedPrimary", tr("hbui.PlayScreen.serverTab.serverForm.addAndPlayButton", "Add and play"), right)) {
            saveServerForm(true);
        }
    }
}

void Menu::settings(Context& ui, float width, float height)
{
    header(ui, width, upperCase(tr("menu.settings", "Settings")), false);
    Rect content = column(width, 53.33f, height - 8.0f);
    float sidebarWidth = std::min(196.0f, content.w * 0.31f);
    Rect sidebar { content.x, content.y, sidebarWidth, std::min(402.0f, content.h) };
    Rect page { sidebar.right() + 17.33f, content.y, content.right() - sidebar.right() - 24.33f, content.h };

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

void Menu::settingsRow(Context& ui, float x, float& y, float width, std::string_view label, std::string_view detail, float controlHeight, float controlWidth)
{
    float textWidth = width - 24.0f - controlWidth;
    float labelY = detail.empty() && controlHeight > 24.0f ? std::round(y + (controlHeight - ui.lineHeight(TextStyle::Ui)) * 0.5f) : y + 7.0f;
    ui.text(label, TextStyle::Ui, x + 12.0f, labelY, White, textWidth);
    float height = 18.0f;
    if (!detail.empty()) {
        height += ui.paragraph(detail, TextStyle::UiSmall, x + 12.0f, y + 17.0f, textWidth, Muted0);
    }
    y += std::max(height + 6.0f, controlHeight);
    divider(ui, x, y - css(2.0f), width);
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
                rebindingMod.reset();
            }
        }
        if (!modBinds.empty()) {
            settingsHeading(ui, x, y, w, "Mods", "Keys added by loaded mods. They disappear when the mod is removed.");
            for (const ModKeyBind& bind : modBinds) {
                float rowY = y;
                bool waiting = rebindingMod && *rebindingMod == bind.id;
                settingsRow(ui, x, y, w, bind.label, {}, 31.33f);
                std::string label = waiting ? tr("options.pressKey", "Press a key...") : std::string(keyName(bind.key));
                if (ui.pressableButton(std::string("modbind:") + bind.id, waiting ? "pressableElevatedPrimary" : "pressableElevatedSecondary", label, { x + w - 12.0f - 68.0f, rowY + 5.0f, 68.0f, 20.0f })) {
                    rebindingMod = bind.id;
                    rebinding.reset();
                }
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
        settingsRow(ui, x, y, w, tr("options.fov.toggle.name", "Field of view can be altered by gameplay"),
            tr("options.fov.toggle.description", "Allow gameplay effects like sprinting or potion use to temporarily change the Field Of View"), 31.33f);
        if (toggle(ui, "video:gameplayfov", { x + w - 12.0f - 38.0f, rowY + 7.67f, 38.0f, 16.0f }, fovAlteredByGameplay)) {
            fovAlteredByGameplay = !fovAlteredByGameplay;
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
        settingsRow(ui, x, y, w, tr("options.vsync", "VSync"), "Waits for the screen to refresh before showing each frame, trading latency for no tearing", 31.33f);
        if (toggle(ui, "video:vsync", { x + w - 12.0f - 38.0f, rowY + 7.67f, 38.0f, 16.0f }, verticalSync)) {
            verticalSync = !verticalSync;
        }

        rowY = y;
        settingsRow(ui, x, y, w, tr("options.guiScale.optionName", "GUI scale"), "Rescales and repositions the menus and in-game HUD", 60.0f);
        float segment = std::floor((w - 24.0f) / static_cast<float>(std::size(InterfaceScales)));
        for (size_t i = 0; i < std::size(InterfaceScales); ++i) {
            Rect option { x + 12.0f + segment * static_cast<float>(i), rowY + 33.0f, segment, 20.0f };
            bool active = scale == InterfaceScales[i];
            if (ui.pressableButton(std::string("scale:") + InterfaceScaleLabels[i], active ? "pressableElevatedPrimary" : "pressableElevatedSecondary", InterfaceScaleLabels[i], option)) {
                scale = InterfaceScales[i];
            }
        }
        rowY = y;
        settingsRow(ui, x, y, w, tr("options.hidepaperdoll", "Hide paper doll"), "Hides the small player model in the top left corner of the HUD", 31.33f);
        if (toggle(ui, "video:paperdoll", { x + w - 12.0f - 38.0f, rowY + 7.67f, 38.0f, 16.0f }, hidePaperDoll)) {
            hidePaperDoll = !hidePaperDoll;
        }
        rowY = y;
        std::string safeArea = tr("options.safeZone.title", "Change Screen Safe Area");
        float safeAreaWidth = ui.measure(safeArea, TextStyle::Ui) + 24.0f;
        settingsRow(ui, x, y, w, safeArea, tr("options.safeZone.description", "Make sure the game looks great in your device's screen"), 42.0f, safeAreaWidth + 12.0f);
        if (ui.pressableButton("video:safearea", "pressableElevatedSecondary", safeArea, { x + w - 12.0f - safeAreaWidth, rowY + 8.67f, safeAreaWidth, 24.0f })) {
            dialog = Dialog::SafeArea;
        }
        rowY = y;
        settingsRow(ui, x, y, w, tr("options.gamma", "Brightness"), tr("options.gamma.description", "If your screen is still too dark, change the settings on your device, TV, or monitor"), 44.0f);
        std::string brightnessText = std::to_string(brightnessPercent) + "%";
        ui.text(brightnessText, TextStyle::Ui, x + w - 12.0f - ui.measure(brightnessText, TextStyle::Ui), rowY + 7.0f, White);
        float brightnessFraction = float(brightnessPercent - MinBrightness) / float(MaxBrightness - MinBrightness);
        if (slider(ui, "video:brightness", { x + 12.0f, rowY + 26.0f, w - 24.0f, 14.0f }, brightnessFraction)) {
            setBrightness(MinBrightness + int(std::lround(brightnessFraction * float(MaxBrightness - MinBrightness))));
        }
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
    case SettingsPage::Mods:
        modsPage(ui, x, y, w);
        break;
    default: {
        settingsHeading(ui, x, y, w, entry ? tr(entry->key, entry->label) : tr("menu.settings", "Settings"), {});
        const char* key = "kestrel.settings.unavailable";
        const char* fallback = "These settings aren't available in Kestrel yet.";
        switch (settingsSection) {
        case SettingsPage::Controller:
            key = "kestrel.settings.unavailable.controller";
            fallback = "Kestrel doesn't read game controllers yet, so there is nothing to set up here.";
            break;
        case SettingsPage::Touch:
            key = "kestrel.settings.unavailable.touch";
            fallback = "Kestrel is played with a keyboard and mouse; touch controls aren't supported.";
            break;
        case SettingsPage::Party:
            key = "kestrel.settings.unavailable.party";
            fallback = "Kestrel can't create or join parties yet, so there are no party settings.";
            break;
        case SettingsPage::GlobalResources:
            key = "kestrel.settings.unavailable.globalResources";
            fallback = "Kestrel uses the vanilla resources and the packs servers send; global resource packs aren't supported yet.";
            break;
        default:
            break;
        }
        y += 8.0f;
        y += ui.paragraph(tr(key, fallback), TextStyle::Ui, x + 12.0f, y, w - 24.0f, Muted0) + 12.0f;
        break;
    }
    }
    ui.clearClip();
    pageContent = y + pageScroll - area.y + 8.0f;
}

/**
 * Every library in the mods folder, each one turned on or off, reloaded or
 * deleted on the spot, with its saved settings editable underneath; changes
 * reach the client as mod actions it applies between frames.
 */
void Menu::modsPage(Context& ui, float x, float& y, float w)
{
    auto act = [this](ModAction::Kind kind, const std::string& file, std::string key = {}, std::string value = {}) {
        modActions.push_back({ kind, file, std::move(key), std::move(value) });
    };
    constexpr float ButtonWidth = 68.0f;
    constexpr float ButtonHeight = 20.0f;

    settingsHeading(ui, x, y, w, tr("kestrel.settings.mods", "Mods"), tr("kestrel.settings.mods.description", "Turn mods on and off, reload them after an update, change their settings or remove them. Changes apply right away, even in game."));
    float rowY = y;
    settingsRow(ui, x, y, w, tr("kestrel.settings.mods.folder", "Mods folder"), tr("kestrel.settings.mods.folder.description", "Drop mod libraries here, then rescan to load them"), 31.33f, ButtonWidth * 2.0f + 6.0f);
    if (ui.pressableButton("mods:rescan", "pressableElevatedSecondary", tr("kestrel.settings.mods.rescan", "Rescan"), { x + w - 12.0f - ButtonWidth, rowY + 5.0f, ButtonWidth, ButtonHeight })) {
        act(ModAction::Kind::Rescan, {});
    }
    if (ui.pressableButton("mods:folder", "pressableElevatedSecondary", tr("kestrel.settings.mods.open", "Open"), { x + w - 12.0f - ButtonWidth * 2.0f - 6.0f, rowY + 5.0f, ButtonWidth, ButtonHeight })) {
        act(ModAction::Kind::OpenFolder, {});
    }

    if (modEntries.empty()) {
        y += 8.0f;
        y += ui.paragraph(tr("kestrel.settings.mods.empty", "No mods found. Put a mod library in the mods folder and press Rescan."), TextStyle::Ui, x + 12.0f, y, w - 24.0f, Muted0) + 12.0f;
        return;
    }

    for (const ModEntry& entry : modEntries) {
        std::string title = entry.name.empty() ? (entry.id.empty() ? entry.file : entry.id) : entry.name;
        if (!entry.version.empty()) {
            title += " " + entry.version;
        }
        std::string detail;
        if (!entry.error.empty()) {
            detail = "§c" + entry.error;
        } else if (!entry.enabled) {
            detail = tr("kestrel.settings.mods.disabled", "Turned off");
        } else {
            detail = entry.description.empty() ? entry.file : entry.description;
            if (!entry.author.empty()) {
                detail += " - " + entry.author;
            }
        }
        rowY = y;
        settingsRow(ui, x, y, w, title, detail, 31.33f, 38.0f + ButtonWidth + 12.0f);
        if (toggle(ui, "mods:toggle:" + entry.file, { x + w - 12.0f - 38.0f, rowY + 7.67f, 38.0f, 16.0f }, entry.enabled)) {
            act(entry.enabled ? ModAction::Kind::Disable : ModAction::Kind::Enable, entry.file);
        }
        bool opened = openedMod == entry.file;
        if (ui.pressableButton("mods:open:" + entry.file, opened ? "pressableElevatedPrimary" : "pressableElevatedSecondary", opened ? tr("kestrel.settings.mods.less", "Less") : tr("kestrel.settings.mods.more", "More"), { x + w - 12.0f - 38.0f - 6.0f - ButtonWidth, rowY + 5.0f, ButtonWidth, ButtonHeight })) {
            openedMod = opened ? std::string() : entry.file;
            removingMod.clear();
            if (field == Field::ModConfig) {
                field = Field::None;
            }
        }
        if (!opened) {
            continue;
        }

        rowY = y;
        std::string file = trf("kestrel.settings.mods.file", "File: %s", { entry.file });
        settingsRow(ui, x, y, w, file, entry.id.empty() ? std::string() : trf("kestrel.settings.mods.id", "Id: %s", { entry.id }), 31.33f, ButtonWidth * 2.0f + 6.0f);
        bool removing = removingMod == entry.file;
        if (ui.pressableButton("mods:remove:" + entry.file, removing ? "pressableElevatedPrimary" : "pressableElevatedSecondary", removing ? tr("kestrel.settings.mods.confirm", "Sure?") : tr("kestrel.settings.mods.remove", "Remove"), { x + w - 12.0f - ButtonWidth, rowY + 5.0f, ButtonWidth, ButtonHeight })) {
            if (removing) {
                act(ModAction::Kind::Remove, entry.file);
                removingMod.clear();
                openedMod.clear();
            } else {
                removingMod = entry.file;
            }
        }
        if (entry.enabled && ui.pressableButton("mods:reload:" + entry.file, "pressableElevatedSecondary", tr("kestrel.settings.mods.reload", "Reload"), { x + w - 12.0f - ButtonWidth * 2.0f - 6.0f, rowY + 5.0f, ButtonWidth, ButtonHeight })) {
            act(ModAction::Kind::Reload, entry.file);
        }

        if (entry.config.empty()) {
            if (entry.loaded) {
                rowY = y;
                settingsRow(ui, x, y, w, tr("kestrel.settings.mods.noConfig", "This mod has no saved settings"), {}, 24.0f);
            }
            continue;
        }
        for (const auto& [key, value] : entry.config) {
            rowY = y;
            bool editing = field == Field::ModConfig && editModFile == entry.file && editModKey == key;
            const std::string& shown = editing ? editModValue : value;
            settingsRow(ui, x, y, w, key, {}, 31.33f, 150.0f + ButtonWidth + 6.0f);
            Rect input { x + w - 12.0f - ButtonWidth - 6.0f - 150.0f, rowY + 5.0f, 150.0f, ButtonHeight };
            if (textField(ui, "mods:config:" + entry.file + ":" + key, {}, shown, input, editing) && !editing) {
                field = Field::ModConfig;
                editModFile = entry.file;
                editModKey = key;
                editModValue = value;
            }
            bool changed = editing && editModValue != value;
            if (changed && ui.pressableButton("mods:save:" + entry.file + ":" + key, "pressableElevatedPrimary", tr("kestrel.settings.mods.save", "Save"), { x + w - 12.0f - ButtonWidth, rowY + 5.0f, ButtonWidth, ButtonHeight })) {
                act(ModAction::Kind::SetConfig, entry.file, key, editModValue);
                field = Field::None;
            }
        }
    }
    y += 6.0f;
}

void Menu::todoScreen(Context& ui, float width, float height, std::string_view heading)
{
    header(ui, width, upperCase(std::string(heading)), heading != tr("profileScreen.header", "Dressing Room"));
    Rect panel = column(width, 53.33f, height - 8.0f);
    ui.fill(panel, PanelDark);
    ui.textCentered(heading, TextStyle::HeadingLarge, { panel.x, panel.y + panel.h * 0.4f, panel.w, 20.0f }, White);
    ui.textCentered(trf("kestrel.screen.unavailable", "%1$s isn't available in Kestrel yet.", { std::string(heading) }), TextStyle::Ui, { panel.x, panel.y + panel.h * 0.4f + 24.0f, panel.w, 12.0f }, Muted0);
}

}
