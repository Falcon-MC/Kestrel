#include "platform/Shell.h"

#include <array>
#include <mutex>
#include <utility>

namespace kestrel::platform {

namespace {

std::mutex pickedMutex;
std::array<std::optional<std::string>, 2> picked;

}

std::optional<std::string> takePickedFile(FileKind kind)
{
    std::lock_guard lock(pickedMutex);
    return std::exchange(picked[static_cast<size_t>(kind)], std::nullopt);
}

void deliverPickedFile(FileKind kind, std::string path)
{
    if (path.empty()) {
        return;
    }
    std::lock_guard lock(pickedMutex);
    picked[static_cast<size_t>(kind)] = std::move(path);
}

#if !defined(KESTREL_MOBILE)
void showFilePicker(FileKind kind)
{
    deliverPickedFile(kind, kind == FileKind::Png ? pickPngFile() : pickResourcePackFile());
}
#endif

}
