#include "menu/Menu.h"
#include "menu/PlayLayout.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;
using namespace play;

namespace {

constexpr const char* ExperiencePrefix = "experience_id/";

constexpr float InterfaceScales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
constexpr const char* InterfaceScaleLabels[] = { "100%", "125%", "150%", "200%" };

constexpr float HeaderHeight = 48.0f;
constexpr float ColumnWidth = 631.0f;

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

/**
 * The game draws the Worlds tab with one of three pictures, picked once per
 * run: the second one time in a hundred, the third one time in 9600.
 */
const char* worldsTabIcon()
{
    static const char* icon = [] {
        std::random_device seed;
        std::mt19937 random(seed());
        if (std::uniform_int_distribution<int>(1, 9600)(random) == 9600) {
            return "hbui/UI_Menu_WorldsTab_Alt02";
        }
        if (std::uniform_int_distribution<int>(1, 100)(random) == 10) {
            return "hbui/UI_Menu_WorldsTab_Alt01";
        }
        return "hbui/UI_Menu_WorldsTab";
    }();
    return icon;
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
        std::string detail;
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
        std::string detail;
        auto status = serverStatus.find(saved[i].address);
        if (status != serverStatus.end() && status->second.online) {
            detail = status->second.motd;
        }
        rows.push_back({ ServerGroup::Saved, i, saved[i].name, saved[i].address, std::move(detail), {} });
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
    header(ui, width, upperCase(tr("hbui.PlayScreen.title", "Play")), true);
    Rect body = column(width, 52.0f, height);

    struct TabInfo {
        PlayTab tab;
        std::string label;
        const char* icon;
    };
    TabInfo tabs[] = {
        { PlayTab::Worlds, trf("hbui.PlayScreen.allWorlds", "Worlds (%1$s)", { std::to_string(friendWorlds().size()) }), worldsTabIcon() },
        { PlayTab::Realms, tr("hbui.PlayScreen.realms", "Realms"), "hbui/UI_Menu_RealmsTab" },
        { PlayTab::Servers, tr("hbui.PlayScreen.servers", "Servers"), "hbui/UI_Menu_ServerTab" },
    };
    float tabWidth = body.w / static_cast<float>(std::size(tabs));
    for (size_t i = 0; i < std::size(tabs); ++i) {
        Rect tab { std::round(body.x + tabWidth * static_cast<float>(i)), body.y, std::round(tabWidth), TabHeight };
        bool active = playTab == tabs[i].tab;
        Interaction state = ui.pressable("tab:" + std::to_string(i), "tabBarNeutral", tab, true, active);
        float textWidth = ui.measure(tabs[i].label, TextStyle::Ui);
        constexpr float IconSize = 12.0f;
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
    case PlayTab::Worlds:
        worldsTab(ui, area);
        break;
    case PlayTab::Realms:
        realmsTab(ui, area);
        break;
    case PlayTab::Servers:
        serversTab(ui, area);
        break;
    }
}

void Menu::settings(Context& ui, float width, float height)
{
    if (vanillaSettings(ui, width, height)) {
        return;
    }
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
    case SettingsPage::Accessibility: {
        settingsHeading(ui, x, y, w, tr("menu.accessibility.tab.title", "Accessibility"), {});
        struct GlintSlider {
            const char* id;
            const char* key;
            const char* label;
            int* value;
        };
        GlintSlider sliders[] = {
            { "accessibility:glintStrength", "options.glintStrength", "Glint Strength", &glintStrengthPercent },
            { "accessibility:glintSpeed", "options.glintSpeed", "Glint Speed", &glintSpeedPercent },
        };
        for (GlintSlider& entry : sliders) {
            float rowY = y;
            settingsRow(ui, x, y, w, tr(entry.key, entry.label), {}, 44.0f);
            std::string percent = std::to_string(*entry.value) + "%";
            ui.text(percent, TextStyle::Ui, x + w - 12.0f - ui.measure(percent, TextStyle::Ui), rowY + 7.0f, White);
            float fraction = float(*entry.value) / 100.0f;
            if (slider(ui, entry.id, { x + 12.0f, rowY + 26.0f, w - 24.0f, 14.0f }, fraction)) {
                *entry.value = int(std::lround(fraction * 100.0f));
            }
        }
        break;
    }
    case SettingsPage::Mods:
        modsPage(ui, x, y, w);
        break;
    case SettingsPage::GlobalResources:
        globalResourcesPage(ui, x, y, w);
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
    constexpr float WideButtonWidth = 92.0f;

    settingsHeading(ui, x, y, w, tr("kestrel.settings.mods", "Mods"), tr("kestrel.settings.mods.description", "Turn mods on and off, reload them after an update, change their settings or remove them. Changes apply right away, even in game."));
    float rowY = y;
    settingsRow(ui, x, y, w, tr("kestrel.settings.mods.folder", "Mods folder"), tr("kestrel.settings.mods.folder.description", "Drop mod libraries here, then rescan to load them"), 31.33f, ButtonWidth * 2.0f + WideButtonWidth + 12.0f);
    if (ui.pressableButton("mods:rescan", "pressableElevatedSecondary", tr("kestrel.settings.mods.rescan", "Rescan"), { x + w - 12.0f - ButtonWidth, rowY + 5.0f, ButtonWidth, ButtonHeight })) {
        act(ModAction::Kind::Rescan, {});
    }
    if (ui.pressableButton("mods:folder", "pressableElevatedSecondary", tr("kestrel.settings.mods.open", "Open"), { x + w - 12.0f - ButtonWidth * 2.0f - 6.0f, rowY + 5.0f, ButtonWidth, ButtonHeight })) {
        act(ModAction::Kind::OpenFolder, {});
    }
    if (ui.pressableButton("mods:reload-configs", "pressableElevatedSecondary", tr("kestrel.settings.mods.reload_configs", "Reload configs"), { x + w - 12.0f - ButtonWidth * 2.0f - WideButtonWidth - 12.0f, rowY + 5.0f, WideButtonWidth, ButtonHeight })) {
        act(ModAction::Kind::ReloadConfigs, {});
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
        bool settingsPage = entry.loaded && entry.hasSettings;
        settingsRow(ui, x, y, w, file, entry.id.empty() ? std::string() : trf("kestrel.settings.mods.id", "Id: %s", { entry.id }), 31.33f, settingsPage ? ButtonWidth * 3.0f + 12.0f : ButtonWidth * 2.0f + 6.0f);
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
        if (settingsPage && ui.pressableButton("mods:settings:" + entry.file, "pressableElevatedSecondary", tr("kestrel.settings.mods.settings", "Settings"), { x + w - 12.0f - ButtonWidth * 3.0f - 12.0f, rowY + 5.0f, ButtonWidth, ButtonHeight })) {
            act(ModAction::Kind::OpenSettings, entry.file);
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
