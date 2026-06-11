// Host-side unit tests for the fence-voltage alert state machine
// (processMeasurement, src/alert.cpp). Run with: pio test -e native
//
// These tests encode the "Alert behavior" rules from README.md so the documented
// business logic stays pinned down. Small parameter values are used (rather than the
// production defaults) to keep the cadences short and easy to reason about.
//
// Every test drives an explicit sequence of fence-voltage readings and checks the
// resulting per-cycle send decisions against an explicit expected[] array (1 = the
// device sends an uplink that cycle, 0 = it stays quiet).

#include <unity.h>

#include <cstddef>

#include "alert.h"

// Representative voltages relative to the thresholds below.
static const uint16_t DOWN = 1800;     // < alertThresholdV  -> problem
static const uint16_t BAND = 2400;     // in [alert, recover) -> not-yet-healthy
static const uint16_t HEALTHY = 4000;  // >= recoverThresholdV -> healthy

static AlertConfig makeCfg() {
  AlertConfig c;
  c.alertThresholdV = 2000;
  c.recoverThresholdV = 3000;
  c.burstSends = 3;
  c.idleHeartbeatCycles = 6;
  c.alertHeartbeatCycles = 3;
  c.recoveryConfirmCycles = 3;
  // Neutralize the rapid-drop entry path so these tests exercise the absolute
  // thresholds alone (e.g. HEALTHY -> BAND is a 40% drop that must not open an
  // outage here); the rapid-drop rules have their own suite (test_rapid_drop).
  c.dropPercent = 100;
  return c;
}

static bool step(AlertState& s, const AlertConfig& c, uint16_t v) {
  return processMeasurement(v, c, s).send;
}

// Feed each input reading through the state machine and assert the send decision matches
// expected[i]. Both arrays are passed by reference so their length N is deduced (no count
// to keep in sync), and requiring the same N for both makes a length mismatch a compile
// error.
template <size_t N>
static void runSequence(const AlertConfig& c, const uint16_t (&inputs)[N],
                        const int (&expected)[N]) {
  AlertState s;
  for (size_t i = 0; i < N; i++) {
    int got = step(s, c, inputs[i]) ? 1 : 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected[i], got, "send mismatch at cycle index");
  }
}

void setUp(void) {}
void tearDown(void) {}

// A freshly booted device sends on its very first cycle, even when healthy, then goes
// quiet until the next idle heartbeat.
static void test_boot_sends_first_cycle() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY, HEALTHY};
  const int expected[] = {1, 0};
  runSequence(c, inputs, expected);
}

// While healthy, heartbeats fire every idleHeartbeatCycles (6): cycles 0, 6, 12.
static void test_idle_heartbeat_cadence() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY,
                             HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY};
  const int expected[] = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1};
  runSequence(c, inputs, expected);
}

// A reading in the hysteresis band while idle is treated as not-a-problem: it does NOT
// raise an alert (no burst), the device simply stays in its idle cadence.
static void test_band_while_idle_stays_idle() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY, BAND, BAND};
  const int expected[] = {1, 0, 0};
  runSequence(c, inputs, expected);
}

// An outage opens with a burst of burstSends (3) messages, then a heartbeat every
// alertHeartbeatCycles (3) measured from the END of the burst -> sends at 0,1,2,5,8,11.
static void test_burst_then_heartbeat_anchored_after_burst() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {DOWN, DOWN, DOWN, DOWN, DOWN, DOWN, DOWN,
                             DOWN, DOWN, DOWN, DOWN, DOWN, DOWN};
  const int expected[] = {1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
  runSequence(c, inputs, expected);
}

// Recovery requires recoveryConfirmCycles (3) consecutive healthy readings. The all-clear
// is the send at index 6 (a non-heartbeat cycle, so it can only be the confirmation);
// afterwards the device is back in idle and its heartbeat resumes (index 12, six cycles
// after the all-clear). Index 5 is a recovery-period heartbeat.
static void test_recovery_needs_consecutive_then_all_clear() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {DOWN,    DOWN,    DOWN,    DOWN,    HEALTHY, HEALTHY, HEALTHY,
                             HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY};
  const int expected[] = {1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1};
  runSequence(c, inputs, expected);
}

// Any reading below recover threshold (including the band) restarts the recovery count.
// The band at index 6 resets it, so confirmation (the non-heartbeat send at index 9) lands
// three cycles later than it would have without the band reset.
static void test_band_restarts_recovery_count() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {DOWN,    DOWN,    DOWN,    DOWN,    HEALTHY, HEALTHY, BAND,
                             HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY};
  const int expected[] = {1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 0};
  runSequence(c, inputs, expected);
}

// The device is never silent mid-outage: heartbeats keep firing at the alert cadence
// while recovery is being confirmed. With a high confirm count it never returns to idle,
// yet the send pattern matches a sustained outage (0,1,2,5,8,11).
static void test_heartbeats_continue_during_recovery() {
  AlertConfig c = makeCfg();
  c.recoveryConfirmCycles = 100;  // never confirms within the window
  const uint16_t inputs[] = {DOWN,    HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY,
                             HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY};
  const int expected[] = {1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
  runSequence(c, inputs, expected);
}

// Dipping back below threshold during recovery resumes alerting WITHOUT a fresh burst:
// after the bounce (index 5 onward, all down) there is no new run of three sends, only
// the ongoing heartbeats at indices 5 and 8.
static void test_bounce_does_not_restart_burst() {
  AlertConfig c = makeCfg();
  c.recoveryConfirmCycles = 100;
  const uint16_t inputs[] = {DOWN, DOWN, DOWN, DOWN, HEALTHY, DOWN, DOWN, DOWN, DOWN, DOWN};
  const int expected[] = {1, 1, 1, 0, 0, 1, 0, 0, 1, 0};
  runSequence(c, inputs, expected);
}

// After the all-clear, the next idle heartbeat is a full idleHeartbeatCycles (6) away --
// the all-clear does not immediately trigger a second send. Here recovery confirms on the
// first healthy reading (index 4); the device is then quiet for five cycles and resumes
// the idle heartbeat at index 10.
static void test_all_clear_then_quiet_until_next_idle_heartbeat() {
  AlertConfig c = makeCfg();
  c.recoveryConfirmCycles = 1;
  const uint16_t inputs[] = {DOWN,    DOWN,    DOWN,    DOWN,    HEALTHY, HEALTHY,
                             HEALTHY, HEALTHY, HEALTHY, HEALTHY, HEALTHY};
  const int expected[] = {1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1};
  runSequence(c, inputs, expected);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_sends_first_cycle);
  RUN_TEST(test_idle_heartbeat_cadence);
  RUN_TEST(test_band_while_idle_stays_idle);
  RUN_TEST(test_burst_then_heartbeat_anchored_after_burst);
  RUN_TEST(test_recovery_needs_consecutive_then_all_clear);
  RUN_TEST(test_band_restarts_recovery_count);
  RUN_TEST(test_heartbeats_continue_during_recovery);
  RUN_TEST(test_bounce_does_not_restart_burst);
  RUN_TEST(test_all_clear_then_quiet_until_next_idle_heartbeat);
  return UNITY_END();
}
