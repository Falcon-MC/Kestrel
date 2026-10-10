#include "menu/Menu.h"

#include "Network/Session/RealmsService.h"
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

constexpr float ButtonHeight = 22.0f;
constexpr float SmallButtonHeight = 18.0f;
constexpr float PersonHeight = 30.0f;
constexpr float DrawerWidth = 186.67f;
constexpr float ModalHeader = 22.0f;
constexpr size_t MaxRealmCodeLength = 200;
constexpr Color ErrorInk { 255, 120, 120, 255 };
constexpr Color OnlineDot { 126, 214, 50, 255 };
constexpr Color OfflineDot { 120, 120, 120, 255 };
constexpr Color RowHover { 0x48, 0x49, 0x4a, 140 };

void tabUnderline(Context& ui, const Rect& tab)
{
    ui.fill({ std::round(tab.x + tab.w * 0.5f - 6.0f), tab.bottom() - 5.0f, 12.0f, 1.0f }, White);
}

void sectionLabel(Context& ui, std::string_view label, float x, float y, float width)
{
    ui.fill({ x, y, width, 10.0f }, Primary);
    ui.text(label, TextStyle::UiSmall, x + 3.0f, y + 1.0f, White, width - 6.0f);
}

bool visible(const Rect& row, const Rect& area)
{
    return row.bottom() > area.y && row.y < area.bottom();
}

std::string lowered(std::string text)
{
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

}

void Menu::requestSocial(SocialAction action, std::string target)
{
    socialRequests.push_back({ action, std::move(target) });
}

const SocialOperation* Menu::operation(const std::string& key) const
{
    auto found = social.operations.find(key);
    return found == social.operations.end() ? nullptr : &found->second;
}

void Menu::setSocial(SocialSnapshot snapshot)
{
    RealmCodeState before = social.realmCode.state;
    social = std::move(snapshot);

    for (auto it = answeredInvites.begin(); it != answeredInvites.end();) {
        const SocialOperation* state = operation(inviteOperation(it->first));
        if (state && state->pending) {
            ++it;
            continue;
        }
        bool listed = std::any_of(social.invites.begin(), social.invites.end(), [&](const RealmInvitation& invite) {
            return invite.id == it->first;
        });
        if (!state && !listed) {
            notify(it->second.first ? trf("hbui.JoinRealmsServerTransient.successSnackbar", "You joined \"%1$s\"!", { it->second.second }) : tr("realmsPendingInvitationsScreen.declined", "Declined"));
        }
        it = answeredInvites.erase(it);
    }

    for (auto it = personActions.begin(); it != personActions.end();) {
        const SocialOperation* state = operation(personOperation(it->first));
        if (state && state->pending) {
            ++it;
            continue;
        }
        if (!state) {
            switch (it->second) {
            case SocialAction::SendRequest:
                notify(tr("kestrel.social.requestSent", "Friend request sent"));
                break;
            case SocialAction::AcceptRequest:
                notify(tr("kestrel.social.requestAccepted", "Friend request accepted"));
                break;
            case SocialAction::RemoveFriend:
                notify(tr("kestrel.social.friendRemoved", "Friend removed"));
                break;
            default:
                break;
            }
        }
        it = personActions.erase(it);
    }

    if (before != RealmCodeState::Ready && social.realmCode.state == RealmCodeState::Ready && dialog == Dialog::JoinRealm) {
        RealmCodeView joined = social.realmCode;
        requestSocial(SocialAction::CancelRealmCode);
        dialog = Dialog::None;
        field = Field::None;
        realmCodeInput.clear();
        if (joined.expired) {
            notify(tr("hbui.JoinRealmsServerError.realmExpired", "The Realm you tried to join has expired."));
        } else if (!joined.open) {
            notify(tr("hbui.JoinRealmsServerError.realmClosed", "The Realm you tried to join is closed."));
        } else if (joined.realmId != 0) {
            pending = ConnectRequest { joined.realmName.empty() ? tr("menu.realms", "Realms") : joined.realmName, "realm_id/" + std::to_string(joined.realmId) };
        }
    }

    if (!social.signedIn) {
        socialPage = SocialPage::Friends;
        socialSelected.clear();
    }
}

std::string Menu::onlineErrorText(OnlineError error, int retryAfterSeconds) const
{
    switch (error) {
    case OnlineError::None:
        return {};
    case OnlineError::SignedOut:
        return tr("hbui.SocialDrawerTabErrorState.notLoggedInDescription", "You need an account to play online with friends.");
    case OnlineError::Unauthorized:
        return tr("kestrel.online.unauthorized", "Your sign in has expired. Sign out, then sign in again.");
    case OnlineError::Forbidden:
        return tr("realmsInvitationScreen.sendingInvitesFailed.forbidden", "You are currently not allowed to do this action.");
    case OnlineError::NotFound:
        return tr("kestrel.online.notFound", "This is no longer available.");
    case OnlineError::Conflict:
        return tr("kestrel.online.conflict", "This changed somewhere else. The list is being refreshed.");
    case OnlineError::RateLimited:
        return retryAfterSeconds > 0 ? trf("kestrel.online.rateLimitedFor", "Too many requests. Try again in %1$s seconds.", { std::to_string(retryAfterSeconds) })
                                     : tr("kestrel.online.rateLimited", "Too many requests. Wait a moment, then try again.");
    case OnlineError::Unavailable:
        return tr("kestrel.online.unavailable", "The service isn't available right now. Try again later.");
    case OnlineError::Timeout:
        return tr("kestrel.online.timeout", "The service took too long to answer. Try again.");
    case OnlineError::Network:
        return tr("kestrel.online.network", "Couldn't reach the service. Check your connection, then try again.");
    case OnlineError::Invalid:
        return tr("kestrel.online.invalid", "This action isn't possible.");
    case OnlineError::Failed:
        break;
    }
    return tr("hbui.AddFriendError.loadErrorMessage", "We encountered an unknown error, please try again.");
}

