#include "menu/Menu.h"
#include "menu/GuiScale.h"
#include "menu/SettingsSlider.h"
#include "platform/Shell.h"
#include "ui/Context.h"
#include "ui/JsonUi.h"
#include "ui/Localization.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace kestrel::menu {
using namespace ui;

namespace {

constexpr const char* SettingsRoot = "settings.screen_controls_and_settings";
constexpr int VideoSection = 21;
constexpr int ModsSection = 100;

bool opacityOption(std::string_view name)
{
    return name == "hud_text_background_opacity" || name == "chat_background_opacity" || name == "actionbar_text_background_opacity";
}

/**
 * The section index variables the game's settings screen controller sets,
 * which the section toggles of the navigation_tab radio group carry.
 */
constexpr std::pair<const char*, int> Sections[] = {
    { "server_forced_index", 1 }, { "accessibility_forced_index", 2 }, { "how_to_play_index", 3 },
    { "game_forced_index", 4 }, { "classroom_forced_index", 5 }, { "edu_cloud_level_forced_index", 6 },
    { "multiplayer_forced_index", 7 }, { "world_forced_index", 8 }, { "members_forced_index", 9 },
    { "realms_saves_forced_index", 10 }, { "subscription_forced_index", 11 }, { "backup_forced_index", 12 },
    { "dev_options_forced_index", 13 }, { "keyboard_and_mouse_forced_index", 14 },
    { "controller_and_switch_forced_index", 15 }, { "touch_forced_index", 16 }, { "party_forced_index", 17 },
    { "general_forced_index", 18 }, { "account_forced_index", 19 }, { "creator_forced_index", 20 },
    { "video_forced_index", 21 }, { "view_subscriptions_forced_index", 22 }, { "sound_forced_index", 23 },
    { "global_texture_pack_forced_index", 24 }, { "storage_management_forced_index", 25 },
    { "edu_cloud_storage_forced_index", 26 }, { "language_forced_index", 27 }, { "preview_forced_index", 28 },
    { "debug_forced_index", 29 }, { "discovery_debug_forced_index", 30 }, { "ui_debug_forced_index", 31 },
    { "edu_debug_forced_index", 32 }, { "marketplace_debug_forced_index", 33 }, { "flighting_debug_forced_index", 34 },
    { "realms_debug_forced_index", 35 }, { "automation_forced_index", 36 }, { "level_texture_pack_index", 37 },
    { "broadcast_forced_index", 38 }, { "addon_index", 39 }, { "invite_links_forced_index", 40 },
    { "general_invite_link_forced_index", 41 }, { "advanced_invite_link_forced_index", 42 },
    { "realms_advanced_forced_index", 43 },
};

/**
 * The variables the settings screen controller sets for the global settings
 * a desktop client opens from the start screen: no world, realm or world
 * creation, mouse and keyboard.
 */
constexpr std::pair<const char*, bool> ContextFlags[] = {
    { "include_controls_and_settings_sections", true }, { "include_migrated_json_ui_settings_tabs", true },
    { "show_fullscreen_toggle", true }, { "supports_user_configured_safezone", true }, { "feedback_visible", true },
    { "is_global_texture_packs_visible", true }, { "supports_cross_platform_play_toggle", false },
    { "is_world_create", false }, { "is_world_edit", false }, { "is_template_create", false },
    { "is_realms_edit", false }, { "is_realm_slot", false }, { "is_mp_host", false }, { "is_mp_client", false },
    { "non_config_realms_env", false }, { "realms_pack_feature_enabled", false }, { "gamepad_supported", true },
    { "keyboard_and_mouse_supported", true },
#if defined(KESTREL_MOBILE)
    { "touch_supported", true },
#else
    { "touch_supported", false },
#endif
    { "supports_flite_tts", false },
    { "platform_tts_exists", false }, { "ignore_creator_section", false }, { "may_include_world_section", false },
    { "ignore_global_resources_section", false }, { "ignore_storage_section", false },
    { "ignore_profile_switch_account_button", false }, { "ignore_profile_sso_toggle", true },
    { "ignore_profile_sign_out_button", false }, { "ignore_controller_layout", false },
    { "edu_ignore_cloud_storage", true }, { "storage_location_switch_enabled", false },
    { "copy_interal_storage_button_enabled", false }, { "show_preview_button", false },
    { "show_preview_app1_button", false }, { "show_preview_app2_button", false }, { "debug_settings", false },
    { "party_settings_enabled", false }, { "settings_spatial_pattern_fix_enabled", false },
    { "display_copyright_info", false }, { "is_pregame", true }, { "is_editor_mode_enabled", false },
    { "can_quit", true },
};

enum class OptionKind {
    Toggle,
    Slider,
    Dropdown,
};

struct Choice {
    const char* name;
    const char* label;
};

/**
 * One option the settings sections show, by the name its toggle, slider or
 * dropdown and their bindings share.
 */
struct Option {
    const char* name;
    const char* label;
    OptionKind kind;
    int min;
    int max;
    int fallback;
    const Choice* choices = nullptr;
    size_t choiceCount = 0;
};

constexpr Choice Perspectives[] = {
    { "thirdperson_radio_first", "options.thirdperson.firstperson" },
    { "thirdperson_radio_third_back", "options.thirdperson.thirdpersonback" },
    { "thirdperson_radio_third_front", "options.thirdperson.thirdpersonfront" },
};
constexpr Choice GraphicsModes[] = {
    { "graphics_mode_radio_simple", "options.graphicsMode.simple" },
    { "graphics_mode_radio_fancy", "options.graphicsMode.fancy" },
};
constexpr Choice ContentLevels[] = {
    { "content_log_gui_level_verbose", "options.content_log_gui.level.verbose" },
    { "content_log_gui_level_info", "options.content_log_gui.level.info" },
    { "content_log_gui_level_warn", "options.content_log_gui.level.warn" },
    { "content_log_gui_level_error", "options.content_log_gui.level.error" },
};
constexpr Choice ToastDurations[] = {
    { "notification_duration_radio_ThreeSec", "options.notificationDuration.toast.ThreeSec" },
    { "notification_duration_radio_TenSec", "options.notificationDuration.toast.TenSec" },
    { "notification_duration_radio_ThirtySec", "options.notificationDuration.toast.ThirtySec" },
};
constexpr Choice ChatDurations[] = {
    { "chat_message_duration_radio_ThreeSec", "options.notificationDuration.chat.ThreeSec" },
    { "chat_message_duration_radio_TenSec", "options.notificationDuration.chat.TenSec" },
    { "chat_message_duration_radio_ThirtySec", "options.notificationDuration.chat.ThirtySec" },
};
constexpr Choice JoystickVisibility[] = {
    { "joystick_visibility_visible", "options.joystickVisibilityOption.visibleJoystick" },
    { "joystick_visibility_hidden", "options.joystickVisibilityOption.hiddenJoystick" },
    { "joystick_visibility_hidden_when_unused", "options.joystickVisibilityOption.hiddenJoystickWhenUnused" },
};
constexpr Choice TopButtonScale[] = {
    { "top_button_scale_radio_small", "options.topButtonScale.small" },
    { "top_button_scale_radio_medium", "options.topButtonScale.medium" },
    { "top_button_scale_radio_big", "options.topButtonScale.big" },
};
constexpr Choice SneakMode[] = {
    { "sneak_toggle", "options.sneakOption.toggle" },
    { "sneak_hold", "options.sneakOption.hold" },
};

constexpr Option toggle(const char* name, const char* label, bool on)
{
    return { name, label, OptionKind::Toggle, 0, 1, on ? 1 : 0 };
}

constexpr Option slider(const char* name, const char* label, int min, int max, int fallback)
{
    return { name, label, OptionKind::Slider, min, max, fallback };
}

template <size_t N>
constexpr Option dropdown(const char* name, const char* label, const Choice (&choices)[N], int fallback)
{
    return { name, label, OptionKind::Dropdown, 0, static_cast<int>(N) - 1, fallback, choices, N };
}

const Option Options[] = {
    dropdown("joystick_visibility", "options.joystickVisibilityOption", JoystickVisibility, 0),
    dropdown("top_button_scale", "options.topButtonScale", TopButtonScale, 1),
    dropdown("sneak", "options.sneakOption", SneakMode, 0),
    slider("touch_sensitivity", "options.sensitivity", 0, 100, 50),
    slider("spyglass_touch_dampening", "options.spyglassdampen", 0, 100, 50),
    slider("touch_button_size", "options.buttonSize", 60, 150, 100),
    slider("touch_control_opacity", "options.controlOpacity", 20, 100, 70),
    toggle("touch_invert_y_axis", "options.invertYAxis", false),
    toggle("touch_autojump", "options.autojump", true),
    toggle("left_handed", "options.lefthanded", false),
    toggle("sprint_on_movement", "options.sprintOnMovement", false),
    toggle("show_action_button", "options.showActionButton", true),
    toggle("show_block_select_button", "options.showBlockSelectButton", false),
    toggle("show_toggle_camera_perspective_button", "options.showToggleCameraPerspectiveButton", false),
    toggle("split_controls", "options.usetouchpad", false),
    toggle("swap_jump_and_sneak", "options.swapJumpAndSneak", false),
    toggle("hotbar_only_touch", "options.hotbarOnlyTouch", false),
    dropdown("content_log_gui_level", "options.content_log_gui.level", ContentLevels, 0),
    dropdown("toast_notification_duration", "options.notificationDuration.Toast", ToastDurations, 0),
    dropdown("chat_message_duration", "options.notificationDuration.Chat", ChatDurations, 1),
    slider("controller_sensitivity", "options.sensitivity", 0, 100, 50),
    slider("spyglass_gamepad_dampening", "options.spyglassdampen", 0, 100, 50),
    slider("gamepad_cursor_sensitivity", "options.gamepadcursorsensitivity", 0, 100, 50),
    slider("hud_text_background_opacity", "options.hudTextBackgroundOpacity", 0, 100, 50),
    slider("chat_background_opacity", "options.chatBackgroundOpacity", 0, 100, 50),
    slider("actionbar_text_background_opacity", "options.actionBarTextBackgroundOpacity", 0, 100, 50),
    slider("darkness", "options.darknessEffectModifier", 0, 100, 100),
    slider("screen_distortion", "options.screenDistortion", 0, 100, 100),
    slider("glint_strength", "options.glintStrength", 0, 100, 100),
    slider("glint_speed", "options.glintSpeed", 0, 100, 100),
    slider("render_distance", "options.renderDistance", MinRenderDistance, MaxRenderDistance, 8),
    slider("max_framerate", "options.framerateLimit", 0, MaxMaxFps, 0),
    slider("field_of_view", "options.fov", MinFov, MaxFov, 60),
    slider("gui_scale", "options.guiScale.optionName.name", 0, 0, 0),
    slider("gamma", "options.gamma", MinBrightness, MaxBrightness, DefaultBrightness),
    slider("interface_opacity", "options.hudOpacity", 0, 100, 100),
    slider("damage_bob", "options.damageBobbing", 0, 100, 100),
    slider("panorama_speed", "options.panoramaSpeed", 0, 100, 100),
    toggle("hide_hand", "options.hidehand", false),
    toggle("hide_paperdoll", "options.hidepaperdoll", false),
    toggle("hide_hud", "options.hidehud", false),
    toggle("screen_animations", "options.screenAnimations", true),
    toggle("show_auto_save_icon", "options.showautosaveicon", true),
    toggle("classic_box_selection", "options.classic_box_selection", true),
    toggle("ingame_player_names", "options.ingamePlayerNames", true),
    toggle("view_bobbing", "options.viewBobbing", true),
    toggle("camera_shake", "options.screenShake", true),
    toggle("transparent_leaves", "options.transparentleaves", true),
    toggle("bubble_particles", "options.bubbleparticles", true),
    toggle("fancy_skies", "options.fancyskies", true),
    toggle("smooth_lighting", "options.smooth_lighting", true),
    toggle("field_of_view_toggle", "options.fov.toggle", true),
    dropdown("third_person", "options.thirdperson", Perspectives, 0),
    dropdown("graphics_mode", "options.graphicsMode", GraphicsModes, 1),
    slider("keyboard_mouse_sensitivity", "options.sensitivity", 0, 100, 50),
    slider("spyglass_mouse_dampening", "options.spyglassdampen", 0, 100, 50),
    toggle("keyboard_mouse_invert_y_axis", "options.invertYAxis", false),
    toggle("keyboard_mouse_autojump", "options.autojump", true),
    toggle("keyboard_mouse_toggle_crouch", "options.toggleCrouch", false),
    toggle("keyboard_show_full_keyboard_options", "options.fullKeyboardGameplay", false),
    slider("main_volume", "soundCategory.main", 0, 100, 100),
    slider("music_volume", "soundCategory.music", 0, 100, 100),
    slider("sound_volume", "soundCategory.sound", 0, 100, 100),
    slider("ambient_volume", "soundCategory.ambient", 0, 100, 100),
    slider("block_volume", "soundCategory.block", 0, 100, 100),
    slider("hostile_volume", "soundCategory.hostile", 0, 100, 100),
    slider("neutral_volume", "soundCategory.neutral", 0, 100, 100),
    slider("player_volume", "soundCategory.player", 0, 100, 100),
    slider("record_volume", "soundCategory.record", 0, 100, 100),
    slider("weather_volume", "soundCategory.weather", 0, 100, 100),
    slider("texttospeech_volume", "soundCategory.texttospeech", 0, 100, 100),
    toggle("controller_invert_y_axis", "options.invertYAxis", false),
    toggle("controller_autojump", "options.autojump", false),
    toggle("controller_toggle_crouch", "options.toggleCrouch", false),
    toggle("hide_tooltips", "options.hidetooltips", false),
    toggle("hide_gamepad_cursor", "options.hidegamepadcursor", false),
    toggle("controller_clear_hotbar", "options.clearhotbar", false),
    toggle("swap_gamepad_ab_buttons", "options.swapGamepadAB", false),
    toggle("swap_gamepad_xy_buttons", "options.swapGamepadXY", false),
    toggle("websockets_enabled", "options.websocketsEnabled", false),
    toggle("websocket_encryption", "options.websocketEncryption", false),
    toggle("auto_update_enabled", "options.autoUpdateEnabled", false),
    toggle("only_trusted_skins_allowed", "options.onlyTrustedSkinsAllowed", false),
    toggle("filter_profanity", "options.filterProfanity", false),
    toggle("pause_option_toggle", "options.pauseHint", false),
    toggle("pause_menu_on_focus_lost", "options.pauseMenuOnFocusLost", false),
    toggle("ecomode_toggle", "options.enableEcoMode", false),
    toggle("copy_coordinate_ui", "options.copyCoordinateUI", false),
    toggle("content_log_file", "options.content_log_file", false),
    toggle("content_log_gui", "options.content_log_gui", false),
    toggle("content_log_gui_show_on_errors", "options.content_log_gui_show_on_errors", false),
    toggle("enable_gameplay_subtitles", "options.enableGameplaySubtitles", false),
    toggle("hide_own_gameplay_subtitles", "options.hideOwnGameplaySubtitles", false),
    toggle("hide_ambient_gameplay_subtitles", "options.hideAmbientGameplaySubtitles", false),
    toggle("enable_ui_text_to_speech", "options.enableUITextToSpeech", false),
    toggle("enable_chat_text_to_speech", "options.enableChatTextToSpeech", false),
    toggle("enable_open_chat_message", "options.enableOpenChatMessage", false),
    toggle("hide_endflash", "options.hideEndFlash", false),
    toggle("enable_dithering_blocks", "options.enableDitheringBlocks", false),
    toggle("enable_dithering_mobs", "options.enableDitheringMobs", false),
    toggle("gui_accessibility_scaling", "options.gui.accessibility.scaling", false),
};

/**
 * The volume channel of a vanilla sound slider, or -1 for another option.
 */
int volumeChannel(std::string_view name)
{
    constexpr std::pair<std::string_view, int> Channels[] = {
        { "main_volume", 0 }, { "music_volume", 1 }, { "ambient_volume", 2 }, { "weather_volume", 3 },
        { "block_volume", 4 }, { "hostile_volume", 5 }, { "neutral_volume", 6 }, { "player_volume", 7 },
        { "record_volume", 8 }, { "sound_volume", 9 },
    };
    for (const auto& [option, channel] : Channels) {
        if (option == name) {
            return channel;
        }
    }
    return -1;
}

std::string sectionTitle(int section)
{
    switch (section) {
    case 2:
        return "options.accessibility.title";
    case 14:
        return "options.keyboardAndMouseSettings";
    case 15:
        return "options.controllerSettings";
    case 16:
        return "options.touchSettings";
    case 18:
        return "options.generalTitle";
    case 19:
        return "options.accountTitle";
    case 20:
        return "options.creatorTitle";
    case 22:
        return "options.viewSubscriptions";
    case 23:
        return "options.sounds.title";
    case 24:
        return "menu.globalpacks";
    case 25:
        return "menu.storageManagement";
    case 27:
        return "options.language";
    case ModsSection:
        return "kestrel.settings.mods";
    default:
        return "options.videoTitle";
    }
}

}

