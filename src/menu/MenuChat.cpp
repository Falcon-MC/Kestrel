#include "menu/Menu.h"
#include "platform/Shell.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"
#include "ui/Utf8.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;

namespace {

// Sizes and timings from the game's hud_screen.json and chat_screen.json.
constexpr size_t MaxChatLines = 100;
constexpr size_t MaxFeedLines = 50;
constexpr size_t MaxChatHistory = 100;
constexpr float FeedFadeSeconds = 1.0f;
constexpr const char* ChatRoot = "chat.chat_screen";
// commands_panel is the screen less 50px, and each auto_complete row is 10px tall; the
// game keeps the last row of that panel empty above the text box.
constexpr float ChatChromeHeight = 50.0f;
constexpr float AutoCompleteRowHeight = 10.0f;
constexpr const char* HelpAlias = "?";
constexpr const char* HelpDescription = "commands.help.description";
constexpr const char* Ellipsis = "\xE2\x80\xA6";
constexpr const char* Italic = "\xC2\xA7o";
constexpr const char* ChatSettingsRoot = "chat_settings.chat_settings_popup";
constexpr const char* ChatColorPrefix = "chat_";
constexpr const char* MentionsColorPrefix = "mentions_";
// One line of the chat font at its normal size, which line spacing adds to.
constexpr double ChatLineHeight = 10.0;
// Factory items remade after a settings change so their variables take effect.
constexpr unsigned StyleSerialShift = 40;

/**
 * A color of the chat settings' font_colors list, the game's formatting
 * colors of the same names.
 */
struct ChatColor {
    const char* key;
    const char* name;
    float red;
    float green;
    float blue;
};

constexpr std::array<ChatColor, ChatColorCount> ChatColors { {
    { "color.white", "White", 1.0f, 1.0f, 1.0f },
    { "color.gray", "Gray", 0.667f, 0.667f, 0.667f },
    { "color.yellow", "Yellow", 1.0f, 1.0f, 0.333f },
    { "color.gold", "Gold", 1.0f, 0.667f, 0.0f },
    { "color.aqua", "Aqua", 0.333f, 1.0f, 1.0f },
    { "color.green", "Green", 0.333f, 1.0f, 0.333f },
    { "color.light_purple", "Light Purple", 1.0f, 0.333f, 1.0f },
} };

bool blank(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\t'; });
}

std::string lowered(std::string_view text)
{
    std::string folded(text);
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return folded;
}

const ChatColor& chatColor(int index)
{
    return ChatColors[static_cast<size_t>(std::clamp(index, 0, ChatColorCount - 1))];
}

/**
 * A color as a bound #color reads it: its channels separated by commas.
 */
std::string colorChannels(const ChatColor& color)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f,%.3f,%.3f,1.0", color.red, color.green, color.blue);
    return buffer;
}

/**
 * A color as a control's color property holds it: a JSON array.
 */
std::string colorArray(const ChatColor& color)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "[%.3f, %.3f, %.3f]", color.red, color.green, color.blue);
    return buffer;
}

std::string oneDecimal(float value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

int lineSpacingSteps()
{
    return static_cast<int>(std::round((MaxChatLineSpacing - MinChatLineSpacing) / ChatLineSpacingStep));
}

float snapLineSpacing(float spacing)
{
    int step = std::clamp(static_cast<int>(std::round((spacing - MinChatLineSpacing) / ChatLineSpacingStep)), 0, lineSpacingSteps());
    return MinChatLineSpacing + static_cast<float>(step) * ChatLineSpacingStep;
}

/**
 * The color index a chat settings radio names, like "#mentions_3", when it
 * belongs to the list with that prefix.
 */
std::optional<int> colorPick(std::string_view name, std::string_view prefix)
{
    if (!name.empty() && name.front() == '#') {
        name.remove_prefix(1);
    }
    if (!name.starts_with(prefix)) {
        return std::nullopt;
    }
    std::string_view digits = name.substr(prefix.size());
    if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        return std::nullopt;
    }
    return std::clamp(std::atoi(std::string(digits).c_str()), 0, ChatColorCount - 1);
}

