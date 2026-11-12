#include "ImuFrontEnd.h"

namespace gaia {

namespace {
// MPU-9250 gyro DLPF_CFG cut-offs (FCHOICE_B = 0); index 7 (3600 Hz) is never automatic.
constexpr float kMpuDlpfHz[8] = {250.0f, 184.0f, 92.0f, 41.0f, 20.0f, 10.0f, 5.0f, 3600.0f};
// ICM-20948 GYRO_CONFIG_1.GYRO_DLPFCFG 3 dB bandwidths (GYRO_FCHOICE = 1). Checked
// against the 3 dB figures encoded in the enumerator names of the SparkFun ICM-20948
// library this repo vendored until 57f237c (ICM_20948_GYRO_CONFIG_1_DLPCFG_e, e.g.
// gyr_d196bw6 = 196.6 Hz), not against the InvenSense datasheet (DS-000189) itself,
// which remains the upstream authority. Index 7 (361.4 Hz) is never automatic.
constexpr float kIcmDlpfHz[8] = {196.6f, 151.8f, 119.5f, 51.2f, 23.9f, 11.6f, 5.7f, 361.4f};

constexpr float kMpuBaseHz = 1000.0f;
constexpr float kIcmBaseHz = 1125.0f;

const float *table(ImuChip chip) { return chip == ImuChip::Icm20948 ? kIcmDlpfHz : kMpuDlpfHz; }

uint8_t autoDlpfMode(ImuChip chip, uint8_t hz) {
  const float *t = table(chip);
  const float limit = 0.47f * (float)hz;
  for (uint8_t m = 0; m <= 6; ++m)
    if (t[m] <= limit) return m;
  return 6;  // the narrowest available, for the lowest output rates
}

// Rounded divider: (base + hz/2) / hz - 1, floored at 0 and capped at the register width.
uint16_t divider(uint16_t base, uint8_t hz, uint16_t maxDiv) {
  uint16_t div = (uint16_t)((base + hz / 2) / hz);
  if (div > 0) div -= 1;
  if (div > maxDiv) div = maxDiv;
  return div;
}
}  // namespace

float dlpfCutoffHzFor(ImuChip chip, uint8_t mode) { return table(chip)[mode & 0x07]; }

FrontEndPlan planFrontEnd(ImuChip chip, uint8_t requestedHz, uint8_t forcedDlpfMode) {
  if (requestedHz < 4) requestedHz = 4;
  FrontEndPlan p{};
  p.dlpfMode = (forcedDlpfMode == 0 || forcedDlpfMode > 7) ? autoDlpfMode(chip, requestedHz)
                                                           : forcedDlpfMode;
  p.cutoffHz = dlpfCutoffHzFor(chip, p.dlpfMode);
  if (chip == ImuChip::Icm20948) {
    p.gyroDiv = divider(1125, requestedHz, 255);     // GYRO_SMPLRT_DIV: 8 bits
    p.accelDiv = divider(1125, requestedHz, 4095);   // ACCEL_SMPLRT_DIV: 12 bits
    p.actualHz = kIcmBaseHz / (1.0f + (float)p.gyroDiv);
  } else {
    p.gyroDiv = divider(1000, requestedHz, 255);
    p.accelDiv = p.gyroDiv;
    p.actualHz = kMpuBaseHz / (1.0f + (float)p.gyroDiv);
  }
  return p;
}

}  // namespace gaia
