#include "Icm20948.h"

#include <Arduino.h>

#include "../logic/ImuFrontEnd.h"
#include "Logger.h"

namespace gaia {

// SOURCE OF EVERY ADDRESS AND BIT FIELD BELOW
// This repository vendored the SparkFun ICM-20948 library until commit 57f237c; it is
// still in git history and it is what drove THIS chip in THIS project. Nothing here was
// guessed. The citations name the file as of 57f237c^:
//   R = firmware/Gaiaball/src/vendor/ICM_20948/util/ICM_20948_REGISTERS.h
//   E = .../util/ICM_20948_ENUMERATIONS.h          AR = .../util/AK09916_REGISTERS.h
//   C = .../util/ICM_20948_C.c (reference sequences)   CH = .../util/ICM_20948_C.h
//   W = firmware/Gaiaball/src/vendor/ICM_20948/ICM_20948.cpp (Arduino wrapper)
// The bit POSITIONS come from the register structs in R, whose fields are declared
// LSB-first (GCC bitfield layout on a little-endian target), e.g. in
// ICM_20948_PWR_MGMT_1_t the first field CLKSEL occupies bits 2:0.
// The InvenSense datasheet DS-000189 remains the upstream authority; it was not read
// for this driver, so anything the vendored map does not pin is called out in the task
// report rather than invented.
namespace {
constexpr const char *kTag = "icm";

// Visible in every bank; bits 5:4 hold the bank number (C: ICM_20948_set_bank does
// `bank = (bank << 4) & 0x30`).                                                    [R]
constexpr uint8_t kRegBankSel = 0x7F;

// --- bank 0: identity, power, interrupts, data --------------------------------- [R]
constexpr uint8_t kB0WhoAmI = 0x00;      // expects 0xEA (CH: ICM_20948_WHOAMI)
constexpr uint8_t kB0UserCtrl = 0x03;    // bit5 I2C_MST_EN (ICM_20948_USER_CTRL_t)
constexpr uint8_t kB0PwrMgmt1 = 0x06;    // CLKSEL 2:0, TEMP_DIS 3, LP_EN 5, SLEEP 6,
                                         // DEVICE_RESET 7 (ICM_20948_PWR_MGMT_1_t)
constexpr uint8_t kB0PwrMgmt2 = 0x07;    // DISABLE_GYRO 2:0, DISABLE_ACCEL 5:3
constexpr uint8_t kB0IntPinCfg = 0x0F;   // bit1 BYPASS_EN (ICM_20948_INT_PIN_CFG_t)
constexpr uint8_t kB0IntEnable1 = 0x11;  // bit0 RAW_DATA_0_RDY_EN
constexpr uint8_t kB0IntStatus1 = 0x1A;  // bit0 RAW_DATA_0_RDY_INT, clear-on-read
constexpr uint8_t kB0AccelXoutH = 0x2D;  // 6 accel + 6 gyro bytes, contiguous, big-endian

// --- bank 2: rate and filter configuration ------------------------------------- [R]
constexpr uint8_t kB2GyroSmplrtDiv = 0x00;   // u8
constexpr uint8_t kB2GyroConfig1 = 0x01;     // FCHOICE 0, FS_SEL 2:1, DLPFCFG 5:3
constexpr uint8_t kB2AccelSmplrtDiv1 = 0x10; // bits 11:8 of the 12-bit divider
constexpr uint8_t kB2AccelSmplrtDiv2 = 0x11; // bits 7:0
constexpr uint8_t kB2AccelConfig = 0x14;     // FCHOICE 0, FS_SEL 2:1, DLPFCFG 5:3

constexpr uint8_t kWhoAmIExpected = 0xEA;                     // [CH:41]
constexpr uint8_t kPwrMgmt1Reset = 0x80;                      // DEVICE_RESET
// CLKSEL = 1 (ICM_20948_Clock_Auto: "auto select the best available") with SLEEP and
// LP_EN cleared, in one write. The chip wakes up with SLEEP set.            [E, C]
constexpr uint8_t kPwrMgmt1Run = 0x01;
constexpr uint32_t kResetSettleMs = 50;  // C/W: swReset() then delay(50)
constexpr uint8_t kAccelFs4g = 1;        // gpm4 [E]; 8192 LSB/g (W: getAccMG case 1)
constexpr uint8_t kFchoiceDlpfOn = 1;    // FCHOICE = 1 enables the DLPF    [C, E]

// --- AK09916 magnetometer, directly addressable at 0x0C in pass-through ----- [AR, CH]
constexpr uint8_t kMagAddr = 0x0C;      // CH:43 MAG_AK09916_I2C_ADDR
constexpr uint8_t kMagRegSt1 = 0x10;    // bit0 DRDY, bit1 DOR (AK09916_ST1_Reg_t)
constexpr uint8_t kMagRegWia1 = 0x00;   // expects 0x48; WIA2 (0x01) expects 0x09
constexpr uint8_t kMagRegCntl2 = 0x31;  // MODE 4:0
constexpr uint8_t kMagRegCntl3 = 0x32;  // bit0 SRST (self-clearing soft reset)
constexpr uint8_t kMagWia1Expected = 0x48;  // CH:44 MAG_AK09916_WHO_AM_I = 0x4809
constexpr uint8_t kMagWia2Expected = 0x09;  // the AK8963 also reports 0x48 in WIA1, so
                                            // only the pair tells the two chips apart
constexpr uint8_t kMagModeCont100Hz = 0x08;  // AK09916_mode_cont_100hz = 0x04 << 1 [E]
constexpr uint8_t kMagStatusDrdy = 0x01;     // ST1 bit0
constexpr uint8_t kMagStatusHofl = 0x08;     // ST2 bit3 (AK09916_ST2_Reg_t)
constexpr uint8_t kMagBurstLen = 9;          // ST1 + 6 data + dummy + ST2 (0x10..0x18)
constexpr float kMagUtPerLsb = 0.15f;        // FIXED, no ASA (W: getMagUT)

// Same codes and sensitivities as the MPU-9250 (W: getGyrDPS cases 0..3), which is why
// the stored gyroFsSel means the same thing on both chips.
constexpr float kGyroLsbPerDps[ImuDriver::kGyroFsCount] = {131.0f, 65.5f, 32.8f, 16.4f};
constexpr float kGyroRangeDps[ImuDriver::kGyroFsCount] = {250.0f, 500.0f, 1000.0f, 2000.0f};
}  // namespace

float Icm20948::dlpfCutoffHz() const { return dlpfCutoffHzFor(ImuChip::Icm20948, dlpfMode_); }

bool Icm20948::write8(uint8_t addr, uint8_t reg, uint8_t val) {
  wire_->beginTransmission(addr);
  wire_->write(reg);
  wire_->write(val);
  if (wire_->endTransmission() == 0) return true;
  errors_.fetch_add(1);
  return false;
}

bool Icm20948::readBytes(uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n) {
  wire_->beginTransmission(addr);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0 || wire_->requestFrom((int)addr, (int)n) != (int)n) {
    errors_.fetch_add(1);
    return false;
  }
  for (uint8_t i = 0; i < n; i++) out[i] = (uint8_t)wire_->read();
  return true;
}

