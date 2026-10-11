#include "menu/Menu.h"
#include "menu/PlayLayout.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;
using namespace play;

namespace {

constexpr float RealmRowHeight = 30.0f;
constexpr float BannerPadding = 4.0f;
constexpr float HeaderButtonWidth = 126.0f;

/**
 * The order the game lists Realms in: live ones before expired ones, open
 * before closed, owned before joined, and ones with players online first.
 */
bool realmBefore(const RealmEntry* a, const RealmEntry* b)
{
    if (a->expired != b->expired) {
        return !a->expired;
    }
    if (a->open != b->open) {
        return a->open;
    }
    if (a->owned != b->owned) {
        return a->owned;
    }
    return a->onlinePlayers > 0 && b->onlinePlayers == 0;
}

/**
 * A full width notice strip with dark text on a tinted background, the way
 * the game's banners print. Returns its height.
 */
float banner(Context& ui, std::string_view text, Color tint, float x, float y, float width)
{
    float height = ui.paragraphHeight(text, TextStyle::UiSmall, width - BannerPadding * 3.0f) + BannerPadding * 2.0f;
    ui.fill({ x, y, width, height }, tint);
    ui.paragraph(text, TextStyle::UiSmall, x + BannerPadding * 1.5f, y + BannerPadding, width - BannerPadding * 3.0f, InkDark);
    return height;
}

/**
 * A tag pill ending at the right edge given, returning the space it took.
 */
float tag(Context& ui, std::string_view label, Color tint, float right, float y)
{
    float width = ui.measure(label, TextStyle::UiSmall) + 8.0f;
    Rect pill { right - width, y, width, 11.0f };
    ui.fill(pill, tint);
    ui.textCentered(label, TextStyle::UiSmall, pill, InkDark);
    return width + 3.0f;
}

/**
 * The red or yellow state tag a row carries: expired, closed, or full when
 * the player cannot join a full Realm.
 */
std::pair<std::string, Color> rowTag(const RealmEntry& realm)
{
    if (realm.expired) {
        return { tr("hbui.RealmTag.expired", "Expired"), DestructiveTint };
    }
    if (!realm.open) {
        return { tr("hbui.RealmTag.closed", "Closed"), DestructiveTint };
    }
    if (realm.full && !realm.owned) {
        return { tr("hbui.RealmTag.full", "Full"), NoticeTint };
    }
    return { std::string(), Clear };
}

}

void Menu::realmsInvitationsButton(Context& ui, const Rect& rect, bool enabled)
{
    bool clicked =ui.pressableButton("realms:invites", "pressableElevatedSecondary", tr("hbui.PlayScreen.realmsTab.buttonHeader.invitations", "Invitations"), rect, TextStyle::Ui, enabled);
    if (enabled && !social.invites.empty()) {
        std::string count = std::to_string(social.invites.size());
        float width = std::max(11.0f, ui.measure(count, TextStyle::UiSmall) + 6.0f);
        Rect badge { rect.right() - width - 5.0f, std::round(rect.y + (rect.h - 11.0f) * 0.5f), width, 11.0f };
        ui.fill(badge, HeaderBar);
        ui.textCentered(count, TextStyle::UiSmall, badge, InkDark);
    }
    if (clicked) {
        dialog = Dialog::RealmInvites;
        requestSocial(SocialAction::RefreshInvites);
    }
}

