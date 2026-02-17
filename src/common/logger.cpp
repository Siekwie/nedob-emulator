#include "logger.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Logger {

namespace {

FILE* g_log_file = nullptr;
bool g_file_logging_disabled = false;
std::size_t g_bytes_written = 0;
std::size_t g_max_bytes = 0;
std::size_t g_check_counter = 0;

std::size_t clampMaxBytes(std::size_t v) {
    // Avoid absurdly small caps that make logs useless.
    constexpr std::size_t kMin = 1u * 1024u * 1024u;        // 1 MiB
    constexpr std::size_t kMax = 4u * 1024u * 1024u * 1024u; // 4 GiB
    if (v < kMin) return kMin;
    if (v > kMax) return kMax;
    return v;
}

std::size_t getMaxBytesFromEnvOrDefault() {
    // Default cap: keep logs bounded even if something spams.
    constexpr std::size_t kDefault = 256u * 1024u * 1024u; // 256 MiB
    const char* env = std::getenv("NEDOB_LOG_MAX_MB");
    if (!env || !*env) return kDefault;
    char* end = nullptr;
    const unsigned long mb = std::strtoul(env, &end, 10);
    if (end == env) return kDefault;
    const std::size_t bytes = static_cast<std::size_t>(mb) * 1024u * 1024u;
    return clampMaxBytes(bytes);
}

void writeToFile(const char* buf) {
    if (g_file_logging_disabled || !g_log_file) {
        return;
    }

    // Track size in-process so we can hard-stop before logs explode.
    const std::size_t n = std::strlen(buf);
    g_bytes_written += n;
    // Periodic cap check (cheap). We can overshoot slightly; that's fine.
    if (g_max_bytes != 0 && g_bytes_written > g_max_bytes) {
        g_file_logging_disabled = true;
        std::fputs("\n[Logger] nedob.log reached size cap; file logging disabled.\n", stderr);
        std::fclose(g_log_file);
        g_log_file = nullptr;
        return;
    }

    if (g_log_file) {
        std::fputs(buf, g_log_file);
        // Flush frequently enough for debugging, but not on every line.
        // This also reduces overhead when something is still chatty.
        if ((++g_check_counter & 0x3Fu) == 0) {
            std::fflush(g_log_file);
        }
    }
}

}  // namespace

bool init() {
    if (g_log_file) {
        return true;
    }
    g_file_logging_disabled = false;
    g_bytes_written = 0;
    g_check_counter = 0;
    g_max_bytes = getMaxBytesFromEnvOrDefault();

    g_log_file = std::fopen("nedob.log", "w");
    return g_log_file != nullptr;
}

void shutdown() {
    if (g_log_file) {
        std::fflush(g_log_file);
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