bool Icm20948::selectBank(uint8_t bank) {
  if (bank == bank_) return true;  // keep redundant selections off the bus
  if (!write8(addr_, kRegBankSel, (uint8_t)((bank << 4) & 0x30))) {
    bank_ = 0xFF;  // the chip's bank is now unknown: never assume it stayed
    return false;
  }
  bank_ = bank;
  return true;
}

// Bank 2 must already be selected by the caller (one bank switch per operation).
bool Icm20948::writeGyroConfig1() {
  const uint8_t v = (uint8_t)((dlpfMode_ << 3) | (gyroFsSel_ << 1) | kFchoiceDlpfOn);
  return write8(addr_, kB2GyroConfig1, v);
}

// Accelerometer full scale is NOT configurable: ±4 g is the wire scale
// (ImuDriver::kAccelLsbPerG). Note the DLPF *index* is shared with the gyroscope on
// purpose (that is what FrontEndPlan means), but the two tables are not the same: at
// index 4 both are 23.9 Hz, while e.g. index 3 is 51.2 Hz for the gyro and 50.4 Hz for
// the accelerometer, and indices 0, 1, 2, 5 and 7 differ more (E:
// ICM_20948_ACCEL_CONFIG_DLPCFG_e vs ..._GYRO_CONFIG_1_DLPCFG_e). dlpfCutoffHz()
// reports the GYRO figure only.
bool Icm20948::writeAccelConfig() {
  const uint8_t v = (uint8_t)((dlpfMode_ << 3) | (kAccelFs4g << 1) | kFchoiceDlpfOn);
  return write8(addr_, kB2AccelConfig, v);
}

