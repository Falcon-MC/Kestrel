#include "menu/Menu.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr const char* PauseRoot = "pause.pause_screen";
constexpr const char* SocialIcon = "textures/ui/socialbuttonicon/social-default-icon";
constexpr const char* SocialIconHovered = "textures/ui/socialbuttonicon/social-hover-icon";
constexpr const char* UnlockFullGameText = "trial.pauseScreen.buyGame";
constexpr float PaperDollModelPixels = 32.0f;
constexpr float PaperDollFramePixels = 39.4f;
constexpr float NameTagPadding = 1.0f;
constexpr float NameTagHeight = 10.0f;
constexpr float NameTagBackgroundAlpha = 0.8f;

/**
 * The pause store button's text on a server, the way the game's pause
 * screen controller names it: "%s Store" with the generic server name, as
 * servers do not send a store name of their own.
 */
std::string serverStoreText()
{
    return trf("menu.serverStore", "%s Store", { tr("menu.serverGenericName", "Server") });
}

}

/**
 * The pause menu: pause_screen.json with the server's packs over it when the
 * JSON UI is loaded, the classic one otherwise.
 */
void Menu::pause(Context& ui, float width, float height)
{
    if (!pauseScreen(ui, width, height)) {
        classicPause(ui, width, height);
    }
}

/**
 * pause_screen.json, fed the way the game's pause screen controller feeds it
 * on a multiplayer server: the player's name and paper doll, the dressing
 * room, the friends drawer button with the friends online, the server store
 * button that packs dress up, and everything the game hides there turned
 * off. Opening it plays its entrance, closing it the exit it takes as it is
 * popped or pushed away. False when the screen cannot be made.
 */
bool Menu::pauseScreen(Context& ui, float width, float height)
{
    bool open = dialog == Dialog::Pause;
    if (!jsonUi) {
        pauseUi.reset();
        pauseOpen = open;
        return false;
    }
    if (open && !pauseOpen) {
        pauseUi.reset();
        pausePressed.clear();
    }
    if (!open && pauseOpen && pauseUi) {
        bool resumed = dialog == Dialog::None && screen == Screen::Title;
        pauseUi->fire(resumed ? "screen.exit_pop" : "screen.exit_push");
    }
    pauseOpen = open;
    if (!pauseUi) {
        if (!open) {
            return false;
        }
        UiRow variables;
        variables["$ignore_edu_pause"] = UiValue::of(true);
        variables["$store_button_text"] = UiValue::of(serverStoreText());
        variables["$unlock_full_game_button_text"] = UiValue::of(std::string(UnlockFullGameText));
        pauseUi = std::make_unique<JsonUiScreen>(jsonUi, PauseRoot, variables);
        pauseUi->setRenderer([this](Context& context, const std::string& renderer, const Rect& rect, float alpha, const UiLookup& lookup) {
            pauseRenderer(context, renderer, rect, alpha, lookup);
        });
        pauseUi->setKeyboardNavigation(true);
    }
    if (!pauseUi->valid()) {
        pauseUi.reset();
        return false;
    }

    UiData data = pauseData();
    bool blocked = ui.isBlocked();
    if (!open || socialOpen) {
        ui.setBlocked(true);
    }
    pauseUi->draw(ui, { 0.0f, 0.0f, width, height }, data);
    ui.setBlocked(blocked);
    std::vector<UiEvent> events = pauseUi->takeEvents();
    if (!open) {
        return true;
    }

    std::vector<std::string> pressed = std::exchange(pausePressed, {});
    const InputState& input = ui.input();
    for (const UiEvent& event : events) {
        if (event.kind != UiEvent::Kind::Button || input.escape) {
            continue;
        }
        if (input.enter) {
            pausePressed.push_back(event.name);
        } else {
            pressed.push_back(event.name);
        }
    }
    for (const std::string& id : pressed) {
        if (dialog != Dialog::Pause) {
            break;
        }
        pauseButton(id);
    }
    return true;
}

/**
 * What pause_screen.json binds, as the game's pause screen controller sets
 * it for a player on a server with mouse and keyboard.
 */
