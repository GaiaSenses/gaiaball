#pragma once
#include <stdint.h>

namespace gaia {

// Which inertial chip is on the I2C bus. The physical ball carries an ICM-20948; units
// that ship later carry the GY-91 (MPU-9250). The firmware drives both.
enum class ImuChip : uint8_t { None = 0, Icm20948 = 1, Mpu9250 = 2 };

// Rate and DLPF arithmetic lives here, not in the drivers, so the host-side `native`
// tests can compile it: nothing under services/ is host-testable. A wrong cut-off
// silently aliases real motion into the signal and is very hard to spot later.
struct FrontEndPlan {
  uint8_t dlpfMode;    // register index, 0..7
  float cutoffHz;      // effective cut-off of the chosen filter (gyro table)
  uint16_t gyroDiv;    // gyroscope sample-rate divider
  uint16_t accelDiv;   // ICM: own accelerometer divider; MPU: same as gyroDiv
  float actualHz;      // output rate the dividers really produce
};

// `forcedDlpfMode` 0 or > 7 means automatic: the widest DLPF whose cut-off is
// <= 0.47 * rate (0.47, not 0.5, leaves a guard band for the filter's transition
// region), falling back to index 6 (the narrowest) at the lowest rates. Rates below
// 4 Hz are raised to 4. Decimation base: 1000 Hz (MPU-9250), 1125 Hz (ICM-20948).
FrontEndPlan planFrontEnd(ImuChip chip, uint8_t requestedHz, uint8_t forcedDlpfMode);

// Cut-off of DLPF index `mode & 7` for the chip. Named ...For so it cannot be hidden by
// the virtual member ImuDriver::dlpfCutoffHz() inside the driver classes.
float dlpfCutoffHzFor(ImuChip chip, uint8_t mode);

}  // namespace gaia