/**
 * A command description as the list shows it: servers send the game's own
 * as language keys and their own as plain text.
 */
std::string commandDescription(const std::string& description)
{
    if (!description.empty() && description.front() == '%') {
        return Localization::shared().translateMessage(description);
    }
    return Localization::shared().has(description) ? tr(description, description) : description;
}

CommandHints chatCompletions(const std::shared_ptr<const std::vector<ChatCommand>>& commands,
    const std::vector<std::string>& players, std::string_view draft)
{
    if (!draft.empty() && draft.front() == '/') {
        CommandHints hints = commands ? commandHints(*commands, players, draft) : CommandHints {};
        std::string_view typed = draft.substr(1);
        bool naming = commands && typed.find_first_of(" \t") == std::string_view::npos;
        bool listed = std::any_of(hints.suggestions.begin(), hints.suggestions.end(), [](const CommandSuggestion& suggestion) { return suggestion.text == HelpAlias; });
        if (naming && std::string_view(HelpAlias).starts_with(typed) && !listed) {
            hints.replaceFrom = 1;
            hints.suggestions.insert(hints.suggestions.begin(), CommandSuggestion { HelpAlias, HelpDescription });
        }
        return hints;
    }

    CommandHints hints;
    size_t separator = draft.find_last_of(" \t\r\n");
    size_t tokenStart = separator == std::string_view::npos ? 0 : separator + 1;
    if (tokenStart >= draft.size() || draft[tokenStart] != '@') {
        return hints;
    }
    std::string query = lowered(draft.substr(tokenStart + 1));
    hints.replaceFrom = tokenStart;
    for (const std::string& name : players) {
        if (lowered(name).starts_with(query)) {
            hints.suggestions.push_back({ "@" + name + " ", {} });
        }
    }
    return hints;
}

}

void Menu::addChatLine(std::string text, ChatSource source)
{
    chatLines.push_back({ std::move(text), std::chrono::steady_clock::now(), ++chatSerial, source });
    while (chatLines.size() > MaxChatLines) {
        chatLines.pop_front();
    }
}

void Menu::clearChat()
{
    chatLines.clear();
    chatToBottom = true;
}

void Menu::openChat(std::string draft)
{
    dialog = Dialog::Chat;
    field = Field::Chat;
    selectedField = Field::None;
    chatDraft = std::move(draft);
    chatCaret.reset();
    chatCycle.clear();
    chatRecall.reset();
    chatToBottom = true;
}

void Menu::setChatSettings(const ChatSettings& value)
{
    ChatSettings clamped = value;
    clamped.fontSize = std::clamp(value.fontSize, MinChatFontSize, MaxChatFontSize);
    clamped.lineSpacing = snapLineSpacing(std::clamp(value.lineSpacing, MinChatLineSpacing, MaxChatLineSpacing));
    clamped.chatColor = std::clamp(value.chatColor, 0, ChatColorCount - 1);
    clamped.mentionsColor = std::clamp(value.mentionsColor, 0, ChatColorCount - 1);
    if (clamped == chatOptions) {
        return;
    }
    chatOptions = clamped;
    ++chatStyleRevision;
}

/**
 * Whether the chat settings let a line show: muting all chat hides what
 * players say and do, muting emotes hides their emotes, and what the server
 * or a command prints always shows.
 */
bool Menu::chatLineShown(const ChatLine& line) const
{
    if (chatOptions.muteAll && line.source != ChatSource::System) {
        return false;
    }
    return !(chatOptions.muteEmotes && line.source == ChatSource::Emote);
}

/**
 * The $chat_text_color of a line: the mentions color when a player's line
 * names the player with an @, the chat color otherwise.
 */
std::string Menu::chatLineColor(const ChatLine& line) const
{
    bool mentioned = line.source != ChatSource::System && !displayName.empty() && lowered(line.text).find("@" + lowered(displayName)) != std::string::npos;
    return colorArray(chatColor(mentioned ? chatOptions.mentionsColor : chatOptions.chatColor));
}

/**
 * The font variables chat lines are made with. Mojangles keeps its one size;
 * Noto Sans scales with the size setting, and line spacing pads each line by
 * the share of a line above one.
 */