UiData Menu::pauseData() const
{
    UiData data;
    UiRow& globals = data.globals;
    std::string name = session.displayName.empty() ? displayName : session.displayName;
    globals["#playername"] = UiValue::of(name);
    globals["#playername_visible"] = UiValue::of(true);
    globals["#can_change_skin"] = UiValue::of(true);
    globals["#dressing_room_button_visible"] = UiValue::of(true);
    globals["#dressing_room_controller_button_visible"] = UiValue::of(false);
    globals["#profile_button_b_visible"] = UiValue::of(false);
    globals["#profile_button_x_icon_visible"] = UiValue::of(false);
    globals["#is_signed_in_xbl"] = UiValue::of(signedIn());
    globals["#achievements_visible"] = UiValue::of(false);

    globals["#friends_drawer_button_visible"] = UiValue::of(true);
    globals["#social_icon_content"] = UiValue::of(1.0);
    globals["#social_icon"] = UiValue::of(std::string(SocialIcon));
    globals["#social_icon_hovered"] = UiValue::of(std::string(SocialIconHovered));
    globals["#social_button_text"] = UiValue::of(std::to_string(onlineCount(social.friends)));
    globals["#social_notification_icon_visibility"] = UiValue::of(!social.received.people.empty());
    globals["#tooltip_visible"] = UiValue::of(false);

    globals["#store_button_visible"] = UiValue::of(true);
    globals["#store_button_text"] = UiValue::of(serverStoreText());
    for (const char* hidden : { "#store_button_secondary_visible", "#store_button_secondary_enabled", "#store_button_secondary_disabled", "#store_error_tooltip_visible" }) {
        globals[hidden] = UiValue::of(false);
    }

    for (const char* hidden : {
             "#screenshot_button_visible",
             "#feedback_button_visible",
             "#realms_stories_button_primary_visible",
             "#realms_stories_button_secondary_visible",
             "#realms_unread_story_count_visible",
             "#realms_hub_enabled",
             "#is_trial_version",
             "#invite_button_visible",
             "#invite_button_enabled",
             "#legacy_invite_button_visible",
             "#legacy_invite_button_enabled",
             "#pause_annoucement_visible",
             "#debug_drawer_button_visible",
             "#edu_keyboard_helper_visible",
             "#list_has_display_objective",
             "#disconnected_from_xbox_live_label_visible",
             "#disconnected_from_third_party_label_visible",
             "#disconnected_from_adhoc_label_visible",
             "#disconnected_from_crossplatform_multiplayer",
             "#disconnected_from_multiplayer",
         }) {
        globals[hidden] = UiValue::of(false);
    }
    globals["#realms_access_button_text"] = UiValue::of(tr("pauseScreen.realmsStories", "Realms Stories"));
    globals["#players_list_title"] = UiValue::of(std::string());
    return data;
}

/**
 * The custom controls of the pause screen: the paper doll standing on the
 * bottom of its panel, the name tag above it, and the player's picture.
 */
void Menu::pauseRenderer(Context& ui, const std::string& renderer, const Rect& rect, float alpha, const UiLookup& lookup)
{
    if (alpha <= 0.0f || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    if (renderer == "paper_doll_renderer" || renderer == "live_player_renderer") {
        float pixel = std::min(rect.h / PaperDollFramePixels, rect.w / (PaperDollModelPixels * 0.5f));
        playerModel(ui, rect.x + rect.w * 0.5f, rect.y + rect.h - pixel * PaperDollModelPixels, pixel);
        return;
    }
    if (renderer == "name_tag_renderer") {
        std::string name = lookup("#playername").toText();
        if (name.empty()) {
            return;
        }
        float textWidth = ui.measure(name, TextStyle::Pixel);
        float x = std::floor(rect.x + (rect.w - textWidth) * 0.5f);
        float y = std::floor(rect.y + (rect.h - NameTagHeight) * 0.5f);
        auto channel = [&](float value) {
            return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        ui.fill({ x - NameTagPadding, y - NameTagPadding, textWidth + NameTagPadding * 2.0f, NameTagHeight }, { 0, 0, 0, channel(NameTagBackgroundAlpha * alpha) });
        ui.text(name, TextStyle::Pixel, x, y, { White.r, White.g, White.b, channel(alpha) });
        return;
    }
    if (renderer == "profile_image_renderer") {
        const Sprite& avatar = ui.skin().sprite("dynamic/avatar");
        if (avatar.valid) {
            ui.sprite(rect, "dynamic/avatar", { 255, 255, 255, static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) });
        }
    }
}

/**
 * What a button of the pause screen does, by the button id it sends. Ids the
 * game gives no action, like the button.null of informational buttons packs
 * add, do nothing.
 */
void Menu::pauseButton(const std::string& id)
{
    if (id == "button.menu_continue" || id == "button.menu_exit") {
        dialog = Dialog::None;
    } else if (id == "button.menu_settings") {
        dialog = Dialog::None;
        returnScreen = Screen::Title;
        navigate(Screen::Settings);
    } else if (id == "button.menu_quit" || id == "button.main_menu_button") {
        dialog = Dialog::None;
        disconnectRequested = true;
    } else if (id == "button.friends_drawer" || id == "button.menu_friends" || id == "button.menu_invite_players") {
        socialOpen = true;
        socialParty = id == "button.menu_invite_players";
    } else if (id == "button.to_profile_or_skins_screen") {
        dialog = Dialog::None;
        returnScreen = Screen::Title;
        navigate(Screen::DressingRoom);
    } else if (id == "button.menu_profile") {
        if (signedIn()) {
            dialog = Dialog::None;
            returnScreen = Screen::Title;
            navigate(Screen::Profile);
        } else {
            beginSignIn();
        }
    } else if (id == "button.menu_store") {
        dialog = Dialog::None;
        returnScreen = Screen::Title;
        navigate(Screen::Marketplace);
    } else if (id == "button.menu_how_to_play") {
        notify(tr("menu.howToPlay", "How to Play") + ": not available in Kestrel");
    }
}

}
