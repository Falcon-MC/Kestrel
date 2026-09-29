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
constexpr float ChatTopBarHeight = 23.0f;
constexpr float ChatBottomBarHeight = 27.0f;
constexpr float SendButtonWidth = 44.0f;
constexpr float HintRowHeight = 10.0f;
constexpr Color ChatBackground { 0, 0, 0, 178 };
constexpr Color ChatTitleInk { 76, 76, 76, 255 };
constexpr Color SelectionFill { 0x3c, 0x8a, 0xd6, 255 };
constexpr Color HintBorder { 179, 179, 179, 255 };

Interaction lightButton(Context& ui, std::string_view id, const Rect& rect, bool enabled)
{
    Interaction state = enabled ? ui.interact(id, rect) : Interaction {};
    ui.fill(rect, { 19, 19, 19, 255 });
    const char* face = !enabled ? "ui/button_borderless_dark"
        : state.pressed ? "ui/button_borderless_lightpressed"
        : state.hovered ? "ui/button_borderless_lighthover"
                        : "ui/button_borderless_light";
    ui.nineSlice(rect.inset(1.0f), face);
    return state;
}

bool blank(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\t'; });
}

// The end of text that fits in width, so the box follows the caret like the game's edit box does.
std::string_view visibleTail(Context& ui, std::string_view text, float width)
{
    size_t start = 0;
    while (start < text.size() && ui.measure(text.substr(start), TextStyle::Pixel) > width) {
        nextCodepoint(text, start);
    }
    return text.substr(start);
}