ChatStyle Menu::chatStyle() const
{
    ChatStyle style;
    constexpr std::array<double, 3> durations { 3.0, 10.0, 30.0 };
    style.lifetime = durations[static_cast<size_t>(std::clamp(option("chat_message_duration", 1), 0, 2))];
    style.backgroundOpacity = std::clamp(option("chat_background_opacity", 50), 0, 100) / 100.0;
    style.fontType = chatOptions.smoothFont ? "smooth" : "default";
    style.fontScale = chatOptions.smoothFont ? static_cast<double>(chatOptions.fontSize) / static_cast<double>(DefaultChatFontSize) : 1.0;
    style.linePadding = (static_cast<double>(chatOptions.lineSpacing) - 1.0) * ChatLineHeight * style.fontScale;
    return style;
}

uint64_t Menu::chatItemSerial(const ChatLine& line) const
{
    return line.serial + (chatStyleRevision << StyleSerialShift);
}

void Menu::openChatSettings()
{
    chatSettingsOpen = true;
    chatSettingsUi.reset();
}

void Menu::closeChatSettings()
{
    chatSettingsOpen = false;
    chatSettingsClosed = true;
}

void Menu::closeChat()
{
    chatSettingsOpen = false;
    dialog = Dialog::None;
    field = Field::None;
    selectedField = Field::None;
    chatDraft.clear();
    chatCaret.reset();
    chatCycle.clear();
    chatRecall.reset();
}

/**
 * Sends the draft and keeps the screen open with an empty box, the way the
 * game does after Enter. A command closes the screen instead.
 */
void Menu::submitChat()
{
    bool command = !chatDraft.empty() && chatDraft.front() == '/';
    if (!blank(chatDraft)) {
        if (chatHistory.empty() || chatHistory.back() != chatDraft) {
            chatHistory.push_back(chatDraft);
            if (chatHistory.size() > MaxChatHistory) {
                chatHistory.erase(chatHistory.begin());
            }
        }
        chatOutgoing.push_back(chatDraft);
    }
    chatDraft.clear();
    chatCaret.reset();
    chatCycle.clear();
    chatRecall.reset();
    selectedField = Field::None;
    chatToBottom = true;
    if (command) {
        closeChat();
    }
}

void Menu::recallChat(int step)
{
    if (chatHistory.empty()) {
        return;
    }
    size_t index = chatRecall.value_or(chatHistory.size());
    if (step < 0 && index > 0) {
        --index;
    } else if (step > 0 && index < chatHistory.size()) {
        ++index;
    } else {
        return;
    }
    if (index == chatHistory.size()) {
        chatRecall.reset();
        chatDraft.clear();
    } else {
        chatRecall = index;
        chatDraft = chatHistory[index];
    }
    chatCaret.reset();
    chatCycle.clear();
    selectedField = Field::None;
}

void Menu::eraseChat(bool word)
{
    size_t caret = std::min(chatCaret.value_or(chatDraft.size()), chatDraft.size());
    if (selectedField == Field::Chat) {
        chatDraft.clear();
        chatCaret.reset();
    } else {
        std::string prefix = chatDraft.substr(0, caret);
        if (word) {
            while (!prefix.empty() && prefix.back() == ' ') prefix.pop_back();
            while (!prefix.empty() && prefix.back() != ' ') popUtf8(prefix);
        } else {
            popUtf8(prefix);
        }
        chatDraft.erase(prefix.size(), caret - prefix.size());
        chatCaret = prefix.size();
    }
    selectedField = Field::None;
    chatCycle.clear();
}

/**
 * Tab fills in the first completion for the word being typed, pressing it
 * again (Shift+Tab backwards) cycles through the rest.
 */