std::string Menu::realmCodeErrorText(OnlineError error) const
{
    switch (error) {
    case OnlineError::Invalid:
        return tr("kestrel.realms.codeFormat", "Enter an invite code, or a realms.gg link like realms.gg/lCb-aaSCYBk.");
    case OnlineError::NotFound:
        return tr("hbui.JoinRealmsServerError.badInvite", "This link is incorrect or deactivated. Make sure you have entered it correctly or ask for a new link.");
    case OnlineError::SignedOut:
        return tr("hbui.JoinRealmsServerError.anonymousAccount", "You're currently playing with a guest account. Please sign in to add a Realm.");
    default:
        return onlineErrorText(error, 0);
    }
}

Rect Menu::modalFrame(Context& ui, float width, float height, float frameWidth, float frameHeight, std::string_view heading, bool& closed)
{
    ui.fill(screenBounds, { 0, 0, 0, 150 });
    float w = std::min(frameWidth, width - 16.0f);
    float h = std::min(frameHeight, height - 16.0f);
    Rect frame { std::round((width - w) * 0.5f), std::round((height - h) * 0.5f), w, h };
    ui.fill(frame, Divider);
    Rect inner = frame.inset(1.0f);
    ui.fill(inner, PanelDark);
    Rect bar { inner.x, inner.y, inner.w, ModalHeader };
    ui.fill(bar, HeaderBar);
    ui.textCentered(heading, TextStyle::Heading, { bar.x + 24.0f, bar.y, bar.w - 48.0f, bar.h }, InkDark);
    Rect close { bar.right() - ModalHeader, bar.y, ModalHeader, ModalHeader };
    Interaction state = ui.interact("modal:close", close);
    if (state.hovered) {
        ui.fill(close, { 0, 0, 0, 30 });
    }
    ui.sprite({ close.x + 7.5f, close.y + 7.5f, 7.0f, 7.0f }, "hbui/Close", InkDark);
    closed = state.clicked;
    return { inner.x + 8.0f, bar.bottom() + 8.0f, inner.w - 16.0f, inner.bottom() - bar.bottom() - 16.0f };
}

void Menu::openJoinRealm()
{
    dialog = Dialog::JoinRealm;
    field = Field::RealmCode;
    realmCodeInput.clear();
    realmCodeProblem.clear();
    requestSocial(SocialAction::CancelRealmCode);
}

void Menu::submitRealmCode()
{
    realmCodeProblem.clear();
    if (realmCodeInput.size() > MaxRealmCodeLength || RealmsService::parseInvite(realmCodeInput, true).empty()) {
        realmCodeProblem = realmCodeErrorText(OnlineError::Invalid);
        field = Field::RealmCode;
        return;
    }
    requestSocial(SocialAction::CheckRealmCode, realmCodeInput);
}

