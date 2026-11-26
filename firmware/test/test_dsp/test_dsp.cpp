#include <unity.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "../../Gaiaball/src/logic/AudioDecimator.h"
#include "../../Gaiaball/src/logic/AudioFeatures.h"
#include "../../Gaiaball/src/logic/AudioFrontEnd.h"
#include "../../Gaiaball/src/logic/FilterCascade.h"
#include "../../Gaiaball/src/logic/Filters.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static constexpr float kFs = 16000.0f;

// Unity is built without double support here; the values compared are dB or counts, so
// float precision is plenty.
#define ASSERT_NEAR(tol, want, got) TEST_ASSERT_FLOAT_WITHIN((float)(tol), (float)(want), (float)(got))
static const int kSlopes[] = {6, 12, 18, 24, 36, 48};

// Steady-state gain in dB of `process` for a sine at `hz` (integer Hz): settle, then a
// 1 s window of whole cycles, demodulated (I/Q) so that noise and harmonics do not count.
template <typename F>
static double sineGainDb(F &&process, double hz, double settleS) {
  const int settle = (int)(settleS * kFs), measure = (int)kFs;
  double i = 0.0, q = 0.0;
  for (int n = 0; n < settle + measure; n++) {
    const double ph = 2.0 * M_PI * hz * n / kFs;
    const float y = process((float)std::sin(ph));
    if (n >= settle) {
      i += y * std::sin(ph);
      q += y * std::cos(ph);
    }
  }
  return 20.0 * std::log10(2.0 * std::sqrt(i * i + q * q) / measure);
}

static double cascadeGainDb(FilterCascade::Type t, float fc, float q, int slope, double hz,
                            double settleS = 1.0) {
  FilterCascade f;
  f.configure(t, fc, q, (uint8_t)slope, kFs);
  return sineGainDb([&](float x) { return f.process(x); }, hz, settleS);
}

// Analytic response of the same design: Butterworth prototype of order slope/6, highest-Q
// section scaled so the gain at the cutoff is q, bilinear transform prewarped at fc.
static double analyticDb(bool highPass, double fc, double q, int slope, double hz) {
  const int order = slope / 6;
  double x = std::tan(M_PI * hz / kFs) / std::tan(M_PI * fc / kFs);
  if (highPass) x = 1.0 / x;  // HP(x) = LP(1/x) for these prototypes
  double mag2 = 1.0;
  if (order % 2) mag2 /= 1.0 + x * x;
  for (int i = 0; i < order / 2; i++) {
    double sq = 1.0 / (2.0 * std::sin((2 * i + 1) * M_PI / (2.0 * order)));
    if (i == 0) sq *= q * M_SQRT2;
    mag2 /= (1 - x * x) * (1 - x * x) + (x / sq) * (x / sq);
  }
  return 10.0 * std::log10(mag2);
}

// --- FilterCascade ------------------------------------------------------------

void test_valid_slopes_and_order() {
  for (int s : kSlopes) {
    TEST_ASSERT_TRUE(FilterCascade::validSlope((uint8_t)s));
    FilterCascade f;
    f.configure(FilterCascade::Type::LowPass, 200.0f, 0.71f, (uint8_t)s, kFs);
    TEST_ASSERT_EQUAL(s / 6, f.order());
  }
  for (int s : {0, 1, 7, 30, 42, 54, 60, 255}) TEST_ASSERT_FALSE(FilterCascade::validSlope((uint8_t)s));
  // An invalid slope falls back to 12 dB/oct instead of running some other order.
  FilterCascade f;
  f.configure(FilterCascade::Type::HighPass, 10.0f, 0.71f, 30, kFs);
  TEST_ASSERT_EQUAL(2, f.order());
}