void Menu::completeChat(bool backwards)
{
    size_t caret = std::min(chatCaret.value_or(chatDraft.size()), chatDraft.size());
    if (chatCycle.empty() || chatDraft != chatCycleDraft || caret != chatCycleCaret) {
        CommandHints hints = chatCompletions(commands, players, std::string_view(chatDraft).substr(0, caret));
        if (hints.suggestions.empty()) {
            chatCycle.clear();
            return;
        }
        chatCycle = std::move(hints.suggestions);
        chatCycleBase = chatDraft.substr(0, hints.replaceFrom);
        size_t end = chatDraft.find_first_of(" \t", caret);
        chatCycleTail = end == std::string::npos ? std::string() : chatDraft.substr(end);
        chatCycleIndex = backwards ? chatCycle.size() - 1 : 0;
    } else {
        chatCycleIndex = (chatCycleIndex + (backwards ? chatCycle.size() - 1 : 1)) % chatCycle.size();
    }
    chatDraft = chatCycleBase + chatCycle[chatCycleIndex].text + chatCycleTail;
    chatCaret = chatCycleBase.size() + chatCycle[chatCycleIndex].text.size();
    chatCycleCaret = *chatCaret;
    chatCycleDraft = chatDraft;
    chatRecall.reset();
    selectedField = Field::None;
}

/**
 * T or Enter opens chat while playing and a typed slash opens it with the
 * slash already in the box; in chat, Enter sends, Tab completes commands and
 * the arrows walk through what was sent before. Returns true when the keys
 * were used up.
 */
bool Menu::handleChatKeys(const InputState& input)
{
    if (capturesMouse()) {
        if (input.text == U"/") {
            openChat("/");
            return true;
        }
        if (input.enter || (input.pressedKey != Key::None && input.pressedKey == bindings.chat())) {
            openChat({});
            return true;
        }
        return false;
    }
    if (dialog != Dialog::Chat) {
        return false;
    }
    if (chatSettingsOpen || chatSettingsClosed) {
        chatSettingsClosed = false;
        return input.escape || input.enter || input.tab || input.pressedKey == Key::Up || input.pressedKey == Key::Down;
    }
    if (input.enter) {
        if (!input.text.empty()) type(input.text);
        submitChat();
        return true;
    }
    if (input.tab) {
        if (!input.text.empty()) type(input.text);
        completeChat(input.isHeld(Key::Shift));
        return true;
    }
    if (input.pressedKey == Key::Up || input.pressedKey == Key::Down) {
        recallChat(input.pressedKey == Key::Up ? -1 : 1);
        return true;
    }
    if (input.pressedKey == Key::Left || input.pressedKey == Key::Right) {
        bool left = input.pressedKey == Key::Left;
        size_t caret = std::min(chatCaret.value_or(chatDraft.size()), chatDraft.size());
        if (selectedField == Field::Chat) {
            caret = left ? 0 : chatDraft.size();
        } else if (left) {
            std::string prefix = chatDraft.substr(0, caret);
            popUtf8(prefix);
            if (input.isHeld(Key::Control)) {
                while (!prefix.empty() && prefix.back() == ' ') prefix.pop_back();
                while (!prefix.empty() && prefix.back() != ' ') popUtf8(prefix);
            }
            caret = prefix.size();
        } else if (caret < chatDraft.size()) {
            nextCodepoint(chatDraft, caret);
            if (input.isHeld(Key::Control)) {
                while (caret < chatDraft.size() && chatDraft[caret] != ' ') nextCodepoint(chatDraft, caret);
                while (caret < chatDraft.size() && chatDraft[caret] == ' ') ++caret;
            }
        }
        chatCaret = caret;
        selectedField = Field::None;
        chatCycle.clear();
        return true;
    }
    return false;
}

/**
 * The HUD log in the top left: the newest lines on a translucent strip over
 * the left 40% of the screen, each fading out after its lifetime, no taller
 * than half the screen.
 */
/**
 * The lines the HUD chat still shows, oldest first: hud_screen.json keeps a
 * line for its lifetime, fades it over a second and holds at most fifty.
 * Lines the chat settings mute are left out.
 */
std::vector<HudChatLine> Menu::hudChat() const
{
    std::vector<HudChatLine> lines;
    double lifetime = chatStyle().lifetime;
    auto now = std::chrono::steady_clock::now();
    for (auto it = chatLines.rbegin(); it != chatLines.rend() && lines.size() < MaxFeedLines; ++it) {
        if (std::chrono::duration<float>(now - it->arrived).count() >= lifetime + FeedFadeSeconds) {
            break;
        }
        if (chatLineShown(*it)) {
            lines.push_back({ it->text, chatItemSerial(*it), chatLineColor(*it) });
        }
    }
    std::reverse(lines.begin(), lines.end());
    return lines;
}

