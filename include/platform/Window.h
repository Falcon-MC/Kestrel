#pragma once

#include "platform/Input.h"
#include "ui/Types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kestrel {

enum class Cursor {
    Arrow,
    Hand,
    Text,
};

struct WindowChrome {
    float captionHeight = 0.0f;
    std::vector<ui::Rect> interactive;
};

class Window {
public:
    virtual ~Window() = default;

    virtual bool pump() = 0;
    virtual void* nativeHandle() const = 0;
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
    virtual float contentScale() const = 0;
    virtual ui::Rect safeArea() const { return { 0, 0, float(width()), float(height()) }; }
    virtual bool consumeResize() = 0;

    /**
     * True once after the window lost keyboard focus to another window.
     */
    virtual bool consumeFocusLost() = 0;
    virtual InputState& input() = 0;

    virtual void setChrome(WindowChrome chrome) = 0;
    virtual void setCursor(Cursor cursor) = 0;
    virtual void setMouseCaptured(bool captured) = 0;
    virtual void setTextInput(bool enabled) { }
    virtual bool drawsCaptionButtons() const = 0;
    virtual float captionInsetLeft() const = 0;
    virtual bool maximized() const = 0;
    virtual void minimize() = 0;
    virtual void toggleMaximize() = 0;
    virtual bool fullscreen() const = 0;
    virtual void toggleFullscreen() = 0;
    virtual void close() = 0;

    /**
     * False for a window kept off screen, which renderers draw offscreen
     * for instead of presenting.
     */
    virtual bool visible() const = 0;

    static std::unique_ptr<Window> create(const std::string& title, uint32_t width, uint32_t height, bool visible = true);
};

}