int Menu::optionValue(std::string_view name, int fallback) const
{
    if (int channel = volumeChannel(name); channel >= 0) {
        return volumes[static_cast<size_t>(channel)];
    }
    if (name == "field_of_view") {
        return fov();
    }
    if (name == "gamma") {
        return brightnessPercent;
    }
    if (name == "render_distance") {
        return renderDistance();
    }
    if (name == "max_framerate") {
        return maxFps();
    }
    if (name == "glint_strength") {
        return glintStrengthPercent;
    }
    if (name == "glint_speed") {
        return glintSpeedPercent;
    }
    if (name == "hide_paperdoll") {
        return paperDollHidden() ? 1 : 0;
    }
    if (name == "field_of_view_toggle") {
        return gameplayFov() ? 1 : 0;
    }
    auto found = extraOptionValues.find(std::string(name));
    return found == extraOptionValues.end() ? fallback : found->second;
}

void Menu::setOptionValue(std::string_view name, int value)
{
    if (int channel = volumeChannel(name); channel >= 0) {
        setSoundVolume(static_cast<size_t>(channel), value);
    } else if (name == "field_of_view") {
        setFov(value);
    } else if (name == "gamma") {
        setBrightness(value);
    } else if (name == "render_distance") {
        setRenderDistance(std::clamp(value, MinRenderDistance, MaxRenderDistance));
    } else if (name == "max_framerate") {
        setMaxFps(value <= 0 ? UnlimitedFps : std::clamp(value, MinMaxFps, MaxMaxFps));
    } else if (name == "glint_strength") {
        setGlintStrength(value);
    } else if (name == "glint_speed") {
        setGlintSpeed(value);
    } else if (name == "hide_paperdoll") {
        setPaperDollHidden(value != 0);
    } else if (name == "field_of_view_toggle") {
        setGameplayFov(value != 0);
    } else {
        extraOptionValues[std::string(name)] = value;
    }
    if (opacityOption(name)) {
        hudScreen.reset();
    }
}

