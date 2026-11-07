#include "Diagnostics.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

namespace gaia {

namespace {
constexpr uint32_t kWdtTimeoutS = 10;
uint16_t clamp16(uint32_t v) { return v > 0xFFFF ? 0xFFFF : (uint16_t)v; }
}  // namespace

void Diagnostics::begin() { esp_task_wdt_init(kWdtTimeoutS, true); }

void Diagnostics::watchTask() { esp_task_wdt_add(nullptr); }

void Diagnostics::feed() { esp_task_wdt_reset(); }

DiagPacket Diagnostics::collect(uint16_t seq, uint16_t notifyHzX10, const ImuService &imu,
                                const AudioService &audio, const Co2Service &co2,
                                uint32_t nowMs) const {
  DiagPacket p = {};
  p.version = kProtocolVersion;
  p.type = (uint8_t)PacketType::Diagnostics;
  p.seq = seq;
  p.uptimeS = millis() / 1000;
  p.imuHzX10 = clamp16((uint32_t)(imu.measuredHz() * 10.0f + 0.5f));
  p.notifyHzX10 = notifyHzX10;
  // Legacy wire slot "fifoOverflows" now carries the MPU-9250 I2C read-error count.
  p.fifoOverflows = clamp16(imu.readErrorCount());
  p.i2sErrors = clamp16(audio.i2sErrorCount());
  p.uartFrameErrors = clamp16(co2.errorCount());
  p.freeHeapKb = clamp16(esp_get_free_heap_size() / 1024);
  p.minFreeHeapKb = clamp16(esp_get_minimum_free_heap_size() / 1024);
  uint8_t f = 0;
  if (imu.imuOk()) f |= kDiagImuOk;
  if (audio.ok()) f |= kDiagAudioOk;
  if (co2.co2Ok(nowMs)) f |= kDiagCo2Ok;
  p.flags = f;
  return p;
}

}  // namespace gaia
