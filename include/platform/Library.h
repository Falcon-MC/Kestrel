#pragma once

#include <filesystem>
#include <string>

namespace kestrel::platform {

/**
 * A shared library opened at run time (.dll, .so or .dylib), closed again
 * when the object goes away.
 */
class Library {
public:
    Library() = default;
    ~Library();

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    Library(Library&& other) noexcept;
    Library& operator=(Library&& other) noexcept;

    /**
     * Opens the library; false with error filled in when the system refused.
     */
    bool open(const std::filesystem::path& path, std::string& error);
    void close();
    void* symbol(const char* name) const;

    bool loaded() const
    {
        return handle != nullptr;
    }

    static const char* extension();

private:
    void* handle = nullptr;
};

}
