#include "menu/Menu.h"

#include "platform/Shell.h"
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

constexpr float HeaderHeight = 48.0f;
constexpr float PageWidth = 631.0f;
constexpr float CardWidth = 204.0f;
constexpr float CardHeight = 181.0f;
constexpr float PanelGap = 10.0f;
constexpr float TabHeight = 22.0f;
constexpr float ProfileRowHeight = 34.0f;
constexpr float RowGap = 1.0f;
constexpr float SectionTagHeight = 12.0f;
constexpr int ScreenshotLimit = 100;
constexpr Color SuggestedTint { 140, 179, 255, 255 };
constexpr Color RecentTint { 160, 224, 129, 255 };
constexpr Color EarnedFrame { 248, 175, 43, 255 };
constexpr Color OnlineDot { 88, 196, 72, 255 };

/**
 * A count with a comma between every group of three digits, the way the
 * profile statistics print numbers.
 */
std::string grouped(int64_t value)
{
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out += ',';
        }
        out += digits[i];
    }
    return value < 0 ? "-" + out : out;
}

std::string playTime(int64_t minutes)
{
    return trf("hbui.BannedOrSuspendedMessage.timer", "%1$s d %2$s h %3$s m", { std::to_string(minutes / 1440), std::to_string(minutes / 60 % 24), std::to_string(minutes % 60) });
}

/**
 * A profile list row: a small icon, a muted label above its value, and an
 * optional value on the right after the gamerscore glyph.
 */
void labelRow(Context& ui, const Rect& row, std::string_view icon, std::string_view label, std::string_view value, std::string_view gamerscore)
{
    ui.fill(row, Panel);
    ui.fill({ row.x, row.bottom() - 1.0f, row.w, 1.0f }, Divider);
    ui.sprite({ row.x + 5.0f, std::round(row.y + (row.h - 12.0f) * 0.5f), 12.0f, 12.0f }, icon);
    ui.text(label, TextStyle::UiSmall, row.x + 20.0f, row.y + 9.0f, Muted1);
    ui.text(value, TextStyle::Ui, row.x + 20.0f, row.y + 18.0f, White);
    if (!gamerscore.empty()) {
        float width = ui.measure(gamerscore, TextStyle::UiSmall);
        float x = row.right() - 7.0f - width;
        ui.sprite({ x - 10.0f, std::round(row.y + (row.h - 7.0f) * 0.5f), 7.0f, 7.0f }, "hbui/gamerscore_small@0.5x.icon", Muted0);
        ui.text(gamerscore, TextStyle::UiSmall, x, std::round(row.y + (row.h - ui.lineHeight(TextStyle::UiSmall)) * 0.5f), Muted0);
    }
}

/**
 * The coloured tag that opens an achievement list, with the rule under it
 * running the whole width.
 */
float sectionTag(Context& ui, float x, float y, float width, std::string_view label, Color tint)
{
    float tagWidth = ui.measure(label, TextStyle::UiSmall) + 8.0f;
    ui.fill({ x, y, tagWidth, SectionTagHeight }, tint);
    ui.text(label, TextStyle::UiSmall, x + 4.0f, y + 2.0f, InkDark);
    ui.fill({ x, y + SectionTagHeight, width, 2.0f }, tint);
    return SectionTagHeight + 2.0f;
}

void achievementRow(Context& ui, const Rect& row, const ProfileAchievement& achievement)
{
    ui.fill(row, Panel);
    ui.fill({ row.x, row.bottom() - 1.0f, row.w, 1.0f }, Divider);
    Rect icon { row.x + 4.0f, row.y + 4.0f, 44.0f, row.h - 8.0f };
    ui.fill(icon, PanelDark);
    if (!achievement.icon.empty()) {
        ui.sprite(icon, achievement.icon);
    }
    if (achievement.achieved) {
        ui.outline(icon, EarnedFrame);
    }
    float textRoom = row.w - 52.0f - 40.0f;
    ui.text(achievement.name, TextStyle::Ui, row.x + 52.0f, row.y + 9.0f, White, textRoom);
    ui.text(achievement.description, TextStyle::UiSmall, row.x + 52.0f, row.y + 20.0f, Muted1, textRoom);
    std::string points = std::to_string(achievement.gamerscore);
    float width = ui.measure(points, TextStyle::UiSmall);
    float x = row.right() - 7.0f - width;
    ui.sprite({ x - 10.0f, std::round(row.y + (row.h - 7.0f) * 0.5f), 7.0f, 7.0f }, "hbui/gamerscore_small@0.5x.icon", Muted0);
    ui.text(points, TextStyle::UiSmall, x, std::round(row.y + (row.h - ui.lineHeight(TextStyle::UiSmall)) * 0.5f), Muted0);
}

}