void Menu::realmsTab(Context& ui, const Rect& area)
{
    ui.fill(area, { 0, 0, 0, 200 });
    float top = area.y + 5.0f;
    float width = area.w - 5.0f;

    if (!signedIn()) {
        std::string message = tr("hbui.notLoggedInWarning.playScreen.message.realms", "You need a Microsoft account to play on Realms.");
        std::string link = tr("hbui.notLoggedInWarning.loginLink", "Log in or sign up");
        float height = banner(ui, message, NoticeTint, area.x, top, width);
        float linkWidth = ui.measure(link, TextStyle::UiSmall);
        Rect linkRect { area.x + BannerPadding * 1.5f, top + height - BannerPadding, linkWidth, ui.lineHeight(TextStyle::UiSmall) };
        ui.fill({ area.x, top + height, width, linkRect.h + BannerPadding }, NoticeTint);
        Interaction state = ui.interact("realms:signin", linkRect);
        ui.text(link, TextStyle::UiSmall, linkRect.x, linkRect.y, InkDark);
        ui.fill({ linkRect.x, linkRect.bottom(), linkRect.w, 1.0f }, state.hovered ? Black : InkDark);
        if (state.clicked) {
            beginSignIn();
        }
        realmsPurchasePage(ui, area, top + height + linkRect.h + BannerPadding + 5.0f, true);
        return;
    }

    if (account.realmsLoading && account.realms.empty()) {
        ui.textCentered(tr("hbui.Realms.JoinRealmModals.fetchRealmInProgress", "Fetching Realm"), TextStyle::Ui, { area.x, top + 10.0f, width, 20.0f }, Muted0);
        return;
    }
    if (!account.realmsError.empty() && !account.realmsLoading) {
        float center = std::round(area.x + width * 0.5f);
        float y = top + 20.0f;
        bool rateLimited = account.realmsRateLimited;
        std::string title = rateLimited ? tr("hbui.PlayScreen.realmsTab.realmsTabErrorStates.realmsRateLimitError.title", "Unable to connect to Realms")
                                        : tr("hbui.PlayScreen.realmsTab.realmsTabErrorStates.realmsServiceError.title", "Realms is not available right now");
        std::string text = rateLimited ? tr("hbui.PlayScreen.realmsTab.realmsTabErrorStates.realmsRateLimitError.text", "Please wait a few minutes and try again.")
                                       : tr("hbui.PlayScreen.realmsTab.realmsTabErrorStates.realmsServiceError.text.tryAgain", "We encountered an unknown error, please try again.");
        ui.textCentered(title, TextStyle::HeadingSmall, { area.x, y, width, 14.0f }, White);
        y += 20.0f;
        float textWidth = std::min(width - 20.0f, 260.0f);
        y += ui.paragraph(text, TextStyle::Ui, std::round(center - textWidth * 0.5f), y, textWidth, Muted0) + 8.0f;
        if (!rateLimited && ui.pressableButton("realms:retry", "pressableElevatedSecondary", tr("hbui.PlayScreen.realmsTab.realmsTabErrorStates.realmsServiceError.button.text", "Try Again"), { center - HeaderButtonWidth * 0.5f, y, HeaderButtonWidth, ButtonHeight })) {
            realmsRefreshRequested = true;
        }
        return;
    }
    if (account.realms.empty()) {
        realmsPurchasePage(ui, area, top, false);
        return;
    }

    std::vector<const RealmEntry*> sorted;
    for (const RealmEntry& realm : account.realms) {
        sorted.push_back(&realm);
    }
    std::stable_sort(sorted.begin(), sorted.end(), realmBefore);
    std::vector<const RealmEntry*> owned;
    std::vector<const RealmEntry*> joined;
    for (const RealmEntry* realm : sorted) {
        (realm->owned ? owned : joined).push_back(realm);
    }
    auto selected = std::find_if(sorted.begin(), sorted.end(), [&](const RealmEntry* realm) {
        return realm->id == realmSelected;
    });
    if (selected == sorted.end()) {
        realmSelected = !owned.empty() ? owned.front()->id : joined.front()->id;
        realmDetailScroll = 0.0f;
        selected = std::find_if(sorted.begin(), sorted.end(), [&](const RealmEntry* realm) {
            return realm->id == realmSelected;
        });
    }

    float sideWidth = std::round(width * 4.0f / 12.0f);
    Rect side { area.x, top, sideWidth, area.bottom() - top };
    Rect detail { side.right() + 8.0f, top, area.x + width - side.right() - 8.0f, area.bottom() - top };
    realmsSideMenu(ui, side, owned, joined);
    realmDetails(ui, detail, **selected);
}

