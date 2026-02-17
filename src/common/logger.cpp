#include "logger.hpp"
#include <cstdarg>
#include <cstdio>

namespace Logger {

namespace {

FILE* g_log_file = nullptr;

void writeToFile(const char* buf) {
    if (g_log_file) {
        std::fputs(buf, g_log_file);
        std::fflush(g_log_file);
    }
}

}  // namespace

bool init() {
    if (g_log_file) {
        return true;
    }
    g_log_file = std::fopen("nedob.log", "w");
    return g_log_file != nullptr;
}

void shutdown() {
    if (g_log_file) {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
}

void log(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    char buf[4096];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fputs(buf, stderr);
    writeToFile(buf);
}

void logInfo(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    char buf[4096];
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fputs(buf, stdout);
    writeToFile(buf);
}

}  // namespace Logger