void Menu::joinRealmDialog(Context& ui, float width, float height)
{
    bool closed = false;
    Rect content = modalFrame(ui, width, height, 300.0f, 190.0f, upperCase(tr("hbui.JoinRealmsServer.title", "Join a Realm")), closed);
    if (closed) {
        requestSocial(SocialAction::CancelRealmCode);
        dialog = Dialog::None;
        field = Field::None;
        return;
    }
    float x = content.x;
    float y = content.y;
    float w = content.w;
    Rect buttons { x, content.bottom() - ButtonHeight, w, ButtonHeight };
    Rect left { buttons.x, buttons.y, std::floor(w * 0.5f) - 2.0f, ButtonHeight };
    Rect right { left.right() + 4.0f, buttons.y, w - left.w - 4.0f, ButtonHeight };

    if (!signedIn()) {
        y += ui.paragraph(tr("hbui.JoinRealmsServerError.anonymousAccount", "You're currently playing with a guest account. Please sign in to add a Realm."), TextStyle::Ui, x, y, w, Muted0);
        if (ui.pressableButton("joinrealm:signin", "pressableElevatedPrimary", tr("gui.signIn", "Sign In"), right)) {
            dialog = Dialog::None;
            beginSignIn();
        }
        if (ui.pressableButton("joinrealm:cancel", "pressableElevatedSecondary", tr("gui.cancel", "Cancel"), left)) {
            dialog = Dialog::None;
            field = Field::None;
        }
        return;
    }

    const RealmCodeView& lookup = social.realmCode;
    if (lookup.state == RealmCodeState::Found || lookup.state == RealmCodeState::Joining || lookup.state == RealmCodeState::Ready) {
        std::string name = lookup.realmName.empty() ? tr("menu.realms", "Realms") : lookup.realmName;
        ui.text(name, TextStyle::HeadingSmall, x, y, White, w);
        y += 16.0f;
        if (lookup.member) {
            y += ui.paragraph(tr("hbui.JoinRealmsServerError.alreadyMember", "You are already a member of this Realm."), TextStyle::Ui, x, y, w, Muted0) + 4.0f;
        } else if (!lookup.owner.empty()) {
            y += ui.paragraph(trf("hbui.JoinRealmsServerTransient.ownerInformation", "You're about to join %1$s's Realm", { lookup.owner }), TextStyle::Ui, x, y, w, Muted0) + 4.0f;
        }
        if (lookup.expired) {
            y += ui.paragraph(tr("hbui.JoinRealmsServerError.realmExpired", "The Realm you tried to join has expired."), TextStyle::Ui, x, y, w, ErrorInk) + 4.0f;
        } else if (!lookup.open) {
            y += ui.paragraph(tr("hbui.JoinRealmsServerError.realmClosed", "The Realm you tried to join is closed."), TextStyle::Ui, x, y, w, ErrorInk) + 4.0f;
        }
        if (!lookup.member) {
            ui.paragraph(tr("hbui.JoinRealmsServerTransient.caption", "You can leave this Realm at any time."), TextStyle::UiSmall, x, y, w, Muted1);
        }
        bool joining = lookup.state != RealmCodeState::Found;
        std::string action = joining ? tr("hbui.JoinRealmsServerTransient.joinRealmModal", "Joining Realm")
            : lookup.member ? tr("menu.play", "Play")
                            : tr("hbui.JoinRealmsServerTransient.joinButtonLabel", "Join");
        if (ui.pressableButton("joinrealm:join", "pressableElevatedPrimary", action, right, TextStyle::Ui, !joining && !lookup.expired)) {
            requestSocial(SocialAction::JoinRealmCode);
        }
        if (ui.pressableButton("joinrealm:back", "pressableElevatedSecondary", tr("hbui.SocialDrawer.backButton", "Back"), left, TextStyle::Ui, !joining)) {
            requestSocial(SocialAction::CancelRealmCode);
            field = Field::RealmCode;
        }
        return;
    }

    bool checking = lookup.state == RealmCodeState::Checking;
    y += ui.paragraph(tr("hbui.JoinRealmsServer.inputDescription", "Enter the invite link you received to join a Realm."), TextStyle::Ui, x, y, w, Muted0) + 6.0f;
    ui.text(tr("hbui.JoinRealmsServer.inputLabel", "Invite link"), TextStyle::UiSmall, x, y, White, w);
    y += 11.0f;
    constexpr float PasteWidth = 56.0f;
    Rect input { x, y, w - PasteWidth - 4.0f, 22.67f };
    if (textField(ui, "joinrealm:code", tr("hbui.JoinRealmsServer.inputPlaceholder", "Example: realms.gg/lCb-aaSCYBk"), realmCodeInput, input, field == Field::RealmCode) && !checking) {
        field = Field::RealmCode;
    }
    if (ui.pressableButton("joinrealm:paste", "pressableElevatedSecondary", tr("kestrel.clipboard.paste", "Paste"), { input.right() + 4.0f, y, PasteWidth, 22.67f }, TextStyle::Ui, !checking)) {
        std::string pasted = platform::pasteText();
        size_t start = pasted.find_first_not_of(" \t\r\n");
        size_t end = pasted.find_last_not_of(" \t\r\n");
        realmCodeInput = start == std::string::npos ? std::string() : pasted.substr(start, std::min(end - start + 1, MaxRealmCodeLength));
        realmCodeProblem.clear();
        field = Field::RealmCode;
    }
    y = input.bottom() + 6.0f;
    std::string problem = !realmCodeProblem.empty() ? realmCodeProblem
        : lookup.state == RealmCodeState::Failed ? realmCodeErrorText(lookup.error)
                                                 : std::string();
    if (checking) {
        ui.text(tr("hbui.JoinRealmsServer.verifyingLinkModalTitle", "Verifying invite link"), TextStyle::Ui, x, y, Muted0, w);
    } else if (!problem.empty()) {
        ui.paragraph(problem, TextStyle::UiSmall, x, y, w, ErrorInk);
    }
    if (ui.pressableButton("joinrealm:submit", "pressableElevatedPrimary", tr("hbui.JoinRealmsServer.joinButtonLabel", "Join"), right, TextStyle::Ui, !checking && !realmCodeInput.empty())) {
        submitRealmCode();
    }
    if (ui.pressableButton("joinrealm:cancel", "pressableElevatedSecondary", tr("gui.cancel", "Cancel"), left)) {
        requestSocial(SocialAction::CancelRealmCode);
        dialog = Dialog::None;
        field = Field::None;
    }
}