void Menu::realmsSideMenu(Context& ui, const Rect& area, const std::vector<const RealmEntry*>& owned, const std::vector<const RealmEntry*>& joined)
{
    ui.fill(area, NeutralAlpha60);
    bool invites = !social.invites.empty();
    auto sectionHeight = [](size_t rows) {
        return rows == 0 ? 0.0f : 12.0f + RealmRowHeight * static_cast<float>(rows);
    };
    float content = 5.0f + (invites ? ButtonHeight + 8.0f : 0.0f) + sectionHeight(owned.size()) + (joined.empty() ? 0.0f : SectionGap + sectionHeight(joined.size())) + SectionGap + ButtonHeight + 5.0f;
    scrollArea(ui, area, listScroll, content);
    ui.setClip(area);
    float y = area.y + 5.0f - listScroll;
    float inner = area.w - 4.0f;

    if (invites) {
        realmsInvitationsButton(ui, { area.x + 6.0f, y, inner - 12.0f, ButtonHeight }, true);
        y += ButtonHeight + 8.0f;
    }

    auto section = [&](std::string_view label, const std::vector<const RealmEntry*>& rows) {
        ui.text(label, TextStyle::UiSmall, area.x + 7.33f, y, Muted0);
        y += 12.0f;
        for (const RealmEntry* realm : rows) {
            Rect bounds { area.x, y, inner, RealmRowHeight };
            bool selected = realm->id == realmSelected;
            Interaction state = ui.interact("realm:" + std::to_string(realm->id), bounds);
            if (selected) {
                ui.fill(bounds, Panel);
            } else if (state.hovered) {
                ui.fill(bounds, { 0x48, 0x49, 0x4a, 140 });
            }
            bool grey = realm->expired || !realm->open;
            std::string picture = socialAvatarSprite(realm->ownerXuid);
            Color avatarTint = grey ? Color { 120, 120, 120, 255 } : White;
            ui.sprite({ bounds.x + 4.0f, bounds.y + 3.0f, 24.0f, 24.0f }, !realm->ownerXuid.empty() && ui.skin().sprite(picture).valid ? picture : std::string("ui/profile_glyph_color"), avatarTint);
            ui.fill({ bounds.x + 23.0f, bounds.y + 22.0f, 5.0f, 5.0f }, Divider);
            ui.fill({ bounds.x + 24.0f, bounds.y + 23.0f, 3.0f, 3.0f }, realm->onlinePlayers > 0 ? SuccessTint : Neutral60);
            auto [label, tint] = rowTag(*realm);
            float tagWidth = label.empty() ? 0.0f : tag(ui, label, tint, bounds.right() - 4.0f, std::round(bounds.y + (bounds.h - 11.0f) * 0.5f));
            float textX = bounds.x + 32.0f;
            float room = bounds.right() - textX - 4.0f - tagWidth;
            ui.text(realm->name, TextStyle::Ui, textX, bounds.y + 5.0f, White, room);
            ui.text(realm->owner, TextStyle::UiSmall, textX, bounds.y + 17.0f, Muted1, room);
            if (state.clicked && !selected) {
                realmSelected = realm->id;
                realmDetailScroll = 0.0f;
            }
            y += RealmRowHeight;
        }
    };
    if (!owned.empty()) {
        section(trf("hbui.PlayScreen.realmsTab.sideMenu.ownedRealms", "Your Realms (%1$s)", { std::to_string(owned.size()) }), owned);
    }
    if (!joined.empty()) {
        y += SectionGap;
        section(trf("hbui.PlayScreen.realmsTab.sideMenu.joinedRealms", "Joined Realms (%1$s)", { std::to_string(joined.size()) }), joined);
    }
    y += SectionGap;
    std::string addLabel = tr("hbui.PlayScreen.realmsTab.sideMenu.addOrJoinRealm", "Add/join Realm");
    Rect add { area.x + 6.0f, y, inner - 12.0f, ButtonHeight };
    if (ui.pressableButton("realms:add", "pressableElevatedSecondary", addLabel, add)) {
        dialog = Dialog::RealmAddMenu;
    }
    ui.sprite({ std::round(add.x + add.w * 0.5f - ui.measure(addLabel, TextStyle::Ui) * 0.5f - 9.0f), add.y + 8.0f, 6.0f, 6.0f }, "hbui/Plus", InkDark);
    ui.clearClip();
}

