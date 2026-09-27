#include "platform/Shell.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstring>

namespace kestrel::platform {

namespace {

std::wstring widen(const std::string& text)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

}

void openUrl(const std::string& url)
{
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool copyText(const std::string& text)
{
    std::wstring wide = widen(text);
    if (!OpenClipboard(nullptr)) {
        return false;
    }
    EmptyClipboard();

    size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }
    std::memcpy(GlobalLock(memory), wide.c_str(), bytes);
    GlobalUnlock(memory);

    bool copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    if (!copied) {
        GlobalFree(memory);
    }
    CloseClipboard();
    return copied;
}

std::string pasteText()
{
    if (!OpenClipboard(nullptr)) {
        return {};
    }
    std::string text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(data))) {
            int length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
            if (length > 1) {
                text.resize(static_cast<size_t>(length - 1));
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, text.data(), length, nullptr, nullptr);
            }
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

}