void Menu::realmInvitesDialog(Context& ui, float width, float height)
{
    bool closed = false;
    Rect content = modalFrame(ui, width, height, 380.0f, 300.0f, upperCase(tr("realmsPendingInvitationsScreen.pendingInvitations", "Realms Membership Invites")), closed);
    if (closed) {
        dialog = Dialog::None;
        return;
    }
    if (!signedIn()) {
        float y = content.y + 10.0f;
        y += ui.paragraph(tr("hbui.JoinRealmsServerError.anonymousAccount", "You're currently playing with a guest account. Please sign in to add a Realm."), TextStyle::Ui, content.x, y, content.w, Muted0) + 8.0f;
        if (ui.pressableButton("invites:signin", "pressableElevatedPrimary", tr("gui.signIn", "Sign In"), { std::round(content.x + (content.w - 126.0f) * 0.5f), y, 126.0f, ButtonHeight })) {
            dialog = Dialog::None;
            beginSignIn();
        }
        return;
    }

    float y = content.y;
    std::string count = trf("kestrel.realms.inviteCount", "%1$s pending", { std::to_string(social.invites.size()) });
    ui.text(social.invitesLoaded ? count : std::string(), TextStyle::Ui, content.x, y + 6.0f, Muted0, content.w - 90.0f);
    if (ui.pressableButton("invites:refresh", "pressableElevatedSecondary", tr("kestrel.online.refresh", "Refresh"), { content.right() - 84.0f, y, 84.0f, ButtonHeight }, TextStyle::Ui, !social.invitesLoading)) {
        requestSocial(SocialAction::RefreshInvites);
    }
    y += ButtonHeight + 6.0f;
    if (social.invitesError != OnlineError::None && !social.invites.empty()) {
        y += ui.paragraph(onlineErrorText(social.invitesError, social.invitesRetryAfterSeconds), TextStyle::UiSmall, content.x, y, content.w, ErrorInk) + 4.0f;
    }

    Rect list { content.x, y, content.w, content.bottom() - y };
    if (social.invites.empty()) {
        if (social.invitesLoading || (!social.invitesLoaded && social.invitesError == OnlineError::None)) {
            ui.textCentered(tr("realmsPendingInvitationsScreen.fetchingInvites", "Fetching invites..."), TextStyle::Ui, { list.x, list.y + 20.0f, list.w, 12.0f }, Muted0);
        } else if (social.invitesError != OnlineError::None) {
            float h = ui.paragraph(onlineErrorText(social.invitesError, social.invitesRetryAfterSeconds), TextStyle::Ui, list.x, list.y + 12.0f, list.w, ErrorInk);
            if (ui.pressableButton("invites:retry", "pressableElevatedPrimary", tr("hbui.AddFriendError.tryAgain", "Try again"), { std::round(list.x + (list.w - 126.0f) * 0.5f), list.y + 20.0f + h, 126.0f, ButtonHeight })) {
                requestSocial(SocialAction::RefreshInvites);
            }
        } else {
            ui.textCentered(tr("realmsPendingInvitationsScreen.noInvites", "You have no pending invites."), TextStyle::Ui, { list.x, list.y + 20.0f, list.w, 12.0f }, Muted0);
        }
        return;
    }

    scrollArea(ui, list, invitesScroll, invitesContent);
    ui.setClip(list);
    float rowY = list.y - invitesScroll;
    constexpr float ActionWidth = 64.0f;
    for (const RealmInvitation& invite : social.invites) {
        const SocialOperation* state = operation(inviteOperation(invite.id));
        bool busy = state && state->pending;
        std::string failure = state && !state->pending && state->error != OnlineError::None
            ? (state->error == OnlineError::NotFound || state->error == OnlineError::Conflict ? tr("hbui.Realms.InviteExpiredModals.message", "This invite is no longer valid.") : onlineErrorText(state->error, 0))
            : std::string();
        float textWidth = list.w - ActionWidth * 2.0f - 20.0f;
        float rowHeight = 40.0f + (failure.empty() ? 0.0f : ui.paragraphHeight(failure, TextStyle::UiSmall, list.w - 12.0f) + 4.0f);
        Rect row { list.x, rowY, list.w - 4.0f, rowHeight };
        if (visible(row, list)) {
            ui.fill(row, Panel);
            ui.sprite({ row.x + 6.0f, row.y + 8.0f, 20.0f, 20.0f }, "hbui/Realms");
            ui.text(invite.worldName.empty() ? tr("menu.realms", "Realms") : invite.worldName, TextStyle::Ui, row.x + 32.0f, row.y + 5.0f, White, textWidth);
            ui.text(invite.ownerName, TextStyle::UiSmall, row.x + 32.0f, row.y + 16.0f, Muted0, textWidth);
            ui.text(invite.description, TextStyle::UiSmall, row.x + 32.0f, row.y + 26.0f, Muted1, textWidth);
            Rect accept { row.right() - ActionWidth * 2.0f - 10.0f, row.y + 9.0f, ActionWidth, ButtonHeight };
            Rect decline { row.right() - ActionWidth - 6.0f, row.y + 9.0f, ActionWidth, ButtonHeight };
            if (ui.pressableButton("invite:accept:" + invite.id, "pressableElevatedPrimary", busy ? std::string("...") : tr("hbui.JoinRealmsServerTransient.joinButtonLabel", "Join"), accept, TextStyle::Ui, !busy)) {
                answeredInvites[invite.id] = { true, invite.worldName };
                requestSocial(SocialAction::AcceptInvite, invite.id);
            }
            if (ui.pressableButton("invite:decline:" + invite.id, "pressableElevatedSecondary", tr("realmsPendingInvitationsScreen.decline", "Decline"), decline, TextStyle::Ui, !busy)) {
                answeredInvites[invite.id] = { false, invite.worldName };
                requestSocial(SocialAction::DeclineInvite, invite.id);
            }
            if (!failure.empty()) {
                ui.paragraph(failure, TextStyle::UiSmall, row.x + 6.0f, row.y + 38.0f, list.w - 12.0f, ErrorInk);
            }
        }
        rowY += rowHeight + 4.0f;
    }
    ui.clearClip();
    invitesContent = rowY + invitesScroll - list.y;
}

