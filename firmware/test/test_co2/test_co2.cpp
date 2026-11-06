#include <unity.h>
#include "../../Gaiaball/src/logic/Co2FrameParser.h"
#include "../../Gaiaball/src/logic/VirtualCo2Pulse.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static const size_t kLen = 16;

// Builds a frame with header 42 4D, ppm big-endian at hiIdx, real checksum.
static void makeFrame(uint8_t *f, uint16_t ppm, uint8_t variant, uint8_t hiIdx = 6) {
  for (size_t i = 0; i < kLen; i++) f[i] = 0;
  f[0] = 0x42;
  f[1] = 0x4D;
  f[hiIdx] = ppm >> 8;
  f[hiIdx + 1] = ppm & 0xFF;
  uint8_t sum = 0;
  size_t last = variant == 0 ? kLen - 2 : kLen - 1;  // exclusive end
  for (size_t i = 0; i < last; i++) sum += f[i];
  f[kLen - 1] = sum;
}

struct Counts { int frames = 0, errors = 0; };
static Counts feedAll(Co2FrameParser &p, const uint8_t *d, size_t n) {
  Counts c;
  for (size_t i = 0; i < n; i++) {
    auto r = p.feed(d[i]);
    if (r == Co2FrameParser::Result::Frame) c.frames++;
    if (r == Co2FrameParser::Result::ChecksumError) c.errors++;
  }
  return c;
}

void test_aligned_frame() {
  Co2FrameParser p;
  uint8_t f[kLen];
  makeFrame(f, 1234, 0);
  for (size_t i = 0; i + 1 < kLen; i++)
    TEST_ASSERT_EQUAL(Co2FrameParser::Result::None, p.feed(f[i]));
  TEST_ASSERT_EQUAL(Co2FrameParser::Result::Frame, p.feed(f[kLen - 1]));
  TEST_ASSERT_EQUAL_UINT16(1234, p.ppm());
  TEST_ASSERT_EQUAL_UINT32(1, p.frameCount());
  TEST_ASSERT_EQUAL_UINT32(0, p.errorCount());
}

void test_legacy_checksum_variant() {
  Co2FrameParser p;
  Co2FrameSpec s;
  s.checksumVariant = 1;
  p.configure(s);
  uint8_t f[kLen];
  makeFrame(f, 777, 1);
  Counts c = feedAll(p, f, kLen);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL_UINT16(777, p.ppm());
  // A variant-0 frame must be rejected under variant 1 (unless sums coincide).
  makeFrame(f, 777, 0);
  f[14] = 0x01;  // byte 14 is covered only by variant 1, so the sums differ
  uint8_t sum = 0;
  for (size_t i = 0; i < kLen - 2; i++) sum += f[i];
  f[kLen - 1] = sum;
  c = feedAll(p, f, kLen);
  TEST_ASSERT_EQUAL(0, c.frames);
  TEST_ASSERT_EQUAL(1, c.errors);
}

void test_resync_after_garbage() {
  Co2FrameParser p;
  uint8_t f[kLen];
  makeFrame(f, 555, 0);
  const uint8_t noise[] = {0x00, 0xFF, 0x42, 0x13, 0x4D, 0x42, 0x42, 0x07, 0x42};  // partial headers
  Counts c = feedAll(p, noise, sizeof noise);
  Counts d = feedAll(p, f, kLen);
  TEST_ASSERT_EQUAL(0, c.frames + c.errors);
  TEST_ASSERT_EQUAL(1, d.frames);
  TEST_ASSERT_EQUAL(0, d.errors);
  TEST_ASSERT_EQUAL_UINT16(555, p.ppm());
  TEST_ASSERT_EQUAL_UINT32(1, p.frameCount());
}

