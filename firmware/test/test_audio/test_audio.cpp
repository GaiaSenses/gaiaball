#include <unity.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include "../../Gaiaball/src/logic/AudioDecimator.h"
#include "../../Gaiaball/src/logic/AudioFeatures.h"
#include "../../Gaiaball/src/logic/BreathDetector.h"
#include "../../Gaiaball/src/logic/GaiaConfig.h"
#include "../../Gaiaball/src/logic/StillnessDetector.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

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

// --- Breath discriminators (v10) -------------------------------------------------

static bool absStep(BreathDetector &d, float level, uint32_t now, float coherence = 0.0f,
                    bool moving = false) {
  const float lv[2] = {level, level};
  BreathGate g;
  g.coherence = coherence;
  g.moving = moving;
  return d.update(lv, now, g);
}

// A loud sound that reaches both microphones alike (coherence above the limit) never fires;
// the same level as independent noise at each microphone does.
void test_coherence_blocks_a_far_sound() {
  BreathParams p = absParams(1000.f);
  p.coherenceMax = 0.6f;
  BreathDetector d;
  d.configure(p);
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 100, 0.95f));
  TEST_ASSERT_EQUAL_HEX8(kBreathOver | kBreathCoherent, d.status());
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 105, 0.60f));  // at the limit: still blocked
  TEST_ASSERT_TRUE(absStep(d, 2000.f, 110, 0.59f));
  TEST_ASSERT_TRUE(d.recognized());
}

// Off (anything above 1): a perfectly coherent sound still fires, as before v10.
void test_coherence_off_never_blocks() {
  BreathParams p = absParams(1000.f);
  p.coherenceMax = 2.0f;
  BreathDetector d;
  d.configure(p);
  TEST_ASSERT_TRUE(absStep(d, 2000.f, 100, 1.0f));
}

// Below the line the coherence says nothing: no "coherent" bit for quiet sound.
void test_coherence_bit_only_when_over() {
  BreathParams p = absParams(1000.f);
  p.coherenceMax = 0.6f;
  BreathDetector d;
  d.configure(p);
  TEST_ASSERT_FALSE(absStep(d, 500.f, 100, 0.99f));
  TEST_ASSERT_EQUAL_HEX8(0, d.status());
}

// The condition must hold without a break for minDurationMs, at the audio block cadence.
void test_min_duration_fires_after_a_sustained_sound() {
  BreathParams p = absParams(1000.f);
  p.minDurationMs = 120;
  BreathDetector d;
  d.configure(p);
  uint32_t now = 1000;
  for (int i = 0; i < 24; i++, now += 5) {  // 0..115 ms after the onset
    TEST_ASSERT_FALSE(absStep(d, 2000.f, now));
    TEST_ASSERT_TRUE((d.status() & kBreathWaiting) != 0);
  }
  TEST_ASSERT_TRUE(absStep(d, 2000.f, now));  // 120 ms
}

// A knock (40 ms) and syllables (sound with gaps) never reach 120 ms in one stretch.
void test_min_duration_rejects_knocks_and_syllables() {
  BreathParams p = absParams(1000.f);
  p.minDurationMs = 120;
  BreathDetector d;
  d.configure(p);
  uint32_t now = 1000;
  for (int i = 0; i < 8; i++, now += 5) TEST_ASSERT_FALSE(absStep(d, 5000.f, now));  // 40 ms knock
  for (int i = 0; i < 20; i++, now += 5) TEST_ASSERT_FALSE(absStep(d, 200.f, now));  // silence
  for (int syl = 0; syl < 10; syl++) {      // 100 ms on, 40 ms off, ten times
    for (int i = 0; i < 20; i++, now += 5) TEST_ASSERT_FALSE(absStep(d, 3000.f, now));
    for (int i = 0; i < 8; i++, now += 5) TEST_ASSERT_FALSE(absStep(d, 200.f, now));
  }
}

