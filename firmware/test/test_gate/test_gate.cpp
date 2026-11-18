#include <unity.h>
#include <cstring>
#include "../../Gaiaball/src/logic/CommandGate.h"
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
  TEST_ASSERT_FALSE(g.unlock(0, bad, 4, 0));
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 3, 0));
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 5, 0));
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 0, 0));
  TEST_ASSERT_FALSE(g.isUnlocked(0));
}

void test_unlock_per_connection() {
  CommandGate g = makeGate();
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, 0));
  TEST_ASSERT_TRUE(g.isUnlocked(0));
  TEST_ASSERT_FALSE(g.isUnlocked(1));
}

void test_out_of_range_conn_always_locked() {
  CommandGate g = makeGate();
  TEST_ASSERT_FALSE(g.unlock(2, GOOD, 4, 0));
  TEST_ASSERT_FALSE(g.isUnlocked(2));
  TEST_ASSERT_FALSE(g.isUnlocked(255));
  g.onDisconnect(7);  // must not corrupt anything
}

void test_relock_on_disconnect() {
  CommandGate g = makeGate();
  g.unlock(0, GOOD, 4, 0);
  g.unlock(1, GOOD, 4, 0);
  g.onDisconnect(0);
  TEST_ASSERT_FALSE(g.isUnlocked(0));
  TEST_ASSERT_TRUE(g.isUnlocked(1));
}

static const uint8_t BAD[4] = {'9', '9', '9', '9'};

static void failN(CommandGate &g, uint8_t slot, int n, uint32_t nowMs) {
  for (int i = 0; i < n; i++) TEST_ASSERT_FALSE(g.unlock(slot, BAD, 4, nowMs));
}

void test_lockout_after_five_failures() {
  CommandGate g = makeGate();
  failN(g, 0, 5, 1000);
  // 6th attempt with the CORRECT pin inside the 30 s window is refused.
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 4, 1000));
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 4, 1000 + CommandGate::kLockoutMs - 1));
  TEST_ASSERT_FALSE(g.isUnlocked(0));
}

void test_four_failures_no_lockout() {
  CommandGate g = makeGate();
  failN(g, 0, 4, 1000);
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, 1001));
}

void test_unlock_allowed_after_lockout_expires() {
  CommandGate g = makeGate();
  failN(g, 0, 5, 1000);
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, 1000 + CommandGate::kLockoutMs));
  TEST_ASSERT_TRUE(g.isUnlocked(0));
}

void test_failure_after_lockout_rearms_it() {
  CommandGate g = makeGate();
  failN(g, 0, 5, 1000);
  const uint32_t t = 1000 + CommandGate::kLockoutMs;
  TEST_ASSERT_FALSE(g.unlock(0, BAD, 4, t));       // one compared attempt, fails
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 4, t + 1));  // locked out again
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, t + CommandGate::kLockoutMs));
}

void test_lockout_wraparound() {
  CommandGate g = makeGate();
  const uint32_t t0 = 0xFFFFFFFFu - 1000u;
  failN(g, 0, 5, t0);
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 4, t0 + 5000u));  // wrapped, 5 s later
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, t0 + CommandGate::kLockoutMs));
}

void test_bad_length_counts_as_failure() {
  CommandGate g = makeGate();
  for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(g.unlock(0, GOOD, 3, 1000));
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 4, 1000));
}

void test_success_resets_streak() {
  CommandGate g = makeGate();
  failN(g, 0, 4, 1000);
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, 1000));
  failN(g, 0, 4, 1000);  // streak restarted: 4 more failures still not a lockout
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, 1000));
}

void test_disconnect_resets_lockout() {
  CommandGate g = makeGate();
  failN(g, 0, 5, 1000);
  g.onDisconnect(0);
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, 1001));
}

void test_lockout_slot_isolation() {
  CommandGate g = makeGate();
  failN(g, 0, 5, 1000);
  TEST_ASSERT_FALSE(g.unlock(0, GOOD, 4, 1001));
  TEST_ASSERT_TRUE(g.unlock(1, GOOD, 4, 1001));
  TEST_ASSERT_TRUE(g.isUnlocked(1));
  TEST_ASSERT_FALSE(g.isUnlocked(0));
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

// --- command access policy (App::dispatch) ------------------------------------

void test_anchor_is_open_while_locked() {
  // 0x17 needs no PIN: the site anchors the map without ever unlocking.
  TEST_ASSERT_EQUAL_HEX8(0x17, kCmdAnchor);
  TEST_ASSERT_TRUE(commandAccess(kCmdAnchor) == CommandAccess::Open);
  TEST_ASSERT_TRUE(commandAllowed(kCmdAnchor, false));
  TEST_ASSERT_TRUE(commandAllowed(kCmdAnchor, true));
}

void test_gated_opcodes_refused_while_locked() {
  const uint8_t gated[] = {kCmdZero,   kCmdSave,        kCmdFactoryReset, kCmdReboot,
                           kCmdMountCal, kCmdMagCalStart, kCmdMagCalStop};
  const uint8_t values[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16};
  for (size_t i = 0; i < sizeof gated; i++) {
    TEST_ASSERT_EQUAL_HEX8(values[i], gated[i]);
    TEST_ASSERT_TRUE(commandAccess(gated[i]) == CommandAccess::PinRequired);
    TEST_ASSERT_FALSE(commandAllowed(gated[i], false));
    TEST_ASSERT_TRUE(commandAllowed(gated[i], true));
  }
}

void test_unknown_opcodes_never_run() {
  // 0x00/0x01: the removed legacy LED command; 0x20 (unlock) never reaches the App task.
  const uint8_t unknown[] = {0x00, 0x01, 0x0F, 0x18, 0x20, 0xFF};
  for (uint8_t op : unknown) {
    TEST_ASSERT_TRUE(commandAccess(op) == CommandAccess::Unknown);
    TEST_ASSERT_FALSE(commandAllowed(op, false));
    TEST_ASSERT_FALSE(commandAllowed(op, true));
  }
}


// --- state(): what the config read reports as 0x37 (unlockState) ----------------------

static uint8_t st(const CommandGate &g, uint8_t conn, uint32_t now) {
  return static_cast<uint8_t>(g.state(conn, now));
}

void test_state_locked_then_unlocked_per_connection() {
  CommandGate g = makeGate();
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(GateState::Locked));
  TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(GateState::Unlocked));
  TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(GateState::LockedOut));
  TEST_ASSERT_EQUAL_UINT8(0, st(g, 0, 0));
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, 0));
  TEST_ASSERT_EQUAL_UINT8(1, st(g, 0, 10));
  TEST_ASSERT_EQUAL_UINT8(0, st(g, 1, 10));  // the other connection stays locked
  g.onDisconnect(0);
  TEST_ASSERT_EQUAL_UINT8(0, st(g, 0, 20));
}

