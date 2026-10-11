#include "client/PrepareAssets.h"
#include "client/LaunchOptions.h"
#include "platform/Paths.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "ui/DrawList.h"
#include "ui/AssetDownloadScreen.h"
#include "world/DownloadedAssets.h"
#include "world/PackSource.h"

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <thread>

namespace kestrel {
bool prepareAssets(const LaunchOptions& options)
{
    if (!world::PackSource::locateVanilla().empty()) return true;
    auto directory = platform::dataDirectory() / "assets" / world::VanillaResourceVersion;
    if (!world::downloadedAssetsReady(directory)) {
        std::atomic<bool> cancel {false};
        if (options.hidden || options.headless) {
            world::downloadVanillaAssets(directory, cancel, [](world::AssetDownloadProgress) {});
        } else {
            auto window = Window::create("Kestrel - Preparing your game", 960, 600);
            auto renderer = Renderer::create(*window);
            ui::AssetDownloadScreen screen;
            bool pixelFont = false;
            renderer->uploadUiAtlas(screen.atlas().data(), screen.AtlasSize, screen.AtlasSize);
            std::mutex mutex;
            world::AssetDownloadProgress progress;
            std::future<void> work;
            std::string error;
            auto start = [&] {
                cancel = false;
                error.clear();
                {
                    std::lock_guard lock(mutex);
                    progress = {};
                }
                work = std::async(std::launch::async, [&] {
                    world::downloadVanillaAssets(directory, cancel, [&](world::AssetDownloadProgress value) {
                        std::lock_guard lock(mutex);
                        progress = value;
                    });
                });
            };
            start();
            auto began = std::chrono::steady_clock::now();
            bool complete = false;
            bool closed = false;
            try {
                while (!complete) {
                    if (!window->pump()) {
                        cancel = true;
                        closed = true;
                    }
                    auto& input = window->input();
                    float scale = window->contentScale();
                    float mouseX = input.mouseX / scale, mouseY = input.mouseY / scale;
                    bool clicked = input.mouseReleased;
                    if (input.escape || (clicked && screen.cancelButton().contains(mouseX, mouseY))) cancel = true;
                    if (work.valid() && work.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                        try {
                            work.get();
                            complete = true;
                        } catch (const std::exception& failure) {
                            error = failure.what();
                        }
                    }
                    if (cancel && !work.valid()) break;
                    if (!cancel && !work.valid() && !error.empty()
                        && (input.enter || (clicked && screen.retryButton().contains(mouseX, mouseY)))) start();
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
                                    screen = ui::AssetDownloadScreen(path);
                                    renderer->uploadUiAtlas(screen.atlas().data(), screen.AtlasSize, screen.AtlasSize);
                                    pixelFont = true;
                                } catch (const std::exception&) {}
                            }
                        }
                        ui::DrawList list;
                        float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - began).count();
                        screen.draw(list, window->width() / scale, window->height() / scale, scale,
                            snapshot, seconds, error, cancel, mouseX, mouseY);
                        window->setCursor(!cancel && (screen.cancelButton().contains(mouseX, mouseY)
                            || (!error.empty() && screen.retryButton().contains(mouseX, mouseY))) ? Cursor::Hand : Cursor::Arrow);
                        renderer->beginFrame(0, 0, 0);
                        renderer->drawUi(list);
                        renderer->endFrame();
                    }
                    if (!complete) std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            } catch (...) {
                cancel = true;
                throw;
            }
            renderer.reset();
            window->close();
            window.reset();
            if (cancel) return false;
        }
    }
    platform::useDownloadedAssets(directory);
    return true;
}
}
