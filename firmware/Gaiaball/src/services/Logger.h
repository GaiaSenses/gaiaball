#pragma once
#include <stdint.h>

namespace gaia {

enum class LogLevel : uint8_t { Error = 0, Warn = 1, Info = 2, Debug = 3 };

// Leveled, globally rate-limited (max 20 lines/s) Serial logger.
// Thread-safe across FreeRTOS tasks (mutex created once in begin(); no heap per call).
// Must NOT be called from an ISR.
// No heap allocation per call; lines longer than the stack buffer are truncated.
class Logger {
 public:
  static void begin(LogLevel level = LogLevel::Info);  // Serial must already be started
  static void setLevel(LogLevel level);
  static LogLevel level();
  // Master switch (config TLV 0x50, default OFF after boot config load). While
  // disabled, log() returns before formatting: zero cost on the hot path.
  static void setEnabled(bool on);
  static bool enabled();
  static void log(LogLevel level, const char *tag, const char *fmt, ...)
      __attribute__((format(printf, 3, 4)));
};

}  // namespace gaia

#define GB_LOGE(tag, fmt, ...) ::gaia::Logger::log(::gaia::LogLevel::Error, tag, fmt, ##__VA_ARGS__)
#define GB_LOGW(tag, fmt, ...) ::gaia::Logger::log(::gaia::LogLevel::Warn, tag, fmt, ##__VA_ARGS__)
#define GB_LOGI(tag, fmt, ...) ::gaia::Logger::log(::gaia::LogLevel::Info, tag, fmt, ##__VA_ARGS__)
#define GB_LOGD(tag, fmt, ...) ::gaia::Logger::log(::gaia::LogLevel::Debug, tag, fmt, ##__VA_ARGS__)