// The measured response matches the analytic design across both bands, for every slope.
void test_response_matches_analytic_design() {
  for (int s : kSlopes) {
    for (double hz : {50.0, 100.0, 200.0, 400.0, 800.0}) {
      const double lp = cascadeGainDb(FilterCascade::Type::LowPass, 200.0f, 0.71f, s, hz);
      ASSERT_NEAR(0.05, analyticDb(false, 200.0, 0.71, s, hz), lp);
    }
    for (double hz : {100.0, 200.0, 400.0, 800.0, 1600.0}) {
      const double hp = cascadeGainDb(FilterCascade::Type::HighPass, 400.0f, 0.71f, s, hz);
      ASSERT_NEAR(0.05, analyticDb(true, 400.0, 0.71, s, hz), hp);
    }
  }
}

// Slope: one octave of the stopband, once the response is on its asymptote, loses the
// configured dB. Low orders get there later (a first-order filter between 2x and 4x the
// cutoff is still at 5.3 dB/oct), so the octave measured is the last one before ~100 dB of
// attenuation: (8x, 16x) for 6..24 dB/oct, (2x, 4x) for 36 and 48. On the LP side the
// bilinear warping near Nyquist steepens it a little (18 dB/oct reads 18.7 at 0.8-1.6
// kHz), exactly as the design predicts.
void test_slope_is_db_per_octave() {
  for (int s : kSlopes) {
    const int k = s <= 24 ? 4 : 2;  // octaves from the cutoff to the far point
    const double lpA = 100.0 * (1 << (k - 1)), lpB = 100.0 * (1 << k);
    const double lp = cascadeGainDb(FilterCascade::Type::LowPass, 100.0f, 0.71f, s, lpA) -
                      cascadeGainDb(FilterCascade::Type::LowPass, 100.0f, 0.71f, s, lpB);
    ASSERT_NEAR(0.1, analyticDb(false, 100.0, 0.71, s, lpA) - analyticDb(false, 100.0, 0.71, s, lpB), lp);
    ASSERT_NEAR(0.05 * s, (double)s, lp);
    const double hpA = 1600.0 / (1 << (k - 1)), hpB = 1600.0 / (1 << k);
    const double hp = cascadeGainDb(FilterCascade::Type::HighPass, 1600.0f, 0.71f, s, hpA) -
                      cascadeGainDb(FilterCascade::Type::HighPass, 1600.0f, 0.71f, s, hpB);
    ASSERT_NEAR(0.1, analyticDb(true, 1600.0, 0.71, s, hpA) - analyticDb(true, 1600.0, 0.71, s, hpB), hp);
    ASSERT_NEAR(0.05 * s, (double)s, hp);
  }
}

// Q is the gain exactly at the cutoff, at every slope with a second-order section:
// 0.71 -> -3 dB (Butterworth), 1 -> 0 dB, 5 -> +14 dB. At 6 dB/oct Q does nothing.
void test_q_is_the_gain_at_the_cutoff() {
  for (int s : kSlopes) {
    for (float q : {0.71f, 1.0f, 5.0f}) {
      const double want = s == 6 ? -3.0103 : 20.0 * std::log10((double)q);
      ASSERT_NEAR(0.05, want, cascadeGainDb(FilterCascade::Type::LowPass, 200.0f, q, s, 200.0, 2.0));
      ASSERT_NEAR(0.05, want, cascadeGainDb(FilterCascade::Type::HighPass, 200.0f, q, s, 200.0, 2.0));
    }
  }
}

void test_highpass_blocks_dc_at_every_slope() {
  for (int s : kSlopes) {
    FilterCascade f;
    f.configure(FilterCascade::Type::HighPass, 10.0f, 0.71f, (uint8_t)s, kFs);
    float y = 0.0f;
    for (int i = 0; i < 5 * 16000; i++) y = f.process(1000.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, y);  // float rounding residue: -86 dB re the input
  }
}

