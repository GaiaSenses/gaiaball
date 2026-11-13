#pragma once
#include <Wire.h>
#include <stdint.h>

#include <atomic>

#include "ImuDriver.h"

namespace gaia {

// Minimal register-level driver for the GY-91 module's MPU-9250 (+ AK8963 mag via
// bypass). No DMP, no FIFO: the data registers are polled and fusion runs on the
// ESP32 (logic/Ahrs.h). Scales:
//   accel ±4 g  -> 8192 LSB/g (ImuDriver::kAccelLsbPerG), FIXED by the wire protocol
//         (same scale the ICM-20948 DMP published, so decoders keep working)
//   gyro  selectable ±250/500/1000/2000 dps -> 131/65.5/32.8/16.4 LSB/dps. The
//         gyroscope never reaches the wire (only the fused quaternion does), so the
//         range is free to choose: the narrowest range that does not clip wins, since
//         quantisation noise is integrated straight into orientation drift.
//   mag   16-bit, 0.15 µT/LSB x factory ASA adjustment (hard/soft-iron correction is
//         applied downstream, in ImuService, from the stored calibration)
// The GY-91 also carries a BMP280 barometer at 0x76 (not read by this firmware).
// Single-task use (the IMU task) after begin(); see ImuDriver for the contract.
class Mpu9250 : public ImuDriver {
 public:
  // WHO_AM_I accepted values: 0x71 (MPU-9250), 0x73 (MPU-9255).
  bool begin(TwoWire &wire, bool ad0) override;
  // Internal rate 1 kHz, decimated by SMPLRT_DIV. dlpfMode 0 = auto (widest DLPF still
  // below the output Nyquist); 1..7 force DLPF_CFG (1=184, 2=92, 3=41, 4=20, 5=10,
  // 6=5, 7=3600 Hz).
  bool setSampleRateHz(uint8_t hz, uint8_t dlpfMode) override;
  // Writes GYRO_CONFIG's FS_SEL (0..3). Changes gyroLsbPerDps()/gyroRangeDps().
  bool setGyroFullScale(uint8_t fsSel) override;
  // True when a new accel/gyro sample is ready (INT_STATUS, clear-on-read).
  bool dataReady() override;
  bool readAccelGyro(int16_t acc[3], int16_t gyro[3]) override;
  // Latest magnetometer reading in the AK8963's own frame, µT.
  bool readMag(float uT[3]) override;

  bool magOk() const override { return magOk_; }
  float gyroLsbPerDps() const override { return gyroLsbPerDps_; }
  float gyroRangeDps() const override { return gyroRangeDps_; }
  float dlpfCutoffHz() const override;
  uint8_t dlpfMode() const override { return dlpfMode_; }
  uint8_t whoAmI() const override { return whoAmI_; }
  uint32_t errorCount() const override { return errors_.load(); }

 private:
  bool write8(uint8_t addr, uint8_t reg, uint8_t val);
  bool readBytes(uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n);
  bool initMag();

  TwoWire *wire_ = nullptr;
  uint8_t addr_ = 0x68;
  uint8_t whoAmI_ = 0;
  uint8_t dlpfMode_ = 4;  // 20 Hz: the auto choice for the default 50 Hz output
  float gyroLsbPerDps_ = 32.8f;   // ±1000 dps default
  float gyroRangeDps_ = 1000.0f;
  bool magOk_ = false;
  float magAdj_[3] = {1.0f, 1.0f, 1.0f};
  std::atomic<uint32_t> errors_{0};
};

}  // namespace gaia
