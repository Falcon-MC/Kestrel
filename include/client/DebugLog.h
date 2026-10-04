#pragma once

#include <string>

namespace kestrel {

/**
 * Appends one timestamped line to debug.txt in the data directory. The file
 * is rotated to debug.previous.txt at the start of every connection.
 */
void debugLog(const std::string& line);
void resetDebugLog();

/**
 * Logs how long each startup step took, as "startup <step> <ms> ms".
 */
class StartupTimer {
public:
    StartupTimer();
    void mark(const std::string& step);

private:
    double last = 0.0;
};

/**
 * Milliseconds since the process started, for the first frame's log line.
 */
double processMilliseconds();

}