// The worst corners: a 48 dB/oct filter at the lowest and highest cutoffs, with the
// highest Q, stays finite and bounded on loud noise.
void test_stable_at_the_extremes() {
  std::mt19937 rng(7);
  std::normal_distribution<float> g(0.0f, 300000.0f);
  for (float fc : {1.0f, 7000.0f}) {
    for (auto t : {FilterCascade::Type::LowPass, FilterCascade::Type::HighPass}) {
      FilterCascade f;
      f.configure(t, fc, 20.0f, 48, kFs);
      float maxAbs = 0.0f;
      for (int i = 0; i < 4 * 16000; i++) maxAbs = std::fmax(maxAbs, std::fabs(f.process(g(rng))));
      TEST_ASSERT_TRUE(std::isfinite(maxAbs));
      TEST_ASSERT_TRUE(maxAbs < 3.0e8f);  // a Q-20 peak on 300k-count noise, not a runaway
    }
  }
}

// Re-applying the same settings (every config write does it) leaves the output bit-exact;
// a new cutoff keeps the state (no jump to zero); a new slope starts over.
void test_reconfigure_keeps_state_unless_the_order_changes() {
  FilterCascade a, b;
  a.configure(FilterCascade::Type::LowPass, 200.0f, 0.71f, 24, kFs);
  b.configure(FilterCascade::Type::LowPass, 200.0f, 0.71f, 24, kFs);
  for (int i = 0; i < 1000; i++) {
    const float x = 1000.0f * std::sin(2.0f * (float)M_PI * 50.0f * i / kFs);
    if (i == 500) b.configure(FilterCascade::Type::LowPass, 200.0f, 0.71f, 24, kFs);
    const float ya = a.process(x), yb = b.process(x);  // the Unity macro evaluates twice
    TEST_ASSERT_EQUAL_FLOAT(ya, yb);
  }
  // A DC of 1000 settled through the LP: a cutoff change keeps the output near 1000.
  FilterCascade c;
  c.configure(FilterCascade::Type::LowPass, 200.0f, 0.71f, 24, kFs);
  for (int i = 0; i < 16000; i++) c.process(1000.0f);
  c.configure(FilterCascade::Type::LowPass, 300.0f, 0.71f, 24, kFs);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 1000.0f, c.process(1000.0f));
  // A slope change restarts from zero state: the first output is far below 1000.
  c.configure(FilterCascade::Type::LowPass, 300.0f, 0.71f, 48, kFs);
  TEST_ASSERT_TRUE(c.process(1000.0f) < 10.0f);
}

// --- DcBlocker ------------------------------------------------------------------

void test_dc_blocker_removes_offset_from_the_first_sample() {
  DcBlocker d;
  d.configure(AudioFrontEnd::kDcCutoffHz, kFs);
  // Seeded with the first sample: a constant offset never comes out, not even as a step.
  for (int i = 0; i < 1000; i++) TEST_ASSERT_EQUAL_FLOAT(0.0f, d.process(123456.0f));
}

void test_dc_blocker_is_far_below_the_breath_band() {
  auto gain = [](double hz) {
    DcBlocker d;
    d.configure(AudioFrontEnd::kDcCutoffHz, kFs);
    return sineGainDb([&](float x) { return d.process(x); }, hz, 2.0);
  };
  ASSERT_NEAR(0.2, -3.0, gain(2.0));
  ASSERT_NEAR(0.05, -0.17, gain(10.0));  // the old per-block mean: -21 dB
  ASSERT_NEAR(0.02, 0.0, gain(50.0));    // old: -7 dB
  ASSERT_NEAR(0.02, 0.0, gain(100.0));   // old: -2.3 dB
}

// --- AudioFrontEnd ---------------------------------------------------------------

// Interleaved stereo I2S words (24-bit counts left-aligned, like the INMP441 DMA data).
static std::vector<int32_t> stereo(int frames, double hz, double ampL, double offL, double ampR, double offR) {
  std::vector<int32_t> v(2 * frames);
  for (int i = 0; i < frames; i++) {
    const double s = std::sin(2.0 * M_PI * hz * i / kFs);
    v[2 * i] = (int32_t)std::lround(offL + ampL * s) * 256;
    v[2 * i + 1] = (int32_t)std::lround(offR + ampR * s) * 256;
  }
  return v;
}