/**
 * The player's own profile: a card with the featured picture, the player's
 * head, gamertag and what they play, and next to it the overview and
 * statistics tabs filled from their Xbox achievements and stats.
 */
void Menu::profile(Context& ui, float width, float height)
{
    header(ui, width, upperCase(tr("hbui.ProfileRoute.headerSelf", "Your Profile")), true);
    float pageWidth = std::min(PageWidth, width - 16.0f);
    float left = std::round((width - pageWidth) * 0.5f);
    float top = HeaderHeight + 6.0f;
    profileCard(ui, { left, top, CardWidth, CardHeight });

    Rect panel { left + CardWidth + PanelGap, top, pageWidth - CardWidth - PanelGap, height - top - 8.0f };
    const std::string labels[] = { tr("hbui.ProfileRoute.overviewTabButtonLabel", "Overview"), tr("hbui.ProfileRoute.statsTabButtonLabel", "Statistics") };
    float tabWidth = std::floor((panel.w - 2.0f) * 0.5f);
    for (int i = 0; i < 2; ++i) {
        Rect tab { panel.x + (tabWidth + 2.0f) * static_cast<float>(i), panel.y, i == 0 ? tabWidth : panel.w - tabWidth - 2.0f, TabHeight };
        bool active = profileStatsTab == (i == 1);
        Interaction state = ui.interact("profile:tab:" + std::to_string(i), tab);
        ui.fill(tab, Divider);
        ui.fill(tab.inset(1.0f), active ? PanelDark : state.hovered ? Color { 0x58, 0x59, 0x5a, 255 } : Panel);
        ui.textCentered(labels[i], TextStyle::Ui, { tab.x, tab.y + (active ? 1.0f : 0.0f), tab.w, tab.h }, White);
        if (active) {
            ui.fill({ std::round(tab.x + tab.w * 0.5f - 12.0f), tab.bottom() - 4.0f, 24.0f, 1.0f }, White);
        }
        if (state.clicked) {
            profileStatsTab = i == 1;
        }
    }
    Rect content { panel.x, panel.y + TabHeight + 6.0f, panel.w, panel.bottom() - panel.y - TabHeight - 6.0f };
    if (profileStatsTab) {
        profileStats(ui, content);
    } else {
        profileSummary(ui, content);
    }
}

