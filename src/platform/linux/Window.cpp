#include "platform/Window.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <stdexcept>

namespace kestrel {

namespace {

class GlfwWindow final : public Window {
public:
    GlfwWindow(const std::string& title, uint32_t w, uint32_t h)
    {
        if (!glfwInit()) {
            throw std::runtime_error("glfwInit failed");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        window = glfwCreateWindow(static_cast<int>(w), static_cast<int>(h), title.c_str(), nullptr, nullptr);
        if (!window) {
            glfwTerminate();
            throw std::runtime_error("glfwCreateWindow failed");
        }
        glfwSetWindowSizeLimits(window, 760, 520, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSetWindowUserPointer(window, this);

        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* handle, int, int) {
            self(handle)->resized = true;
        });
        glfwSetWindowContentScaleCallback(window, [](GLFWwindow* handle, float, float) {
            self(handle)->resized = true;
        });
        glfwSetCursorPosCallback(window, [](GLFWwindow* handle, double x, double y) {
            self(handle)->mouse(x, y);
        });
        glfwSetCursorEnterCallback(window, [](GLFWwindow* handle, int entered) {
            GlfwWindow* owner = self(handle);
            if (!entered && !owner->state.mouseDown) {
                owner->state.mouseX = -1.0f;
                owner->state.mouseY = -1.0f;
            }
        });
        glfwSetMouseButtonCallback(window, [](GLFWwindow* handle, int button, int action, int) {
            if (button != GLFW_MOUSE_BUTTON_LEFT) {
                return;
            }
            InputState& input = self(handle)->state;
            if (action == GLFW_PRESS) {
                input.mouseDown = true;
                input.mousePressed = true;
            } else if (action == GLFW_RELEASE) {
                input.mouseDown = false;
                input.mouseReleased = true;
            }
        });
        glfwSetScrollCallback(window, [](GLFWwindow* handle, double, double y) {
            self(handle)->state.wheel += static_cast<float>(y);
        });
        glfwSetCharCallback(window, [](GLFWwindow* handle, unsigned int codepoint) {
            if (codepoint >= 32 && codepoint != 127) {
                self(handle)->state.text.push_back(static_cast<char32_t>(codepoint));
            }
        });
        glfwSetKeyCallback(window, [](GLFWwindow* handle, int key, int, int action, int) {
            InputState& input = self(handle)->state;
            if (action != GLFW_REPEAT) {
                input.setKey(translateKey(key), action == GLFW_PRESS);
            }
            if (action == GLFW_RELEASE) {
                return;
            }
            switch (key) {
            case GLFW_KEY_BACKSPACE:
                input.backspace = true;
                break;
            case GLFW_KEY_ENTER:
            case GLFW_KEY_KP_ENTER:
                input.enter = true;
                break;
            case GLFW_KEY_ESCAPE:
                input.escape = true;
                break;
            case GLFW_KEY_TAB:
                input.tab = true;
                break;
            default:
                break;
            }
        });

        cursors[0] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        cursors[1] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
        cursors[2] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    }

    ~GlfwWindow() override
    {
        for (GLFWcursor* cursor : cursors) {
            if (cursor) {
                glfwDestroyCursor(cursor);
            }
        }
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    bool pump() override
    {
        state.beginFrame();
        glfwPollEvents();
        return !glfwWindowShouldClose(window);
    }

    void* nativeHandle() const override
    {
        return window;
    }

    uint32_t width() const override
    {
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        return static_cast<uint32_t>(w);
    }

    uint32_t height() const override
    {
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        return static_cast<uint32_t>(h);
    }

    float contentScale() const override
    {
        float x = 1.0f;
        float y = 1.0f;
        glfwGetWindowContentScale(window, &x, &y);
        return x > 0.0f ? x : 1.0f;
    }

    bool consumeResize() override
    {
        bool value = resized;
        resized = false;
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
        glfwSetCursor(window, cursors[static_cast<size_t>(value)]);
    }

    void setMouseCaptured(bool value) override
    {
        if (value == captured) {
            return;
        }
        captured = value;
        glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
        }
        hasLastCursor = false;
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
        return glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
    }

    void minimize() override
    {
        glfwIconifyWindow(window);
    }

    void toggleMaximize() override
    {
        if (maximized()) {
            glfwRestoreWindow(window);
        } else {
            glfwMaximizeWindow(window);
        }
    }

