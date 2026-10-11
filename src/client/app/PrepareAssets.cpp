#include "client/PrepareAssets.h"
#include "client/LaunchOptions.h"
#include "platform/Paths.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/DrawList.h"
#include "ui/AssetDownloadScreen.h"
#include "world/DownloadedAssets.h"
#include "world/PackSource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <thread>

namespace kestrel {
struct AssetPreparation::Impl {
    std::filesystem::path directory = platform::dataDirectory() / "assets" / world::VanillaResourceVersion;
    std::unique_ptr<Window> window;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<ui::AssetDownloadScreen> screen;
    std::atomic<bool> cancel {false};
    std::mutex mutex;
    world::AssetDownloadProgress progress;
    std::string error;
    bool pixelFont = false, closed = false;
    State state = State::Working;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    std::future<void> work;

    Impl()
    {
        if (!world::PackSource::locateVanilla().empty()) {
            state = State::Ready;
            return;
        }
        if (world::downloadedAssetsReady(directory)) {
            platform::useDownloadedAssets(directory);
            state = State::Ready;
            return;
        }
        window = Window::create("Kestrel - Preparing your game", 960, 600);
        renderer = Renderer::create(*window);
        screen = std::make_unique<ui::AssetDownloadScreen>();
        renderer->uploadUiAtlas(screen->atlas().data(), screen->AtlasSize, screen->AtlasSize);
        start();
    }

    ~Impl()
    {
        cancel = true;
        if (work.valid()) work.wait();
        renderer.reset();
        if (window) window->close();
    }

    void start()
    {
        cancel = false;
        error.clear();
        {
            std::lock_guard lock(mutex);
            progress = {};
        }
        work = std::async(std::launch::async, [this] {
            world::downloadVanillaAssets(directory, cancel, [this](world::AssetDownloadProgress value) {
                std::lock_guard lock(mutex);
                progress = value;
            });
        });
    }

    State frame()
    {
        if (state != State::Working) return state;
        if (!window->pump()) {
            cancel = true;
            closed = true;
        }
        auto& input = window->input();
        // Fit the same desktop layout on landscape phones without clipping its buttons.
        float scale = std::min({window->contentScale(), window->width() / 640.0f, window->height() / 500.0f});
        scale = std::max(scale, 0.01f);
        float mouseX = input.mouseX / scale, mouseY = input.mouseY / scale;
        bool clicked = input.mouseReleased;
        if (input.escape || (clicked && screen->cancelButton().contains(mouseX, mouseY))) cancel = true;
        if (work.valid() && work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            try {
                work.get();
                if (!cancel) {
                    platform::useDownloadedAssets(directory);
                    state = State::Ready;
                }
            } catch (const std::exception& failure) {
                error = failure.what();
            }
        }
        if (cancel && !work.valid()) return state = State::Cancelled;
        if (!cancel && !work.valid() && !error.empty()
            && (input.enter || (clicked && screen->retryButton().contains(mouseX, mouseY)))) start();
        if (!closed && window->width() && window->height()) {
            if (window->consumeResize()) renderer->resize(window->width(), window->height());
            world::AssetDownloadProgress snapshot;
            {
                std::lock_guard lock(mutex);
                snapshot = progress;
            }
            if (!pixelFont && !snapshot.font.empty()) {
                auto path = snapshot.font;
                if (!std::filesystem::is_regular_file(path)) path = directory / "fonts/Monocraft.ttf";
                if (std::filesystem::is_regular_file(path)) {
                    // Publishing the cache can rename the font between this check and the read.
                    try {
                        screen = std::make_unique<ui::AssetDownloadScreen>(path);
                        renderer->uploadUiAtlas(screen->atlas().data(), screen->AtlasSize, screen->AtlasSize);
                        pixelFont = true;
                    } catch (const std::exception&) {}
                }
            }
            ui::DrawList list;
            float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - began).count();
            screen->draw(list, window->width() / scale, window->height() / scale, scale,
                snapshot, seconds, error, cancel, mouseX, mouseY);
            window->setCursor(!cancel && (screen->cancelButton().contains(mouseX, mouseY)
                || (!error.empty() && screen->retryButton().contains(mouseX, mouseY))) ? Cursor::Hand : Cursor::Arrow);
            renderer->beginFrame(0, 0, 0);
            renderer->drawUi(list);
            renderer->endFrame();
        }
        return state;
    }
};

AssetPreparation::AssetPreparation() : impl(std::make_unique<Impl>()) {}
AssetPreparation::~AssetPreparation() = default;
AssetPreparation::State AssetPreparation::frame() { return impl->frame(); }

bool prepareAssets(const LaunchOptions& options)
{
    if (options.hidden || options.headless) {
        if (!world::PackSource::locateVanilla().empty()) return true;
        auto directory = platform::dataDirectory() / "assets" / world::VanillaResourceVersion;
        std::atomic<bool> cancel {false};
        world::downloadVanillaAssets(directory, cancel, [](world::AssetDownloadProgress) {});
        platform::useDownloadedAssets(directory);
        return true;
    }
    AssetPreparation preparation;
    AssetPreparation::State state;
    while ((state = preparation.frame()) == AssetPreparation::State::Working)
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    return state == AssetPreparation::State::Ready;
}
}