bool Icm20948::begin(TwoWire &wire, bool ad0) {
  wire_ = &wire;
  addr_ = ad0 ? 0x69 : 0x68;
  bank_ = 0xFF;  // a fresh probe knows nothing about the chip's current bank

  if (!selectBank(0)) return false;
  if (!readBytes(addr_, kB0WhoAmI, &whoAmI_, 1)) return false;
  if (whoAmI_ != kWhoAmIExpected) {
    GB_LOGE(kTag, "WHO_AM_I 0x%02X at 0x%02X: not an ICM-20948", whoAmI_, addr_);
    return false;
  }

  // Reference order (C/W: checkID, swReset, delay(50), sleep(false), lowPower(false)).
  if (!write8(addr_, kB0PwrMgmt1, kPwrMgmt1Reset)) return false;
  delay(kResetSettleMs);  // setup-time only
  bank_ = 0;              // DEVICE_RESET returns the chip to bank 0

  bool ok = true;
  ok &= write8(addr_, kB0PwrMgmt1, kPwrMgmt1Run);  // clock source, awake, not low-power
  ok &= write8(addr_, kB0PwrMgmt2, 0x00);          // all six axes on
  ok &= write8(addr_, kB0IntEnable1, 0x01);        // RAW_DATA_RDY latches INT_STATUS_1
  // Pass-through: the auxiliary bus is tied to SDA/SCL so the AK09916 answers at 0x0C
  // directly, and the chip's own I2C master must be off for that (C:
  // i2c_master_passthrough / i2c_master_enable). USER_CTRL = 0 is also the reset state;
  // written explicitly because a stale I2C_MST_EN would silently hide the magnetometer.
  ok &= write8(addr_, kB0UserCtrl, 0x00);
  ok &= write8(addr_, kB0IntPinCfg, 0x02);  // BYPASS_EN
  // Accelerometer full scale, with the DLPF defaults; setSampleRateHz() rewrites the
  // filter part, and the gyro range and rate arrive from the stored configuration.
  // Short-circuited on purpose: a configuration byte must never be written when the
  // bank switch before it failed, because bank 0 holds reserved registers at the same
  // addresses and what a write there does is not documented in anything available here.
  ok = ok && selectBank(2) && writeAccelConfig();
  ok = selectBank(0) && ok;  // bank 0 (where the data is) restored whatever happened
  if (!ok) return false;

  magOk_ = initMag();
  if (!magOk_) GB_LOGW(kTag, "AK09916 mag not available; Quat9 will fall back to 6-axis");
  GB_LOGI(kTag, "ICM-20948 ready (WHO_AM_I 0x%02X, mag %s)", whoAmI_, magOk_ ? "ok" : "off");
  return true;
}

// The AK09916 is NOT an AK8963: no fuse-ROM ASA registers (its sensitivity is fixed at
// 0.15 µT/LSB), and WIA1 alone cannot tell the two apart - both answer 0x48 there.
bool Icm20948::initMag() {
  // Soft reset first, as the reference does (W: startupMagnetometer calls resetMag()
  // before magWhoIAm()). It also leaves the chip in power-down, which is the state a
  // mode change has to start from. SRST clears itself.
  // Retried because the reference warns the magnetometer may not answer immediately
  // after an ICM reset ("After a ICM reset the Mag sensor may stop responding"); it
  // recovers by resetting the chip's I2C master, which pass-through does not use, so
  // the equivalent here is simply to ask again. Setup-time only.
  uint8_t wia[2] = {0, 0};  // WIA1 (0x00) then WIA2 (0x01), one transaction
  bool identified = false;
  for (uint8_t attempt = 0; attempt < 3 && !identified; ++attempt) {
    if (!write8(kMagAddr, kMagRegCntl3, 0x01)) {
      delay(10);
      continue;
    }
    delay(10);
    if (!readBytes(kMagAddr, kMagRegWia1, wia, 2)) continue;
    identified = wia[0] == kMagWia1Expected && wia[1] == kMagWia2Expected;
  }
  if (!identified) {
    GB_LOGW(kTag, "mag WIA 0x%02X 0x%02X: not an AK09916", wia[0], wia[1]);
    return false;
  }
  // Continuous measurement mode 4 (100 Hz). The IMU can be polled faster than that;
  // readMag() then simply reports "no new data" and ImuService keeps its last value.
  return write8(kMagAddr, kMagRegCntl2, kMagModeCont100Hz);
}

bool Icm20948::setGyroFullScale(uint8_t fsSel) {
  if (fsSel >= kGyroFsCount) fsSel = kGyroFsCount - 1;
  const uint8_t previous = gyroFsSel_;
  gyroFsSel_ = fsSel;
  if (!selectBank(2) || !writeGyroConfig1()) {
    gyroFsSel_ = previous;  // the shadow must never claim a range the chip is not in
    selectBank(0);
    return false;
  }
  // The register write succeeded: the chip IS in the new range. Publish the matching
  // sensitivity now, before the fallible bank restore, so it can never describe a range
  // the chip is not in (ImuService divides every gyro reading by it).
  gyroLsbPerDps_ = kGyroLsbPerDps[fsSel];
  gyroRangeDps_ = kGyroRangeDps[fsSel];
  if (!selectBank(0)) return false;  // bank_ is already marked unknown; readers re-select
  GB_LOGI(kTag, "gyro range ±%d dps (%.1f LSB/dps)", (int)gyroRangeDps_, gyroLsbPerDps_);
  return true;
}

