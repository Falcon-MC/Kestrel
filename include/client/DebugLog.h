#pragma once

#include <string>

namespace kestrel {

/**
 * Appends one timestamped line to debug.txt in the data directory. The file
 * is rotated to debug.previous.txt at the start of every connection.
 */
void debugLog(const std::string& line);
void resetDebugLog();

}
