#include "Mpu9250.h"

#include <Arduino.h>

#include "Logger.h"
#include "../logic/ImuFrontEnd.h"

namespace gaia {

namespace {
constexpr const char *kTag = "mpu";
// MPU-9250 registers.
constexpr uint8_t kRegSmplrtDiv = 0x19;
constexpr uint8_t kRegConfig = 0x1A;
constexpr uint8_t kRegGyroConfig = 0x1B;
constexpr uint8_t kRegAccelConfig = 0x1C;
constexpr uint8_t kRegAccelConfig2 = 0x1D;
constexpr uint8_t kRegIntPinCfg = 0x37;
constexpr uint8_t kRegIntEnable = 0x38;
constexpr uint8_t kRegIntStatus = 0x3A;
constexpr uint8_t kRegAccelXoutH = 0x3B;
constexpr uint8_t kRegPwrMgmt1 = 0x6B;
constexpr uint8_t kRegPwrMgmt2 = 0x6C;
constexpr uint8_t kRegWhoAmI = 0x75;
// AK8963 (magnetometer, visible at 0x0C with bypass enabled).
constexpr uint8_t kMagAddr = 0x0C;
constexpr uint8_t kMagRegWia = 0x00;   // expects 0x48
constexpr uint8_t kMagRegSt1 = 0x02;   // bit0 DRDY
constexpr uint8_t kMagRegHxl = 0x03;   // 6 data bytes + ST2
constexpr uint8_t kMagRegCntl1 = 0x0A;
constexpr uint8_t kMagRegAsax = 0x10;
constexpr float kMagUtPerLsb = 0.15f;  // 16-bit mode

// Gyroscope sensitivity per FS_SEL value, and the matching full-scale magnitude.
constexpr float kGyroLsbPerDps[Mpu9250::kGyroFsCount] = {131.0f, 65.5f, 32.8f, 16.4f};
constexpr float kGyroRangeDps[Mpu9250::kGyroFsCount] = {250.0f, 500.0f, 1000.0f, 2000.0f};
}  // namespace

float Mpu9250::dlpfCutoffHz() const { return dlpfCutoffHzFor(ImuChip::Mpu9250, dlpfMode_); }

bool Mpu9250::write8(uint8_t addr, uint8_t reg, uint8_t val) {
  wire_->beginTransmission(addr);
  wire_->write(reg);
  wire_->write(val);
  if (wire_->endTransmission() == 0) return true;
  errors_.fetch_add(1);
  return false;
}

bool Mpu9250::readBytes(uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n) {
  wire_->beginTransmission(addr);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0 || wire_->requestFrom((int)addr, (int)n) != (int)n) {
    errors_.fetch_add(1);
    return false;
  }
  for (uint8_t i = 0; i < n; i++) out[i] = (uint8_t)wire_->read();
  return true;
}

bool Mpu9250::begin(TwoWire &wire, bool ad0) {
  wire_ = &wire;
  addr_ = ad0 ? 0x69 : 0x68;

  if (!readBytes(addr_, kRegWhoAmI, &whoAmI_, 1)) return false;
  if (whoAmI_ != 0x71 && whoAmI_ != 0x73) {
    GB_LOGE(kTag, "WHO_AM_I 0x%02X at 0x%02X: not an MPU-9250/9255", whoAmI_, addr_);
    return false;
  }

  bool ok = true;
  ok &= write8(addr_, kRegPwrMgmt1, 0x80);  // reset
  delay(100);
  ok &= write8(addr_, kRegPwrMgmt1, 0x01);  // clock from PLL
  ok &= write8(addr_, kRegPwrMgmt2, 0x00);  // all axes on
  ok &= write8(addr_, kRegAccelConfig, 0x08);  // ±4 g (8192 LSB/g, wire scale)
  // Gyro range and the DLPF pair are written by setGyroFullScale()/setSampleRateHz(),
  // which begin() callers invoke with the stored configuration.
  ok &= write8(addr_, kRegIntEnable, 0x01);     // RAW_DATA_RDY latches INT_STATUS
  ok &= write8(addr_, kRegIntPinCfg, 0x02);     // I2C bypass: AK8963 visible at 0x0C
  if (!ok) return false;

  magOk_ = initMag();
  if (!magOk_) GB_LOGW(kTag, "AK8963 mag not available; Quat9 will fall back to 6-axis");
  GB_LOGI(kTag, "MPU-9250 ready (WHO_AM_I 0x%02X, mag %s)", whoAmI_, magOk_ ? "ok" : "off");
  return true;
}