bool Icm20948::setSampleRateHz(uint8_t hz, uint8_t dlpfMode) {
  // All the arithmetic (auto DLPF choice, both dividers, the 1125 Hz base) lives in
  // logic/ImuFrontEnd.h, where the host tests can reach it.
  const FrontEndPlan plan = planFrontEnd(ImuChip::Icm20948, hz, dlpfMode);
  const uint8_t previous = dlpfMode_;
  dlpfMode_ = plan.dlpfMode;

  bool ok = selectBank(2);
  // Gyroscope: 8-bit divider. Accelerometer: its own 12-bit divider across two
  // registers, high nibble first (C: ICM_20948_set_sample_rate).
  ok = ok && write8(addr_, kB2GyroSmplrtDiv, (uint8_t)(plan.gyroDiv & 0xFF));
  ok = ok && write8(addr_, kB2AccelSmplrtDiv1, (uint8_t)((plan.accelDiv >> 8) & 0x0F));
  ok = ok && write8(addr_, kB2AccelSmplrtDiv2, (uint8_t)(plan.accelDiv & 0xFF));
  ok = ok && writeGyroConfig1();
  ok = ok && writeAccelConfig();
  const bool bankOk = selectBank(0);  // data lives in bank 0: always go back
  // Only a failed register write rolls the shadow back. If every write landed but the
  // bank restore failed, the chip already runs the new filter and the shadow must say so
  // (writeGyroConfig1() reuses dlpfMode_ on the next gyro range change).
  if (!ok) {
    dlpfMode_ = previous;
    return false;
  }
  if (!bankOk) return false;
  GB_LOGI(kTag, "rate %u Hz, DLPF %.0f Hz (mode %u%s), div g=%u a=%u",
          (unsigned)(hz < 4 ? 4 : hz), plan.cutoffHz, (unsigned)plan.dlpfMode,
          dlpfMode == 0 ? ", auto" : "", (unsigned)plan.gyroDiv, (unsigned)plan.accelDiv);
  return true;
}

bool Icm20948::dataReady() {
  uint8_t st = 0;
  if (!selectBank(0)) return false;
  if (!readBytes(addr_, kB0IntStatus1, &st, 1)) return false;
  return (st & 0x01) != 0;
}

bool Icm20948::readAccelGyro(int16_t acc[3], int16_t gyro[3]) {
  uint8_t b[12];
  if (!selectBank(0)) return false;
  // Accelerometer and gyroscope are contiguous here, with no temperature in between
  // (unlike the MPU-9250): ACCEL_XOUT_H..GYRO_ZOUT_L, big-endian.                 [R]
  if (!readBytes(addr_, kB0AccelXoutH, b, 12)) return false;
  acc[0] = (int16_t)((b[0] << 8) | b[1]);
  acc[1] = (int16_t)((b[2] << 8) | b[3]);
  acc[2] = (int16_t)((b[4] << 8) | b[5]);
  gyro[0] = (int16_t)((b[6] << 8) | b[7]);
  gyro[1] = (int16_t)((b[8] << 8) | b[9]);
  gyro[2] = (int16_t)((b[10] << 8) | b[11]);
  return true;
}

bool Icm20948::readMag(float uT[3]) {
  if (!magOk_) return false;
  // One burst, ST1 through ST2, which is exactly what the chip's own I2C master is
  // configured to fetch in the reference implementation (W: 9 bytes from ST1 - "the six
  // magnetometer readings (little endian). After a dummy byte, the STATUS2 register
  // (0x18) contains the HOFL (Overflow) bit"). Reading ST2 is what COMPLETES the
  // measurement: without it the data registers never update again.
  // No bank selection here - the magnetometer is a separate I2C address, not a bank.
  uint8_t b[kMagBurstLen];
  if (!readBytes(kMagAddr, kMagRegSt1, b, kMagBurstLen)) return false;
  if ((b[0] & kMagStatusDrdy) == 0) return false;  // no new measurement
  if ((b[8] & kMagStatusHofl) != 0) return false;  // magnetic overflow, discard
  // AK09916 data is little-endian (unlike the ICM's own registers).
  const int16_t x = (int16_t)((b[2] << 8) | b[1]);
  const int16_t y = (int16_t)((b[4] << 8) | b[3]);
  const int16_t z = (int16_t)((b[6] << 8) | b[5]);
  // NATIVE AK09916 frame, per the ImuDriver contract: ImuService applies the
  // configured axis permutation (logic/MagAxisRemap.h).
  uT[0] = (float)x * kMagUtPerLsb;
  uT[1] = (float)y * kMagUtPerLsb;
  uT[2] = (float)z * kMagUtPerLsb;
  return true;
}

}  // namespace gaia
