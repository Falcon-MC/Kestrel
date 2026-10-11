#include "platform/Window.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <optional>
#include <stdexcept>

namespace kestrel {

namespace {

constexpr DWORD DarkModeAttribute = 20;
constexpr DWORD CaptionColorAttribute = 35;
constexpr DWORD CaptionTextColorAttribute = 36;

class Win32Window final : public Window {
public:
    Win32Window(const std::string& title, uint32_t w, uint32_t h, bool shown)
        : shown(shown)
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Win32Window::proc;
        wc.hInstance = instance;
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(15, 13, 19));
        wc.lpszClassName = L"KestrelWindow";
        RegisterClassExW(&wc);

        dpi = GetDpiForSystem();
        int windowWidth = MulDiv(static_cast<int>(w), dpi, 96);
        int windowHeight = MulDiv(static_cast<int>(h), dpi, 96);
        int x = (GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2;

        std::wstring wideTitle(title.begin(), title.end());
        hwnd = CreateWindowExW(
            0,
            wc.lpszClassName,
            wideTitle.c_str(),
            WS_OVERLAPPEDWINDOW,
            x,
            y,
            windowWidth,
            windowHeight,
            nullptr,
            nullptr,
            instance,
            this);
        if (!hwnd) {
            throw std::runtime_error("CreateWindowExW failed");
        }

        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DarkModeAttribute, &dark, sizeof(dark));
        COLORREF caption = RGB(0, 0, 0);
        DwmSetWindowAttribute(hwnd, CaptionColorAttribute, &caption, sizeof(caption));
        COLORREF captionText = RGB(255, 255, 255);
        DwmSetWindowAttribute(hwnd, CaptionTextColorAttribute, &captionText, sizeof(captionText));
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        dpi = GetDpiForWindow(hwnd);
        RECT client {};
        GetClientRect(hwnd, &client);
        clientWidth = static_cast<uint32_t>(client.right - client.left);
        clientHeight = static_cast<uint32_t>(client.bottom - client.top);

        RAWINPUTDEVICE mouseDevice { 0x01, 0x02, 0, hwnd };
        RegisterRawInputDevices(&mouseDevice, 1, sizeof(mouseDevice));

        if (shown) {
            ShowWindow(hwnd, SW_SHOW);
        }
    }

    bool visible() const override
    {
        return shown;
    }

    ~Win32Window() override
    {
        if (hwnd) {
            DestroyWindow(hwnd);
        }
    }

    bool pump() override
    {
        state.beginFrame();
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return open;
    }

    void* nativeHandle() const override
    {
        return hwnd;
    }

    uint32_t width() const override
    {
        return clientWidth;
    }

    uint32_t height() const override
    {
        return clientHeight;
    }