/**
 * The rows of the chat screen's auto_complete collection, top down: the
 * completions for the word being typed, each with its description in
 * italics, then the syntax hint of every overload that still fits. A list
 * longer than the panel ends in an ellipsis, and while Tab cycles the window
 * follows the pick.
 */
std::vector<Menu::ChatRow> Menu::chatRows(size_t capacity) const
{
    std::vector<ChatRow> rows;
    if (chatDraft.empty() || capacity == 0) {
        return rows;
    }
    size_t caret = std::min(chatCaret.value_or(chatDraft.size()), chatDraft.size());
    bool cycling = !chatCycle.empty() && chatDraft == chatCycleDraft && caret == chatCycleCaret;
    CommandHints hints = chatCompletions(commands, players, std::string_view(chatDraft).substr(0, caret));
    const std::vector<CommandSuggestion>& suggestions = cycling ? chatCycle : hints.suggestions;
    std::string base = cycling ? chatCycleBase : chatDraft.substr(0, hints.replaceFrom);
    size_t end = chatDraft.find_first_of(" \t", caret);
    std::string tail = cycling ? chatCycleTail : end == std::string::npos ? std::string() : chatDraft.substr(end);

    std::vector<ChatRow> all;
    for (const CommandSuggestion& suggestion : suggestions) {
        std::string text = suggestion.text;
        if (!suggestion.description.empty()) {
            text += std::string(" - ") + Italic + commandDescription(suggestion.description);
        }
        all.push_back({ std::move(text), base + suggestion.text + tail, base.size() + suggestion.text.size() });
    }
    for (const std::string& line : hints.usage) {
        all.push_back({ line, std::nullopt });
    }
    if (all.size() <= capacity) {
        return all;
    }
    size_t room = capacity - 1;
    size_t first = cycling && chatCycleIndex >= room ? chatCycleIndex + 1 - room : 0;
    first = std::min(first, all.size() - room);
    rows.assign(std::make_move_iterator(all.begin() + static_cast<std::ptrdiff_t>(first)), std::make_move_iterator(all.begin() + static_cast<std::ptrdiff_t>(first + room)));
    rows.push_back({ Ellipsis, std::nullopt });
    return rows;
}

/**
 * chat_screen.json, fed the way the game's chat screen controller feeds it:
 * the kept lines in messages_factory, the completions in the auto_complete
 * collection with the log hidden under them, the draft in the message box
 * and the header, keyboard, settings and send buttons. The draft is typed
 * and kept by the menu, so the edit box only shows it.
 */