void test_state_wrong_pin_is_locked_until_the_fifth() {
  CommandGate g = makeGate();
  const uint8_t bad[4] = {'9', '9', '9', '9'};
  for (int i = 0; i < 4; i++) TEST_ASSERT_FALSE(g.unlock(0, bad, 4, 1000));
  TEST_ASSERT_EQUAL_UINT8(0, st(g, 0, 1000));
  TEST_ASSERT_FALSE(g.unlock(0, bad, 4, 1000));
  TEST_ASSERT_EQUAL_UINT8(2, st(g, 0, 1000));
  TEST_ASSERT_EQUAL_UINT8(2, st(g, 0, 1000 + CommandGate::kLockoutMs - 1));
  TEST_ASSERT_EQUAL_UINT8(0, st(g, 0, 1000 + CommandGate::kLockoutMs));  // expired: one try allowed
}

void test_state_lockout_wraparound() {
  CommandGate g = makeGate();
  const uint8_t bad[4] = {'9', '9', '9', '9'};
  const uint32_t t = 0xFFFFFF00u;
  for (int i = 0; i < 5; i++) g.unlock(0, bad, 4, t);
  TEST_ASSERT_EQUAL_UINT8(2, st(g, 0, t + 0x200u));  // wrapped, 512 ms later: still out
}

void test_wrong_pin_relocks_an_unlocked_connection() {
  // Someone checking a new PIN on a connection that is already unlocked must learn it is wrong.
  CommandGate g = makeGate();
  const uint8_t bad[4] = {'9', '9', '9', '9'};
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, 0));
  TEST_ASSERT_FALSE(g.unlock(0, bad, 4, 10));
  TEST_ASSERT_FALSE(g.isUnlocked(0));
  TEST_ASSERT_EQUAL_UINT8(0, st(g, 0, 10));
  TEST_ASSERT_TRUE(g.unlock(0, GOOD, 4, 20));
  TEST_ASSERT_EQUAL_UINT8(1, st(g, 0, 20));
}

void test_state_out_of_range_conn_is_locked() {
  CommandGate g = makeGate();
  TEST_ASSERT_EQUAL_UINT8(0, st(g, 2, 0));
  TEST_ASSERT_EQUAL_UINT8(0, st(g, 255, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_wrong_pin_stays_locked);
  RUN_TEST(test_unlock_per_connection);
  RUN_TEST(test_out_of_range_conn_always_locked);
  RUN_TEST(test_relock_on_disconnect);
  RUN_TEST(test_lockout_after_five_failures);
  RUN_TEST(test_four_failures_no_lockout);
  RUN_TEST(test_unlock_allowed_after_lockout_expires);
  RUN_TEST(test_failure_after_lockout_rearms_it);
  RUN_TEST(test_lockout_wraparound);
  RUN_TEST(test_bad_length_counts_as_failure);
  RUN_TEST(test_success_resets_streak);
  RUN_TEST(test_disconnect_resets_lockout);
  RUN_TEST(test_lockout_slot_isolation);
  RUN_TEST(test_throttle_50hz);
  RUN_TEST(test_throttle_wraparound);
  RUN_TEST(test_throttle_zero_never_sends);
  RUN_TEST(test_throttle_even_spacing);
  RUN_TEST(test_throttle_resumes_after_idle);
  RUN_TEST(test_anchor_is_open_while_locked);
  RUN_TEST(test_gated_opcodes_refused_while_locked);
  RUN_TEST(test_unknown_opcodes_never_run);
  RUN_TEST(test_state_locked_then_unlocked_per_connection);
  RUN_TEST(test_state_wrong_pin_is_locked_until_the_fifth);
  RUN_TEST(test_state_lockout_wraparound);
  RUN_TEST(test_state_out_of_range_conn_is_locked);
  RUN_TEST(test_wrong_pin_relocks_an_unlocked_connection);
  return UNITY_END();
}
