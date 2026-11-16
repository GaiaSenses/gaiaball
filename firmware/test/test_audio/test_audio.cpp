#include <unity.h>
#include <cmath>
#include <vector>
#include "../../Gaiaball/src/logic/AudioBlock.h"
#include "../../Gaiaball/src/logic/AudioDecimator.h"
#include "../../Gaiaball/src/logic/AudioFeatures.h"
#include "../../Gaiaball/src/logic/BreathDetector.h"
#include "../../Gaiaball/src/logic/GaiaConfig.h"

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

// Adaptive steps are 15 ms (3 blocks of 5 ms), the real cadence of the adaptive mode.
static uint32_t t = 0;
static BreathParams adaptiveParams(float thr, float fastA, float slowA, uint32_t debounce,
                                   uint8_t rearmDb = 3) {
  BreathParams p;
  p.mode = BreathMode::Adaptive;
  p.thresholdCounts = thr;
  p.rearmDb = rearmDb;
  p.debounceMs = debounce;
  p.fastAlpha = fastA;
  p.slowAlpha = slowA;
  return p;
}
static bool step(BreathDetector &d, float level, uint32_t dtMs = 15) {
  t += dtMs;
  const float lv[2] = {level, level};
  return d.update(lv, t);
}

void test_adaptive_fires_and_debounces() {
  BreathDetector d;
  d.configure(adaptiveParams(1000.f, 0.5f, 0.99f, 300));
  t = 0;
  for (int i = 0; i < 50; i++) TEST_ASSERT_FALSE(step(d, 100.f));
  int fires = 0;
  for (int i = 0; i < 5; i++) fires += step(d, 5000.f);  // burst 1
  TEST_ASSERT_EQUAL(1, fires);
  for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(step(d, 100.f));  // drop, re-armed
  fires = 0;
  for (int i = 0; i < 5; i++) fires += step(d, 5000.f);  // burst 2 inside debounce
  TEST_ASSERT_EQUAL(0, fires);
  for (int i = 0; i < 30; i++) TEST_ASSERT_FALSE(step(d, 100.f));
  fires = 0;
  for (int i = 0; i < 5; i++) fires += step(d, 5000.f);  // burst 3, after debounce
  TEST_ASSERT_EQUAL(1, fires);
}

void test_adaptive_rearm_db() {
  BreathDetector d;
  d.configure(adaptiveParams(1000.f, 1.0f, 1.0f, 0, 3));  // fast = level, slow frozen at first sample (0)
  t = 0;
  step(d, 0.f);
  TEST_ASSERT_TRUE(step(d, 2000.f));
  TEST_ASSERT_FALSE(step(d, 2000.f));  // still over: no refire
  TEST_ASSERT_FALSE(step(d, 800.f));   // below threshold but above 707.9
  TEST_ASSERT_FALSE(step(d, 2000.f));  // not re-armed yet
  TEST_ASSERT_FALSE(step(d, 500.f));   // below 707.9 -> re-arm
  TEST_ASSERT_TRUE(step(d, 2000.f));
}

void test_adaptive_baseline_adapts() {
  BreathDetector d;
  d.configure(adaptiveParams(1000.f, 0.25f, 0.99f, 300));
  t = 0;
  int fires = 0;
  // Noise floor drifts 100 -> 3000 over 3000 steps (~1 unit/step); threshold 1000.
  for (int i = 0; i < 3000; i++) fires += step(d, 100.f + i);
  TEST_ASSERT_EQUAL(0, fires);
  TEST_ASSERT_TRUE(d.triggerLevel() > 2500.f + 1000.f);
  fires = 0;
  for (int i = 0; i < 4; i++) fires += step(d, 3100.f + 6000.f);
  TEST_ASSERT_EQUAL(1, fires);
}

void test_debounce_wraparound() {
  BreathDetector d;
  d.configure(adaptiveParams(1000.f, 1.0f, 1.0f, 300));
  uint32_t n = 0xFFFFFF00u;
  const float lo[2] = {0.f, 0.f}, hi[2] = {5000.f, 5000.f};
  d.update(lo, n);
  TEST_ASSERT_TRUE(d.update(hi, n + 10));
  d.update(lo, n + 20);  // re-arm
  TEST_ASSERT_FALSE(d.update(hi, n + 100));
  d.update(lo, n + 150);
  // 0xFFFFFF00 + 400 wraps past zero; elapsed since fire = 390 ms >= 300.
  TEST_ASSERT_TRUE(d.update(hi, n + 400));
}

static BreathParams absParams(float thr, uint8_t rearmDb = 3, uint32_t debounce = 300) {
  BreathParams p;
  p.mode = BreathMode::Absolute;
  p.thresholdCounts = thr;
  p.rearmDb = rearmDb;
  p.debounceMs = debounce;
  return p;
}