void Menu::profileCard(Context& ui, const Rect& card)
{
    ui.fill(card, PanelDark);
    Rect picture { card.x + 1.0f, card.y + 1.0f, card.w - 2.0f, 114.0f };
    const Sprite& art = ui.skin().sprite("hbui/screenshot_3");
    if (art.valid && art.width > 0.0f && art.height > 0.0f) {
        float visible = art.width * picture.h / picture.w;
        float offset = std::max(0.0f, (art.height - visible) * 0.5f);
        ui.spriteRegion(picture, "hbui/screenshot_3", { 0.0f, offset, art.width, std::min(visible, art.height) });
    }
    Rect edit { card.x + 6.0f, card.y + 7.0f, 21.0f, 21.0f };
    Interaction editState = ui.interact("profile:edit", edit);
    ui.fill(edit, editState.hovered ? Color { 0x58, 0x59, 0x5a, 230 } : Color { 0x31, 0x32, 0x33, 220 });
    ui.sprite({ edit.x + 4.5f, edit.y + 4.5f, 12.0f, 12.0f }, "hbui/edit-image@0.5x.icon");
    if (editState.clicked) {
        notify(tr("kestrel.profile.screenshotUnavailable", "Changing the featured screenshot isn't available in Kestrel yet."));
    }

    Rect head { card.x + 7.0f, card.y + 120.0f, 25.0f, 25.0f };
    const Sprite& avatar = ui.skin().sprite("dynamic/avatar");
    ui.sprite(head, avatar.valid ? "dynamic/avatar" : "ui/profile_glyph_color");
    ui.fill({ head.right() - 5.0f, head.y - 1.0f, 6.0f, 6.0f }, PanelDark);
    ui.fill({ head.right() - 4.0f, head.y, 4.0f, 4.0f }, OnlineDot);
    ui.text(displayName, TextStyle::UiLarge, card.x + 36.0f, card.y + 122.0f, White, card.w - 42.0f);
    ui.text(trf("hbui.PlayerCard.playing", "Playing: %s", { "Minecraft" }), TextStyle::UiSmall, card.x + 36.0f, card.y + 136.0f, Muted1, card.w - 42.0f);

    Rect dressing { card.x + 7.0f, card.y + 150.0f, 162.0f, 22.0f };
    std::string dressingLabel = tr("hbui.PlayerCard.dressingRoomButton", "Dressing Room");
    if (ui.pressableButton("profile:dressing", "pressableElevatedSecondary", "", dressing)) {
        returnScreen = Screen::Title;
        navigate(Screen::DressingRoom);
    }
    float labelWidth = ui.measure(dressingLabel, TextStyle::Ui);
    float x = std::round(dressing.x + (dressing.w - labelWidth - 16.0f) * 0.5f);
    ui.sprite({ x, std::round(dressing.y + (dressing.h - 12.0f) * 0.5f) - 1.0f, 12.0f, 12.0f }, "hbui/hanger@0.5x.icon", InkDark);
    ui.text(dressingLabel, TextStyle::Ui, x + 16.0f, std::round(dressing.y + (dressing.h - ui.lineHeight(TextStyle::Ui)) * 0.5f) - 1.0f, InkDark);

    Rect more { card.x + 174.0f, card.y + 150.0f, 23.0f, 22.0f };
    if (ui.pressableButton("profile:more", "pressableElevatedSecondary", "", more)) {
        dialog = Dialog::ProfileOptions;
    }
    ui.sprite({ more.x + 5.5f, std::round(more.y + (more.h - 12.0f) * 0.5f) - 1.0f, 12.0f, 12.0f }, "hbui/options@0.5x.icon", InkDark);
}

/**
 * The account options window of the profile: share the profile link, sign
 * out of the Microsoft account, or open the online privacy and safety page.
 */
void Menu::profileOptions(Context& ui, float width, float height)
{
    ui.fill(screenBounds, { 0, 0, 0, 150 });
    constexpr float WindowWidth = 235.0f;
    constexpr float TitleHeight = 24.0f;
    constexpr float OptionHeight = 24.0f;
    Rect frame { std::round((width - WindowWidth) * 0.5f), std::round(height * 0.5f - 49.0f), WindowWidth, TitleHeight + 1.0f + (OptionHeight + 1.0f) * 3.0f };
    ui.fill(frame, Secondary);
    Rect inner = frame.inset(1.0f);
    ui.fill({ inner.x, inner.y, inner.w, TitleHeight - 1.0f }, PanelDark);
    ui.textCentered(tr("hbui.PlayerOptionsModal.optionsTitle", "Account Options"), TextStyle::Ui, { inner.x, inner.y, inner.w, TitleHeight - 1.0f }, White);
    Rect close { inner.right() - 20.0f, inner.y + 3.0f, 16.0f, 16.0f };
    Interaction closeState = ui.interact("profile:options:close", close);
    ui.sprite({ close.x + 4.0f, close.y + 4.0f, 8.0f, 8.0f }, "hbui/Close", closeState.hovered ? White : Muted0);
    if (closeState.clicked) {
        dialog = Dialog::None;
    }

    struct Option {
        const char* id;
        std::string label;
        bool external;
    };
    const Option options[] = {
        { "share", tr("hbui.PlayerOptionsModal.shareMyProfile", "Share My Profile"), false },
        { "signout", tr("hbui.PlayerOptionsModal.signOut", "Sign Out"), false },
        { "privacy", tr("hbui.PlayerOptionsModal.privacyAndSafety", "Online Safety and Privacy"), true },
    };
    float y = inner.y + TitleHeight;
    for (const Option& option : options) {
        Rect row { inner.x, y, inner.w, OptionHeight };
        Interaction state = ui.interact(std::string("profile:options:") + option.id, row);
        ui.fill(row, state.hovered ? Color { 0x58, 0x59, 0x5a, 255 } : Panel);
        float x = row.x + 8.0f;
        if (option.external) {
            ui.sprite({ x, std::round(row.y + (row.h - 12.0f) * 0.5f), 12.0f, 12.0f }, "hbui/ExternalLink");
            x += 16.0f;
        }
        ui.text(option.label, TextStyle::Ui, x, std::round(row.y + (row.h - ui.lineHeight(TextStyle::Ui)) * 0.5f), White);
        if (state.clicked) {
            dialog = Dialog::None;
            if (std::string_view(option.id) == "share") {
                platform::copyText("https://www.xbox.com/play/user/" + displayName);
                notify("Profile link copied");
            } else if (std::string_view(option.id) == "signout") {
                accountRequest = AccountRequest::SignOut;
                navigate(Screen::Title);
            } else {
                platform::openUrl("https://www.xbox.com/family-toolbox/online-safety");
            }
        }
        y += OptionHeight + 1.0f;
    }
}

