#pragma once
#include <stdint.h>

// Runtime configuration. Defaults are the legacy firmware values.
// Wire format: see TlvCodec.h and firmware/docs/protocol.md.
namespace gaia {

struct GaiaConfig {
  // IMU mounting calibration quaternion (w,x,y,z). Persisted in the NVS blob only:
  // no TLV register, set exclusively by the 0x14 calibration command. Kept first so
  // the 4-byte-aligned array adds no padding.
  float mountQuat[4] = {1.0f, 0.0f, 0.0f, 0.0f};

  uint8_t orientationMode = 0;  // 0=Quat6 relative, 1=Quat9
  // Target output rate. Drives BOTH the notify throttle and the MPU-9250 sample
  // rate (SMPLRT_DIV picked so the chip samples at or just above this rate).
  uint8_t imuNotifyHz = 50;  // 10..200
  uint8_t slerpAmount = 0;   // 0..100, 0=off
  // EMA low-pass on the published accelerometer counts. 0 = off (raw passthrough);
  // 1..1000 = alpha*1000 (smaller = smoother).
  uint16_t accelLpAlphaX1000 = 0;

  uint8_t audioEnabled = 1;
  uint16_t audioFastAlphaX1000 = 250;  // 1..1000
  uint16_t audioSlowAlphaX1000 = 995;  // 1..1000
  uint16_t debounceMs = 300;
  uint8_t potControlsThreshold = 1;
  uint32_t thresholdFixed = 100000;  // used when potControlsThreshold == 0
  // Breath band-pass (HP then LP biquads) applied to the mic stream before the RMS /
  // peak detector. Q is x100 (500 = 5.00). NOTE: Q > 0.707 adds a resonant peak of
  // ~20*log10(Q) dB at the cutoff; validation enforces lpHz > hpHz.
  uint8_t audioBpfEnabled = 1;
  uint16_t audioHpHz = 10;      // 1..7000
  uint16_t audioHpQX100 = 500;  // 10..2000
  uint16_t audioLpHz = 200;     // 1..7000
  uint16_t audioLpQX100 = 500;  // 10..2000
  // Potentiometer EMA alpha x1000 (300 = legacy 0.3).
  uint16_t potEmaAlphaX1000 = 300;

  // Sensor: SC8-CO2-5K ("YYS SC8"), active 0x42 0x4D stream, 1 frame/s. The frame
  // layout is fixed by the datasheet (see Co2FrameParser.h) — nothing to configure.
  uint8_t co2Enabled = 1;
  uint16_t co2StaleTimeoutMs = 5000;
  uint16_t virtualPeakPpm = 2100;
  uint16_t virtualDelayMs = 1000;
  uint16_t virtualHoldMs = 17000;
  uint8_t co2RawLogMode = 0;
  // EMA over accepted CO2 readings, alpha x1000. 1000 = no smoothing (legacy).
  uint16_t co2EmaAlphaX1000 = 1000;

  // One pin = one function (see firmware/docs/hardware-pinout.md). Defaults follow the
  // KiCAD board: D2 "LED mic" on GPIO2, D1 "LED co2" on GPIO4. The two pins must be
  // distinct (enforced by tlvApply). The legacy BLE LED command (0x00/0x01) was
  // removed: no client ever sent it and the board has no LED for it.
  uint8_t soundLedPin = 2;  // D2 on the board
  uint8_t co2LedPin = 4;    // D1 on the board
  uint16_t co2AlertPpm = 2000;

  // Serial (USB) logging. Default OFF: log formatting steals CPU from the hot path.
  uint8_t serialLogEnabled = 0;

  char bleName[17] = {'G', 'l', 'o', 'b', 'o', '_', '1', 0};  // 1..16 printable ASCII
  char pin[5] = {'1', '2', '3', '4', 0};                       // write-only: never serialized
};

}  // namespace gaia
