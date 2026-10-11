#include "client/Client.h"
#include "client/LaunchOptions.h"
#include "client/PrepareAssets.h"
#include "../mobile/Resources.h"
#include "platform/Paths.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr const char* BundleIndex = "kestrel_bundle.txt";

std::string readAsset(const std::string& path)
{
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        throw std::runtime_error("The app is missing " + path + ": " + SDL_GetError());
    }
    std::string bytes(static_cast<const char*>(data), size);
    SDL_free(data);
    return bytes;
}

/**
 * APK assets are not files the C++ side can open, so the free fonts and the touch layout the app ships
 * are copied out once per build, which the index of shipped files identifies.
 */
fs::path extractBundle()
{
    fs::path bundle = kestrel::platform::dataDirectory() / "bundle";
    std::string index = readAsset(BundleIndex);
    std::error_code error;
    if (fs::exists(bundle / BundleIndex, error)) {
        std::ifstream previous(bundle / BundleIndex, std::ios::binary);
        std::stringstream text;
        text << previous.rdbuf();
        if (text.str() == index) {
            return bundle;
        }
    }
    fs::remove_all(bundle, error);
    std::istringstream lines(index);
    std::string path;
    while (std::getline(lines, path)) {
        if (path.empty() || path.front() == '#') {
            continue;
        }
        fs::path relative = fs::path(path).lexically_normal();
        if (relative.is_absolute() || relative.empty() || *relative.begin() == "..") {
            throw std::runtime_error("The app ships an invalid resource path: " + path);
        }
        fs::create_directories((bundle / relative).parent_path());
        std::string bytes = readAsset("bundle/" + path);
        std::ofstream output(bundle / relative, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            throw std::runtime_error("Cannot save " + path);
        }
    }
    std::ofstream marker(bundle / BundleIndex, std::ios::binary);
    marker << index;
    if (!marker) {
        throw std::runtime_error("Cannot finish unpacking the app's resources");
    }
    return bundle;
}

bool prepareResources()
{
    fs::path bundle = extractBundle();
    setenv("KESTREL_FONTS", (bundle / "fonts").c_str(), 1);
    kestrel::LaunchOptions options;
    if (!kestrel::prepareAssets(options)) return false;
    kestrel::platform::refreshTouchControls(bundle);
    return true;
}

}

int main(int argc, char* argv[])
{
    if (const char* storage = SDL_GetAndroidInternalStoragePath()) {
        setenv("XDG_DATA_HOME", storage, 1);
        setenv("HOME", storage, 1);
    }
    try {
        if (!prepareResources()) return 0;
        kestrel::LaunchOptions options = kestrel::LaunchOptions::parse(std::vector<std::string>(argv + 1, argv + argc));
        options.hidden = false;
        options.headless = false;
        kestrel::Client client(std::move(options));
        return client.run();
    } catch (const std::exception& error) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kestrel", error.what(), nullptr);
        return 1;
    }
}