void Menu::profileSummary(Context& ui, const Rect& area)
{
    const ProfileInfo& info = account.profile;
    float y = area.y;
    labelRow(ui, { area.x, y, area.w, ProfileRowHeight }, "hbui/image@0.5x.icon", tr("hbui.ProfileRoute.screenshotsItemLabel", "Screenshot Gallery"),
        trf("hbui.ProfileRoute.listItemFractionValue", "%1$s/%2$s", { "0", std::to_string(ScreenshotLimit) }), {});
    y += ProfileRowHeight + RowGap;
    std::string achieved = info.achievementsLoaded ? trf("hbui.ProfileRoute.listItemFractionValue", "%1$s/%2$s", { std::to_string(info.achieved), std::to_string(info.total) }) : "-";
    std::string score = info.achievementsLoaded ? trf("hbui.ProfileRoute.listItemFractionValue", "%1$s/%2$s", { std::to_string(info.gamerscore), std::to_string(info.totalGamerscore) }) : std::string();
    labelRow(ui, { area.x, y, area.w, ProfileRowHeight }, "hbui/achievements@0.5x.icon", tr("hbui.ProfileRoute.achievementsItemLabel", "Achievements"), achieved, score);
    y += ProfileRowHeight + 5.0f;

    auto list = [&](std::string_view label, Color tint, const std::vector<ProfileAchievement>& entries) {
        if (entries.empty()) {
            return;
        }
        y += sectionTag(ui, area.x, y, area.w, label, tint) + 2.0f;
        for (const ProfileAchievement& entry : entries) {
            achievementRow(ui, { area.x, y, area.w, ProfileRowHeight }, entry);
            y += ProfileRowHeight + RowGap;
        }
        y += 4.0f;
    };
    list(tr("hbui.ProfileRoute.suggestedAchievementsListLabel", "Suggested Next Achievements"), SuggestedTint, info.suggested);
    list(tr("hbui.ProfileRoute.completedAchievementsListLabel", "Recently Earned Achievements"), RecentTint, info.recent);
}

void Menu::profileStats(Context& ui, const Rect& area)
{
    const ProfileInfo& info = account.profile;
    struct Stat {
        const char* icon;
        const char* key;
        const char* fallback;
        std::string value;
    };
    const Stat stats[] = {
        { "hbui/time@0.5x.icon", "xbox.statistics.timePlayed", "Time Played", playTime(info.minutesPlayed) },
        { "hbui/pickaxe@0.5x.icon", "xbox.statistics.blocksBroken", "Blocks Broken", grouped(info.blocksBroken) },
        { "hbui/sword@0.5x.icon", "xbox.statistics.mobsDefeated", "Mobs Defeated", grouped(info.mobsDefeated) },
        { "hbui/boots@0.5x.icon", "xbox.statistics.distanceTravelled", "Distance Travelled", grouped(info.distanceTravelled) },
    };
    float y = area.y;
    for (const Stat& stat : stats) {
        labelRow(ui, { area.x, y, area.w, ProfileRowHeight }, stat.icon, tr(stat.key, stat.fallback), info.statsLoaded ? stat.value : "-", {});
        y += ProfileRowHeight + RowGap;
    }
}

}