void Menu::chatScreen(Context& ui, float width, float height)
{
    bool open = dialog == Dialog::Chat;
    if (!jsonUi) {
        if (open) {
            closeChat();
        }
        return;
    }
    if (!chatUi) {
        chatUi = std::make_unique<ui::JsonUiScreen>(jsonUi, ChatRoot);
    }
    if (!chatUi->valid()) {
        if (open) {
            closeChat();
        }
        return;
    }
    if (open) {
        field = Field::Chat;
        chatSettingsClosed = false;
    }
    bool interactive = open && !chatSettingsOpen;

    using ui::UiValue;
    size_t capacity = static_cast<size_t>(std::max(0.0f, std::floor((height - ChatChromeHeight) / AutoCompleteRowHeight) - 1.0f));
    std::vector<ChatRow> rows = chatRows(capacity);

    ui::UiData data;
    ui::UiRow& globals = data.globals;
    bool cheats = commands && !commands->empty();
    globals["#chat_title_text"] = UiValue::of(cheats ? tr("chat.title.cheats", "Chat and Commands") : tr("chat.title", "Chat"));
    globals["#chat_title_visible"] = UiValue::of(true);
    globals["#back_button_text"] = UiValue::of(tr("controller.buttonTip.back", "Back"));
    globals["#chat_visible"] = UiValue::of(rows.empty());
    globals["#scroll_chat_to_bottom"] = UiValue::of(chatToBottom);
    globals["#has_new_messages"] = UiValue::of(false);
    globals["#keyboard_button_visible"] = UiValue::of(false);
    globals["#keyboard_being_used"] = UiValue::of(false);
    globals["#gamepad_helper_visible"] = UiValue::of(false);
    globals["#text_box_enabled"] = UiValue::of(true);
    globals["#send_button_visible"] = UiValue::of(!blank(chatDraft));
    globals["#send_button_accessibility_text"] = UiValue::of(tr("accessibility.chat.tts.sendChatMessage", "Send"));
    bool coordinatesShown = inGame() && optionValue("copy_coordinate_ui", 0) != 0;
    const std::optional<std::array<int, 3>>& picked = chatFacingCoordinates ? session.facingBlock : session.playerBlock;
    std::string coordinates = picked ? std::to_string((*picked)[0]) + " " + std::to_string((*picked)[1]) + " " + std::to_string((*picked)[2]) : std::string();
    globals["#chat_coordinate_dropdown_visible"] = UiValue::of(coordinatesShown);
    globals["#copy_button_enabled"] = UiValue::of(coordinatesShown && !coordinates.empty());
    globals["#coordinates_text"] = UiValue::of(coordinates);
    globals["#chat_coordinate_dropdown_label"] = UiValue::of(chatFacingCoordinates ? tr("chat.coordinateTypeFacing", "Facing") : tr("chat.coordinateTypePosition", "My Position"));
    globals["#coordinate_type_position"] = UiValue::of(!chatFacingCoordinates);
    globals["#coordinate_type_facing"] = UiValue::of(chatFacingCoordinates);
    globals["#cheats_on"] = UiValue::of(operatorCommands);
    for (const char* panel : { "#host_main_visible", "#host_teleport_main_visible", "#host_teleport_players_visible", "#host_time_visible", "#host_weather_visible" }) {
        globals[panel] = UiValue::of(false);
    }

    globals["#message_text_box_content"] = UiValue::of(chatDraft);
    chatUi->showListeningCaret(interactive, chatCaret);
    chatUi->showListeningSelection(interactive && selectedField == Field::Chat && !chatDraft.empty());

    std::vector<ui::UiRow>& messages = data.collections["messages_factory"];
    std::vector<ui::UiFactoryItem>& made = data.factories["messages_factory"];
    ChatStyle style = chatStyle();
    for (const ChatLine& line : chatLines) {
        if (!chatLineShown(line)) {
            continue;
        }
        messages.push_back({ { "#text", UiValue::of(line.text) } });
        made.push_back({ "chat_screen_messages", {
            { "$chat_font_scale_factor", UiValue::of(style.fontScale) },
            { "$chat_line_spacing", UiValue::of(style.linePadding) },
            { "$chat_font_type", UiValue::of(style.fontType) },
            { "$chat_text_color", UiValue::of(chatLineColor(line)) },
        }, chatItemSerial(line) });
    }

    std::vector<ui::UiRow>& completions = data.collections["auto_complete"];
    for (const ChatRow& row : rows) {
        completions.push_back({
            { "#auto_complete_text", UiValue::of(row.text) },
            { "#auto_complete_item", UiValue::of(0.0) },
            { "#is_autocomplete_suggestion", UiValue::of(row.pick.has_value()) },
        });
    }

    bool blocked = ui.isBlocked();
    if (!interactive) {
        ui.setBlocked(true);
    }
    chatUi->draw(ui, { 0.0f, 0.0f, width, height }, data);
    ui.setBlocked(blocked);
    chatUi->blur();
    std::vector<ui::UiEvent> events = chatUi->takeEvents();
    if (!open) {
        return;
    }
    chatToBottom = false;
    if (chatSettingsOpen) {
        chatSettingsScreen(ui, width, height);
        return;
    }

    for (const ui::UiEvent& event : events) {
        if (event.kind == ui::UiEvent::Kind::Toggle && event.state) {
            std::string_view toggle = event.name;
            if (!toggle.empty() && toggle.front() == '#') {
                toggle.remove_prefix(1);
            }
            if (toggle == "coordinate_type_position" || toggle == "coordinate_type_facing") {
                chatFacingCoordinates = toggle == "coordinate_type_facing";
            }
            continue;
        }
        if (event.kind != ui::UiEvent::Kind::Button) {
            continue;
        }
        if (event.name == "copy_coordinates_button" && !coordinates.empty()) {
            platform::copyText(coordinates);
            continue;
        }
        if ((event.name == "paste_button" || event.name == "button.chat_paste_coordinates") && !coordinates.empty()) {
            if (!chatDraft.empty() && chatDraft.back() != ' ') {
                chatDraft += ' ';
            }
            chatDraft += coordinates;
            chatCaret.reset();
            chatCycle.clear();
            chatRecall.reset();
            selectedField = Field::None;
            continue;
        }
        if (event.name == "button.send") {
            if (!blank(chatDraft)) {
                submitChat();
            }
        } else if (event.name == "button.menu_exit" || event.name == "button.chat_menu_cancel") {
            closeChat();
        } else if (event.name == "button.click_autocomplete" && event.index >= 0 && static_cast<size_t>(event.index) < rows.size()) {
            if (const std::optional<std::string>& pick = rows[static_cast<size_t>(event.index)].pick) {
                chatDraft = *pick;
                chatCaret = rows[static_cast<size_t>(event.index)].caret;
                chatCycle.clear();
                chatRecall.reset();
                selectedField = Field::None;
            }
        } else if (event.name == "button.open_chat_settings") {
            openChatSettings();
            break;
        } else if (event.name == "button.keyboard_toggle" || event.name == "button.host_toggle") {
            chatDraft = chatDraft == "/" ? std::string() : chatDraft.starts_with('/') ? chatDraft : "/" + chatDraft;
            chatCaret.reset();
            chatCycle.clear();
            chatRecall.reset();
            selectedField = Field::None;
        }
        if (dialog != Dialog::Chat) {
            break;
        }
    }
}

