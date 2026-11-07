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
  uint8_t imuNotifyHz = 50;     // 10..60
  uint8_t slerpAmount = 0;      // 0..100, 0=off

  uint8_t audioEnabled = 1;
  uint16_t audioFastAlphaX1000 = 250;  // 1..1000
  uint16_t audioSlowAlphaX1000 = 995;  // 1..1000
  uint16_t debounceMs = 300;
  uint8_t potControlsThreshold = 1;
  uint32_t thresholdFixed = 100000;  // used when potControlsThreshold == 0

  uint8_t co2Enabled = 1;
  // 0 = Senseair S8 Modbus polling (the sensor on the KiCAD board: it never transmits
  //     unsolicited, so it MUST be polled); 1 = passive 0x42 0x4D frame stream (legacy).
  uint8_t co2Protocol = 0;
  uint8_t co2ChecksumVariant = 0;  // passive mode only: 0=sum bytes 0..13; 1=legacy sum 0..14
  uint16_t co2StaleTimeoutMs = 5000;
  uint16_t virtualPeakPpm = 2100;
  uint16_t virtualDelayMs = 1000;
  uint16_t virtualHoldMs = 17000;
  uint8_t co2RawLogMode = 0;

  // One pin = one function (see firmware/docs/hardware-pinout.md). Defaults follow the
  // KiCAD board: D2 "LED mic" on GPIO2, D1 "LED co2" on GPIO4. The status LED is a NEW
  // physical LED (GPIO23 + 220R + LED to GND must be added to the board); it carries
  // ONLY the legacy BLE LED command (0x00/0x01). The three pins must be distinct
  // (enforced by tlvApply).
  uint8_t soundLedPin = 2;    // D2 on the board
  uint8_t co2LedPin = 4;      // D1 on the board
  uint8_t statusLedPin = 23;  // new LED; GPIO23 is free on the board headers
  uint16_t co2AlertPpm = 2000;

  char bleName[17] = {'G', 'l', 'o', 'b', 'o', '_', '1', 0};  // 1..16 printable ASCII
  char pin[5] = {'1', '2', '3', '4', 0};                       // write-only: never serialized
};

}  // namespace gaia
