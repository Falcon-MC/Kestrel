#pragma once

namespace kestrel {
struct LaunchOptions;
// False when the user cancels first-run setup, so the client must not launch.
bool prepareAssets(const LaunchOptions& options);
}