bool Mpu9250::initMag() {
  uint8_t wia = 0;
  if (!readBytes(kMagAddr, kMagRegWia, &wia, 1) || wia != 0x48) return false;
  if (!write8(kMagAddr, kMagRegCntl1, 0x00)) return false;  // power-down
  delay(10);
  if (!write8(kMagAddr, kMagRegCntl1, 0x0F)) return false;  // fuse ROM access
  delay(10);
  uint8_t asa[3];
  if (!readBytes(kMagAddr, kMagRegAsax, asa, 3)) return false;
  for (int i = 0; i < 3; i++) magAdj_[i] = ((float)(asa[i] - 128) * 0.5f) / 128.0f + 1.0f;
  if (!write8(kMagAddr, kMagRegCntl1, 0x00)) return false;
  delay(10);
  // 16-bit, continuous measurement mode 2 (100 Hz).
  return write8(kMagAddr, kMagRegCntl1, 0x16);
}

bool Mpu9250::setGyroFullScale(uint8_t fsSel) {
  if (fsSel >= kGyroFsCount) fsSel = kGyroFsCount - 1;
  if (!write8(addr_, kRegGyroConfig, (uint8_t)(fsSel << 3))) return false;
  gyroLsbPerDps_ = kGyroLsbPerDps[fsSel];
  gyroRangeDps_ = kGyroRangeDps[fsSel];
  GB_LOGI(kTag, "gyro range ±%d dps (%.1f LSB/dps)", (int)gyroRangeDps_, gyroLsbPerDps_);
  return true;
}

bool Mpu9250::setSampleRateHz(uint8_t hz, uint8_t dlpfMode) {
  const FrontEndPlan plan = planFrontEnd(ImuChip::Mpu9250, hz, dlpfMode);
  const uint8_t mode = plan.dlpfMode;
  bool ok = write8(addr_, kRegConfig, mode);          // gyro DLPF, internal rate 1 kHz
  ok &= write8(addr_, kRegAccelConfig2, mode);        // accel DLPF (same index)
  ok &= write8(addr_, kRegSmplrtDiv, (uint8_t)plan.gyroDiv);
  if (!ok) return false;
  dlpfMode_ = mode;
  GB_LOGI(kTag, "rate %u Hz, DLPF %.0f Hz (mode %u%s)", (unsigned)(hz < 4 ? 4 : hz),
          plan.cutoffHz, (unsigned)mode, dlpfMode == 0 ? ", auto" : "");
  return true;
}

bool Mpu9250::dataReady() {
  uint8_t st = 0;
  if (!readBytes(addr_, kRegIntStatus, &st, 1)) return false;
  return (st & 0x01) != 0;
}

bool Mpu9250::readAccelGyro(int16_t acc[3], int16_t gyro[3]) {
  uint8_t b[14];
  if (!readBytes(addr_, kRegAccelXoutH, b, 14)) return false;
  acc[0] = (int16_t)((b[0] << 8) | b[1]);
  acc[1] = (int16_t)((b[2] << 8) | b[3]);
  acc[2] = (int16_t)((b[4] << 8) | b[5]);
  // b[6..7] = temperature, unused.
  gyro[0] = (int16_t)((b[8] << 8) | b[9]);
  gyro[1] = (int16_t)((b[10] << 8) | b[11]);
  gyro[2] = (int16_t)((b[12] << 8) | b[13]);
  return true;
}

bool Mpu9250::readMag(float uT[3]) {
  if (!magOk_) return false;
  uint8_t st1 = 0;
  if (!readBytes(kMagAddr, kMagRegSt1, &st1, 1) || (st1 & 0x01) == 0) return false;
  uint8_t b[7];  // HXL..HZH + ST2 (reading ST2 completes the sample)
  if (!readBytes(kMagAddr, kMagRegHxl, b, 7)) return false;
  if ((b[6] & 0x08) != 0) return false;  // HOFL: magnetic overflow, discard
  // AK8963 data is little-endian (unlike the MPU registers).
  const int16_t x = (int16_t)((b[1] << 8) | b[0]);
  const int16_t y = (int16_t)((b[3] << 8) | b[2]);
  const int16_t z = (int16_t)((b[5] << 8) | b[4]);
  uT[0] = (float)x * kMagUtPerLsb * magAdj_[0];
  uT[1] = (float)y * kMagUtPerLsb * magAdj_[1];
  uT[2] = (float)z * kMagUtPerLsb * magAdj_[2];
  return true;
}

}  // namespace gaia
