#include "platform/Window.h"

#include "AppIconPng.h"
#include "ui/Image.h"
#include "ui/Utf8.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace kestrel {

namespace {

class SdlWindow final : public Window {
public:
    SdlWindow(const std::string& title, uint32_t w, uint32_t h, bool shown)
        : shown(shown)
    {
        // a fullscreen window should stay up when another app takes focus
        SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        // X11 sizes windows in pixels, so scale the requested size the way Wayland already does
        if (shown && std::strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
            float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
            if (scale > 0.0f) {
                w = static_cast<uint32_t>(w * scale);
                h = static_cast<uint32_t>(h * scale);
            }
        }
        SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
        // a hidden window is drawn offscreen at exactly the size asked for, so agents get the pixels they expect
        flags |= shown ? SDL_WINDOW_HIGH_PIXEL_DENSITY : SDL_WINDOW_HIDDEN;
        window = SDL_CreateWindow(title.c_str(), static_cast<int>(w), static_cast<int>(h), flags);
        if (!window) {
            std::string error = SDL_GetError();
            SDL_Quit();
            throw std::runtime_error("SDL_CreateWindow failed: " + error);
        }
        SDL_SetWindowMinimumSize(window, 760, 520);
        setIcon();
        SDL_StartTextInput(window);

        cursors[0] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        cursors[1] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
        cursors[2] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    }

    ~SdlWindow() override
    {
        for (SDL_Cursor* cursor : cursors) {
            if (cursor) {
                SDL_DestroyCursor(cursor);
            }
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    bool pump() override
    {
        state.beginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handle(event);
        }
        return !shouldClose;
    }

    void* nativeHandle() const override
    {
        return window;
    }

    uint32_t width() const override
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        return static_cast<uint32_t>(w);
    }

    uint32_t height() const override
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        return static_cast<uint32_t>(h);
    }

    float contentScale() const override
    {
        float scale = SDL_GetWindowDisplayScale(window);
        return scale > 0.0f ? scale : 1.0f;
    }

    bool consumeResize() override
    {
        bool value = resized;
        resized = false;
        return value;
    }

    bool consumeFocusLost() override
    {
        bool value = focusLost;
        focusLost = false;
        return value;
    }

    InputState& input() override
    {
        return state;
    }

    void setChrome(WindowChrome) override
    {
    }

    void setCursor(Cursor value) override
    {
        if (value == cursor) {
            return;
        }
        cursor = value;
        SDL_SetCursor(cursors[static_cast<size_t>(value)]);
    }

    void setMouseCaptured(bool value) override
    {
        if (value == captured) {
            return;
        }
        captured = value;
        SDL_SetWindowRelativeMouseMode(window, captured);
        // An input method left on in game eats held letters, KDE's press and hold accent picker swallows A, S and D.
        if (captured) {
            SDL_StopTextInput(window);
        } else {
            SDL_StartTextInput(window);
        }
    }

    bool drawsCaptionButtons() const override
    {
        return false;
    }

    float captionInsetLeft() const override
    {
        return 0.0f;
    }

