#pragma once

#include <optional>
#include <string>

namespace kestrel::platform {

enum class FileKind { Png, ResourcePack };

/**
 * Opens the system's file picker without waiting for it. The chosen file reaches takePickedFile on a later
 * frame as a path the game can read: desktops answer before returning, while phones copy the file into the
 * app's own storage first, since what their pickers hand out is not a plain file.
 */
void showFilePicker(FileKind kind);

/**
 * The file picked for kind since the last call, or nothing while a picker is open or after it was cancelled.
 */
std::optional<std::string> takePickedFile(FileKind kind);

/**
 * Called by a platform's picker, possibly from another thread, once the player chose a file.
 */
void deliverPickedFile(FileKind kind, std::string path);

void openUrl(const std::string& url);
bool copyText(const std::string& text);
std::string pasteText();

/**
 * Asks the player for a PNG image with the system's open dialog; empty when
 * they cancel or the platform has no dialog.
 */
std::string pickPngFile();
std::string pickResourcePackFile();

}
