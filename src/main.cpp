#include "client/Client.h"
#include "client/HeadlessClient.h"
#include "client/LaunchOptions.h"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
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

int runKestrel(const std::vector<std::string>& arguments)
{
    try {
        kestrel::LaunchOptions options = kestrel::LaunchOptions::parse(arguments);
        if (options.headless) {
            kestrel::HeadlessClient client(std::move(options));
            return client.run();
        }
        kestrel::Client client(std::move(options));
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
    std::vector<std::string> arguments;
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) {
            int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string argument(size > 0 ? size - 1 : 0, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, argument.data(), size, nullptr, nullptr);
            arguments.push_back(std::move(argument));
        }
        LocalFree(wide);
    }
    return runKestrel(arguments);
}
#else
int main(int argc, char** argv)
{
    return runKestrel(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
