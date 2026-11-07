#pragma once
#include <Wire.h>
#include <stdint.h>

#include <atomic>

namespace gaia {

// Minimal register-level driver for the GY-91 module's MPU-9250 (+ AK8963 mag via
// bypass). No DMP, no FIFO: the data registers are polled and fusion runs on the
// ESP32 (logic/Fusion.h). Scales are fixed to match the wire protocol:
//   accel ±4 g  -> 8192 LSB/g (same scale the ICM-20948 DMP published)
//   gyro  ±2000 dps -> 16.4 LSB/dps
//   mag   16-bit, 0.15 µT/LSB x factory ASA adjustment
// The GY-91 also carries a BMP280 barometer at 0x76 (not read by this firmware).
// Single-task use (the IMU task) after begin().
class Mpu9250 {
 public:
  static constexpr float kAccelLsbPerG = 8192.0f;
  static constexpr float kGyroLsbPerDps = 16.4f;

  // WHO_AM_I accepted values: 0x71 (MPU-9250), 0x73 (MPU-9255).
  bool begin(TwoWire &wire, bool ad0);
  // Internal rate is 1 kHz (DLPF on); SMPLRT_DIV picks the output rate.
  bool setSampleRateHz(uint8_t hz);
  // True when a new accel/gyro sample is ready (INT_STATUS, clear-on-read).
  bool dataReady();
  bool readAccelGyro(int16_t acc[3], int16_t gyro[3]);
  // Latest magnetometer reading in the AK8963 frame, µT. Returns false when no new
  // data is ready or the sample overflowed; the caller keeps its previous value.
  bool readMag(float uT[3]);

  bool magOk() const { return magOk_; }
  uint8_t whoAmI() const { return whoAmI_; }
  uint32_t errorCount() const { return errors_.load(); }

 private:
  bool write8(uint8_t addr, uint8_t reg, uint8_t val);
  bool readBytes(uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n);
  bool initMag();

  TwoWire *wire_ = nullptr;
  uint8_t addr_ = 0x68;
  uint8_t whoAmI_ = 0;
  bool magOk_ = false;
  float magAdj_[3] = {1.0f, 1.0f, 1.0f};
  std::atomic<uint32_t> errors_{0};
};

}  // namespace gaia