// A coherent stretch breaks the duration too: the count starts over after it.
void test_min_duration_restarts_after_a_coherent_stretch() {
  BreathParams p = absParams(1000.f);
  p.minDurationMs = 50;
  p.coherenceMax = 0.6f;
  BreathDetector d;
  d.configure(p);
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1000, 0.1f));
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1040, 0.1f));
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1045, 0.9f));  // coherent: the stretch ends
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1050, 0.1f));  // starts again here
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1095, 0.1f));
  TEST_ASSERT_TRUE(absStep(d, 2000.f, 1100, 0.1f));
}

// Still only: a breath while the ball moves never fires; once it is still, it does.
void test_still_only_blocks_while_moving() {
  BreathParams p = absParams(1000.f);
  p.stillOnly = true;
  BreathDetector d;
  d.configure(p);
  TEST_ASSERT_FALSE(absStep(d, 5000.f, 100, 0.0f, true));
  TEST_ASSERT_EQUAL_HEX8(kBreathOver | kBreathMoving, d.status());
  TEST_ASSERT_FALSE(d.recognized());
  TEST_ASSERT_TRUE(absStep(d, 5000.f, 105, 0.0f, false));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.f, d.triggerLevel());  // the line never moves
}

// Still only off: moving is reported (for the config-app) but changes nothing.
void test_still_only_off_reports_but_fires() {
  BreathParams p = absParams(1000.f);
  p.stillOnly = false;
  BreathDetector d;
  d.configure(p);
  TEST_ASSERT_TRUE(absStep(d, 1200.f, 100, 0.0f, true));
  TEST_ASSERT_TRUE((d.status() & kBreathMoving) != 0);
}

// Moving breaks the duration: the count starts when the ball is still again.
void test_moving_breaks_the_duration() {
  BreathParams p = absParams(1000.f);
  p.stillOnly = true;
  p.minDurationMs = 50;
  BreathDetector d;
  d.configure(p);
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1000));
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1045));
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1048, 0.0f, true));  // a turn
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1050));              // still again: from here
  TEST_ASSERT_FALSE(absStep(d, 2000.f, 1095));
  TEST_ASSERT_TRUE(absStep(d, 2000.f, 1100));
}

// Adaptive: moving blocks it too.
void test_still_only_adaptive() {
  BreathParams p = adaptiveParams(100.f, 1.0f, 0.999f, 0);
  p.stillOnly = true;
  BreathDetector d;
  d.configure(p);
  const float quiet[2] = {1000.f, 1000.f}, loud[2] = {5000.f, 5000.f};
  BreathGate mv;
  mv.moving = true;
  TEST_ASSERT_FALSE(d.update(quiet, 0));  // seeds the baseline
  TEST_ASSERT_FALSE(d.update(loud, 15, mv));
  TEST_ASSERT_TRUE(d.update(loud, 30));
}

// The defaults in GaiaConfig (v10): all three on.
void test_defaults_from_config() {
  GaiaConfig c;
  TEST_ASSERT_EQUAL(60, c.breathCoherenceMaxX100);
  TEST_ASSERT_EQUAL(154, c.micSpacingMmX10);
  TEST_ASSERT_EQUAL(120, c.breathMinDurationMs);
  TEST_ASSERT_EQUAL(1, c.breathStillOnly);
  TEST_ASSERT_EQUAL(20, c.stillMaxDps);
  TEST_ASSERT_EQUAL(300, c.stillMaxAccelMg);
  TEST_ASSERT_EQUAL(300, c.stillHoldMs);
  TEST_ASSERT_EQUAL(100, c.stillWindowMs);
}

// Feeds `ms` of IMU samples at 100 Hz; gyro(tS) gives the angular velocity at time tS.
// Returns true if the detector said "moving" at any sample; maxNet = the largest net speed.
template <typename G>
static bool feedImu(StillnessDetector &s, uint32_t &tMs, uint32_t ms, G gyro, float accelG,
                    float *maxNet = nullptr) {
  bool any = false;
  for (uint32_t k = 0; k < ms / 10; k++, tMs += 10) {
    float w[3] = {0.0f, 0.0f, 0.0f};
    gyro(tMs / 1000.0f, w);
    any |= s.update(w, accelG, tMs * 1000u, tMs);
    if (maxNet && s.netDps() > *maxNet) *maxNet = s.netDps();
  }
  return any;
}

