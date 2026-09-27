#pragma once

#include <string>

namespace kestrel {

/**
 * Appends one timestamped line to debug.txt in the data directory. The file
 * is emptied by resetDebugLog at the start of every connection.
 */
void debugLog(const std::string& line);
void resetDebugLog();

}