float Menu::personRow(Context& ui, const SocialPerson& person, const Rect& row, bool expandable)
{
    bool selected = expandable && socialSelected == person.xuid;
    Interaction state = expandable ? ui.interact("person:" + person.xuid, row) : Interaction {};
    if (selected) {
        ui.fill(row, Panel);
    } else if (state.hovered) {
        ui.fill(row, RowHover);
    }
    std::string picture = socialAvatarSprite(person.xuid);
    ui.sprite({ row.x + 3.0f, row.y + 3.0f, 24.0f, 24.0f }, ui.skin().sprite(picture).valid ? picture : std::string("ui/profile_glyph_color"));
    ui.fill({ row.x + 22.0f, row.y + 22.0f, 5.0f, 5.0f }, Divider);
    ui.fill({ row.x + 23.0f, row.y + 23.0f, 3.0f, 3.0f }, person.online ? OnlineDot : OfflineDot);
    float textX = row.x + 31.0f;
    float room = row.right() - textX - 3.0f;
    bool self = person.xuid == social.xuid;
    ui.text(self ? person.gamertag + " (" + tr("hbui.FriendList.you", "You") + ")" : person.gamertag, TextStyle::Ui, textX, row.y + 5.0f, White, room);
    std::string detail = joinable(person) && !person.worldName.empty() ? person.worldName
        : !person.activity.empty()                                    ? person.activity
        : person.online                                               ? tr("kestrel.social.online", "Online")
                                                                      : tr("kestrel.social.offline", "Offline");
    ui.text(detail, TextStyle::UiSmall, textX, row.y + 17.0f, Muted0, room);
    if (state.clicked) {
        socialSelected = selected ? std::string() : person.xuid;
    }
    return row.h;
}

bool Menu::listState(Context& ui, const PeopleList& list, const Rect& area, std::string_view emptyText, SocialAction retry)
{
    if (!list.people.empty()) {
        return false;
    }
    if (list.loading || (!list.loaded && list.error == OnlineError::None)) {
        ui.textCentered(tr("realmsInvitationScreen.loadingFriends", "Loading Friends and Members..."), TextStyle::UiSmall, { area.x, area.y + 8.0f, area.w, 12.0f }, Muted0);
        return true;
    }
    if (list.error != OnlineError::None) {
        float h = ui.paragraph(onlineErrorText(list.error, list.retryAfterSeconds), TextStyle::UiSmall, area.x + 4.0f, area.y + 6.0f, area.w - 8.0f, ErrorInk);
        if (ui.pressableButton("social:retry:" + std::to_string(static_cast<int>(retry)), "pressableElevatedSecondary", tr("hbui.AddFriendError.tryAgain", "Try again"), { area.x + 4.0f, area.y + 12.0f + h, area.w - 8.0f, ButtonHeight })) {
            requestSocial(retry, retry == SocialAction::Search ? social.searchQuery : std::string());
        }
        return true;
    }
    ui.paragraph(emptyText, TextStyle::UiSmall, area.x + 4.0f, area.y + 8.0f, area.w - 8.0f, Muted0);
    return true;
}

