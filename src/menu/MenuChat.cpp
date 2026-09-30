#include "menu/Menu.h"

#include "ui/Context.h"
#include "ui/Localization.h"
#include "ui/Theme.h"
#include "ui/Utf8.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>

namespace kestrel::menu {

using namespace ui;
using namespace ui::theme;

namespace {

// Sizes and timings from the game's hud_screen.json and chat_screen.json.
constexpr size_t MaxChatLines = 100;
constexpr size_t MaxFeedLines = 50;
constexpr size_t MaxChatHistory = 100;
constexpr float FeedLifetimeSeconds = 10.0f;
constexpr float FeedFadeSeconds = 1.0f;
constexpr const char* ChatRoot = "chat.chat_screen";
// commands_panel is the screen less 50px, and each auto_complete row is 10px tall.
constexpr float ChatChromeHeight = 50.0f;
constexpr float AutoCompleteRowHeight = 10.0f;
constexpr const char* HelpAlias = "?";
constexpr const char* HelpDescription = "commands.help.description";
constexpr const char* Ellipsis = "\xE2\x80\xA6";
constexpr const char* Italic = "\xC2\xA7o";
constexpr const char* GreenSlash = "\xC2\xA7" "a/\xC2\xA7r";

bool blank(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\t'; });
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
    std::string query(draft.substr(tokenStart + 1));
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    hints.replaceFrom = tokenStart;
    for (const std::string& name : players) {
        std::string folded = name;
        std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (folded.starts_with(query)) {
            hints.suggestions.push_back({ "@" + name + " ", {} });
        }
    }
    return hints;
}

}

void Menu::addChatLine(std::string text)
{
    chatLines.push_back({ std::move(text), std::chrono::steady_clock::now(), ++chatSerial });
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
    chatRecall.reset();
    chatToBottom = true;
}

void Menu::closeChat()
{
    dialog = Dialog::None;
    field = Field::None;
    selectedField = Field::None;
    chatDraft.clear();
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
    selectedField = Field::None;
}

/**
 * Tab fills in the first completion for the word being typed, pressing it
 * again (Shift+Tab backwards) cycles through the rest.
 */
void Menu::completeChat(bool backwards)
{
    if (chatCycle.empty() || chatDraft != chatCycleDraft) {
        CommandHints hints = chatCompletions(commands, players, chatDraft);
        if (hints.suggestions.empty()) {
            chatCycle.clear();
            return;
        }
        chatCycle = std::move(hints.suggestions);
        chatCycleBase = chatDraft.substr(0, hints.replaceFrom);
        chatCycleIndex = backwards ? chatCycle.size() - 1 : 0;
    } else {
        chatCycleIndex = (chatCycleIndex + (backwards ? chatCycle.size() - 1 : 1)) % chatCycle.size();
    }
    chatDraft = chatCycleBase + chatCycle[chatCycleIndex].text;
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
    if (input.enter) {
        submitChat();
        return true;
    }
    if (input.tab) {
        completeChat(input.isHeld(Key::Shift));
        return true;
    }
    if (input.pressedKey == Key::Up || input.pressedKey == Key::Down) {
        recallChat(input.pressedKey == Key::Up ? -1 : 1);
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
 */
std::vector<HudChatLine> Menu::hudChat() const
{
    std::vector<HudChatLine> lines;
    auto now = std::chrono::steady_clock::now();
    for (auto it = chatLines.rbegin(); it != chatLines.rend() && lines.size() < MaxFeedLines; ++it) {
        if (std::chrono::duration<float>(now - it->arrived).count() >= FeedLifetimeSeconds + FeedFadeSeconds) {
            break;
        }
        lines.push_back({ it->text, it->serial });
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
    bool cycling = !chatCycle.empty() && chatDraft == chatCycleDraft;
    CommandHints hints = chatCompletions(commands, players, chatDraft);
    const std::vector<CommandSuggestion>& suggestions = cycling ? chatCycle : hints.suggestions;
    std::string base = cycling ? chatCycleBase : chatDraft.substr(0, hints.replaceFrom);

    std::vector<ChatRow> all;
    for (const CommandSuggestion& suggestion : suggestions) {
        std::string text = suggestion.text;
        if (!suggestion.description.empty()) {
            text += std::string(" - ") + Italic + commandDescription(suggestion.description);
        }
        all.push_back({ std::move(text), base + suggestion.text });
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
    }

    using ui::UiValue;
    size_t capacity = static_cast<size_t>(std::max(0.0f, std::floor((height - ChatChromeHeight) / AutoCompleteRowHeight)));
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
    globals["#keyboard_button_visible"] = UiValue::of(true);
    globals["#keyboard_being_used"] = UiValue::of(false);
    globals["#gamepad_helper_visible"] = UiValue::of(false);
    globals["#text_box_enabled"] = UiValue::of(true);
    globals["#send_button_visible"] = UiValue::of(!blank(chatDraft));
    globals["#send_button_accessibility_text"] = UiValue::of(tr("accessibility.chat.tts.sendChatMessage", "Send"));
    globals["#chat_coordinate_dropdown_visible"] = UiValue::of(false);
    globals["#copy_button_enabled"] = UiValue::of(false);
    globals["#coordinates_text"] = UiValue::of(std::string());
    globals["#cheats_on"] = UiValue::of(false);
    for (const char* panel : { "#host_main_visible", "#host_teleport_main_visible", "#host_teleport_players_visible", "#host_time_visible", "#host_weather_visible" }) {
        globals[panel] = UiValue::of(false);
    }

    std::string shown = chatDraft.starts_with('/') ? GreenSlash + chatDraft.substr(1) : chatDraft;
    bool caretOn = std::fmod(std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt).count(), 1.0f) < 0.5f;
    if (open && caretOn) {
        shown += "_";
    }
    globals["#message_text_box_content"] = UiValue::of(std::move(shown));

    std::vector<ui::UiRow>& messages = data.collections["messages_factory"];
    std::vector<ui::UiFactoryItem>& made = data.factories["messages_factory"];
    for (const ChatLine& line : chatLines) {
        messages.push_back({ { "#text", UiValue::of(line.text) } });
        made.push_back({ "chat_screen_messages", {
            { "$chat_font_scale_factor", UiValue::of(1.0) },
            { "$chat_line_spacing", UiValue::of(0.0) },
            { "$chat_font_type", UiValue::of(std::string("default")) },
        }, line.serial });
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
    if (!open) {
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

    for (const ui::UiEvent& event : events) {
        if (event.kind != ui::UiEvent::Kind::Button) {
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
                chatCycle.clear();
                chatRecall.reset();
                selectedField = Field::None;
            }
        } else if (event.name == "button.keyboard_toggle") {
            chatDraft = chatDraft == "/" ? std::string() : chatDraft.starts_with('/') ? chatDraft : "/" + chatDraft;
            chatCycle.clear();
            chatRecall.reset();
            selectedField = Field::None;
        }
        if (dialog != Dialog::Chat) {
            break;
        }
    }
}

}