void test_abs_fires_on_left() {
  BreathDetector d;
  d.configure(absParams(1000.f));
  const float lv[2] = {1200.f, 0.f};
  TEST_ASSERT_TRUE(d.update(lv, 1000));
}

void test_abs_fires_on_right() {
  BreathDetector d;
  d.configure(absParams(1000.f));
  const float lv[2] = {0.f, 1200.f};
  TEST_ASSERT_TRUE(d.update(lv, 1000));
}

void test_abs_rearm_at_configured_db() {
  BreathDetector d;
  d.configure(absParams(1000.f, 3, 0));
  const float hi[2] = {1200.f, 1200.f}, mid[2] = {800.f, 800.f}, low[2] = {700.f, 700.f};
  TEST_ASSERT_TRUE(d.update(hi, 1000));
  TEST_ASSERT_FALSE(d.update(mid, 1010));
  TEST_ASSERT_FALSE(d.update(hi, 1020));  // still disarmed
  TEST_ASSERT_FALSE(d.update(low, 1030));  // 700 < 707.9 re-arms
  TEST_ASSERT_TRUE(d.update(hi, 1040));
}

void test_abs_one_fire_per_long_breath() {
  BreathDetector d;
  d.configure(absParams(1000.f));
  const float hi[2] = {1500.f, 0.f};
  int fires = 0;
  for (int i = 0; i < 100; i++) {
    fires += d.update(hi, 1000 + 5 * i);
    TEST_ASSERT_TRUE(d.recognized());
  }
  TEST_ASSERT_EQUAL(1, fires);
  const float low[2] = {0.f, 0.f};
  d.update(low, 2000);
  TEST_ASSERT_FALSE(d.recognized());
}

void test_abs_debounce() {
  BreathDetector d;
  d.configure(absParams(1000.f, 3, 300));
  const float hi[2] = {1500.f, 1500.f}, low[2] = {0.f, 0.f};
  TEST_ASSERT_TRUE(d.update(hi, 1000));
  d.update(low, 1050);  // re-arm
  TEST_ASSERT_FALSE(d.update(hi, 1100));  // 100 ms after fire
  d.update(low, 1150);
  TEST_ASSERT_TRUE(d.update(hi, 1400));  // 400 ms after fire
}

void test_rearm_zero_db() {
  BreathDetector d;
  d.configure(absParams(1000.f, 0, 0));
  const float hi[2] = {1200.f, 1200.f}, under[2] = {999.f, 999.f};
  TEST_ASSERT_TRUE(d.update(hi, 1000));
  TEST_ASSERT_FALSE(d.update(under, 1010));  // re-arms immediately below thr
  TEST_ASSERT_TRUE(d.update(hi, 1020));
}

void test_trigger_level() {
  BreathDetector a;
  a.configure(absParams(1234.f));
  TEST_ASSERT_EQUAL_FLOAT(1234.f, a.triggerLevel());
  BreathDetector d;
  d.configure(adaptiveParams(800.f, 0.25f, 0.995f, 300));
  const float lv[2] = {5000.f, 5000.f};
  d.update(lv, 1000);  // seeds fast = slow = 5000
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 5800.f, d.triggerLevel());
}

void test_dbfs_to_counts() {
  TEST_ASSERT_FLOAT_WITHIN(99580.f * 0.005f, 99580.f, dbfsToRmsCounts(-35.5f));
  TEST_ASSERT_EQUAL_FLOAT(8388608.f, dbfsToPeakCounts(0.f));
  TEST_ASSERT_FLOAT_WITHIN(5931642.f * 0.001f, 5931642.f, dbfsToRmsCounts(0.f));
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.70795f, dbToAmplitudeRatio(3.f));
}

// --- RMS accumulator precision (float, not double) -------------------------
// A full-scale sine is the worst case for a float sum of squares: 256 terms of up to
// 2^46. If the accumulator is adequate the error stays far below the 0.01 dB
// resolution of the wire format (0.115 % in amplitude).
void test_rms_float_accumulator_precision() {
  const double amp = 8388607.0;  // 2^23 - 1
  auto b = makeBlock(amp, 0, amp, 0);
  StereoRms r = computeStereoRms(b.data(), b.size());
  const float expect = (float)(amp / std::sqrt(2.0));
  TEST_ASSERT_FLOAT_WITHIN(expect * 0.0005f, expect, r.left);
  TEST_ASSERT_FLOAT_WITHIN(expect * 0.0005f, expect, r.right);
}