void Menu::socialFriends(Context& ui, const Rect& area)
{
    const PeopleList& list = social.friends;
    std::string filter = lowered(socialSearch);
    std::vector<const SocialPerson*> joinableFriends;
    std::vector<const SocialPerson*> onlineFriends;
    std::vector<const SocialPerson*> offlineFriends;
    for (const SocialPerson& person : list.people) {
        if (!filter.empty() && lowered(person.gamertag).find(filter) == std::string::npos) {
            continue;
        }
        (joinable(person) ? joinableFriends : person.online ? onlineFriends : offlineFriends).push_back(&person);
    }

    scrollArea(ui, area, socialScroll, socialContent);
    ui.setClip(area);
    float y = area.y - socialScroll;
    float w = area.w - 4.0f;
    if (list.error != OnlineError::None && !list.people.empty()) {
        y += ui.paragraph(onlineErrorText(list.error, list.retryAfterSeconds), TextStyle::UiSmall, area.x + 2.0f, y, w - 4.0f, ErrorInk) + 4.0f;
    }
    if (listState(ui, list, { area.x, y, w, area.bottom() - y }, tr("hbui.SocialDrawerTabErrorState.people.noDataTitle", "Let's add some friends!"), SocialAction::RefreshFriends)) {
        ui.clearClip();
        socialContent = 0.0f;
        return;
    }
    if (!filter.empty()) {
        ui.paragraph(tr("kestrel.social.searchHint", "Press Enter to search every player by gamertag."), TextStyle::UiSmall, area.x + 2.0f, y, w - 4.0f, Muted1);
        y += 12.0f;
    }

    auto section = [&](std::string_view label, const std::vector<const SocialPerson*>& people) {
        if (people.empty()) {
            return;
        }
        sectionLabel(ui, label, area.x, y, w);
        y += 12.0f;
        for (const SocialPerson* person : people) {
            Rect row { area.x, y, w, PersonHeight };
            if (visible(row, area)) {
                personRow(ui, *person, row, true);
            }
            y += PersonHeight;
            if (socialSelected != person->xuid) {
                continue;
            }
            Rect actions { area.x + 4.0f, y, w - 8.0f, SmallButtonHeight };
            const SocialOperation* state = operation(personOperation(person->xuid));
            bool busy = state && state->pending;
            if (visible(actions, area)) {
                float half = std::floor((actions.w - 4.0f) * 0.5f);
                Rect join { actions.x, actions.y, half, actions.h };
                Rect remove { actions.x + half + 4.0f, actions.y, actions.w - half - 4.0f, actions.h };
                std::string joinLabel = joinable(*person) ? tr("hbui.SocialDrawer.PlayerOptionsMenu.joinGame", "Join game") : tr("hbui.SocialDrawer.PlayerOptionsMenu.unableToJoin", "Unable to join");
                if (ui.pressableButton("friend:join:" + person->xuid, "pressableElevatedPrimary", joinLabel, join, TextStyle::UiSmall, joinable(*person))) {
                    pending = ConnectRequest { person->worldName.empty() ? person->gamertag : person->worldName, "session_handle/" + person->sessionHandle };
                    socialOpen = false;
                }
                if (ui.pressableButton("friend:remove:" + person->xuid, "pressableElevatedDestructive", busy ? std::string("...") : tr("hbui.SocialDrawer.PlayerOptionsMenu.removeFriend", "Remove friend"), remove, TextStyle::UiSmall, !busy)) {
                    removingXuid = person->xuid;
                    removingName = person->gamertag;
                    socialOpen = false;
                    dialog = Dialog::ConfirmRemoveFriend;
                }
            }
            y += SmallButtonHeight + 3.0f;
            std::string reason;
            if (person->versionMismatch) {
                reason = tr("hbui.FriendsDrawer.joinFriendServerErrorModal.blockedByVersion.title", "Incompatible game version");
            } else if (person->unreachable) {
                reason = tr("kestrel.social.unreachable", "Their world uses a connection Kestrel can't open.");
            } else if (person->sessionHandle.empty()) {
                reason = tr("kestrel.social.notJoinable", "Not in a world you can join right now.");
            } else if (person->maxMembers > 0) {
                reason = std::to_string(person->members) + "/" + std::to_string(person->maxMembers);
            }
            if (state && !state->pending && state->error != OnlineError::None) {
                reason = onlineErrorText(state->error, 0);
            }
            if (!reason.empty()) {
                y += ui.paragraph(reason, TextStyle::UiSmall, area.x + 4.0f, y, w - 8.0f, state && state->error != OnlineError::None ? ErrorInk : Muted1) + 3.0f;
            }
        }
        y += 4.0f;
    };
    section(trf("hbui.FriendList.joinableFriends", "Joinable friends (%1$s)", { std::to_string(joinableFriends.size()) }), joinableFriends);
    section(trf("hbui.FriendList.online", "Online (%1$s)", { std::to_string(onlineFriends.size()) }), onlineFriends);
    section(trf("hbui.FriendList.offline", "Offline (%1$s)", { std::to_string(offlineFriends.size()) }), offlineFriends);
    if (joinableFriends.empty() && onlineFriends.empty() && offlineFriends.empty()) {
        ui.paragraph(tr("hbui.AddFriendRoute.noGamertagFound", "No gamertag found"), TextStyle::UiSmall, area.x + 4.0f, y, w - 8.0f, Muted0);
        y += 12.0f;
    }
    ui.clearClip();
    socialContent = y + socialScroll - area.y;
}