// A hand holding the ball still trembles: 10 Hz, 30 deg/s peak on one axis (above the
// 20 deg/s limit sample by sample) plus a slow drift. The net speed stays far below.
void test_stillness_ignores_hand_tremor() {
  StillnessDetector s;
  s.configure(StillnessParams());  // 20 deg/s, 300 mg, 300 ms, 100 ms
  uint32_t t = 0;
  auto tremor = [](float ts, float w[3]) {
    w[0] = 30.0f * sinf(2.0f * (float)M_PI * 10.0f * ts);
    w[1] = 3.0f;  // slow drift
  };
  float startMax = 0.0f, steadyMax = 0.0f;
  const bool movedStart = feedImu(s, t, 500, tremor, 1.02f, &startMax);
  const bool movedSteady = feedImu(s, t, 2500, tremor, 1.02f, &steadyMax);
  // The same filter simulated in Python: 8.17 at the start (transient), 5.62 steady.
  printf("tremor 10 Hz 30 deg/s peak + 3 deg/s drift: net speed max %.2f (first 0.5 s), "
         "%.2f (steady) deg/s\n", startMax, steadyMax);
  TEST_ASSERT_FALSE(movedStart || movedSteady);
  TEST_ASSERT_TRUE(startMax < 10.0f);   // half the 20 deg/s limit, even in the transient
  TEST_ASSERT_TRUE(steadyMax < 6.0f);
}

// The same tremor with a 10 ms window is no longer averaged out: the window matters.
void test_stillness_short_window_lets_tremor_through() {
  StillnessParams p;
  p.windowMs = 10;
  StillnessDetector s;
  s.configure(p);
  uint32_t t = 0;
  TEST_ASSERT_TRUE(feedImu(s, t, 1000, [](float ts, float w[3]) {
    w[0] = 30.0f * sinf(2.0f * (float)M_PI * 10.0f * ts);
  }, 1.0f));
}

// A deliberate turn (60 deg/s) reads as moving within ~1 window, and the ball counts as
// still again holdMs (plus the decay of the average) after the turn ends.
void test_stillness_turn_and_hold() {
  StillnessDetector s;
  s.configure(StillnessParams());
  uint32_t t = 0;
  feedImu(s, t, 500, [](float, float w[3]) {}, 1.0f);  // still
  TEST_ASSERT_FALSE(s.moving());
  uint32_t firstMoving = 0;
  for (uint32_t k = 0; k < 50; k++, t += 10) {  // 500 ms turn
    const float w[3] = {0.0f, 0.0f, 60.0f};
    if (s.update(w, 1.0f, t * 1000u, t) && firstMoving == 0) firstMoving = t;
  }
  TEST_ASSERT_TRUE(firstMoving > 0 && firstMoving - 500 <= 60);  // 60 deg/s: 20 reached in ~41 ms
  uint32_t stillAt = 0;
  for (uint32_t k = 0; k < 100 && stillAt == 0; k++, t += 10) {
    const float w[3] = {0.0f, 0.0f, 0.0f};
    if (!s.update(w, 1.0f, t * 1000u, t)) stillAt = t;
  }
  printf("60 deg/s turn: moving after %u ms; still again %u ms after it ended\n",
         (unsigned)(firstMoving - 500), (unsigned)(stillAt - 1000));
  TEST_ASSERT_TRUE(stillAt - 1000 >= 300 && stillAt - 1000 <= 450);
}