    bool maximized() const override
    {
        return (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
    }

    void minimize() override
    {
        SDL_MinimizeWindow(window);
    }

    void toggleMaximize() override
    {
        if (maximized()) {
            SDL_RestoreWindow(window);
        } else {
            SDL_MaximizeWindow(window);
        }
    }

    bool fullscreen() const override
    {
        return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    }

    // Desktop fullscreen, so on Wayland the compositor keeps the window on the output it's already on.
    void toggleFullscreen() override
    {
        SDL_SetWindowFullscreenMode(window, nullptr);
        SDL_SetWindowFullscreen(window, !fullscreen());
    }

    void close() override
    {
        shouldClose = true;
    }

    bool visible() const override
    {
        return shown;
    }

private:
    void handle(const SDL_Event& event)
    {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            shouldClose = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            resized = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            focusLost = true;
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (!state.mouseDown) {
                state.mouseX = -1.0f;
                state.mouseY = -1.0f;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            state.recordReceipt();
            mouse(event.motion);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            state.recordReceipt();
            mouseButton(event.button.button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            state.recordReceipt();
            state.wheel += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            break;
        case SDL_EVENT_TEXT_INPUT:
            state.recordReceipt();
            text(event.text.text);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            key(event.key);
            break;
        default:
            break;
        }
    }

    void mouse(const SDL_MouseMotionEvent& motion)
    {
        if (captured) {
            state.mouseDeltaX += motion.xrel;
            state.mouseDeltaY += motion.yrel;
            return;
        }
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        float ratioX = windowWidth > 0 ? static_cast<float>(width()) / windowWidth : 1.0f;
        float ratioY = windowHeight > 0 ? static_cast<float>(height()) / windowHeight : 1.0f;
        state.mouseX = motion.x * ratioX;
        state.mouseY = motion.y * ratioY;
    }

    void mouseButton(uint8_t button, bool down)
    {
        if (button == SDL_BUTTON_RIGHT) {
            state.rightMouseDown = down;
            if (down) state.recordMousePress(true);
            state.rightMouseReleased |= !down;
            return;
        }
        if (button == SDL_BUTTON_MIDDLE) {
            state.middleMousePressed |= down;
            state.middleMouseReleased |= !down;
            return;
        }
        if (button != SDL_BUTTON_LEFT) {
            return;
        }
        if (down) {
            state.mouseDown = true;
            state.recordMousePress(false);
        } else {
            state.mouseDown = false;
            state.mouseReleased = true;
        }
    }

    void text(std::string_view utf8)
    {
        size_t i = 0;
        while (i < utf8.size()) {
            char32_t codepoint = ui::nextCodepoint(utf8, i);
            if (codepoint >= 32 && codepoint != 127) {
                state.text.push_back(codepoint);
            }
        }
    }

    void key(const SDL_KeyboardEvent& event)
    {
        if (!event.repeat) {
            state.setKey(translateKey(event.scancode), event.down);
        }
        if (!event.down) {
            return;
        }
        switch (event.scancode) {
        case SDL_SCANCODE_BACKSPACE:
            state.backspace = true;
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            state.enter = true;
            break;
        case SDL_SCANCODE_ESCAPE:
            state.escape = true;
            break;
        case SDL_SCANCODE_TAB:
            state.tab = true;
            break;
        default:
            break;
        }
    }

    // Scancodes, not keycodes, so WASD stays where it is on any keyboard layout.
    static Key translateKey(SDL_Scancode key)
    {
        if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z) {
            return letterKey(static_cast<uint32_t>(key - SDL_SCANCODE_A));
        }
        // SDL orders the digit row 1 to 9 and then 0
        if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9) {
            return digitKey(static_cast<uint32_t>(key - SDL_SCANCODE_1 + 1));
        }
        if (key == SDL_SCANCODE_0) {
            return digitKey(0);
        }
        if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12) {
            return functionKey(static_cast<uint32_t>(key - SDL_SCANCODE_F1));
        }
        switch (key) {
        case SDL_SCANCODE_SPACE:
            return Key::Space;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
            return Key::Shift;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
            return Key::Control;
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:
            return Key::Alt;
        case SDL_SCANCODE_TAB:
            return Key::Tab;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            return Key::Enter;
        case SDL_SCANCODE_BACKSPACE:
            return Key::Backspace;
        case SDL_SCANCODE_ESCAPE:
            return Key::Escape;
        case SDL_SCANCODE_UP:
            return Key::Up;
        case SDL_SCANCODE_DOWN:
            return Key::Down;
        case SDL_SCANCODE_LEFT:
            return Key::Left;
        case SDL_SCANCODE_RIGHT:
            return Key::Right;
        default:
            return Key::None;
        }
    }

    // The largest size is the icon, the smaller ones ride along as alternates for taskbars and title bars.
    void setIcon()
    {
        std::string encoded(reinterpret_cast<const char*>(KestrelAppIconData::kAppIconPng), KestrelAppIconData::kAppIconPngSize);
        constexpr std::array<int, 4> Sizes { 256, 64, 32, 16 };
        std::array<std::vector<uint8_t>, Sizes.size()> pixels;
        std::array<SDL_Surface*, Sizes.size()> surfaces {};
        bool ok = true;
        for (size_t i = 0; i < Sizes.size() && ok; ++i) {
            ok = ui::decodeSquareImage(encoded, static_cast<uint32_t>(Sizes[i]), pixels[i]);
            if (ok) {
                surfaces[i] = SDL_CreateSurfaceFrom(Sizes[i], Sizes[i], SDL_PIXELFORMAT_RGBA32, pixels[i].data(), Sizes[i] * 4);
                ok = surfaces[i] != nullptr;
            }
        }
        if (ok) {
            for (size_t i = 1; i < Sizes.size(); ++i) {
                SDL_AddSurfaceAlternateImage(surfaces[0], surfaces[i]);
            }
            SDL_SetWindowIcon(window, surfaces[0]);
        }
        for (SDL_Surface* surface : surfaces) {
            SDL_DestroySurface(surface);
        }
    }

    bool shown = true;
    SDL_Window* window = nullptr;
    SDL_Cursor* cursors[3] {};
    Cursor cursor = Cursor::Arrow;
    bool resized = false;
    bool focusLost = false;
    bool captured = false;
    bool shouldClose = false;
    InputState state;
};

}

std::unique_ptr<Window> Window::create(const std::string& title, uint32_t width, uint32_t height, bool visible)
{
    return std::make_unique<SdlWindow>(title, width, height, visible);
}

}