void Menu::socialRequestsPage(Context& ui, const Rect& area)
{
    scrollArea(ui, area, socialScroll, socialContent);
    ui.setClip(area);
    float y = area.y - socialScroll;
    float w = area.w - 4.0f;

    auto actionRow = [&](const SocialPerson& person, SocialAction primary, std::string_view primaryLabel, SocialAction secondary, std::string_view secondaryLabel) {
        Rect row { area.x, y, w, PersonHeight };
        if (visible(row, area)) {
            personRow(ui, person, row, false);
        }
        y += PersonHeight;
        const SocialOperation* state = operation(personOperation(person.xuid));
        bool busy = state && state->pending;
        Rect actions { area.x + 4.0f, y, w - 8.0f, SmallButtonHeight };
        if (visible(actions, area)) {
            bool twoButtons = !secondaryLabel.empty();
            float half = twoButtons ? std::floor((actions.w - 4.0f) * 0.5f) : actions.w;
            Rect first { actions.x, actions.y, half, actions.h };
            if (ui.pressableButton("person:" + std::to_string(static_cast<int>(primary)) + ":" + person.xuid, "pressableElevatedPrimary", busy ? std::string("...") : std::string(primaryLabel), first, TextStyle::UiSmall, !busy)) {
                personActions[person.xuid] = primary;
                requestSocial(primary, person.xuid);
            }
            if (twoButtons && ui.pressableButton("person:" + std::to_string(static_cast<int>(secondary)) + ":" + person.xuid, "pressableElevatedSecondary", std::string(secondaryLabel), { first.right() + 4.0f, actions.y, actions.w - half - 4.0f, actions.h }, TextStyle::UiSmall, !busy)) {
                personActions[person.xuid] = secondary;
                requestSocial(secondary, person.xuid);
            }
        }
        y += SmallButtonHeight + 3.0f;
        if (state && !state->pending && state->error != OnlineError::None) {
            y += ui.paragraph(onlineErrorText(state->error, 0), TextStyle::UiSmall, area.x + 4.0f, y, w - 8.0f, ErrorInk) + 3.0f;
        }
        y += 3.0f;
    };

    sectionLabel(ui, trf("hbui.FriendList.pending", "Pending requests (%1$s)", { std::to_string(social.received.people.size()) }), area.x, y, w);
    y += 12.0f;
    Rect receivedArea { area.x, y, w, 40.0f };
    if (listState(ui, social.received, receivedArea, tr("kestrel.social.noReceivedRequests", "No one has sent you a friend request."), SocialAction::RefreshRequests)) {
        y += 40.0f;
    } else {
        for (const SocialPerson& person : social.received.people) {
            actionRow(person, SocialAction::AcceptRequest, tr("kestrel.social.accept", "Accept"), SocialAction::DeclineRequest, tr("realmsPendingInvitationsScreen.decline", "Decline"));
        }
    }
    y += 4.0f;
    sectionLabel(ui, trf("kestrel.social.sentRequests", "Sent requests (%1$s)", { std::to_string(social.sent.people.size()) }), area.x, y, w);
    y += 12.0f;
    if (social.sent.people.empty()) {
        if (!social.sent.loading && social.sent.error == OnlineError::None) {
            ui.paragraph(tr("kestrel.social.noSentRequests", "You have no friend requests waiting for an answer."), TextStyle::UiSmall, area.x + 4.0f, y + 4.0f, w - 8.0f, Muted0);
        } else if (social.sent.error != OnlineError::None && social.received.error == OnlineError::None) {
            ui.paragraph(onlineErrorText(social.sent.error, social.sent.retryAfterSeconds), TextStyle::UiSmall, area.x + 4.0f, y + 4.0f, w - 8.0f, ErrorInk);
        }
        y += 30.0f;
    } else {
        for (const SocialPerson& person : social.sent.people) {
            actionRow(person, SocialAction::CancelRequest, tr("kestrel.social.cancelRequest", "Cancel request"), SocialAction::CancelRequest, {});
        }
    }
    ui.clearClip();
    socialContent = y + socialScroll - area.y;
}

void Menu::socialSearchPage(Context& ui, const Rect& area)
{
    scrollArea(ui, area, socialScroll, socialContent);
    ui.setClip(area);
    float y = area.y - socialScroll;
    float w = area.w - 4.0f;
    const PeopleList& list = social.search;
    if (social.searchQuery.empty()) {
        ui.paragraph(tr("hbui.AddFriendSearchByGamertag.searchGamertagDescription", "Find players by gamertag"), TextStyle::UiSmall, area.x + 4.0f, y + 6.0f, w - 8.0f, Muted0);
        ui.clearClip();
        socialContent = 0.0f;
        return;
    }
    if (listState(ui, list, { area.x, y, w, area.bottom() - y }, tr("hbui.AddFriendRoute.refineSearch", "Please refine your search"), SocialAction::Search)) {
        ui.clearClip();
        socialContent = 0.0f;
        return;
    }
    for (const SocialPerson& person : list.people) {
        Rect row { area.x, y, w, PersonHeight };
        if (visible(row, area)) {
            personRow(ui, person, row, false);
        }
        y += PersonHeight;
        const SocialOperation* state = operation(personOperation(person.xuid));
        bool busy = state && state->pending;
        Rect action { area.x + 4.0f, y, w - 8.0f, SmallButtonHeight };
        std::string note;
        SocialAction next = SocialAction::SendRequest;
        std::string label;
        if (person.xuid == social.xuid) {
            note.clear();
        } else if (person.isFriend) {
            note = tr("kestrel.social.alreadyFriends", "You're friends");
        } else if (person.requestReceived) {
            next = SocialAction::AcceptRequest;
            label = tr("kestrel.social.accept", "Accept");
        } else if (person.requestSent) {
            next = SocialAction::CancelRequest;
            label = tr("kestrel.social.cancelRequest", "Cancel request");
        } else if (person.canBeFriended) {
            label = tr("hbui.SocialDrawer.PlayerOptionsMenu.addFriend", "Add friend");
        } else {
            note = tr("kestrel.social.cannotAdd", "This player can't be added as a friend.");
        }
        if (!label.empty()) {
            if (visible(action, area) && ui.pressableButton("search:" + person.xuid, next == SocialAction::CancelRequest ? "pressableElevatedSecondary" : "pressableElevatedPrimary", busy ? std::string("...") : label, action, TextStyle::UiSmall, !busy)) {
                personActions[person.xuid] = next;
                requestSocial(next, person.xuid);
            }
            y += SmallButtonHeight + 3.0f;
        }
        if (state && !state->pending && state->error != OnlineError::None) {
            note = onlineErrorText(state->error, 0);
        }
        if (!note.empty()) {
            bool failed = state && state->error != OnlineError::None;
            y += ui.paragraph(note, TextStyle::UiSmall, area.x + 4.0f, y, w - 8.0f, failed ? ErrorInk : Muted1) + 3.0f;
        }
        y += 3.0f;
    }
    ui.clearClip();
    socialContent = y + socialScroll - area.y;
}

