#include "Logger.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdarg.h>
#include <stdio.h>

#include <atomic>

namespace gaia {

namespace {
constexpr size_t kLineMax = 160;
constexpr uint8_t kMaxLinesPerWindow = 20;
constexpr uint32_t kWindowMs = 1000;

bool g_started = false;
SemaphoreHandle_t g_mutex = nullptr;  // created once in begin(); guards limiter state + Serial write
constexpr TickType_t kLockWait = pdMS_TO_TICKS(20);
LogLevel g_level = LogLevel::Info;
uint32_t g_windowStart = 0;
uint8_t g_linesInWindow = 0;
uint32_t g_dropped = 0;
const char kLevelChar[] = {'E', 'W', 'I', 'D'};
}  // namespace

void Logger::begin(LogLevel level) {
  if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
  g_level = level;
  g_windowStart = millis();
  g_linesInWindow = 0;
  g_dropped = 0;
  g_started = true;
}

void Logger::setLevel(LogLevel level) { g_level = level; }
LogLevel Logger::level() { return g_level; }

static std::atomic<bool> g_enabled{true};  // boot logs stay on until config loads
void Logger::setEnabled(bool on) { g_enabled.store(on); }
bool Logger::enabled() { return g_enabled.load(); }

void Logger::log(LogLevel level, const char *tag, const char *fmt, ...) {
  if (!g_started || !g_enabled.load(std::memory_order_relaxed) || level > g_level) return;

  // Format outside the lock; only the limiter and the Serial write are serialized.
  char buf[kLineMax];
  int n = snprintf(buf, sizeof(buf), "%c [%s] ", kLevelChar[(uint8_t)level], tag ? tag : "?");
  if (n < 0) return;
  if ((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);

  // Without a mutex (begin() could not create one) fall back to unguarded behavior.
  const bool locked = g_mutex && xSemaphoreTake(g_mutex, kLockWait) == pdTRUE;
  if (g_mutex && !locked) return;  // contention timeout: drop this line

  const uint32_t now = millis();
  if (now - g_windowStart >= kWindowMs) {
    g_windowStart = now;
    g_linesInWindow = 0;
    if (g_dropped > 0) {
      Serial.printf("[logger] %lu messages dropped\n", (unsigned long)g_dropped);
      g_dropped = 0;
      g_linesInWindow = 1;
    }
  }
  if (g_linesInWindow >= kMaxLinesPerWindow) {
    g_dropped++;
  } else {
    g_linesInWindow++;
    Serial.println(buf);
  }
  if (locked) xSemaphoreGive(g_mutex);
}

}  // namespace gaia
