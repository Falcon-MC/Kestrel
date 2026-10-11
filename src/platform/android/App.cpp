#include "client/Client.h"
#include "client/LaunchOptions.h"
#include "../mobile/Resources.h"
#include "platform/Paths.h"

#include "Network/Http/HttpClient.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr const char* BundleIndex = "kestrel_bundle.txt";
constexpr int DownloadTimeoutMs = 30 * 60 * 1000;
constexpr size_t MaxDownloadSize = 512ull * 1024 * 1024;
constexpr int MaxRedirects = 5;

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
 * APK assets are not files the C++ side can open, so the fonts, the Ore UI and the touch layout the app ships
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

/**
 * GitHub answers a release download with a redirect to its file host, which the HTTP client leaves to the
 * caller.
 */
std::string download(std::string url, std::atomic<float>& fraction)
{
    auto progress = [&fraction](size_t received) {
        fraction = static_cast<float>(std::min(1.0, static_cast<double>(received) / kestrel::platform::MobileResourceSize));
    };
    for (int redirect = 0; redirect <= MaxRedirects; ++redirect) {
        HttpResponse response;
        std::string error;
        if (!HttpClient::get(url, { { "User-Agent", "Kestrel" } }, response, error, DownloadTimeoutMs, MaxDownloadSize, nullptr, progress)) {
            throw std::runtime_error("Cannot download Minecraft resources from Mojang: " + error);
        }
        int status = response.mStatus;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            url = response.getHeader("location");
            if (url.empty()) {
                throw std::runtime_error("Mojang's resource download redirected nowhere");
            }
            continue;
        }
        if (status != 200) {
            throw std::runtime_error("Mojang's resource download failed with status " + std::to_string(status) + ". Restart Kestrel to try again.");
        }
        return std::move(response.mBody);
    }
    throw std::runtime_error("Mojang's resource download redirected too many times");
}

/**
 * A plain SDL renderer shows the first launch's download, since the game's own window and renderer only come
 * up once the resources it draws with are installed.
 */
class ProgressScreen {
public:
    ProgressScreen()
    {
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        window = SDL_CreateWindow("Kestrel", 0, 0, SDL_WINDOW_FULLSCREEN);
        renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    }

    ~ProgressScreen()
    {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    ProgressScreen(const ProgressScreen&) = delete;
    ProgressScreen& operator=(const ProgressScreen&) = delete;

    /**
     * Runs work on its own thread while the screen shows text and, when fraction is given, a progress bar
     * under it.
     */
    void run(const std::string& text, const std::atomic<float>* fraction, const std::function<void()>& work)
    {
        std::atomic<bool> done { false };
        std::exception_ptr failure;
        std::thread worker([&] {
            try {
                work();
            } catch (...) {
                failure = std::current_exception();
            }
            done = true;
        });
        while (!done) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
            }
            draw(text, fraction ? fraction->load() : -1.0f);
            SDL_Delay(FrameMs);
        }
        worker.join();
        if (failure) {
            std::rethrow_exception(failure);
        }
    }

private:
    static constexpr Uint32 FrameMs = 33;
    static constexpr float CharacterSize = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

    void draw(const std::string& text, float fraction)
    {
        if (!renderer) {
            return;
        }
        int width = 0;
        int height = 0;
        SDL_GetRenderOutputSize(renderer, &width, &height);
        float scale = std::max(1.0f, std::floor(height / 240.0f));
        float logicalWidth = width / scale;
        float logicalHeight = height / scale;
        SDL_SetRenderScale(renderer, scale, scale);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        float textX = (logicalWidth - text.size() * CharacterSize) / 2;
        float textY = logicalHeight / 2 - CharacterSize * 2;
        SDL_RenderDebugText(renderer, textX, textY, text.c_str());
        if (fraction >= 0.0f) {
            SDL_FRect bar { logicalWidth * 0.2f, textY + CharacterSize * 2, logicalWidth * 0.6f, CharacterSize };
            SDL_RenderRect(renderer, &bar);
            SDL_FRect filled { bar.x + 2, bar.y + 2, (bar.w - 4) * std::clamp(fraction, 0.0f, 1.0f), bar.h - 4 };
            SDL_RenderFillRect(renderer, &filled);
            std::string percent = std::to_string(static_cast<int>(fraction * 100)) + "%";
            SDL_RenderDebugText(renderer, (logicalWidth - percent.size() * CharacterSize) / 2, bar.y + CharacterSize * 2, percent.c_str());
        }
        SDL_RenderPresent(renderer);
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
};

void prepareResources()
{
    fs::path bundle = extractBundle();
    setenv("KESTREL_FONTS", (bundle / "fonts").c_str(), 1);
    if (kestrel::platform::mobileResourcesReady()) {
        kestrel::platform::refreshTouchControls(bundle);
        return;
    }
    ProgressScreen screen;
    std::atomic<float> fraction { 0.0f };
    std::string archive;
    screen.run("Downloading Minecraft resources from Mojang...", &fraction, [&] {
        archive = download(kestrel::platform::MobileResourceUrl, fraction);
    });
    screen.run("Installing Minecraft resources...", nullptr, [&] {
        kestrel::platform::installMobileResources(std::move(archive), bundle);
    });
}

}

int main(int argc, char* argv[])
{
    if (const char* storage = SDL_GetAndroidInternalStoragePath()) {
        setenv("XDG_DATA_HOME", storage, 1);
        setenv("HOME", storage, 1);
    }
    try {
        fs::path vanilla = kestrel::platform::mobileResources() / "resource_packs/vanilla";
        setenv("KESTREL_VANILLA_PACK", vanilla.c_str(), 1);
        prepareResources();
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