// --- dBFS encoding ---------------------------------------------------------
void test_rms_dbfs_is_aes17() {
  // RMS of a full-scale sine reads 0.00 dBFS, not -3.01.
  TEST_ASSERT_EQUAL_INT16(0, rmsToDbfsX100(kAudioFullScale / std::sqrt(2.0f)));
  TEST_ASSERT_EQUAL_INT16(-2000, rmsToDbfsX100(kAudioFullScale / std::sqrt(2.0f) / 10.0f));
  TEST_ASSERT_EQUAL_INT16(-6000, rmsToDbfsX100(kAudioFullScale / std::sqrt(2.0f) / 1000.0f));
}

void test_peak_dbfs_is_amplitude() {
  TEST_ASSERT_EQUAL_INT16(0, peakToDbfsX100(kAudioFullScale));
  TEST_ASSERT_EQUAL_INT16(-602, peakToDbfsX100(kAudioFullScale / 2.0f));
  TEST_ASSERT_EQUAL_INT16(-6000, peakToDbfsX100(kAudioFullScale / 1000.0f));
}

void test_dbfs_floor_and_saturation() {
  TEST_ASSERT_EQUAL_INT16(kAudioDbFloorX100, rmsToDbfsX100(0.0f));
  TEST_ASSERT_EQUAL_INT16(kAudioDbFloorX100, rmsToDbfsX100(-5.0f));
  TEST_ASSERT_EQUAL_INT16(kAudioDbFloorX100, rmsToDbfsX100(NAN));
  TEST_ASSERT_EQUAL_INT16(kAudioDbFloorX100, peakToDbfsX100(INFINITY * 0.0f));
  TEST_ASSERT_EQUAL_INT16(kAudioDbFloorX100, rmsToDbfsX100(1e-9f));  // below the floor
  TEST_ASSERT_EQUAL_INT16(kAudioDbCeilX100, peakToDbfsX100(kAudioFullScale * 1e6f));
}

// --- sliding level window --------------------------------------------------
static void pushBlock(AudioLevelWindow &w, float ampL, float ampR, uint16_t frames) {
  const float sumSq[2] = {frames * ampL * ampL, frames * ampR * ampR};
  const float peak[2] = {ampL, ampR};
  w.push(sumSq, peak, frames);
}

void test_window_pools_blocks_into_one_rms() {
  AudioLevelWindow w;
  for (int i = 0; i < w.blocks(); i++) pushBlock(w, 100.f, 50.f, 80);
  AudioLevels l = w.levels();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.f, l.rms[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.f, l.rms[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.f, l.peak[0]);
}

void test_window_slides_oldest_block_out() {
  AudioLevelWindow w;
  for (int i = 0; i < w.blocks(); i++) pushBlock(w, 10.f, 10.f, 80);
  pushBlock(w, 0.f, 0.f, 80);  // one block past the span evicts the oldest
  AudioLevels l = w.levels();
  // (N-1) blocks at 10 + 1 silent block -> sqrt((N-1)/N) * 10
  const float n = (float)w.blocks();
  TEST_ASSERT_FLOAT_WITHIN(0.01f, sqrtf((n - 1.f) / n) * 10.f, l.rms[0]);
}

void test_window_peak_ages_out() {
  AudioLevelWindow w;
  pushBlock(w, 1000.f, 1000.f, 80);
  for (int i = 0; i < w.blocks() - 1; i++) pushBlock(w, 10.f, 10.f, 80);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.f, w.levels().peak[0]);  // still inside the window
  pushBlock(w, 10.f, 10.f, 80);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.f, w.levels().peak[0]);  // evicted
}

void test_window_empty_and_reset() {
  AudioLevelWindow w;
  AudioLevels l = w.levels();
  TEST_ASSERT_EQUAL_FLOAT(0.f, l.rms[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.f, l.peak[1]);
  pushBlock(w, 500.f, 500.f, 80);
  pushBlock(w, 500.f, 500.f, 0);  // empty block ignored, must not divide by zero
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.f, w.levels().rms[0]);
  w.reset();
  TEST_ASSERT_EQUAL_FLOAT(0.f, w.levels().rms[0]);
  TEST_ASSERT_EQUAL_FLOAT(0.f, w.levels().peak[0]);
}

// --- configurable span (TLV 0x2F) ------------------------------------------
void test_window_default_is_50ms() {
  AudioLevelWindow w;
  TEST_ASSERT_EQUAL_UINT8(10, w.blocks());
  TEST_ASSERT_EQUAL_UINT8(10, AudioLevelWindow::blocksForMs(GaiaConfig{}.audioWindowMs));
}