/**
 * chat_settings_menu_screen.json's popup over the chat screen, fed the way
 * the game's chat settings controller feeds it: the mute and text to speech
 * toggles, the font dropdown with the size and line spacing sliders, the
 * font_colors list both color dropdowns share and the reset button. What the
 * player changes goes straight into the chat settings.
 */
void Menu::chatSettingsScreen(Context& ui, float width, float height)
{
    if (!chatSettingsUi) {
        chatSettingsUi = std::make_unique<ui::JsonUiScreen>(jsonUi, ChatSettingsRoot);
    }
    if (!chatSettingsUi->valid()) {
        closeChatSettings();
        return;
    }

    using ui::UiValue;
    const ChatSettings& options = chatOptions;
    std::string size = std::to_string(options.fontSize);
    std::string spacing = oneDecimal(options.lineSpacing);
    std::string noto = tr("typeface.notoSans", "Noto Sans");

    ui::UiData data;
    ui::UiRow& globals = data.globals;
    globals["#close_button_visible"] = UiValue::of(true);
    globals["#hide_chat"] = UiValue::of(options.muteAll);
    globals["#toggle_emote_chat"] = UiValue::of(options.muteEmotes);
    globals["#toggle_tts"] = UiValue::of(options.textToSpeech);

    globals["#chat_typeface_visible"] = UiValue::of(true);
    globals["#chat_typeface_dropdown_enabled"] = UiValue::of(true);
    globals["#chat_typeface_dropdown_label"] = UiValue::of(options.smoothFont ? noto : tr("typeface.mojangles", "Mojangles"));
    globals["#chat_font_type"] = UiValue::of(std::string(options.smoothFont ? "smooth" : "default"));
    globals["#typeface_radio_mojangles"] = UiValue::of(!options.smoothFont);
    globals["#typeface_radio_notoSans"] = UiValue::of(options.smoothFont);

    double sizeRange = static_cast<double>(MaxChatFontSize - MinChatFontSize);
    globals["#chat_font_size"] = UiValue::of(static_cast<double>(options.fontSize - MinChatFontSize));
    globals["#chat_font_size_enabled"] = UiValue::of(options.smoothFont);
    globals["#chat_font_size_steps"] = UiValue::of(sizeRange + 1.0);
    globals["#chat_font_size_custom_label"] = UiValue::of(options.smoothFont ? trf("chat.settings.fontSize", "Size: %s", { size }) : trf("chat.settings.fontSize.disabled", "Size: Available with %s", { noto }));
    globals["#chat_font_size_text_value"] = UiValue::of(size);

    double spacingRange = static_cast<double>(MaxChatLineSpacing - MinChatLineSpacing);
    globals["#chat_line_spacing"] = UiValue::of(static_cast<double>(options.lineSpacing - MinChatLineSpacing) / spacingRange);
    globals["#chat_line_spacing_enabled"] = UiValue::of(true);
    globals["#chat_line_spacing_slider_label"] = UiValue::of(tr("chat.settings.lineSpacing", "Line Spacing") + ": " + trf("chat.settings.lineSpacingNumber", "x%s", { spacing }));
    globals["#chat_line_spacing_text_value"] = UiValue::of(spacing);

    std::vector<ui::UiRow>& colors = data.collections["font_colors"];
    for (const ChatColor& color : ChatColors) {
        colors.push_back({
            { "#font_color", UiValue::of(colorChannels(color)) },
            { "#font_color_label", UiValue::of(tr(color.key, color.name)) },
        });
    }
    for (auto [prefix, picked] : { std::pair { ChatColorPrefix, options.chatColor }, std::pair { MentionsColorPrefix, options.mentionsColor } }) {
        std::string list = std::string(prefix);
        const ChatColor& color = chatColor(picked);
        globals["#" + list + "color_dropdown_enabled"] = UiValue::of(true);
        globals["#" + list + "color_dropdown_label"] = UiValue::of(tr(color.key, color.name));
        globals["#" + list + "toggle_color"] = UiValue::of(colorChannels(color));
        for (int index = 0; index < ChatColorCount; ++index) {
            globals["#" + list + std::to_string(index)] = UiValue::of(index == picked);
        }
    }

    chatSettingsUi->draw(ui, { 0.0f, 0.0f, width, height }, data);

    ChatSettings changed = chatOptions;
    bool close = false;
    for (const ui::UiEvent& event : chatSettingsUi->takeEvents()) {
        std::string_view name = event.name;
        if (!name.empty() && name.front() == '#') {
            name.remove_prefix(1);
        }
        if (event.kind == ui::UiEvent::Kind::Button) {
            if (name == "button.close_chat_settings" || name == "button.menu_exit") {
                close = true;
            } else if (name == "button.reset_chat_settings") {
                changed = ChatSettings {};
            }
        } else if (event.kind == ui::UiEvent::Kind::Toggle) {
            if (name == "hide_chat") {
                changed.muteAll = event.state;
            } else if (name == "toggle_emote_chat") {
                changed.muteEmotes = event.state;
            } else if (name == "toggle_tts") {
                changed.textToSpeech = event.state;
            } else if (name == "typeface_radio_mojangles" && event.state) {
                changed.smoothFont = false;
            } else if (name == "typeface_radio_notoSans" && event.state) {
                changed.smoothFont = true;
            } else if (std::optional<int> pick = colorPick(name, ChatColorPrefix); pick && event.state) {
                changed.chatColor = *pick;
            } else if (std::optional<int> pick = colorPick(name, MentionsColorPrefix); pick && event.state) {
                changed.mentionsColor = *pick;
            }
        } else if (event.kind == ui::UiEvent::Kind::Slider) {
            if (name == "chat_font_size") {
                changed.fontSize = MinChatFontSize + std::clamp(static_cast<int>(std::round(event.value)), 0, static_cast<int>(sizeRange));
            } else if (name == "chat_line_spacing") {
                changed.lineSpacing = snapLineSpacing(MinChatLineSpacing + static_cast<float>(event.value * spacingRange));
            }
        }
    }
    setChatSettings(changed);
    if (close) {
        closeChatSettings();
    }
}

}
