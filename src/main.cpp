#include "client/Client.h"

#include <cstdio>
#include <exception>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void reportFatal(const std::string& message)
{
#if defined(_WIN32)
    MessageBoxA(nullptr, message.c_str(), "Kestrel", MB_OK | MB_ICONERROR);
#else
    std::fprintf(stderr, "Kestrel: %s\n", message.c_str());
#endif
}

int runKestrel()
{
    try {
        kestrel::Client client;
        return client.run();
    } catch (const std::exception& error) {
        reportFatal(error.what());
        return 1;
    }
}

}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return runKestrel();
}
#else
int main()
{
    return runKestrel();
}
#endif
