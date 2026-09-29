#include "platform/Library.h"

#include <utility>

#include <windows.h>

namespace kestrel::platform {

Library::~Library()
{
    close();
}

Library::Library(Library&& other) noexcept
    : handle(std::exchange(other.handle, nullptr))
{
}

Library& Library::operator=(Library&& other) noexcept
{
    if (this != &other) {
        close();
        handle = std::exchange(other.handle, nullptr);
    }
    return *this;
}

bool Library::open(const std::filesystem::path& path, std::string& error)
{
    close();
    HMODULE module = LoadLibraryW(path.c_str());
    if (!module) {
        error = "LoadLibrary failed with error " + std::to_string(GetLastError());
        return false;
    }
    handle = module;
    return true;
}

void Library::close()
{
    if (handle) {
        FreeLibrary(static_cast<HMODULE>(handle));
        handle = nullptr;
    }
}

void* Library::symbol(const char* name) const
{
    return handle ? reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name)) : nullptr;
}

const char* Library::extension()
{
    return ".dll";
}

}
