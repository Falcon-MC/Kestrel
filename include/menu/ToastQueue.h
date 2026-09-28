#pragma once

#include <chrono>
#include <deque>
#include <optional>
#include <string>

namespace kestrel::ui {
class Context;
}

namespace kestrel::menu {

/**
 * The toasts a server asks for, shown one after another at the top of the
 * screen the way toast_screen.json draws its popup control: sliding down,
 * staying for the default toast notification duration, then sliding back up.
 */
class ToastQueue {
public:
    void push(std::string title, std::string content);
    void clear();
    void draw(ui::Context& ui, float width, float height);

private:
    struct Toast {
        std::string title;
        std::string content;
    };

    std::deque<Toast> queue;
    std::optional<std::chrono::steady_clock::time_point> shownAt;
};

}