// RMS per channel over the last `measureFrames`, fed in 80-frame blocks like the task.
static void frontEndRms(AudioFrontEnd &fe, const std::vector<int32_t> &v, int measureFrames, double rms[2]) {
  const int frames = (int)v.size() / 2;
  double acc[2] = {0.0, 0.0};
  for (int b = 0; b + 80 <= frames; b += 80) {
    const BlockEnergy e = fe.processBlock(v.data() + 2 * b, 80);
    if (b >= frames - measureFrames) {
      acc[0] += e.sumSq[0];
      acc[1] += e.sumSq[1];
    }
  }
  rms[0] = std::sqrt(acc[0] / measureFrames);
  rms[1] = std::sqrt(acc[1] / measureFrames);
}

static BandPassSettings bandOff() {
  BandPassSettings s;
  s.enabled = false;
  return s;
}

void test_front_end_removes_dc_and_splits_channels() {
  AudioFrontEnd fe;
  fe.configure(bandOff());
  // 200 Hz sines with big, different offsets on each side.
  std::vector<int32_t> v(2 * 32000);
  for (int i = 0; i < 32000; i++) {
    const double s = std::sin(2.0 * M_PI * 200.0 * i / kFs);
    v[2 * i] = (int32_t)std::lround(200000 + 2000 * s) * 256;
    v[2 * i + 1] = (int32_t)std::lround(-300000 + 8000 * s) * 256;
  }
  double rms[2];
  frontEndRms(fe, v, 16000, rms);
  ASSERT_NEAR(2000 / M_SQRT2 * 0.01, 2000 / M_SQRT2, rms[0]);
  ASSERT_NEAR(8000 / M_SQRT2 * 0.01, 8000 / M_SQRT2, rms[1]);
}

// The regression the DC blocker fixes: with the band-pass off a 20 Hz tone used to lose
// 15 dB to the per-block mean; now it passes. With the default band on, a tone at the
// 10 Hz cutoff reads -3 dB (Butterworth), not the -21 dB of the old hidden high-pass.
void test_front_end_has_no_hidden_high_pass() {
  double rms[2];
  AudioFrontEnd off;
  off.configure(bandOff());
  frontEndRms(off, stereo(48000, 20.0, 100000, 0, 100000, 0), 32000, rms);
  ASSERT_NEAR(0.1, 0.0, 20.0 * std::log10(rms[0] / (100000 / M_SQRT2)));
  AudioFrontEnd on;  // defaults: HP 10 Hz, LP 200 Hz, 12 dB/oct, Q 0.71
  frontEndRms(on, stereo(64000, 10.0, 100000, 0, 100000, 0), 32000, rms);
  ASSERT_NEAR(0.3, -3.2, 20.0 * std::log10(rms[0] / (100000 / M_SQRT2)));
}

// The raw stream (the spectrum) is taken before the band-pass: it is bit-identical
// whatever the band is, so the spectrum shows what the filters receive.
void test_front_end_raw_stream_ignores_the_band() {
  std::mt19937 rng(3);
  std::normal_distribution<double> g(0.0, 100000.0);
  std::vector<int32_t> v(2 * 16000);
  for (auto &w : v) w = (int32_t)std::lround(g(rng)) * 256;
  BandPassSettings steep;
  steep.hpHz = 300.0f;
  steep.hpSlopeDbOct = 48;
  steep.lpHz = 400.0f;
  steep.lpQ = 5.0f;
  steep.lpSlopeDbOct = 36;
  const BandPassSettings cases[] = {bandOff(), BandPassSettings(), steep};
  std::vector<float> ref;
  for (const BandPassSettings &s : cases) {
    AudioFrontEnd fe;
    fe.configure(s);
    std::vector<float> raw;
    for (size_t b = 0; b + 160 <= v.size(); b += 160)
      fe.processBlock(v.data() + b, 80, [&](float m) { raw.push_back(m); });
    if (ref.empty()) ref = raw;
    TEST_ASSERT_EQUAL(ref.size(), raw.size());
    TEST_ASSERT_EQUAL_MEMORY(ref.data(), raw.data(), ref.size() * sizeof(float));
  }
}

