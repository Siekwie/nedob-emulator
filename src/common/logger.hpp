#pragma once

#include <cstdarg>
#include <cstdio>

namespace Logger {

/// Initialize the file logger. Call once at startup.
/// Log file is "nedob.log" in the current working directory.
/// \return true if log file opened successfully (logging still works to stderr either way)
bool init();

/// Shutdown and close the log file. Call at exit.
void shutdown();

/// Log to stderr and to the log file (printf-style format).
/// Safe to call before init() or after init() fails; will only write to stderr.
void log(const char* fmt, ...);

/// Log to stdout and to the log file (printf-style format).
void logInfo(const char* fmt, ...);

}  // namespace Logger
