#pragma once
#include <stdint.h>

#include "MagCalibration.h"

// Runtime configuration. Defaults are the legacy firmware values.
// Wire format: see TlvCodec.h and firmware/docs/protocol.md.
namespace gaia {

struct GaiaConfig {
  // IMU mounting calibration quaternion (w,x,y,z). Persisted in the NVS blob only:
  // no TLV register, set exclusively by the 0x14 calibration command. Kept first so
  // the 4-byte-aligned array adds no padding.
  float mountQuat[4] = {1.0f, 0.0f, 0.0f, 0.0f};

  // Magnetometer hard/soft-iron calibration (TLV 0x1E, one 24-byte record). Fixed
  // point: hard iron in 0.1 uT, soft iron in 1/4096. Defaults = no calibration.
  // Kept next to mountQuat so the int16 arrays stay naturally aligned.
  int16_t magHardIron[3] = {0, 0, 0};
  int16_t magSoftIron[9] = {kMagSoftIronOne, 0, 0, 0, kMagSoftIronOne, 0, 0, 0,
                            kMagSoftIronOne};

  uint8_t orientationMode = 0;  // 0=Quat6 relative, 1=Quat9
  // Target output rate. Drives BOTH the notify throttle and the MPU-9250 sample
  // rate (SMPLRT_DIV picked so the chip samples at or just above this rate).
  uint8_t imuNotifyHz = 50;  // 10..200
  uint8_t slerpAmount = 0;   // 0..100, 0=off (used when quatSmoothMode == 1)

  // --- IMU front-end -------------------------------------------------------
  // Anti-alias filter of the MPU-9250. 0 = auto: the driver picks the widest DLPF
  // whose cut-off still sits below 40 % of the output rate, so content that the
  // SMPLRT_DIV decimation would fold back into the band is removed first.
  // 1..7 force DLPF_CFG (1=184, 2=92, 3=41, 4=20, 5=10, 6=5, 7=3600 Hz).
  uint8_t imuDlpfMode = 0;
  // Gyroscope full scale: 0=+-250, 1=+-500, 2=+-1000, 3=+-2000 dps. Smaller is finer
  // (0.0076 / 0.0153 / 0.0305 / 0.061 dps per LSB) but clips sooner; a spun ball
  // easily passes 500 dps, so +-1000 is the default. The gyroscope is not on the
  // wire, so this choice is invisible to clients.
  uint8_t gyroFsSel = 2;

  // --- fusion (revised Madgwick / x-io Fusion, logic/Ahrs.h) ---------------
  uint16_t fusionGainX100 = 50;      // 0..500 -> gain 0.00..5.00 (0.50 default)
  uint8_t accelRejectionDeg = 10;    // 0 = off
  uint8_t magRejectionDeg = 10;      // 0 = off
  uint8_t rejectionTimeoutS = 5;     // 0 = disables both rejection features

  // --- gyroscope bias tracker (logic/GyroBias.h) ---------------------------
  uint16_t biasRestThDpsX100 = 200;  // rest = deviation below 2.00 dps
  uint16_t biasMaxDpsX100 = 1000;    // plausibility bound / clip: 10.00 dps
  uint8_t biasTauS = 4;              // estimate time constant at rest

  // --- orientation output smoothing (logic/QuatSmoother.h) -----------------
  uint8_t quatSmoothMode = 0;        // 0=off, 1=fixed slerp (legacy), 2=1-euro
  uint16_t oneEuroFcMinX100 = 100;   // 1.00 Hz cut-off while still
  uint16_t oneEuroBetaX1000 = 20;    // 0.020 Hz per dps of angular speed
  // Hampel spike filter on the published accelerometer, k x 10. 0 = off.
  uint8_t accelHampelKX10 = 0;
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
