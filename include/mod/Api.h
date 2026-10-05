#pragma once

// The one header a mod needs. A mod is a shared library with a class derived
// from kestrel::mod::Mod and a KESTREL_MOD(ClassName) line; drop the built
// library into the mods folder next to settings.txt and it loads on start.

#include "mod/Mod.h"

// Bumped whenever an interface here changes shape; mods built against
// another version are refused instead of crashing.
#define KESTREL_MOD_API_VERSION 4

#define KESTREL_MOD_STRINGIFY_(value) #value
#define KESTREL_MOD_STRINGIFY(value) KESTREL_MOD_STRINGIFY_(value)

// C++ classes cross the library boundary, so the mod has to agree with the
// client on the compiler family and standard library, and on MSVC on the
// debug runtime too.
#if defined(_MSC_VER) && !defined(__clang__)
#define KESTREL_MOD_COMPILER "msvc"
#elif defined(__clang__)
#define KESTREL_MOD_COMPILER "clang"
#elif defined(__GNUC__)
#define KESTREL_MOD_COMPILER "gcc"
#else
#define KESTREL_MOD_COMPILER "unknown"
#endif

#if defined(_LIBCPP_VERSION)
#define KESTREL_MOD_STDLIB "libc++"
#elif defined(__GLIBCXX__)
#define KESTREL_MOD_STDLIB "libstdc++"
#elif defined(_MSC_VER)
#define KESTREL_MOD_STDLIB "msstl"
#else
#define KESTREL_MOD_STDLIB "unknown"
#endif

#if defined(_MSC_VER) && defined(_DEBUG)
#define KESTREL_MOD_RUNTIME "debug"
#else
#define KESTREL_MOD_RUNTIME "release"
#endif

#define KESTREL_MOD_ABI "kestrel-mod/" KESTREL_MOD_STRINGIFY(KESTREL_MOD_API_VERSION) "/" KESTREL_MOD_COMPILER "/" KESTREL_MOD_STDLIB "/" KESTREL_MOD_RUNTIME

#if defined(_WIN32)
#define KESTREL_MOD_EXPORT __declspec(dllexport)
#else
#define KESTREL_MOD_EXPORT __attribute__((visibility("default")))
#endif

#define KESTREL_MOD(ModClass)                                                          \
    extern "C" KESTREL_MOD_EXPORT const char* kestrel_mod_abi()                       \
    {                                                                                  \
        return KESTREL_MOD_ABI;                                                        \
    }                                                                                  \
    extern "C" KESTREL_MOD_EXPORT ::kestrel::mod::Mod* kestrel_mod_create()           \
    {                                                                                  \
        return new ModClass();                                                         \
    }                                                                                  \
    extern "C" KESTREL_MOD_EXPORT void kestrel_mod_destroy(::kestrel::mod::Mod* mod)  \
    {                                                                                  \
        delete mod;                                                                    \
    }