// Turning the band-pass off freezes the filters; turning it back on must not release what
// they held. Reference: a front end whose filters never ran (band off during the loud part),
// so its DC blockers match and its filters are clean. After re-enabling, both must agree
// bit for bit.
void test_front_end_reenabled_band_starts_clean() {
  BandPassSettings band;
  band.lpQ = 5.0f;  // a ringing filter makes any leftover state obvious
  AudioFrontEnd a, ref;
  a.configure(band);
  ref.configure(bandOff());
  const auto loud = stereo(8000, 200.0, 1000000, 0, 1000000, 0);
  for (size_t b = 0; b + 160 <= loud.size(); b += 160) {
    a.processBlock(loud.data() + b, 80);
    ref.processBlock(loud.data() + b, 80);
  }
  a.configure(bandOff());
  a.configure(band);
  ref.configure(band);
  const auto next = stereo(1600, 50.0, 1000, 0, 1000, 0);
  for (size_t b = 0; b + 160 <= next.size(); b += 160) {
    const BlockEnergy ea = a.processBlock(next.data() + b, 80);
    const BlockEnergy er = ref.processBlock(next.data() + b, 80);
    TEST_ASSERT_EQUAL_MEMORY(&er, &ea, sizeof ea);
  }
}

// A full-scale sine is the worst case for the float sum of squares (80 terms of up to
// 2^46); the error must stay far below the 0.01 dB resolution of the wire format.
void test_front_end_float_accumulator_precision() {
  AudioFrontEnd fe;
  fe.configure(bandOff());
  const double amp = 8388607.0;  // 2^23 - 1
  double rms[2];
  frontEndRms(fe, stereo(32000, 1000.0, amp, 0, amp, 0), 16000, rms);
  ASSERT_NEAR(amp / M_SQRT2 * 0.0005, amp / M_SQRT2, rms[0]);
  ASSERT_NEAR(amp / M_SQRT2 * 0.0005, amp / M_SQRT2, rms[1]);
}

// --- AudioDecimator ----------------------------------------------------------------

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

// --- Left/right coherence (breath discriminator, v10) ---------------------------

// Coherence the way AudioService computes it: 80-frame blocks into the 50 ms window.
static std::vector<float> frontEndCoherence(AudioFrontEnd &fe, const std::vector<int32_t> &v,
                                            int skipFrames) {
  AudioLevelWindow w;  // default 10 blocks = 50 ms
  std::vector<float> out;
  const int frames = (int)v.size() / 2;
  for (int b = 0; b + 80 <= frames; b += 80) {
    const BlockEnergy e = fe.processBlock(v.data() + 2 * b, 80);
    w.push(e.sumSq, e.peak, 80, e.cross);
    if (b >= skipFrames) out.push_back(w.levels().coherence);
  }
  return out;
}

static std::vector<double> gaussian(int n, unsigned seed, double sigma) {
  std::mt19937 rng(seed);
  std::normal_distribution<double> nd(0.0, sigma);
  std::vector<double> x(n);
  for (double &v : x) v = nd(rng);
  return x;
}

static std::vector<int32_t> interleave(const std::vector<double> &l, const std::vector<double> &r) {
  std::vector<int32_t> v(2 * l.size());
  for (size_t i = 0; i < l.size(); i++) {
    v[2 * i] = (int32_t)std::lround(l[i]) * 256;
    v[2 * i + 1] = (int32_t)std::lround(r[i]) * 256;
  }
  return v;
}

// One sound reaching both microphones (a far source): coherence ~1 in the default band.
void test_coherence_of_one_sound_is_near_one() {
  AudioFrontEnd fe;  // HP 10, LP 200, 12 dB/oct
  fe.setCorrLags(corrLagsForSpacing(154, kFs));
  const auto v = stereo(16000, 100.0, 50000, 1000, 20000, -3000);  // different gains and offsets
  const auto c = frontEndCoherence(fe, v, 8000);
  float lo = 1.0f;
  for (float x : c) lo = std::min(lo, x);
  TEST_ASSERT_TRUE(lo > 0.99f);
}

