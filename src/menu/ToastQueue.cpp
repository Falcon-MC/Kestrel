#include "menu/ToastQueue.h"

#include "ui/Context.h"

#include <algorithm>
#include <cmath>

namespace kestrel::menu {

namespace {

// Set by the game's ToastMessage for server toasts rather than in toast_screen.json: a
// "100% - 50px" by 32 popup sliding down 32 units in 0.5 seconds and back up in 0.4, held
// for the 3 second default of the toast notification duration setting.
constexpr float SideMargin = 50.0f;
constexpr float PopupHeight = 32.0f;
constexpr float SlideInSeconds = 0.5f;
constexpr float SlideOutSeconds = 0.4f;
constexpr float DisplaySeconds = 3.0f;

// The text stack is 20 units tall, centered and one unit lower, 6 in from the left. Stack
// panels ignore the offset of their children, so the title's 5 unit offset never applies.
constexpr float TextLeft = 6.0f;
constexpr float TitleTop = 7.0f;
constexpr float SubtitleTop = 17.0f;
constexpr ui::Color TitleColor { 255, 255, 255, 255 };
constexpr ui::Color SubtitleColor { 198, 198, 198, 255 };

}

void ToastQueue::push(std::string title, std::string content)
{
    queue.push_back({ std::move(title), std::move(content) });
}

void ToastQueue::clear()
{
    queue.clear();
    shownAt.reset();
}

void ToastQueue::draw(ui::Context& ui, float width, float height)
{
    if (queue.empty()) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    if (!shownAt) {
        shownAt = now;
    }
    float age = std::chrono::duration<float>(now - *shownAt).count();
    if (age >= DisplaySeconds + SlideOutSeconds) {
        queue.pop_front();
        shownAt.reset();
        if (queue.empty()) {
            return;
        }
        shownAt = now;
        age = 0.0f;
    }

    float drop = age < SlideInSeconds ? PopupHeight * age / SlideInSeconds
        : age < DisplaySeconds       ? PopupHeight
                                     : PopupHeight * (1.0f - std::min((age - DisplaySeconds) / SlideOutSeconds, 1.0f));
    ui::Rect frame { SideMargin * 0.5f, std::round(drop) - PopupHeight, std::max(width - SideMargin, 0.0f), PopupHeight };
    if (frame.w <= 0.0f || height <= 0.0f) {
        return;
    }
    const Toast& toast = queue.front();
    ui.nineSlice(frame, "ui/greyBorder");
    ui.text(toast.title, ui::TextStyle::Pixel, frame.x + TextLeft, frame.y + TitleTop, TitleColor);
    if (!toast.content.empty()) {
        ui.text(toast.content, ui::TextStyle::Pixel, frame.x + TextLeft, frame.y + SubtitleTop, SubtitleColor);
    }
}

}