void test_window_ms_to_blocks_rounds_and_clamps() {
  TEST_ASSERT_EQUAL_UINT8(1, AudioLevelWindow::blocksForMs(0));
  TEST_ASSERT_EQUAL_UINT8(1, AudioLevelWindow::blocksForMs(5));
  TEST_ASSERT_EQUAL_UINT8(1, AudioLevelWindow::blocksForMs(7));   // 1.4 blocks
  TEST_ASSERT_EQUAL_UINT8(2, AudioLevelWindow::blocksForMs(8));   // 1.6 blocks
  TEST_ASSERT_EQUAL_UINT8(4, AudioLevelWindow::blocksForMs(20));  // the old fixed window
  TEST_ASSERT_EQUAL_UINT8(40, AudioLevelWindow::blocksForMs(200));
  TEST_ASSERT_EQUAL_UINT8(40, AudioLevelWindow::blocksForMs(65535));
}

void test_window_span_follows_set_blocks() {
  AudioLevelWindow w;
  w.setBlocks(4);
  pushBlock(w, 1000.f, 1000.f, 80);
  for (int i = 0; i < 3; i++) pushBlock(w, 10.f, 10.f, 80);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.f, w.levels().peak[0]);
  pushBlock(w, 10.f, 10.f, 80);  // 5th block: out of a 20 ms window
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.f, w.levels().peak[0]);

  w.setBlocks(40);  // a new span starts over
  TEST_ASSERT_EQUAL_FLOAT(0.f, w.levels().rms[0]);
  pushBlock(w, 1000.f, 1000.f, 80);
  for (int i = 0; i < 39; i++) pushBlock(w, 10.f, 10.f, 80);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.f, w.levels().peak[0]);  // still inside 200 ms
  pushBlock(w, 10.f, 10.f, 80);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.f, w.levels().peak[0]);

  w.setBlocks(0);  // clamped, never a zero-length ring
  TEST_ASSERT_EQUAL_UINT8(1, w.blocks());
  w.setBlocks(200);
  TEST_ASSERT_EQUAL_UINT8(AudioLevelWindow::kMaxBlocks, w.blocks());
}

void test_window_same_span_keeps_contents() {
  AudioLevelWindow w;
  pushBlock(w, 100.f, 100.f, 80);
  w.setBlocks(w.blocks());  // a config write that did not touch 0x2F
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.f, w.levels().rms[0]);
}

// --- decimator reset -------------------------------------------------------
// The raw stream is now switchable, so the decimator can be paused mid-chunk. On
// resume the stale half must be gone instead of being published as fresh audio.
void test_decimator_reset_drops_partial_chunk() {
  AudioDecimator d;
  // 8 feeds = 1 output sample; fill half a chunk with a recognizable value.
  for (size_t i = 0; i < AudioDecimator::kChunkSamples / 2; i++)
    for (int k = 0; k < AudioDecimator::kDecim; k++) TEST_ASSERT_FALSE(d.feed(256000.f));
  d.reset();
  bool ready = false;
  for (size_t i = 0; i < AudioDecimator::kChunkSamples; i++)
    for (int k = 0; k < AudioDecimator::kDecim; k++) ready = d.feed(0.0f);
  TEST_ASSERT_TRUE(ready);  // a full chunk is needed again after the reset
  for (size_t i = 0; i < AudioDecimator::kChunkSamples; i++)
    TEST_ASSERT_EQUAL_INT16(0, d.chunk()[i]);  // and it holds no pre-reset samples
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rms_dc_removal);
  RUN_TEST(test_rms_stereo_split);
  RUN_TEST(test_rms_degenerate);
  RUN_TEST(test_rms_odd_trailing_ignored);
  RUN_TEST(test_rms_float_accumulator_precision);
  RUN_TEST(test_rms_dbfs_is_aes17);
  RUN_TEST(test_peak_dbfs_is_amplitude);
  RUN_TEST(test_dbfs_floor_and_saturation);
  RUN_TEST(test_window_pools_blocks_into_one_rms);
  RUN_TEST(test_window_slides_oldest_block_out);
  RUN_TEST(test_window_peak_ages_out);
  RUN_TEST(test_window_empty_and_reset);
  RUN_TEST(test_window_default_is_50ms);
  RUN_TEST(test_window_ms_to_blocks_rounds_and_clamps);
  RUN_TEST(test_window_span_follows_set_blocks);
  RUN_TEST(test_window_same_span_keeps_contents);
  RUN_TEST(test_decimator_reset_drops_partial_chunk);
  RUN_TEST(test_adaptive_fires_and_debounces);
  RUN_TEST(test_adaptive_rearm_db);
  RUN_TEST(test_adaptive_baseline_adapts);
  RUN_TEST(test_debounce_wraparound);
  RUN_TEST(test_abs_fires_on_left);
  RUN_TEST(test_abs_fires_on_right);
  RUN_TEST(test_abs_rearm_at_configured_db);
  RUN_TEST(test_abs_one_fire_per_long_breath);
  RUN_TEST(test_abs_debounce);
  RUN_TEST(test_rearm_zero_db);
  RUN_TEST(test_trigger_level);
  RUN_TEST(test_dbfs_to_counts);
  return UNITY_END();
}
