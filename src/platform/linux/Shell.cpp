#include "platform/Shell.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

namespace kestrel::platform {

void openUrl(const std::string& url)
{
    pid_t child = fork();
    if (child == 0) {
        pid_t grandchild = fork();
        if (grandchild == 0) {
            execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
        }
        _exit(0);
    }
    if (child > 0) {
        waitpid(child, nullptr, 0);
    }
}

bool copyText(const std::string& text)
{
    return SDL_SetClipboardText(text.c_str());
}

std::string pasteText()
{
    char* text = SDL_GetClipboardText();
    std::string result = text ? std::string(text) : std::string();
    SDL_free(text);
    return result;
}

std::string pickPngFile()
{
    FILE* dialog = popen("zenity --file-selection --file-filter='*.png' 2>/dev/null", "r");
    if (!dialog) {
        return {};
    }
    char buffer[4096] = {};
    std::string chosen = fgets(buffer, sizeof(buffer), dialog) ? std::string(buffer) : std::string();
    pclose(dialog);
    while (!chosen.empty() && (chosen.back() == '\n' || chosen.back() == '\r')) {
        chosen.pop_back();
    }
    return chosen;
}

std::string pickResourcePackFile()
{
    FILE* dialog = popen("zenity --file-selection --file-filter='Minecraft resource packs | *.mcpack *.zip' 2>/dev/null", "r");
    if (!dialog) return {};
    char buffer[4096] {};
    std::string chosen = fgets(buffer, sizeof(buffer), dialog) ? std::string(buffer) : std::string();
    pclose(dialog);
    while (!chosen.empty() && (chosen.back() == '\n' || chosen.back() == '\r')) chosen.pop_back();
    return chosen;
}

namespace {

std::string runZenity(const std::string& command)
{
    FILE* dialog = popen(command.c_str(), "r");
    if (!dialog) {
        return {};
    }
    char buffer[4096] {};
    std::string chosen = fgets(buffer, sizeof(buffer), dialog) ? std::string(buffer) : std::string();
    pclose(dialog);
    while (!chosen.empty() && (chosen.back() == '\n' || chosen.back() == '\r')) {
        chosen.pop_back();
    }
    return chosen;
}

/**
 * The text in single quotes for the shell, so a world name cannot run
 * commands of its own.
 */
std::string shellQuoted(const std::string& text)
{
    std::string quoted = "'";
    for (char c : text) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    return quoted + "'";
}

}

std::string pickWorldFile()
{
    return runZenity("zenity --file-selection --file-filter='Minecraft worlds | *.mcworld *.zip' 2>/dev/null");
}

std::string pickWorldSaveFile(const std::string& suggestedName)
{
    return runZenity("zenity --file-selection --save --confirm-overwrite --filename=" + shellQuoted(suggestedName) + " 2>/dev/null");
}

}
