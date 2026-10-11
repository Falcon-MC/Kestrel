#pragma once
#include "platform/Shell.h"

#include <filesystem>

namespace kestrel::platform {

/**
 * Copies the touch controls layout the app ships over the installed one, so an update reaches players whose
 * resources were installed by an older version.
 */
void refreshTouchControls(const std::filesystem::path& bundle);

/**
 * Where a file the player picked is copied before the game reads it. A later pick of the same kind replaces
 * it, since the game imports the file as soon as it arrives.
 */
std::filesystem::path importedFile(FileKind kind);

}