// A knock: one raw accelerometer sample 450 mg off 1 g is enough, held for holdMs.
void test_stillness_knock_and_shake() {
  StillnessDetector s;
  s.configure(StillnessParams());
  const float w0[3] = {0.0f, 0.0f, 0.0f};
  TEST_ASSERT_FALSE(s.update(w0, 1.0f, 0, 1000));
  TEST_ASSERT_TRUE(s.update(w0, 1.45f, 10000, 1010));  // knock
  TEST_ASSERT_TRUE(s.update(w0, 1.0f, 300000, 1300));  // held 300 ms
  TEST_ASSERT_FALSE(s.update(w0, 1.0f, 311000, 1311));
  TEST_ASSERT_FALSE(s.update(w0, 1.25f, 320000, 1320));  // 250 mg: under the limit
  TEST_ASSERT_TRUE(s.update(w0, 0.5f, 330000, 1330));    // the other way counts too
}

// Lags for a microphone spacing: one sample at 16 kHz is 343/16000 m = 21.4375 mm.
void test_corr_lags_for_spacing() {
  TEST_ASSERT_EQUAL(1, corrLagsForSpacing(154, 16000.0f));   // the board: 15.4 mm
  TEST_ASSERT_EQUAL(1, corrLagsForSpacing(214, 16000.0f));   // 21.4 mm < one sample
  TEST_ASSERT_EQUAL(2, corrLagsForSpacing(215, 16000.0f));
  TEST_ASSERT_EQUAL(0, corrLagsForSpacing(0, 16000.0f));
  TEST_ASSERT_EQUAL(kMaxCorrLag, corrLagsForSpacing(2000, 16000.0f));  // 200 mm: 9.3 -> 10
  TEST_ASSERT_EQUAL(kMaxCorrLag, corrLagsForSpacing(65535, 16000.0f));
}

// The window pools the cross sums like the energy and keeps the best lag, by magnitude.
void test_window_coherence_pools_cross_sums() {
  AudioLevelWindow w;
  w.setBlocks(2);
  const float sq[2] = {100.f, 100.f}, pk[2] = {1.f, 1.f};
  float cross[kCorrLags] = {};
  cross[kMaxCorrLag] = 100.f;
  w.push(sq, pk, 80, cross);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, w.levels().coherence);
  cross[kMaxCorrLag] = 0.f;
  cross[kMaxCorrLag + 1] = -40.f;  // the other block: best lag elsewhere, negative
  w.push(sq, pk, 80, cross);
  // lag 0: 100 + 0, lag +1: 0 - 40 -> best |100| / sqrt(200 * 200) = 0.5
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, w.levels().coherence);
  AudioLevelWindow silent;
  const float sq1[2] = {100.f, 0.f};
  silent.push(sq1, pk, 80, cross);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, silent.levels().coherence);
  AudioLevelWindow none;  // no cross sums given: 0
  none.push(sq, pk, 80);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, none.levels().coherence);
}

int main() {
  UNITY_BEGIN();
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
  RUN_TEST(test_coherence_blocks_a_far_sound);
  RUN_TEST(test_coherence_off_never_blocks);
  RUN_TEST(test_coherence_bit_only_when_over);
  RUN_TEST(test_min_duration_fires_after_a_sustained_sound);
  RUN_TEST(test_min_duration_rejects_knocks_and_syllables);
  RUN_TEST(test_min_duration_restarts_after_a_coherent_stretch);
  RUN_TEST(test_defaults_from_config);
  RUN_TEST(test_corr_lags_for_spacing);
  RUN_TEST(test_window_coherence_pools_cross_sums);
  RUN_TEST(test_still_only_blocks_while_moving);
  RUN_TEST(test_still_only_off_reports_but_fires);
  RUN_TEST(test_moving_breaks_the_duration);
  RUN_TEST(test_still_only_adaptive);
  RUN_TEST(test_stillness_ignores_hand_tremor);
  RUN_TEST(test_stillness_short_window_lets_tremor_through);
  RUN_TEST(test_stillness_turn_and_hold);
  RUN_TEST(test_stillness_knock_and_shake);
  return UNITY_END();
}