void Menu::realmDetails(Context& ui, const Rect& area, const RealmEntry& realm)
{
    float padding = 8.0f;
    float inner = area.w - 4.0f - padding * 2.0f;
    std::string description = realm.description.empty() ? std::string("-") : realm.description;
    float descriptionHeight = ui.paragraphHeight(description, TextStyle::UiSmall, inner - 12.0f) + 12.0f;
    std::vector<std::pair<std::string, Color>> banners;
    if (realm.expired) {
        banners.emplace_back(tr("hbui.PlayScreen.realmsTab.bannerExpired", "Expired"), DestructiveTint);
    }
    if (!realm.open && !realm.expired) {
        banners.emplace_back(tr("hbui.PlayScreen.realmsTab.bannerClosed", "Closed"), DestructiveTint);
    }
    if (realm.full && !realm.owned) {
        banners.emplace_back(tr("hbui.PlayScreen.realmsTab.bannerFull", "Full"), NoticeTint);
    }
    float bannersHeight = 0.0f;
    for (const auto& [text, tint] : banners) {
        bannersHeight += ui.paragraphHeight(text, TextStyle::UiSmall, inner - BannerPadding * 3.0f) + BannerPadding * 2.0f + 4.0f;
    }
    bool live = realm.open && !realm.expired;
    float content = padding + bannersHeight + (live ? 26.0f : 0.0f) + 18.0f + 14.0f + ButtonHeight + 12.0f + 16.0f + descriptionHeight + padding;
    scrollArea(ui, area, realmDetailScroll, content);
    ui.setClip(area);
    Rect panel { area.x, area.y - realmDetailScroll, area.w - 4.0f, std::max(area.h, content) };
    ui.fill(panel, Neutral80);
    float x = panel.x + padding;
    float y = panel.y + padding;

    for (const auto& [text, tint] : banners) {
        y += banner(ui, text, tint, x, y, inner) + 4.0f;
    }

    if (live) {
        std::string ownerLine = realm.owned || realm.owner.empty() ? std::string() : trf("hbui.PlayScreen.realmsTab.playersOnline.realmOwner", "%1$s's Realm", { realm.owner });
        ui.text(ownerLine, TextStyle::UiSmall, x, y + 2.0f, Muted1, inner * 0.6f);
        std::string online = tr("hbui.PlayScreen.realmsTab.playersOnline", "Online") + " " + std::to_string(realm.onlinePlayers) + (realm.maxPlayers > 0 ? "/" + std::to_string(realm.maxPlayers) : std::string());
        ui.text(online, TextStyle::UiSmall, x + inner - ui.measure(online, TextStyle::UiSmall), y + 2.0f, Muted0);
        y += 14.0f;
        divider(ui, x, y, inner);
        y += 12.0f;
    }

    ui.text(realm.name, TextStyle::HeadingSmall, x, y, White, inner * 0.6f);
    tag(ui, realm.owned ? tr("hbui.RealmTag.realmOwner", "Your Realm") : tr("hbui.RealmTag.invitedToRealm", "Joined Realm"), InformativeTint, x + inner, y);
    y += 18.0f + 14.0f;

    bool blocked = realm.expired || !realm.open || realm.full;
    std::string action = tr("hbui.PlayScreen.realmsTab.play", "Play");
    bool playable = !blocked || (!realm.expired && realm.open && realm.full && realm.owned);
    if (realm.expired && realm.owned) {
        action = tr("hbui.PlayScreen.realmsTab.renew", "Renew");
        playable = false;
    } else if (!realm.expired && !realm.open && realm.owned) {
        action = tr("hbui.PlayScreen.realmsTab.reOpen", "Re-open");
        playable = false;
    }
    float playWidth = std::min(157.33f, inner);
    if (ui.pressableButton("realm:play:" + std::to_string(realm.id), "pressableElevatedPrimary", action, { x + inner - playWidth, y, playWidth, ButtonHeight }, TextStyle::HeadingSmall, playable)) {
        pending = ConnectRequest { realm.name, "realm_id/" + std::to_string(realm.id) };
    }
    y += ButtonHeight + 12.0f;

    ui.text(tr("hbui.PlayScreen.realmsTab.realmsDescription.heading", "About this Realm"), TextStyle::Ui, x, y, White, inner);
    y += 16.0f;
    ui.fill({ x, y, inner, descriptionHeight }, Neutral90);
    ui.paragraph(description, TextStyle::UiSmall, x + 6.0f, y + 6.0f, inner - 12.0f, Muted0);
    ui.clearClip();
}

