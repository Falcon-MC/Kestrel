#include "platform/Library.h"

#include <utility>

#include <dlfcn.h>

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
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* reason = dlerror();
        error = reason ? reason : "dlopen failed";
        return false;
    }
    return true;
}

void Library::close()
{
    if (handle) {
        dlclose(handle);
        handle = nullptr;
    }
}

void* Library::symbol(const char* name) const
{
    return handle ? dlsym(handle, name) : nullptr;
}

const char* Library::extension()
{
    return ".dylib";
}

}