// Independent noise at each microphone (the turbulence of a breath on the ports), through
// the default band: the coherence stays low. The 50 ms window over a 190 Hz band holds only
// ~19 independent samples, so single windows scatter (std ~0.23); the test pins the spread
// that the default limit (0.60) has to live with.
void test_coherence_of_independent_noise_is_low() {
  AudioFrontEnd fe;
  fe.setCorrLags(corrLagsForSpacing(154, kFs));
  const int n = 16000 * 20;  // 20 s
  const auto v = interleave(gaussian(n, 1, 200000.0), gaussian(n, 2, 200000.0));
  auto c = frontEndCoherence(fe, v, 8000);
  std::vector<float> sorted = c;
  std::sort(sorted.begin(), sorted.end());
  const float median = sorted[sorted.size() / 2];
  size_t over = 0;
  for (float x : c) over += x >= 0.60f ? 1 : 0;
  const float frac = (float)over / (float)c.size();
  printf("independent noise, default band: median coherence %.3f, %.2f %% of windows >= 0.60\n",
         median, 100.0f * frac);
  TEST_ASSERT_TRUE(median < 0.35f);
  TEST_ASSERT_TRUE(frac < 0.05f);
}

// A far sound that reaches the right microphone one sample later: with the band opened to
// 4 kHz the lag matters, and the spacing (lags) is what recovers it.
void test_coherence_needs_the_lag_the_spacing_allows() {
  BandPassSettings wide;
  wide.lpHz = 4000.0f;
  const int n = 16000 * 2;
  const auto x = gaussian(n + 1, 3, 200000.0);
  std::vector<double> l(x.begin() + 1, x.end()), r(x.begin(), x.end() - 1);  // r[n] = l[n-1]
  const auto v = interleave(l, r);
  AudioFrontEnd noLag, oneLag;
  noLag.configure(wide);
  oneLag.configure(wide);
  noLag.setCorrLags(0);
  oneLag.setCorrLags(corrLagsForSpacing(154, kFs));
  const auto c0 = frontEndCoherence(noLag, v, 8000);
  const auto c1 = frontEndCoherence(oneLag, v, 8000);
  float max0 = 0.0f, min1 = 1.0f;
  for (float c : c0) max0 = std::max(max0, c);
  for (float c : c1) min1 = std::min(min1, c);
  printf("1-sample delay, 4 kHz band: lag 0 only max %.3f, +-1 lag min %.3f\n", max0, min1);
  TEST_ASSERT_TRUE(min1 > 0.98f);
  TEST_ASSERT_TRUE(max0 < 0.90f);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_slopes_and_order);
  RUN_TEST(test_response_matches_analytic_design);
  RUN_TEST(test_slope_is_db_per_octave);
  RUN_TEST(test_q_is_the_gain_at_the_cutoff);
  RUN_TEST(test_highpass_blocks_dc_at_every_slope);
  RUN_TEST(test_stable_at_the_extremes);
  RUN_TEST(test_reconfigure_keeps_state_unless_the_order_changes);
  RUN_TEST(test_dc_blocker_removes_offset_from_the_first_sample);
  RUN_TEST(test_dc_blocker_is_far_below_the_breath_band);
  RUN_TEST(test_front_end_removes_dc_and_splits_channels);
  RUN_TEST(test_front_end_has_no_hidden_high_pass);
  RUN_TEST(test_front_end_raw_stream_ignores_the_band);
  RUN_TEST(test_front_end_float_accumulator_precision);
  RUN_TEST(test_front_end_reenabled_band_starts_clean);
  RUN_TEST(test_decimator_chunk_cadence_and_rate);
  RUN_TEST(test_decimator_scaling_and_saturation);
  RUN_TEST(test_decimator_preserves_low_frequency_sine);
  RUN_TEST(test_coherence_of_one_sound_is_near_one);
  RUN_TEST(test_coherence_of_independent_noise_is_low);
  RUN_TEST(test_coherence_needs_the_lag_the_spacing_allows);
  return UNITY_END();
}
