#pragma once
#include "platform/Shell.h"

#include <filesystem>
#include <string>

namespace kestrel::platform {

/**
 * Mojang's resource pack the phones download on first launch. An install made from another address no
 * longer counts as ready, so pointing this at a new release makes every phone download it again.
 */
inline constexpr const char* MobileResourceUrl = "https://github.com/Mojang/bedrock-samples/releases/download/v1.26.50.4/bedrock-samples-v1.26.50.4-full.zip";

/**
 * The size of that download, for progress bars on platforms whose HTTP client does not report it. It has to
 * change with the address.
 */
inline constexpr double MobileResourceSize = 169385576.0;

/**
 * Where a phone keeps the Minecraft resources it downloads from Mojang on first launch, laid out like an
 * installed game so the vanilla pack is found under resource_packs/vanilla.
 */
std::filesystem::path mobileResources();
bool mobileResourcesReady();

/**
 * Unpacks Mojang's resource pack download, then adds the files the app ships itself from bundle: the fonts,
 * the archived font sheets, the Ore UI and the touch controls layout.
 */
void installMobileResources(std::string archive, const std::filesystem::path& bundle);

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
