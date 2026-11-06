#include <unity.h>
#include <cmath>
#include <vector>
#include "../../Gaiaball/src/logic/AudioBlock.h"
#include "../../Gaiaball/src/logic/PeakDetector.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

// Interleaved stereo block of 256 frames; 8 whole cycles per block (exact RMS = A/sqrt2).
// Values are 24-bit sample units, stored left-aligned (<<8) like the INMP441 DMA data.
static std::vector<int32_t> makeBlock(double ampL, double offL, double ampR, double offR) {
  const int frames = 256;
  std::vector<int32_t> v(frames * 2);
  for (int i = 0; i < frames; i++) {
    double ph = 2.0 * M_PI * 8.0 * i / frames;
    v[2 * i] = (int32_t)std::lround(offL + ampL * std::sin(ph)) * 256;
    v[2 * i + 1] = (int32_t)std::lround(offR + ampR * std::sin(ph)) * 256;
  }
  return v;
}

void test_rms_dc_removal() {
  auto b = makeBlock(10000, 200000, 10000, -300000);
  StereoRms r = computeStereoRms(b.data(), b.size());
  const float expect = 10000.f / std::sqrt(2.f);
  TEST_ASSERT_FLOAT_WITHIN(expect * 0.01f, expect, r.left);
  TEST_ASSERT_FLOAT_WITHIN(expect * 0.01f, expect, r.right);
}

void test_rms_stereo_split() {
  auto b = makeBlock(2000, 0, 8000, 5000);
  StereoRms r = computeStereoRms(b.data(), b.size());
  TEST_ASSERT_FLOAT_WITHIN(2000.f / 1.41421f * 0.01f, 2000.f / 1.41421f, r.left);
  TEST_ASSERT_FLOAT_WITHIN(8000.f / 1.41421f * 0.01f, 8000.f / 1.41421f, r.right);
}

void test_rms_degenerate() {
  int32_t d[3] = {1 << 8, 2 << 8, 3 << 8};
  StereoRms r = computeStereoRms(d, 3);  // < 4 samples
  TEST_ASSERT_EQUAL_FLOAT(0.f, r.left);
  TEST_ASSERT_EQUAL_FLOAT(0.f, r.right);
  r = computeStereoRms(nullptr, 0);
  TEST_ASSERT_EQUAL_FLOAT(0.f, r.left);
  // Constant signal (pure DC) -> zero RMS.
  std::vector<int32_t> dc(64, 123456 * 256);
  r = computeStereoRms(dc.data(), dc.size());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, r.left);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, r.right);
}

void test_rms_odd_trailing_ignored() {
  auto b = makeBlock(4000, 0, 4000, 0);
  b.push_back(0x7FFFFF00);  // stray odd sample must not affect the result
  StereoRms r = computeStereoRms(b.data(), b.size());
  TEST_ASSERT_FLOAT_WITHIN(4000.f / 1.41421f * 0.01f, 4000.f / 1.41421f, r.left);
}

// Steps are 16 ms (one 256-frame block at 16 kHz).
static uint32_t t = 0;
static bool step(PeakDetector &d, float rms, float thr) {
  t += 16;
  return d.update(rms, thr, t);
}

void test_peak_fires_and_debounces() {
  PeakDetector d;
  d.configure(0.5f, 0.99f, 300);
  t = 0;
  for (int i = 0; i < 50; i++) TEST_ASSERT_FALSE(step(d, 100.f, 1000.f));
  int fires = 0;
  for (int i = 0; i < 5; i++) fires += step(d, 5000.f, 1000.f);  // burst 1
  TEST_ASSERT_EQUAL(1, fires);
  for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(step(d, 100.f, 1000.f));  // drop, re-armed
  fires = 0;
  for (int i = 0; i < 5; i++) fires += step(d, 5000.f, 1000.f);  // burst 2 at ~+160 ms
  TEST_ASSERT_EQUAL(0, fires);                                   // inside debounce
  for (int i = 0; i < 30; i++) TEST_ASSERT_FALSE(step(d, 100.f, 1000.f));
  fires = 0;
  for (int i = 0; i < 5; i++) fires += step(d, 5000.f, 1000.f);  // burst 3, after debounce
  TEST_ASSERT_EQUAL(1, fires);
}

void test_hysteresis_rearm() {
  PeakDetector d;
  d.configure(1.0f, 1.0f, 0);  // fast = rms, slow frozen at first sample (0)
  t = 0;
  step(d, 0.f, 1000.f);
  TEST_ASSERT_TRUE(step(d, 2000.f, 1000.f));
  TEST_ASSERT_FALSE(step(d, 2000.f, 1000.f));  // still over: no refire
  TEST_ASSERT_FALSE(step(d, 800.f, 1000.f));   // below threshold but above 70%
  TEST_ASSERT_FALSE(step(d, 2000.f, 1000.f));  // not re-armed yet
  TEST_ASSERT_FALSE(step(d, 500.f, 1000.f));   // below 70% -> re-arm
  TEST_ASSERT_TRUE(step(d, 2000.f, 1000.f));
}

void test_baseline_adapts() {
  PeakDetector d;
  d.configure(0.25f, 0.99f, 300);
  t = 0;
  int fires = 0;
  // Noise floor drifts 100 -> 3000 over 3000 blocks (~1 unit/block); threshold 1000.
  for (int i = 0; i < 3000; i++) fires += step(d, 100.f + i, 1000.f);
  TEST_ASSERT_EQUAL(0, fires);
  TEST_ASSERT_TRUE(d.slow() > 2500.f);
  // A real burst on top of the new floor still fires.
  fires = 0;
  for (int i = 0; i < 4; i++) fires += step(d, 3100.f + 6000.f, 1000.f);
  TEST_ASSERT_EQUAL(1, fires);
}

void test_debounce_wraparound() {
  PeakDetector d;
  d.configure(1.0f, 1.0f, 300);
  uint32_t n = 0xFFFFFF00u;
  d.update(0.f, 1000.f, n);
  TEST_ASSERT_TRUE(d.update(5000.f, 1000.f, n + 10));
  d.update(0.f, 1000.f, n + 20);  // re-arm
  TEST_ASSERT_FALSE(d.update(5000.f, 1000.f, n + 100));  // 90 ms later, across no wrap yet
  d.update(0.f, 1000.f, n + 150);
  // 0xFFFFFF00 + 400 wraps past zero; elapsed since fire = 390 ms >= 300.
  TEST_ASSERT_TRUE(d.update(5000.f, 1000.f, n + 400));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rms_dc_removal);
  RUN_TEST(test_rms_stereo_split);
  RUN_TEST(test_rms_degenerate);
  RUN_TEST(test_rms_odd_trailing_ignored);
  RUN_TEST(test_peak_fires_and_debounces);
  RUN_TEST(test_hysteresis_rearm);
  RUN_TEST(test_baseline_adapts);
  RUN_TEST(test_debounce_wraparound);
  return UNITY_END();
}
