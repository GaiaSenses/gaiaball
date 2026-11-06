#include <unity.h>
#include <cstring>
#include "../../Gaiaball/src/logic/CommandGate.h"
#include "../../Gaiaball/src/logic/LedPriority.h"
#include "../../Gaiaball/src/logic/NotifyThrottle.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static const uint8_t GOOD[4] = {'1', '2', '3', '4'};

static CommandGate makeGate() {
  CommandGate g;
  g.setPin("1234");
  return g;
}

void test_wrong_pin_stays_locked() {
  CommandGate g = makeGate();
  const uint8_t bad[4] = {'1', '2', '3', '5'};
  TEST_ASSERT_FALSE(g.unlock(0, bad, 4));
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 3));
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 5));
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 0));
  TEST_ASSERT_FALSE(g.isUnlocked(0));
}

void test_unlock_per_connection() {
  CommandGate g = makeGate();
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4));
  TEST_ASSERT_TRUE(g.isUnlocked(0));
  TEST_ASSERT_FALSE(g.isUnlocked(1));
}

void test_out_of_range_conn_always_locked() {
  CommandGate g = makeGate();
  TEST_ASSERT_FALSE(g.unlock(2, GOOD, 4));
  TEST_ASSERT_FALSE(g.isUnlocked(2));
  TEST_ASSERT_FALSE(g.isUnlocked(255));
  g.onDisconnect(7);  // must not corrupt anything
}

void test_relock_on_disconnect() {
  CommandGate g = makeGate();
  g.unlock(0, GOOD, 4);
  g.unlock(1, GOOD, 4);
  g.onDisconnect(0);
  TEST_ASSERT_FALSE(g.isUnlocked(0));
  TEST_ASSERT_TRUE(g.isUnlocked(1));
}

void test_led_priority() {
  LedPriority l;
  using S = LedPriority::Source;
  TEST_ASSERT_FALSE(l.resolved());
  l.set(S::BleCommand, true);
  TEST_ASSERT_TRUE(l.resolved());
  l.set(S::BleCommand, false);
  TEST_ASSERT_FALSE(l.resolved());
}

void test_led_highest_source_wins() {
  LedPriority l;
  using S = LedPriority::Source;
  l.set(S::BleCommand, true);
  l.set(S::Co2Alert, false);
  TEST_ASSERT_TRUE(l.resolved());
  // A higher source that is OFF must not mask a lower one that is ON.
  l.set(S::SoundEvent, false);
  TEST_ASSERT_TRUE(l.resolved());
  l.set(S::Co2Alert, true);
  l.set(S::SoundEvent, true);
  l.set(S::Error, true);
  TEST_ASSERT_TRUE(l.resolved());
  l.set(S::Error, false);
  TEST_ASSERT_TRUE(l.resolved());  // falls back to SoundEvent
  l.set(S::SoundEvent, false);
  TEST_ASSERT_TRUE(l.resolved());  // falls back to Co2Alert
  l.set(S::Co2Alert, false);
  TEST_ASSERT_TRUE(l.resolved());  // falls back to BleCommand
  l.set(S::BleCommand, false);
  TEST_ASSERT_FALSE(l.resolved());
}

void test_led_independent_state() {
  LedPriority l;
  using S = LedPriority::Source;
  l.set(S::Error, true);
  l.set(S::Error, false);
  l.set(S::Co2Alert, true);
  TEST_ASSERT_TRUE(l.resolved());
  l.set(S::Co2Alert, false);
  TEST_ASSERT_FALSE(l.resolved());  // Error's earlier ON did not stick
}

static int countSends(NotifyThrottle &t, uint32_t startUs, int calls, uint32_t spanUs) {
  int n = 0;
  for (int i = 0; i < calls; i++) {
    uint32_t now = startUs + (uint32_t)((uint64_t)spanUs * i / calls);
    if (t.shouldSend(now)) n++;
  }
  return n;
}

void test_throttle_50hz() {
  NotifyThrottle t;
  t.setRateHz(50);
  int n = countSends(t, 1000, 120, 1000000);  // 120 calls over 1 s
  TEST_ASSERT_INT_WITHIN(1, 50, n);
}

void test_throttle_wraparound() {
  NotifyThrottle t;
  t.setRateHz(50);
  int n = countSends(t, 0xFFFFFFFFu - 500000u, 120, 1000000);  // wraps mid-second
  TEST_ASSERT_INT_WITHIN(1, 50, n);
}

void test_throttle_zero_never_sends() {
  NotifyThrottle t;
  t.setRateHz(0);
  TEST_ASSERT_EQUAL(0, countSends(t, 0, 120, 1000000));
}

void test_throttle_even_spacing() {
  NotifyThrottle t;
  t.setRateHz(10);
  TEST_ASSERT_TRUE(t.shouldSend(0));
  TEST_ASSERT_FALSE(t.shouldSend(50000));
  TEST_ASSERT_FALSE(t.shouldSend(99999));
  TEST_ASSERT_TRUE(t.shouldSend(100000));
  TEST_ASSERT_FALSE(t.shouldSend(100001));
}

void test_throttle_resumes_after_idle() {
  NotifyThrottle t;
  t.setRateHz(10);
  TEST_ASSERT_TRUE(t.shouldSend(0));
  TEST_ASSERT_TRUE(t.shouldSend(5000000));   // long gap: send once, no burst
  TEST_ASSERT_FALSE(t.shouldSend(5000001));
  TEST_ASSERT_FALSE(t.shouldSend(5050000));
  TEST_ASSERT_TRUE(t.shouldSend(5100000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_wrong_pin_stays_locked);
  RUN_TEST(test_unlock_per_connection);
  RUN_TEST(test_out_of_range_conn_always_locked);
  RUN_TEST(test_relock_on_disconnect);
  RUN_TEST(test_led_priority);
  RUN_TEST(test_led_highest_source_wins);
  RUN_TEST(test_led_independent_state);
  RUN_TEST(test_throttle_50hz);
  RUN_TEST(test_throttle_wraparound);
  RUN_TEST(test_throttle_zero_never_sends);
  RUN_TEST(test_throttle_even_spacing);
  RUN_TEST(test_throttle_resumes_after_idle);
  return UNITY_END();
}
