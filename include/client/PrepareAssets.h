#pragma once

#include <memory>

namespace kestrel {
struct LaunchOptions;
// Advances setup on the platform's event loop without blocking touch input or UIKit.
class AssetPreparation {
public:
    enum class State { Working, Ready, Cancelled };
    AssetPreparation();
    ~AssetPreparation();
    State frame();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
// False when the user cancels first-run setup, so the client must not launch.
bool prepareAssets(const LaunchOptions& options);
}
