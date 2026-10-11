#pragma once

#include <string>

namespace kestrel::platform {

void openUrl(const std::string& url);
bool copyText(const std::string& text);
std::string pasteText();

/**
 * Asks the player for a PNG image with the system's open dialog; empty when
 * they cancel or the platform has no dialog.
 */
std::string pickPngFile();
std::string pickResourcePackFile();
std::string pickWorldFile();

/**
 * Asks where to save an exported world, offering the given file name; empty
 * when they cancel or the platform has no dialog.
 */
std::string pickWorldSaveFile(const std::string& suggestedName);

}
