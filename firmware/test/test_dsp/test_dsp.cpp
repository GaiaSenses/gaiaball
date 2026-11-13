#include <unity.h>
#include <cmath>
#include "../../Gaiaball/src/logic/AudioDecimator.h"
#include "../../Gaiaball/src/logic/Biquad.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static constexpr float kFs = 16000.0f;

// Steady-state RMS gain of a biquad for a sine at `hz` (after settling).
static float sineGain(Biquad &f, float hz) {
  const int settle = 16000, measure = 16000;
  double acc = 0.0;
  for (int i = 0; i < settle + measure; i++) {
    const float x = sinf(2.0f * (float)M_PI * hz * (float)i / kFs);
    const float y = f.process(x);
    if (i >= settle) acc += (double)y * y;
  }
  const float rmsOut = sqrtf((float)(acc / measure));
  return rmsOut / 0.70710678f;  // input RMS of a unit sine
}

void test_highpass_blocks_dc_passes_band() {
  Biquad hp;
  hp.configure(Biquad::Type::HighPass, 10.0f, 0.707f, kFs);
  // DC: step input decays to ~0.
  float y = 0;
  for (int i = 0; i < 32000; i++) y = hp.process(1000.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, y);
  hp.configure(Biquad::Type::HighPass, 10.0f, 0.707f, kFs);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, sineGain(hp, 100.0f));  // well above cutoff
}

void test_lowpass_passes_band_attenuates_high() {
  Biquad lp;
  lp.configure(Biquad::Type::LowPass, 200.0f, 0.707f, kFs);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, sineGain(lp, 50.0f));
  lp.configure(Biquad::Type::LowPass, 200.0f, 0.707f, kFs);
  const float g2k = sineGain(lp, 2000.0f);
  TEST_ASSERT_TRUE_MESSAGE(g2k < 0.05f, "2 kHz should be > 26 dB down");
}

void test_high_q_peaks_at_cutoff_and_stays_stable() {
  Biquad lp;
  lp.configure(Biquad::Type::LowPass, 200.0f, 5.0f, kFs);
  const float gPeak = sineGain(lp, 200.0f);
  // Q=5 -> ~+14 dB resonant peak at the cutoff (the documented Q trade-off).
  TEST_ASSERT_TRUE(gPeak > 3.0f && gPeak < 8.0f);
  TEST_ASSERT_TRUE(std::isfinite(gPeak));
}

void test_decimator_chunk_cadence_and_rate() {
  AudioDecimator d;
  int chunks = 0;
  const int inPerChunk = AudioDecimator::kDecim * (int)AudioDecimator::kChunkSamples;  // 768
  for (int i = 0; i < inPerChunk * 3; i++)
    if (d.feed(1000.0f)) chunks++;
  TEST_ASSERT_EQUAL(3, chunks);
  TEST_ASSERT_EQUAL(2000, AudioDecimator::kOutputRateHz);
}

void test_decimator_scaling_and_saturation() {
  AudioDecimator d;
  // Constant 24-bit value 256000 -> int16 1000 after /256.
  int16_t got = 0;
  for (int i = 0; i < 8 * 96; i++)
    if (d.feed(256000.0f)) got = d.chunk()[0];
  TEST_ASSERT_EQUAL_INT16(1000, got);
  // Full-scale 24-bit (2^23) -> clamps at int16 max.
  AudioDecimator d2;
  int16_t sat = 0;
  for (int i = 0; i < 8 * 96; i++)
    if (d2.feed(8388607.0f)) sat = d2.chunk()[0];
  TEST_ASSERT_EQUAL_INT16(32767, sat);
}

void test_decimator_preserves_low_frequency_sine() {
  AudioDecimator d;
  // 100 Hz sine, amplitude 25600 (24-bit counts) -> int16 amplitude ~100.
  int16_t minV = 0, maxV = 0;
  bool any = false;
  for (int i = 0; i < 8 * 96 * 4; i++) {
    const float s = 25600.0f * sinf(2.0f * (float)M_PI * 100.0f * (float)i / kFs);
    if (d.feed(s)) {
      for (size_t k = 0; k < AudioDecimator::kChunkSamples; k++) {
        const int16_t v = d.chunk()[k];
        if (!any || v < minV) minV = v;
        if (!any || v > maxV) maxV = v;
        any = true;
      }
    }
  }
  TEST_ASSERT_TRUE(any);
  TEST_ASSERT_INT_WITHIN(5, 100, maxV);
  TEST_ASSERT_INT_WITHIN(5, -100, minV);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_highpass_blocks_dc_passes_band);
  RUN_TEST(test_lowpass_passes_band_attenuates_high);
  RUN_TEST(test_high_q_peaks_at_cutoff_and_stays_stable);
  RUN_TEST(test_decimator_chunk_cadence_and_rate);
  RUN_TEST(test_decimator_scaling_and_saturation);
  RUN_TEST(test_decimator_preserves_low_frequency_sine);
  return UNITY_END();
}