    bool fullscreen() const override
    {
        return glfwGetWindowMonitor(window) != nullptr;
    }

    void toggleFullscreen() override
    {
        if (fullscreen()) {
            glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedWidth, windowedHeight, GLFW_DONT_CARE);
            return;
        }
        glfwGetWindowPos(window, &windowedX, &windowedY);
        glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
        GLFWmonitor* monitor = currentMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode) {
            return;
        }
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }

    void close() override
    {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

private:
    // Wayland hides window positions, so our GLFW patch lets the compositor pick the output there.
    GLFWmonitor* currentMonitor() const
    {
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            return glfwGetPrimaryMonitor();
        }
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        GLFWmonitor* best = glfwGetPrimaryMonitor();
        long long bestArea = 0;
        for (int i = 0; i < count; ++i) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (!mode) {
                continue;
            }
            int x = 0;
            int y = 0;
            glfwGetMonitorPos(monitors[i], &x, &y);
            int overlapWidth = std::min(windowedX + windowedWidth, x + mode->width) - std::max(windowedX, x);
            int overlapHeight = std::min(windowedY + windowedHeight, y + mode->height) - std::max(windowedY, y);
            if (overlapWidth <= 0 || overlapHeight <= 0) {
                continue;
            }
            long long area = static_cast<long long>(overlapWidth) * overlapHeight;
            if (area > bestArea) {
                bestArea = area;
                best = monitors[i];
            }
        }
        return best;
    }

    static Key translateKey(int key)
    {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
            return letterKey(static_cast<uint32_t>(key - GLFW_KEY_A));
        }
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
            return digitKey(static_cast<uint32_t>(key - GLFW_KEY_0));
        }
        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
            return functionKey(static_cast<uint32_t>(key - GLFW_KEY_F1));
        }
        switch (key) {
        case GLFW_KEY_SPACE:
            return Key::Space;
        case GLFW_KEY_LEFT_SHIFT:
        case GLFW_KEY_RIGHT_SHIFT:
            return Key::Shift;
        case GLFW_KEY_LEFT_CONTROL:
        case GLFW_KEY_RIGHT_CONTROL:
            return Key::Control;
        case GLFW_KEY_LEFT_ALT:
        case GLFW_KEY_RIGHT_ALT:
            return Key::Alt;
        case GLFW_KEY_TAB:
            return Key::Tab;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            return Key::Enter;
        case GLFW_KEY_BACKSPACE:
            return Key::Backspace;
        case GLFW_KEY_ESCAPE:
            return Key::Escape;
        case GLFW_KEY_UP:
            return Key::Up;
        case GLFW_KEY_DOWN:
            return Key::Down;
        case GLFW_KEY_LEFT:
            return Key::Left;
        case GLFW_KEY_RIGHT:
            return Key::Right;
        default:
            return Key::None;
        }
    }

    static GlfwWindow* self(GLFWwindow* handle)
    {
        return static_cast<GlfwWindow*>(glfwGetWindowUserPointer(handle));
    }

    void mouse(double x, double y)
    {
        if (captured) {
            if (hasLastCursor) {
                state.mouseDeltaX += static_cast<float>(x - lastCursorX);
                state.mouseDeltaY += static_cast<float>(y - lastCursorY);
            }
            lastCursorX = x;
            lastCursorY = y;
            hasLastCursor = true;
            return;
        }
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        float ratioX = windowWidth > 0 ? static_cast<float>(width()) / windowWidth : 1.0f;
        float ratioY = windowHeight > 0 ? static_cast<float>(height()) / windowHeight : 1.0f;
        state.mouseX = static_cast<float>(x) * ratioX;
        state.mouseY = static_cast<float>(y) * ratioY;
    }

    GLFWwindow* window = nullptr;
    GLFWcursor* cursors[3] {};
    Cursor cursor = Cursor::Arrow;
    bool resized = false;
    bool captured = false;
    bool hasLastCursor = false;
    double lastCursorX = 0.0;
    double lastCursorY = 0.0;
    int windowedX = 0;
    int windowedY = 0;
    int windowedWidth = 0;
    int windowedHeight = 0;
    InputState state;
};

}

std::unique_ptr<Window> Window::create(const std::string& title, uint32_t width, uint32_t height)
{
    return std::make_unique<GlfwWindow>(title, width, height);
}

}