/**
 * The game's own settings screen, settings_screen.json with its sections,
 * fed the way its settings screen controller feeds it: the section picked in
 * the navigation_tab radio group, every option's value, label and enabled
 * state under the names its controls bind, the account, the languages and
 * the global resource packs. What the player changes goes straight into the
 * settings. False when the loaded UI has no settings screen.
 */
bool Menu::vanillaSettings(Context& ui, float width, float height)
{
    if (!jsonUi || !jsonUi->has(SettingsRoot)) {
        return false;
    }
    if (!settingsUi || settingsUi->definitions() != jsonUi) {
        UiRow variables;
        for (const auto& [name, on] : ContextFlags) {
            variables[std::string("$") + name] = UiValue::of(on);
        }
        for (const auto& [name, index] : Sections) {
            variables[std::string("$") + name] = UiValue::of(static_cast<double>(index));
        }
        variables["$play_button_target"] = UiValue::of(std::string("button.menu_play"));
        settingsUi = std::make_unique<JsonUiScreen>(jsonUi, SettingsRoot, variables);
        settingsUi->setRenderer([](Context& ui, const std::string& renderer, const Rect& rect, float alpha, const UiLookup&) {
            if (renderer != "profile_image_renderer") {
                return;
            }
            bool avatar = ui.skin().sprite("dynamic/avatar").valid;
            uint8_t opacity = static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
            ui.sprite(rect, avatar ? "dynamic/avatar" : "ui/profile_glyph_color", { 255, 255, 255, opacity });
        });
        settingsUi->fire("screen.entrance_push");
    }
    if (!settingsUi->valid()) {
        return false;
    }
    if (vanillaSettingsSection <= 0) {
        vanillaSettingsSection = VideoSection;
    }

    UiData data;
    data.hideUnboundVisibility = true;
    UiRow& globals = data.globals;
    globals["#radio:navigation_tab"] = UiValue::of(static_cast<double>(vanillaSettingsSection));
    int touchMode = optionValue("touch_control_mode", 1);
    globals["#touch_controls_v2"] = UiValue::of(touchMode != 2);
    globals["#touch_controls_v2_crosshair_mode"] = UiValue::of(touchMode == 1);
    globals["#touch_selected"] = UiValue::of(touchMode == 0);
    globals["#crosshair_selected"] = UiValue::of(touchMode == 1);
    globals["#classic_selected"] = UiValue::of(touchMode == 2);
    globals["#new_touch_control_schemes_settings"] = UiValue::of(true);
    globals["#resizable_ui_active"] = UiValue::of(true);
    globals["#modify_layout_enabled"] = UiValue::of(true);
    globals["#crosshair_action_button_on"] = UiValue::of(touchMode == 1);
    const auto scaleRange = guiScaleRange(width * ui.pixelScale(), height * ui.pixelScale());
    for (Option option : Options) {
        if (std::string_view(option.name) == "gui_scale") {
            option.min = scaleRange.minimum;
            option.max = scaleRange.maximum;
        }
        std::string name = option.name;
        int value = std::clamp(optionValue(option.name, option.fallback), option.min, option.max);
        globals["#" + name + "_enabled"] = UiValue::of(true);
        switch (option.kind) {
        case OptionKind::Toggle:
            globals["#" + name] = UiValue::of(value != 0);
            break;
        case OptionKind::Slider: {
            globals["#" + name] = UiValue::of(settingsSliderBinding(name, value, option.min, option.max));
            globals["#" + name + "_steps"] = UiValue::of(static_cast<double>(option.max - option.min + 1));
            std::string shown;
            if (name == "gui_scale" || name == "field_of_view" || name == "render_distance") {
                shown = std::to_string(value);
            } else if (name == "max_framerate") {
                shown = value == 0 ? tr("options.framerateLimit.max", "Max") : std::to_string(value);
            } else {
                shown = std::to_string(value) + "%%";
            }
            globals["#" + name + "_text_value"] = UiValue::of(shown);
            globals["#" + name + "_slider_label"] = UiValue::of(tr(option.label, option.label) + ": " + shown);
            break;
        }
        case OptionKind::Dropdown: {
            globals["#" + name + "_dropdown_enabled"] = UiValue::of(true);
            globals["#" + name + "_dropdown_toggle_label"] = UiValue::of(tr(option.choices[value].label, option.choices[value].label));
            for (size_t i = 0; i < option.choiceCount; ++i) {
                globals[std::string("#") + option.choices[i].name] = UiValue::of(static_cast<int>(i) == value);
            }
            break;
        }
        }
    }
    for (const char* flag : { "#hint_toggles_enabled", "#screen_animations_visible", "#keyboard_show_standard_keyboard_options",
             "#show_render_distance", "#max_framerate_slider_visible", "#advanced_graphics_options_button_visible", "#full_screen_enabled" }) {
        globals[flag] = UiValue::of(true);
    }
    for (const auto& [name, radio, label] : { std::tuple { "split_screen", "#split_screen_radio_horizontal", "options.splitscreen.horizontal" },
             std::tuple { "ui_profile", "#ui_profile_radio_classic", "options.uiprofile.classic" } }) {
        globals[std::string("#") + name + "_dropdown_toggle_label"] = UiValue::of(tr(label, label));
        globals[radio] = UiValue::of(true);
    }
    globals["#splitscreen_interface_opacity"] = UiValue::of(1.0);
    globals["#splitscreen_interface_opacity_slider_label"] = UiValue::of(tr("options.splitscreenInterfaceOpacity", "Split Screen Interface Opacity") + ": 100%%");
    globals["#splitscreen_ingame_player_names"] = UiValue::of(true);
    globals["#graphics_mode_radio_deferred_enabled"] = UiValue::of(false);
    globals["#graphics_mode_radio_ray_traced_enabled"] = UiValue::of(false);
    globals["#advanced_graphics_options_grid_visible"] = UiValue::of(advancedGraphicsShown);
    globals["#gui_scale_visible"] = UiValue::of(true);
    globals["#full_screen"] = UiValue::of(chrome.fullscreen);
    std::string title = sectionTitle(vanillaSettingsSection);
    globals["#section_title"] = UiValue::of(vanillaSettingsSection == ModsSection ? std::string("Mods") : tr(title, title));
    globals["#dialog_title"] = UiValue::of(tr("menu.settings", "Settings"));

    bool signedIn = this->signedIn();
    globals["#logged_in"] = UiValue::of(signedIn);
    globals["#not_logged_in"] = UiValue::of(!signedIn);
    globals["#gamertag_label"] = UiValue::of(signedIn ? account.gamertag : std::string());
    globals["#ad_account_name"] = UiValue::of(displayName);
    globals["#player_name"] = UiValue::of(offlineNameValue);
    globals["#player_name_enabled"] = UiValue::of(!signedIn);
    globals["#account_info"] = UiValue::of(signedIn ? tr("options.gamertag", "Gamertag:") + " " + account.gamertag : std::string());
    globals["#use_remote_connect"] = UiValue::of(false);
    globals["#ad_edu_remember_me"] = UiValue::of(false);
    globals["#needs_offline_token_authorization"] = UiValue::of(false);

    const std::vector<LanguageInfo>& languages = Localization::shared().languages();
    std::vector<UiRow>& languageRows = data.collections["languages"];
    for (size_t i = 0; i < languages.size(); ++i) {
        if (languages[i].code == languageCode) {
            globals["#radio:languages"] = UiValue::of(static_cast<double>(i));
        }
    }
    for (const LanguageInfo& language : languages) {
        languageRows.push_back({
            { "#language_description", UiValue::of(language.name) },
            { "#language_initial_selected", UiValue::of(language.code == languageCode) },
        });
    }
    globals["#language_grid_dimension"] = UiValue::of("1," + std::to_string(languages.size()));

    std::vector<UiRow>& keyRows = data.collections["keyboard_standard_collection"];
    auto keyRow = [&](const std::string& name, Key key, bool waiting) {
        keyRows.push_back({
            { "#keymapping_name", UiValue::of(name) },
            { "#audible_keymapping_name", UiValue::of(name) },
            { "#binding_button_text", UiValue::of(waiting ? std::string("...") : std::string(keyName(key))) },
            { "#binding_icon_sprite", UiValue::of(std::string()) },
        });
    };
    for (size_t i = 0; i < KeyBindings::Count; ++i) {
        keyRow(tr(KeyBindings::translationKey(i), KeyBindings::label(i)), bindings.keys[i], rebinding && *rebinding == i);
    }
    for (const ModKeyBind& bind : modBinds) {
        keyRow(bind.label, bind.key, rebindingMod && *rebindingMod == bind.id);
    }
    globals["#keyboard_standard_grid_dimension"] = UiValue::of("1," + std::to_string(keyRows.size()));

    constexpr std::pair<const char*, const char*> GamepadControls[] = {
        { "key.attack", "RT" }, { "key.use", "LT" }, { "key.jump", "A" }, { "key.sneak", "B" },
        { "key.sprint", "LS" }, { "key.togglePerspective", "D-Pad Up" }, { "key.cycleItemLeft", "LB" },
        { "key.cycleItemRight", "RB" }, { "key.inventory", "Y" }, { "key.chat", "D-Pad Right" }, { "key.drop", "D-Pad Down" },
    };
    std::vector<UiRow>& padRows = data.collections["gamepad_collection"];
    for (const auto& [action, button] : GamepadControls) {
        std::string name = tr(action, action);
        padRows.push_back({
            { "#keymapping_name", UiValue::of(name) },
            { "#audible_keymapping_name", UiValue::of(name) },
            { "#binding_button_text", UiValue::of(std::string(button)) },
            { "#binding_icon_sprite", UiValue::of(std::string()) },
        });
    }
    globals["#gamepad_grid_dimension"] = UiValue::of("1," + std::to_string(padRows.size()));

    bindGlobalResources(data);

    bool modal = touchSettingsRoot.has_value();
    ui.setBlocked(modal);
    settingsUi->draw(ui, { 0.0f, 0.0f, width, height }, data);
    ui.setBlocked(false);
    if (modal) {
        if (!touchSettingsUi || touchSettingsUi->definitions() != jsonUi) touchSettingsUi = std::make_unique<JsonUiScreen>(jsonUi, *touchSettingsRoot);
        touchSettingsUi->draw(ui, { 0, 0, width, height }, data);
        for (const auto& event : touchSettingsUi->takeEvents()) {
            if (event.kind == UiEvent::Kind::Slider) {
                for (const auto& option : Options) if (event.name == option.name) setOptionValue(option.name, option.min + int(std::lround(event.value)));
            } else if (event.name.starts_with("button.touch_mode_")) {
                setOptionValue("touch_control_mode", event.name.back() - '0');
                touchSettingsRoot.reset();
            } else if (event.name == "button.menu_cancel" || event.name == "button.touch_done") touchSettingsRoot.reset();
        }
        if (ui.input().escape) touchSettingsRoot.reset();
        if (!touchSettingsRoot) touchSettingsUi.reset();
        return true;
    }
    if (vanillaSettingsSection == ModsSection) {
        if (std::optional<Rect> area = settingsUi->controlRect("content_area")) {
            Rect view { area->x, area->y, area->w + 7.0f, area->h };
            scrollArea(ui, view, pageScroll, pageContent);
            ui.setClip(*area);
            float top = area->y - pageScroll;
            float y = top;
            modsPage(ui, area->x, y, area->w);
            pageContent = y - top;
            ui.clearClip();
        }
    }

    for (const UiEvent& event : settingsUi->takeEvents()) {
        if (globalResourcesEvent(event)) {
            continue;
        }
        if (event.kind == UiEvent::Kind::Toggle) {
            if (event.name == "navigation_tab") {
                vanillaSettingsSection = static_cast<int>(event.value);
                continue;
            }
            if (event.collection == "languages" && event.index >= 0 && static_cast<size_t>(event.index) < languages.size()) {
                languageCode = languages[static_cast<size_t>(event.index)].code;
                continue;
            }
            if (event.name == "full_screen") {
                chromeAction = ChromeAction::Fullscreen;
                continue;
            }
            std::string_view name = event.name;
            if (!name.empty() && name.front() == '#') {
                name.remove_prefix(1);
            }
            for (const Option& option : Options) {
                if (option.kind == OptionKind::Toggle && name == option.name) {
                    setOptionValue(option.name, event.state ? 1 : 0);
                    break;
                }
                if (option.kind == OptionKind::Dropdown) {
                    for (size_t i = 0; i < option.choiceCount; ++i) {
                        if (name == option.choices[i].name && event.state) {
                            setOptionValue(option.name, static_cast<int>(i));
                        }
                    }
                }
            }
            continue;
        }
        if (event.kind == UiEvent::Kind::Slider) {
            for (Option option : Options) {
                if (std::string_view(option.name) == "gui_scale") {
                    option.min = scaleRange.minimum;
                    option.max = scaleRange.maximum;
                }
                if (option.kind == OptionKind::Slider && event.name == option.name) {
                    int value = settingsSliderValue(option.name, event.value, option.min, option.max);
                    setOptionValue(option.name, value);
                    break;
                }
            }
            continue;
        }
        if (event.kind == UiEvent::Kind::TextDone && event.name == "player_name_text_box" && !signedIn) {
            setOfflineName(event.text.substr(0, 16));
            continue;
        }
        if (event.kind != UiEvent::Kind::Button) {
            continue;
        }
        if (event.name == "button.select_control_mode" || event.name == "button.modify_control_layout") {
            touchSettingsRoot = event.name == "button.select_control_mode" ? "kestrel_touch.mode_selection" : "kestrel_touch.customize";
            touchSettingsUi.reset();
            continue;
        }
        if (event.name == "button.reset_touch_bindings") {
            extraOptionValues.erase("touch_autojump");
            for (const char* name : { "touch_control_mode", "joystick_visibility", "top_button_scale", "sneak", "touch_sensitivity", "spyglass_touch_dampening", "touch_button_size", "touch_control_opacity", "touch_invert_y_axis", "left_handed", "sprint_on_movement", "show_action_button", "show_block_select_button", "show_toggle_camera_perspective_button", "split_controls", "swap_jump_and_sneak", "hotbar_only_touch" }) extraOptionValues.erase(name);
            continue;
        }
        if (event.name == "button.menu_open_uri" && !event.text.empty()) {
            platform::openUrl(event.text);
            continue;
        }
        if (event.name == "setup_safe_zone_button") {
            dialog = Dialog::SafeArea;
            continue;
        }
        if (event.name == "change_gamertag_button") {
            platform::openUrl("https://social.xbox.com/changegamertag");
            continue;
        }
        if (event.name == "manage_account_button") {
            platform::openUrl("https://account.xbox.com/Settings");
            continue;
        }
        if (event.name == "realms_invites_button") {
            socialOpen = true;
            continue;
        }
        if (event.name == "button.copy_account_info" && signedIn) {
            platform::copyText(account.gamertag);
            notify(tr("options.copiedToClipboard", "Copied to clipboard"));
            continue;
        }
        if (event.name == "button.menu_exit" || event.name == "button.menu_cancel") {
            goBack();
            break;
        }
        if (event.name == "button.reset_keyboard_bindings") {
            bindings = KeyBindings {};
            rebinding.reset();
            rebindingMod.reset();
        } else if (event.collection == "keyboard_standard_collection" && event.index >= 0
            && (event.name == "button.binding_button" || event.name == "button.reset_binding")) {
            size_t row = static_cast<size_t>(event.index);
            bool reset = event.name == "button.reset_binding";
            if (row < KeyBindings::Count) {
                if (reset) {
                    bindings.keys[row] = KeyBindings {}.keys[row];
                } else {
                    rebinding = row;
                    rebindingMod.reset();
                }
            } else if (row - KeyBindings::Count < modBinds.size() && !reset) {
                rebindingMod = modBinds[row - KeyBindings::Count].id;
                rebinding.reset();
            }
        } else if (event.name == "button.expand_advanced_graphics") {
            advancedGraphicsShown = !advancedGraphicsShown;
        } else if (event.name == "sign_in_button" || event.name == "button.switch_accounts") {
            accountRequest = AccountRequest::SignIn;
        } else if (event.name == "sign_out_button" || event.name == "button.sign_out") {
            accountRequest = AccountRequest::SignOut;
        }
    }
    return true;
}

}