CommandHints chatCompletions(const std::shared_ptr<const std::vector<ChatCommand>>& commands,
    const std::vector<std::string>& players, std::string_view draft)
{
    if (!draft.empty() && draft.front() == '/') {
        return commands ? commandHints(*commands, players, draft) : CommandHints {};
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
    chatScroll = 0.0f;
}

void Menu::openChat(std::string draft)
{
    dialog = Dialog::Chat;
    field = Field::Chat;
    selectedField = Field::None;
    chatDraft = std::move(draft);
    chatRecall.reset();
    chatScroll = 0.0f;
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
 * game does after Enter.
 */
void Menu::submitChat()
{
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
    chatScroll = 0.0f;
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
 * The list the game puts right above the box while a command is typed, drawn
 * straight on the chat cover: the completions for the word being typed and,
 * nearest the box, the syntax hint of every overload that still fits.
 * Clicking a completion takes it.
 */
void Menu::commandPanel(Context& ui, float width, float bottom, float top)
{
    if (chatDraft.empty()) {
        return;
    }
    bool cycling = !chatCycle.empty() && chatDraft == chatCycleDraft;
    CommandHints hints = chatCompletions(commands, players, chatDraft);
    const std::vector<CommandSuggestion>& suggestions = cycling ? chatCycle : hints.suggestions;
    std::string base = cycling ? chatCycleBase : chatDraft.substr(0, hints.replaceFrom);
    bool commandNames = chatDraft.front() == '/' && base == "/";

    size_t capacity = static_cast<size_t>(std::max(0.0f, std::floor((bottom - top) / HintRowHeight)));
    size_t usageRows = std::min(hints.usage.size(), capacity);
    size_t room = capacity - usageRows;
    size_t first = cycling && chatCycleIndex >= room ? chatCycleIndex + 1 - room : 0;
    size_t shown = std::min(room, suggestions.size() - std::min(first, suggestions.size()));
    size_t rows = shown + usageRows;
    if (rows == 0) {
        return;
    }

    float y = bottom - static_cast<float>(rows) * HintRowHeight;
    std::optional<std::string> picked;
    for (size_t i = first; i < first + shown; ++i) {
        const CommandSuggestion& suggestion = suggestions[i];
        Rect row { 0.0f, y, width, HintRowHeight };
        Interaction state = ui.interact("chat:hint:" + suggestion.text, row);
        if (state.hovered || (cycling && i == chatCycleIndex)) {
            ui.outline(row, HintBorder);
        }
        std::string label = commandNames ? "/" + suggestion.text : suggestion.text;
        if (!suggestion.description.empty()) {
            label += "\xC2\xA7" "7 - " + suggestion.description;
        }
        ui.text(label, TextStyle::Pixel, 2.0f, y + 1.0f, White, width - 4.0f);
        if (state.clicked) {
            picked = suggestion.text;
        }
        y += HintRowHeight;
    }
    for (size_t i = hints.usage.size() - usageRows; i < hints.usage.size(); ++i) {
        ui.text(hints.usage[i], TextStyle::Pixel, 2.0f, y + 1.0f, White, width - 4.0f);
        y += HintRowHeight;
    }
    if (picked) {
        chatDraft = base + *picked;
        chatCycle.clear();
        chatRecall.reset();
    }
}

/**
 * The chat screen: a dark cover, the top bar with Back and the title, every
 * kept line above the bottom bar scrolled to the newest, and the message box
 * with its send button.
 */
void Menu::chatScreen(Context& ui, float width, float height)
{
    field = Field::Chat;
    ui.fill(screenBounds, ChatBackground);

    Rect area { 2.0f, ChatTopBarHeight, width - 2.0f, height - ChatTopBarHeight - ChatBottomBarHeight };
    float textWidth = area.w - 3.0f - 5.0f;
    std::vector<float> heights;
    heights.reserve(chatLines.size());
    float content = 0.0f;
    for (const ChatLine& line : chatLines) {
        heights.push_back(ui.paragraphHeight(line.text, TextStyle::Pixel, textWidth));
        content += heights.back();
    }
    float limit = std::max(0.0f, content - area.h);
    float offset = limit - std::clamp(chatScroll, 0.0f, limit);
    scrollArea(ui, area, offset, content);
    chatScroll = limit - offset;
    ui.setClip(area);
    float y = area.bottom() - content + chatScroll;
    for (size_t i = 0; i < chatLines.size(); ++i) {
        if (y + heights[i] > area.y && y < area.bottom()) {
            ui.paragraph(chatLines[i].text, TextStyle::Pixel, area.x, y, textWidth, White);
        }
        y += heights[i];
    }
    ui.clearClip();
    commandPanel(ui, width, height - ChatBottomBarHeight, ChatTopBarHeight);

    ui.nineSlice({ 0.0f, 0.0f, width, ChatTopBarHeight }, "ui/StoreTopBar");
    std::string back = tr("controller.buttonTip.back", "Back");
    Rect backRect { 2.0f, 2.0f, 4.0f + 4.0f + 4.0f + ui.measure(back, TextStyle::Pixel) + 8.0f, 18.0f };
    Interaction backState = lightButton(ui, "chat:back", backRect, true);
    float lift = backState.pressed ? 1.0f : 0.0f;
    Color backInk = backState.hovered ? White : ButtonText;
    ui.sprite({ backRect.x + 5.0f, backRect.y + 5.0f + lift, 4.0f, 7.0f }, "ui/chevron_left", backInk);
    ui.text(back, TextStyle::Pixel, backRect.x + 13.0f, backRect.y + 5.0f + lift, backInk);
    ui.textCentered(tr("chat.title", "Chat"), TextStyle::Pixel, { 0.0f, 0.0f, width, ChatTopBarHeight - 3.0f }, ChatTitleInk);

    float barY = height - ChatBottomBarHeight;
    Rect box { 0.0f, barY, width - SendButtonWidth, ChatBottomBarHeight };
    ui.nineSlice(box, "ui/edit_box_indent");
    float room = box.w - 6.0f;
    float textY = std::round(box.y + (box.h - 8.0f) * 0.5f);
    std::string_view shown = visibleTail(ui, chatDraft, room - 2.0f);
    float shownWidth = shown.empty() ? 0.0f : ui.measure(shown, TextStyle::Pixel);
    bool selected = selectedField == Field::Chat && !chatDraft.empty();
    if (selected) {
        ui.fill({ box.x + 2.0f, textY - 1.0f, shownWidth + 2.0f, 10.0f }, SelectionFill);
    }
    ui.text(shown, TextStyle::Pixel, box.x + 3.0f, textY, White);
    bool caretOn = std::fmod(std::chrono::duration<float>(std::chrono::steady_clock::now() - startedAt).count(), 1.0f) < 0.5f;
    if (!selected && caretOn) {
        ui.fill({ box.x + 3.0f + shownWidth + (shown.empty() ? 0.0f : 1.0f), textY - 1.0f, 1.0f, 10.0f }, White);
    }

    bool canSend = !blank(chatDraft);
    Rect send { width - SendButtonWidth, barY, SendButtonWidth, ChatBottomBarHeight };
    Interaction sendState = lightButton(ui, "chat:send", send, canSend);
    ui.sprite({ std::round(send.x + (send.w - 21.0f) * 0.5f), std::round(send.y + (send.h - 18.0f) * 0.5f) + (sendState.pressed ? 1.0f : 0.0f), 21.0f, 18.0f }, "ui/chat_send");

    if (sendState.clicked) {
        submitChat();
    }
    if (backState.clicked) {
        closeChat();
    }
}

}