void test_0x42_inside_payload() {
  Co2FrameParser p;
  // Frame A has 42 4D inside its payload (bytes 8,9) -- cannot lose frame B.
  uint8_t a[kLen], b[kLen];
  makeFrame(a, 400, 0);
  a[8] = 0x42; a[9] = 0x4D;
  uint8_t sum = 0;
  for (size_t i = 0; i < kLen - 2; i++) sum += a[i];
  a[kLen - 1] = sum;
  makeFrame(b, 900, 0);

  // (1) Intact A then B: both parse.
  Counts c = feedAll(p, a, kLen);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL_UINT16(400, p.ppm());
  c = feedAll(p, b, kLen);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL_UINT16(900, p.ppm());

  // (2) Truncated A (first 10 bytes lost its tail) followed by B: the false
  // header inside A's payload swallows B's first bytes, so the checksum fails,
  // yet B must still be recovered by rescanning buffered bytes.
  Co2FrameParser q;
  uint8_t stream[kLen + kLen];
  for (size_t i = 0; i < 10; i++) stream[i] = a[i];  // ends right after 42 4D
  for (size_t i = 0; i < kLen; i++) stream[10 + i] = b[i];
  c = feedAll(q, stream, 10 + kLen);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL_UINT16(900, q.ppm());
  TEST_ASSERT_EQUAL_UINT32(1, q.frameCount());
}

void test_checksum_error_counts() {
  Co2FrameParser p;
  uint8_t f[kLen];
  makeFrame(f, 300, 0);
  f[kLen - 1] ^= 0x55;
  Counts c = feedAll(p, f, kLen);
  TEST_ASSERT_EQUAL(0, c.frames);
  TEST_ASSERT_EQUAL(1, c.errors);
  TEST_ASSERT_EQUAL_UINT32(1, p.errorCount());
  TEST_ASSERT_EQUAL_UINT32(0, p.frameCount());
  // Recovers on the next good frame.
  makeFrame(f, 301, 0);
  c = feedAll(p, f, kLen);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL_UINT16(301, p.ppm());
}

void test_custom_spec() {
  Co2FrameParser p;
  Co2FrameSpec s;
  s.ppmHiIndex = 4;
  p.configure(s);
  uint8_t f[kLen];
  makeFrame(f, 0x1234, 0, 4);
  Counts c = feedAll(p, f, kLen);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL_UINT16(0x1234, p.ppm());
}

void test_pulse_timing() {
  VirtualCo2Pulse v;
  v.configure(1500, 1000, 17000);
  TEST_ASSERT_FALSE(v.pending(0));
  v.trigger(0);
  TEST_ASSERT_TRUE(v.pending(0));
  TEST_ASSERT_TRUE(v.pending(999));
  TEST_ASSERT_FALSE(v.active(999));
  TEST_ASSERT_TRUE(v.active(1001));
  TEST_ASSERT_FALSE(v.pending(1001));
  TEST_ASSERT_TRUE(v.active(1000 + 16999));
  TEST_ASSERT_FALSE(v.active(1000 + 17000));
  TEST_ASSERT_FALSE(v.pending(1000 + 17000));
}

void test_pulse_retrigger_ignored_and_cancel() {
  VirtualCo2Pulse v;
  v.configure(1500, 1000, 5000);
  v.trigger(100);
  v.trigger(500);  // ignored while pending
  TEST_ASSERT_TRUE(v.active(1101));
  v.trigger(2000);  // ignored while active
  TEST_ASSERT_FALSE(v.active(1100 + 5000));
  v.trigger(7000);  // allowed once finished
  TEST_ASSERT_TRUE(v.pending(7500));
  TEST_ASSERT_TRUE(v.active(8001));
  v.cancel();
  TEST_ASSERT_FALSE(v.active(8002));
  TEST_ASSERT_FALSE(v.pending(8002));
}

void test_pulse_millis_wraparound() {
  VirtualCo2Pulse v;
  v.configure(1500, 1000, 5000);
  uint32_t t = 0xFFFFFF00u;
  v.trigger(t);
  TEST_ASSERT_TRUE(v.pending(t + 10));
  TEST_ASSERT_TRUE(v.active(t + 1001));  // wraps past 0
  TEST_ASSERT_FALSE(v.active(t + 6001));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_aligned_frame);
  RUN_TEST(test_legacy_checksum_variant);
  RUN_TEST(test_resync_after_garbage);
  RUN_TEST(test_0x42_inside_payload);
  RUN_TEST(test_checksum_error_counts);
  RUN_TEST(test_custom_spec);
  RUN_TEST(test_pulse_timing);
  RUN_TEST(test_pulse_retrigger_ignored_and_cancel);
  RUN_TEST(test_pulse_millis_wraparound);
  return UNITY_END();
}
