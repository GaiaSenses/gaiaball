#pragma once
#include <Wire.h>
#include <stdint.h>

#include <atomic>

#include "ImuDriver.h"

namespace gaia {

// Minimal register-level driver for the GY-ICM20948V2 module's ICM-20948 (+ the
// AK09916 magnetometer, read directly at 0x0C with the chip's auxiliary bus in
// pass-through). No DMP, no FIFO: the data registers are polled and fusion runs on the
// ESP32 (logic/Ahrs.h), exactly as for the MPU-9250. Scales:
//   accel ±4 g  -> 8192 LSB/g (ImuDriver::kAccelLsbPerG), FIXED by the wire protocol
//   gyro  selectable ±250/500/1000/2000 dps -> 131/65.5/32.8/16.4 LSB/dps, the same
//         codes and sensitivities as the MPU-9250, so the stored gyroFsSel carries over
//   mag   16-bit, FIXED 0.15 µT/LSB: the AK09916 has no fuse-ROM ASA registers, so
//         there is no per-axis factory adjustment like the AK8963's (hard/soft-iron
//         correction is applied downstream, in ImuService, from the stored calibration)
//
// WHY this exists at all: the ball in hand turned out to carry this chip, not the
// GY-91 the firmware was written for. Its WHO_AM_I is at register 0x00, while 0x75
// (the MPU's) is reserved and returned a stable, meaningless 0x48 - a value that looks
// like an answer, which is what made the first flashing so confusing.
//
// THE HAZARD to keep in mind when reading this file: the register map is BANKED. Four
// banks share addresses 0x00..0x7E; only REG_BANK_SEL (0x7F) is visible in all of them.
// A data read in the wrong bank returns plausible garbage - it looks like bad sensor
// data, not like a bug - so the bank is selected once per operation (never per
// register), the selection is cached to keep it off the bus, and every configuration
// path leaves bank 0 selected, because bank 0 is where all the data lives.
//
// Single-task use (the IMU task) after begin(); see ImuDriver for the contract.
class Icm20948 : public ImuDriver {
 public:
  // WHO_AM_I (bank 0, 0x00) must read 0xEA.
  bool begin(TwoWire &wire, bool ad0) override;
  // Internal rate 1125 Hz, decimated by the (separate) gyro and accel dividers.
  // dlpfMode 0 = auto (widest DLPF still below the output Nyquist); 1..7 force
  // GYRO_DLPFCFG (1=151.8, 2=119.5, 3=51.2, 4=23.9, 5=11.6, 6=5.7, 7=361.4 Hz).
  bool setSampleRateHz(uint8_t hz, uint8_t dlpfMode) override;
  // Writes GYRO_CONFIG_1's GYRO_FS_SEL (0..3). Changes gyroLsbPerDps()/gyroRangeDps().
  bool setGyroFullScale(uint8_t fsSel) override;
  // True when a new accel/gyro sample is ready (INT_STATUS_1, clear-on-read).
  bool dataReady() override;
  bool readAccelGyro(int16_t acc[3], int16_t gyro[3]) override;
  // Latest magnetometer reading in the AK09916's own frame, µT.
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
  // Writes REG_BANK_SEL only when the cached bank differs. A failed write invalidates
  // the cache, so a lost bank selection can never be mistaken for a live one.
  bool selectBank(uint8_t bank);
  bool initMag();
  // GYRO_CONFIG_1 and ACCEL_CONFIG each pack FCHOICE, FS_SEL and DLPFCFG into one
  // byte, and two unrelated setters write them. Composing the whole byte from the
  // driver's own shadow state (instead of read-modify-write) keeps each write
  // deterministic and costs one transaction less; `bank 2 must be selected first`.
  bool writeGyroConfig1();
  bool writeAccelConfig();

  TwoWire *wire_ = nullptr;
  uint8_t addr_ = 0x68;
  uint8_t whoAmI_ = 0;
  uint8_t bank_ = 0xFF;   // unknown: forces a REG_BANK_SEL write on the first operation
  uint8_t dlpfMode_ = 4;  // 23.9 Hz: the auto choice for the default 50 Hz output
  uint8_t gyroFsSel_ = 2;         // ±1000 dps default, mirrored by the two fields below
  float gyroLsbPerDps_ = 32.8f;
  float gyroRangeDps_ = 1000.0f;
  bool magOk_ = false;
  std::atomic<uint32_t> errors_{0};
};

}  // namespace gaia