void Menu::socialDrawer(Context& ui, float width, float height)
{
    ui.fill(screenBounds, { 0, 0, 0, 150 });
    Rect panel { width - 1.0f - DrawerWidth, 28.0f, DrawerWidth, height - 29.0f };
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
    if (textField(ui, "social:search", tr("hbui.SocialDrawerHeader.narration.searchForPeople", "Search for people"), socialSearch, search, field == Field::SocialSearch)) {
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
        ui.textCentered(tr("hbui.SocialDrawerTabErrorState.party.noDataTitle", "No parties available"), TextStyle::Ui, { inner.x, inner.y + inner.h * 0.4f, inner.w, 12.0f }, White);
        ui.paragraph(tr("kestrel.social.partiesUnavailable", "Kestrel can't create or join parties yet."), TextStyle::UiSmall, inner.x + 8.0f, inner.y + inner.h * 0.4f + 18.0f, inner.w - 16.0f, Muted0);
        return;
    }

    Rect you { inner.x + 4.0f, y, inner.w - 8.0f, 32.0f };
    ui.fill(you, Panel);
    bool hasAvatar = ui.skin().sprite("dynamic/avatar").valid;
    ui.sprite({ you.x + 4.0f, you.y + 4.0f, 24.0f, 24.0f }, hasAvatar ? "dynamic/avatar" : "ui/profile_glyph_color");
    ui.text(displayName + " (" + tr("hbui.FriendList.you", "You") + ")", TextStyle::Ui, you.x + 32.0f, you.y + 7.0f, White, you.w - 36.0f);
    ui.text(inGame() ? tr("menu.servers", "Playing on a server") : tr("accessibility.screenName.start", "In the Minecraft Menus"), TextStyle::UiSmall, you.x + 32.0f, you.y + 18.0f, Muted0, you.w - 36.0f);
    y = you.bottom() + 4.0f;

    if (!signedIn()) {
        ui.textCentered(tr("hbui.SocialDrawerTabErrorState.notLoggedInTitle", "You're not logged in"), TextStyle::Ui, { you.x, y + 8.0f, you.w, 12.0f }, White);
        float h = ui.paragraph(tr("hbui.SocialDrawerTabErrorState.notLoggedInDescription", "You need an account to play online with friends."), TextStyle::UiSmall, you.x, y + 24.0f, you.w, Muted0);
        if (ui.pressableButton("social:signin", "pressableElevatedPrimary", tr("hbui.SocialDrawerTabErrorState.notLoggedInButton", "Sign in now"), { you.x, y + 30.0f + h, you.w, ButtonHeight })) {
            socialOpen = false;
            beginSignIn();
        }
        return;
    }

    if (socialPage == SocialPage::Friends) {
        float refreshWidth = std::floor(you.w * 0.34f);
        Rect requests { you.x, y, you.w - refreshWidth - 3.0f, ButtonHeight };
        std::string requestsLabel = tr("hbui.FriendRequests.label", "Friend requests");
        if (!social.received.people.empty()) {
            requestsLabel += " (" + std::to_string(social.received.people.size()) + ")";
        }
        if (ui.pressableButton("social:requests", "pressableElevatedSecondary", requestsLabel, requests)) {
            socialPage = SocialPage::Requests;
            socialScroll = 0.0f;
            socialSelected.clear();
            requestSocial(SocialAction::RefreshRequests);
        }
        Rect refresh { requests.right() + 3.0f, y, refreshWidth, ButtonHeight };
        if (ui.pressableButton("social:refresh", "pressableElevatedSecondary", social.friends.loading ? std::string("...") : tr("kestrel.online.refresh", "Refresh"), refresh, TextStyle::UiSmall, !social.friends.loading)) {
            requestSocial(SocialAction::RefreshFriends);
            requestSocial(SocialAction::RefreshRequests);
        }
        y += ButtonHeight + 5.0f;
        socialFriends(ui, { you.x, y, you.w + 2.0f, inner.bottom() - y - 2.0f });
        return;
    }

    Rect back { you.x, y, 60.0f, ButtonHeight };
    if (ui.pressableButton("social:back", "pressableElevatedSecondary", tr("hbui.SocialDrawer.backButton", "Back"), back)) {
        socialPage = SocialPage::Friends;
        socialScroll = 0.0f;
        socialSelected.clear();
        if (!social.searchQuery.empty()) {
            requestSocial(SocialAction::ClearSearch);
        }
        return;
    }
    std::string title = socialPage == SocialPage::Requests ? tr("hbui.FriendRequests.label", "Friend requests") : tr("hbui.AddFriendRoute.header", "Search For People");
    ui.text(title, TextStyle::Ui, back.right() + 6.0f, y + 6.0f, White, you.right() - back.right() - 6.0f);
    y += ButtonHeight + 5.0f;
    Rect area { you.x, y, you.w + 2.0f, inner.bottom() - y - 2.0f };
    if (socialPage == SocialPage::Requests) {
        socialRequestsPage(ui, area);
    } else {
        socialSearchPage(ui, area);
    }
}

}
