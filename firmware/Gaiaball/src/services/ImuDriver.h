#pragma once
#include <Wire.h>
#include <stdint.h>

namespace gaia {

// What ImuService needs from an inertial chip, and nothing more.
//
// WHY an interface: the ball in hand carries a GY-ICM20948V2 (ICM-20948) while the
// units that ship later carry the GY-91 (MPU-9250). One binary has to drive both, with
// the chip picked from the stored configuration at boot (ImuService::begin), so the
// choice cannot be a compile-time one. The interface is deliberately the shape the
// MPU-9250 driver already had: it is the extraction of a driver that works, not a new
// abstraction invented on top of one.
//
// WHY it is this small: everything that can be computed instead of programmed lives
// outside the drivers, in logic/ImuFrontEnd.h (rate and DLPF arithmetic) and
// logic/MagAxisRemap.h (magnetometer axis order). Nothing under services/ is
// host-testable - the `native` environment compiles only src/logic/ - so a driver must
// hold only what genuinely touches registers.
//
// Threading: single-task use (the IMU task) after begin(). errorCount() is the one
// exception: the diagnostics path reads it about once a second from the App task, so
// implementations keep that counter atomic.
class ImuDriver {
 public:
  // FIXED by the wire protocol: ImuPacket carries accelerometer counts and the site
  // and the Pure Data object divide by this number. ±4 g exists on both chips with the
  // same selector code, so both drivers program ±4 g and neither one rescales.
  static constexpr float kAccelLsbPerG = 8192.0f;
  // Gyroscope full-scale selector count: ±250/500/1000/2000 dps, in that order and
  // with the same FS_SEL codes on both chips, so the stored gyroFsSel is chip-agnostic.
  static constexpr uint8_t kGyroFsCount = 4;

  virtual ~ImuDriver() = default;

  // Probes the chip's identity register and programs the part of the front end that
  // never changes (power, clock, ±4 g, data-ready, magnetometer). Returns false - and
  // counts the failed transaction - when the chip does not answer or answers with the
  // wrong identity; ImuService retries. Range, rate and DLPF are NOT programmed here:
  // begin() callers follow up with setGyroFullScale()/setSampleRateHz() carrying the
  // stored configuration.
  virtual bool begin(TwoWire &wire, bool ad0) = 0;

  // Output rate and anti-alias filter in ONE call, because they belong together: the
  // sample-rate divider only DECIMATES the chip's internal rate (no averaging), so
  // anything above half the output rate that the DLPF lets through is folded back into
  // the band and is indistinguishable from real motion afterwards. `dlpfMode` 0 (or
  // > 7) means automatic; see planFrontEnd(), which both drivers delegate to.
  virtual bool setSampleRateHz(uint8_t hz, uint8_t dlpfMode) = 0;
  // Writes the gyroscope FS_SEL field (0..kGyroFsCount-1). Changes
  // gyroLsbPerDps()/gyroRangeDps().
  virtual bool setGyroFullScale(uint8_t fsSel) = 0;

  // True when a new accel/gyro sample is ready. Both chips expose this as a
  // clear-on-read interrupt-status bit, which is why ImuService can poll it (the INT
  // pin is not routed on the board) without ever missing a sample.
  virtual bool dataReady() = 0;
  // Accelerometer and gyroscope counts, accel in kAccelLsbPerG units.
  virtual bool readAccelGyro(int16_t acc[3], int16_t gyro[3]) = 0;
  // Latest magnetometer reading in µT, in the magnetometer's NATIVE frame. Returns
  // false when no new data is ready or the sample overflowed; the caller keeps its
  // previous value. The axis remap into the gyro/accel frame is ImuService's job
  // (applyMagRemap with the configured index), not the driver's: the right mapping is
  // a property of how the chip sits on the board, which only the operator can know.
  virtual bool readMag(float uT[3]) = 0;

  virtual bool magOk() const = 0;
  virtual float gyroLsbPerDps() const = 0;
  // Full-scale magnitude in dps, for the AHRS overrange detection.
  virtual float gyroRangeDps() const = 0;
  // Effective GYROSCOPE DLPF cut-off in Hz (what the auto mode actually chose). The
  // accelerometer gets the same DLPF index, but not necessarily the same cut-off.
  virtual float dlpfCutoffHz() const = 0;
  virtual uint8_t dlpfMode() const = 0;
  // Whatever the identity register returned, for the diagnostics log line.
  virtual uint8_t whoAmI() const = 0;
  // I2C transaction failures since boot. Readable from any task.
  virtual uint32_t errorCount() const = 0;
};

}  // namespace gaia
