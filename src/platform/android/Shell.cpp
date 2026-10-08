#include "platform/Shell.h"
#include "../mobile/Resources.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace kestrel::platform {

void openUrl(const std::string& url)
{
    SDL_OpenURL(url.c_str());
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

namespace {

/**
 * The document picker hands back a content:// address, which SDL reads through Android's content resolver.
 */
void SDLCALL picked(void* userdata, const char* const* files, int)
{
    if (!files || !files[0]) {
        return;
    }
    FileKind kind = static_cast<FileKind>(reinterpret_cast<intptr_t>(userdata));
    size_t size = 0;
    void* data = SDL_LoadFile(files[0], &size);
    if (!data) {
        return;
    }
    std::filesystem::path destination = importedFile(kind);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    SDL_free(data);
    output.close();
    if (output) {
        deliverPickedFile(kind, destination.string());
    }
}

}

void showFilePicker(FileKind kind)
{
    static const SDL_DialogFileFilter Images[] { { "PNG images", "png" } };
    static const SDL_DialogFileFilter Packs[] { { "Minecraft resource packs", "mcpack;zip" } };
    const SDL_DialogFileFilter* filters = kind == FileKind::Png ? Images : Packs;
    SDL_ShowOpenFileDialog(picked, reinterpret_cast<void*>(static_cast<intptr_t>(kind)), nullptr, filters, 1, nullptr, false);
}

}