    float contentScale() const override
    {
        return static_cast<float>(dpi) / 96.0f;
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

    void setChrome(WindowChrome value) override
    {
        chrome = std::move(value);
    }

    void setCursor(Cursor value) override
    {
        if (value == cursor) {
            return;
        }
        cursor = value;
        POINT point {};
        GetCursorPos(&point);
        if (WindowFromPoint(point) == hwnd && SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == HTCLIENT) {
            applyCursor();
        }
    }

    void setMouseCaptured(bool value) override
    {
        if (value == captured) {
            return;
        }
        captured = value;
        if (captured) {
            clipCursor();
            applyCursor();
        } else {
            ClipCursor(nullptr);
            applyCursor();
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
        return IsZoomed(hwnd) != FALSE;
    }

    void minimize() override
    {
        ShowWindow(hwnd, SW_MINIMIZE);
    }

    void toggleMaximize() override
    {
        ShowWindow(hwnd, maximized() ? SW_RESTORE : SW_MAXIMIZE);
    }

    bool fullscreen() const override
    {
        return isFullscreen;
    }

    void toggleFullscreen() override
    {
        if (isFullscreen) {
            SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowPlacement(hwnd, &windowedPlacement);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
            isFullscreen = false;
        } else {
            MONITORINFO monitor { sizeof(monitor) };
            windowedPlacement.length = sizeof(windowedPlacement);
            if (!GetWindowPlacement(hwnd, &windowedPlacement) || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                return;
            }
            SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            const RECT& area = monitor.rcMonitor;
            SetWindowPos(hwnd, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            isFullscreen = true;
        }
        if (captured) {
            clipCursor();
        }
    }

    void close() override
    {
        open = false;
    }

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            if (std::optional<LRESULT> result = self->handle(msg, wParam, lParam)) {
                return *result;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void clipCursor() const
    {
        RECT rect {};
        GetClientRect(hwnd, &rect);
        POINT topLeft { rect.left, rect.top };
        POINT bottomRight { rect.right, rect.bottom };
        ClientToScreen(hwnd, &topLeft);
        ClientToScreen(hwnd, &bottomRight);
        RECT screen { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
        ClipCursor(&screen);
    }

    static Key translateKey(WPARAM key)
    {
        if (key >= 'A' && key <= 'Z') {
            return letterKey(static_cast<uint32_t>(key - 'A'));
        }
        if (key >= '0' && key <= '9') {
            return digitKey(static_cast<uint32_t>(key - '0'));
        }
        if (key >= VK_F1 && key <= VK_F12) {
            return functionKey(static_cast<uint32_t>(key - VK_F1));
        }
        switch (key) {
        case VK_SPACE:
            return Key::Space;
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return Key::Shift;
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return Key::Control;
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return Key::Alt;
        case VK_TAB:
            return Key::Tab;
        case VK_RETURN:
            return Key::Enter;
        case VK_BACK:
            return Key::Backspace;
        case VK_ESCAPE:
            return Key::Escape;
        case VK_UP:
            return Key::Up;
        case VK_DOWN:
            return Key::Down;
        case VK_LEFT:
            return Key::Left;
        case VK_RIGHT:
            return Key::Right;
        default:
            return Key::None;
        }
    }

    void setKey(WPARAM key, bool down)
    {
        state.setKey(translateKey(key), down);
    }

    void rawMouse(LPARAM lParam)
    {
        RAWINPUT input {};
        UINT size = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) {
            return;
        }
        if (input.header.dwType == RIM_TYPEMOUSE && !(input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            state.recordReceipt();
            state.mouseDeltaX += static_cast<float>(input.data.mouse.lLastX);
            state.mouseDeltaY += static_cast<float>(input.data.mouse.lLastY);
        }
    }

    void applyCursor() const
    {
        if (captured) {
            SetCursor(nullptr);
            return;
        }
        LPCWSTR shape = IDC_ARROW;
        if (cursor == Cursor::Hand) {
            shape = IDC_HAND;
        } else if (cursor == Cursor::Text) {
            shape = IDC_IBEAM;
        }
        SetCursor(LoadCursorW(nullptr, shape));
    }

    std::optional<LRESULT> handle(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_CHAR:
            state.recordReceipt();
            break;
        default:
            break;
        }
        switch (msg) {
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                applyCursor();
                return TRUE;
            }
            return std::nullopt;
        case WM_SIZE: {
            uint32_t w = LOWORD(lParam);
            uint32_t h = HIWORD(lParam);
            if (w > 0 && h > 0 && (w != clientWidth || h != clientHeight)) {
                clientWidth = w;
                clientHeight = h;
                resized = true;
            }
            return 0;
        }
        case WM_DPICHANGED: {
            dpi = HIWORD(wParam);
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            resized = true;
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = MulDiv(760, dpi ? dpi : 96, 96);
            info->ptMinTrackSize.y = MulDiv(520, dpi ? dpi : 96, 96);
            return 0;
        }
        case WM_MOUSEMOVE:
            state.mouseX = static_cast<float>(GET_X_LPARAM(lParam));
            state.mouseY = static_cast<float>(GET_Y_LPARAM(lParam));
            if (!tracking) {
                TRACKMOUSEEVENT track { sizeof(track), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&track);
                tracking = true;
            }
            return 0;
        case WM_MOUSELEAVE:
        case WM_NCMOUSEMOVE:
            tracking = msg == WM_MOUSELEAVE ? false : tracking;
            if (!state.mouseDown) {
                state.mouseX = -1.0f;
                state.mouseY = -1.0f;
            }
            return std::nullopt;
        case WM_LBUTTONDOWN:
            state.mouseX = static_cast<float>(GET_X_LPARAM(lParam));
            state.mouseY = static_cast<float>(GET_Y_LPARAM(lParam));
            state.mouseDown = true;
            state.recordMousePress(false);
            SetCapture(hwnd);
            return 0;
        case WM_RBUTTONDOWN:
            state.rightMouseDown = true;
            state.recordMousePress(true);
            return 0;
        case WM_RBUTTONUP:
            state.rightMouseDown = false;
            state.rightMouseReleased = true;
            return 0;
        case WM_MBUTTONDOWN:
            state.middleMousePressed = true;
            return 0;
        case WM_MBUTTONUP:
            state.middleMouseReleased = true;
            return 0;
        case WM_LBUTTONUP:
            state.mouseX = static_cast<float>(GET_X_LPARAM(lParam));
            state.mouseY = static_cast<float>(GET_Y_LPARAM(lParam));
            state.mouseDown = false;
            state.mouseReleased = true;
            ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL:
            state.wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
            return 0;
        case WM_KILLFOCUS:
            focusLost = true;
            if (state.mouseDown) {
                state.mouseDown = false;
                state.mouseReleased = true;
            }
            state.releaseKeys();
            if (captured) {
                ClipCursor(nullptr);
            }
            return std::nullopt;
        case WM_SETFOCUS:
            if (captured) {
                clipCursor();
            }
            return std::nullopt;
        case WM_INPUT:
            if (captured) {
                rawMouse(lParam);
            }
            return std::nullopt;
        case WM_KEYUP:
            setKey(wParam, false);
            return 0;
        case WM_SYSKEYDOWN:
            setKey(wParam, true);
            return std::nullopt;
        case WM_SYSKEYUP:
            setKey(wParam, false);
            return std::nullopt;
        case WM_KEYDOWN:
            setKey(wParam, true);
            switch (wParam) {
            case VK_BACK:
                state.backspace = true;
                break;
            case VK_RETURN:
                state.enter = true;
                break;
            case VK_ESCAPE:
                state.escape = true;
                break;
            case VK_TAB:
                state.tab = true;
                break;
            default:
                break;
            }
            return 0;
        case WM_CHAR:
            character(static_cast<wchar_t>(wParam));
            return 0;
        case WM_CLOSE:
            open = false;
            return 0;
        default:
            return std::nullopt;
        }
    }

    void character(wchar_t c)
    {
        if (c >= 0xD800 && c < 0xDC00) {
            highSurrogate = c;
            return;
        }
        char32_t cp = c;
        if (c >= 0xDC00 && c < 0xE000) {
            if (!highSurrogate) {
                return;
            }
            cp = 0x10000 + ((static_cast<char32_t>(highSurrogate) - 0xD800) << 10) + (c - 0xDC00);
            highSurrogate = 0;
        }
        if (cp >= 32 && cp != 127) {
            state.text.push_back(cp);
        }
    }

    bool shown = true;
    HWND hwnd = nullptr;
    UINT dpi = 96;
    uint32_t clientWidth = 0;
    uint32_t clientHeight = 0;
    bool resized = false;
    bool focusLost = false;
    bool open = true;
    bool tracking = false;
    bool captured = false;
    bool isFullscreen = false;
    WINDOWPLACEMENT windowedPlacement {};
    wchar_t highSurrogate = 0;
    InputState state;
    WindowChrome chrome;
    Cursor cursor = Cursor::Arrow;
};

}

std::unique_ptr<Window> Window::create(const std::string& title, uint32_t width, uint32_t height, bool visible)
{
    return std::make_unique<Win32Window>(title, width, height, visible);
}

}