void Menu::realmsPurchasePage(Context& ui, const Rect& area, float top, bool disabled)
{
    float width = area.w - 5.0f;
    float textWidth = std::min(width - 24.0f, 380.0f);
    float left = std::round(area.x + (width - textWidth) * 0.5f);
    struct Promo {
        std::string title;
        std::string text;
    };
    std::vector<Promo> promos {
        { tr("hbui.RealmsPDPScreen.promos.persistent.title", "Persistent multiplayer worlds"), tr("hbui.RealmsPDPScreen.promos.persistent.description", "Worlds uploaded to your Realm are always available to you and your invited Realm members - at any time, on any device. Members can play even when you're away.") },
        { tr("hbui.RealmsPDPScreen.promos.free.title", "Members play free"), tr("hbui.RealmsPDPScreen.promos.free.description", "People you invite become Realm members. They don't need to pay to join and play on the Realm; they just need a copy of Minecraft: Bedrock Edition.") },
        { tr("hbui.RealmsPDPScreen.promos.easy.title", "Easy and safe"), tr("hbui.RealmsPDPScreen.promos.easy.description", "No need for technical know-how. Free, no-hassle backups. No sharing personal information.") },
    };
    std::string splashTitle = tr("hbui.RealmsPDPScreen.splash.title", "Your own server, always online");
    std::string splashText = tr("hbui.RealmsPDPScreen.splash.text", "Run your own Minecraft server! You set the rules: who can join and how to play. Members play for free, even when you're offline. Easy to set up, manage, and access from any device.");
    float splashHeight = 12.0f + 16.0f + ui.paragraphHeight(splashText, TextStyle::UiSmall, textWidth - 24.0f) + 12.0f;
    float promosHeight = 16.0f;
    for (const Promo& promo : promos) {
        promosHeight += 14.0f + ui.paragraphHeight(promo.text, TextStyle::UiSmall, textWidth - 12.0f) + 14.0f;
    }

    Rect view { area.x, top, width, area.bottom() - top };
    float content = ButtonHeight + 10.0f + splashHeight + 10.0f + promosHeight;
    scrollArea(ui, view, listScroll, content);
    ui.setClip(view);
    float y = view.y - listScroll;

    float center = std::round(area.x + width * 0.5f);
    float rowLeft = center - HeaderButtonWidth * 1.5f - 4.0f;
    ui.pressableButton("realms:buy", "pressableElevatedPrimary", tr("hbui.PlayScreen.realmsTab.buttonHeader.buyNowUpsell", "Buy now"), { rowLeft, y, HeaderButtonWidth, ButtonHeight }, TextStyle::Ui, false);
    realmsInvitationsButton(ui, { rowLeft + HeaderButtonWidth + 4.0f, y, HeaderButtonWidth, ButtonHeight }, !disabled);
    if (ui.pressableButton("realms:join", "pressableElevatedSecondary", tr("hbui.PlayScreen.realmsTab.buttonHeader.joinRealm", "Join a Realm"), { rowLeft + (HeaderButtonWidth + 4.0f) * 2.0f, y, HeaderButtonWidth, ButtonHeight }, TextStyle::Ui, !disabled)) {
        openJoinRealm();
    }
    y += ButtonHeight + 10.0f;

    ui.fill({ left, y, textWidth, splashHeight }, Informative);
    ui.text(splashTitle, TextStyle::HeadingSmall, left + 12.0f, y + 12.0f, White, textWidth - 24.0f);
    ui.paragraph(splashText, TextStyle::UiSmall, left + 12.0f, y + 28.0f, textWidth - 24.0f, White);
    y += splashHeight + 10.0f;

    ui.text(tr("hbui.RealmsPDPScreen.promos.header", "What are Realms?"), TextStyle::Ui, left, y, White, textWidth);
    y += 16.0f;
    for (const Promo& promo : promos) {
        float textHeight = ui.paragraphHeight(promo.text, TextStyle::UiSmall, textWidth - 12.0f);
        ui.fill({ left, y, textWidth, 14.0f + textHeight + 8.0f }, Neutral80);
        ui.text(promo.title, TextStyle::Ui, left + 6.0f, y + 5.0f, White, textWidth - 12.0f);
        ui.paragraph(promo.text, TextStyle::UiSmall, left + 6.0f, y + 16.0f, textWidth - 12.0f, Muted0);
        y += 14.0f + textHeight + 14.0f;
    }
    ui.clearClip();
}

void Menu::realmAddDialog(Context& ui, float width, float height, bool& closed)
{
    Rect content = modalFrame(ui, width, height, 260.0f, 128.0f, upperCase(tr("hbui.PlayScreen.realmsTab.joinRealmOptionsModalMenu.title", "Options")), closed);
    if (closed) {
        return;
    }
    float y = content.y;
    ui.pressableButton("realmadd:subscription", "pressableElevatedSecondary", tr("hbui.PlayScreen.realmsTab.joinRealmOptionsModalMenu.addRealm", "Add a Realms subscription"), { content.x, y, content.w, ButtonHeight }, TextStyle::Ui, false);
    y += ButtonHeight + 4.0f;
    if (ui.pressableButton("realmadd:join", "pressableElevatedSecondary", tr("hbui.PlayScreen.realmsTab.joinRealmOptionsModalMenu.joinRealm", "Join an existing Realm"), { content.x, y, content.w, ButtonHeight })) {
        openJoinRealm();
    }
    y += ButtonHeight + 4.0f;
    ui.pressableButton("realmadd:learn", "pressableElevatedSecondary", tr("hbui.PlayScreen.realmsTab.joinRealmOptionsModalMenu.learnMore", "Learn more"), { content.x, y, content.w, ButtonHeight }, TextStyle::Ui, false);
}

}
