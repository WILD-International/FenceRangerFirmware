// Host-side unit tests for the rapid relative-drop rules of the alert state machine
// (processMeasurement, src/alert.cpp). Run with: pio test -e native
//
// A reading at least dropPercent below the previous one — while still above the alert
// threshold — opens the same Outage state as a threshold crossing, with the opening uplink
// sent as the distinct rapid-drop payload. From there the burst / alert-heartbeat /
// recovery-confirmation machinery is shared with a normal outage (pinned by test_alert);
// these tests pin the README "rapid drop" rules: when a drop opens an outage, which payload
// the opening uplink uses, and how the shared recovery plays out from a drop entry.

#include <unity.h>

#include "alert.h"

// Representative voltages relative to the thresholds below. A drop qualifies at >= 10%
// of the previous reading.
static const uint16_t HEALTHY_HIGH = 8000;
static const uint16_t HEALTHY_LOW = 7000;  // -12.5% from HEALTHY_HIGH -> qualifying drop
static const uint16_t GENTLE = 7800;       // -2.5% from HEALTHY_HIGH  -> no drop
static const uint16_t BAND = 2400;         // in [alert, recover) -> not-yet-healthy
static const uint16_t DOWN = 1800;         // < alertThresholdV -> problem

static AlertConfig makeCfg() {
  AlertConfig c;
  c.alertThresholdV = 2000;
  c.recoverThresholdV = 3000;
  c.burstSends = 3;
  c.idleHeartbeatCycles = 6;
  c.alertHeartbeatCycles = 3;
  c.recoveryConfirmCycles = 3;
  c.dropPercent = 10;
  return c;
}

// Per-cycle expectations: silent, a plain measurement uplink, or the rapid-drop payload.
static const int NONE = 0;
static const int MEAS = 1;
static const int DROP = 2;

// Feed each input reading through the state machine and assert the send decision matches
// expected[i]. Both arrays are passed by reference so their length N is deduced (no count
// to keep in sync), and requiring the same N for both makes a length mismatch a compile
// error.
template <size_t N>
static void runSequence(const AlertConfig& c, const uint16_t (&inputs)[N],
                        const int (&expected)[N]) {
  AlertState s;
  for (size_t i = 0; i < N; i++) {
    AlertDecision d = processMeasurement(inputs[i], c, s);
    int got = d.send ? (d.dropAlert ? DROP : MEAS) : NONE;
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected[i], got, "send mismatch at cycle index");
  }
}

void setUp(void) {}
void tearDown(void) {}

// The very first reading has no predecessor, so it can never count as a drop — the boot
// send is the plain idle heartbeat.
static void test_first_reading_is_not_a_drop() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY_HIGH};
  const int expected[] = {MEAS};
  runSequence(c, inputs, expected);
}

// On the firing cycle the decision carries the pre-drop reading for the fPort-2 payload.
static void test_drop_payload_values() {
  AlertConfig c = makeCfg();
  AlertState s;
  processMeasurement(HEALTHY_HIGH, c, s);
  AlertDecision d = processMeasurement(6800, c, s);  // -15%
  TEST_ASSERT_TRUE(d.send);
  TEST_ASSERT_TRUE(d.dropAlert);
  TEST_ASSERT_EQUAL_UINT16(HEALTHY_HIGH, d.previousFenceVoltage);
}

// Gentle drops and rises never fire: the device stays in its idle cadence.
static void test_gentle_drop_and_rise_stay_idle() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY_HIGH, GENTLE, HEALTHY_HIGH, 7400};
  const int expected[] = {MEAS, NONE, NONE, NONE};
  runSequence(c, inputs, expected);
}

// A qualifying drop to the hysteresis band opens an outage with the drop payload, then the
// normal alert cadence takes over: the rest of the burst (cycles 2-3), then heartbeats
// every alertHeartbeatCycles measured from the end of the burst (cycle 6).
static void test_drop_opens_outage_with_burst_and_heartbeats() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY_HIGH, BAND, BAND, BAND, BAND, BAND, BAND, BAND, BAND};
  const int expected[] = {MEAS, DROP, MEAS, MEAS, NONE, NONE, MEAS, NONE, NONE};
  runSequence(c, inputs, expected);
}

// A drop that also crosses the alert threshold opens a normal outage — the absolute alert
// wins and there is no drop payload (no duplicate message type for one event).
static void test_drop_below_alert_threshold_is_a_normal_outage() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY_HIGH, DOWN, DOWN};
  const int expected[] = {MEAS, MEAS, MEAS};
  runSequence(c, inputs, expected);
}

// A drop that stays above the recover threshold starts recovery counting immediately:
// the burst completes (cycles 2-3), recovery confirms on the third healthy reading after
// entry (cycle 4, the all-clear), and the idle heartbeat resumes a full interval later
// (cycle 10).
static void test_drop_to_healthy_level_recovers_with_all_clear() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY_HIGH, HEALTHY_LOW, HEALTHY_LOW, HEALTHY_LOW,
                             HEALTHY_LOW,  HEALTHY_LOW, HEALTHY_LOW, HEALTHY_LOW,
                             HEALTHY_LOW,  HEALTHY_LOW, HEALTHY_LOW};
  const int expected[] = {MEAS, DROP, MEAS, MEAS, MEAS, NONE, NONE, NONE, NONE, NONE, MEAS};
  runSequence(c, inputs, expected);
}

// The baseline keeps advancing through the outage, and there is no re-arm window: after the
// all-clear (cycle 4) a reading is compared against the latest level — 6500 is only -7.1%
// from 7000 (vs -18.75% from the stale 8000), so it stays quiet — and the next qualifying
// drop (5800, -10.8% from 6500) opens a fresh outage immediately.
static void test_baseline_advances_and_reentry_is_immediate() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY_HIGH, HEALTHY_LOW, HEALTHY_LOW, HEALTHY_LOW,
                             HEALTHY_LOW,  6500,        5800};
  const int expected[] = {MEAS, DROP, MEAS, MEAS, MEAS, NONE, DROP};
  runSequence(c, inputs, expected);
}

// While in Outage only the absolute thresholds matter: further qualifying relative drops
// (2900 -> 2400 -> 2000, each >= 10%) fire no drop payload and don't disturb the cadence —
// the burst continues as plain measurements.
static void test_drops_during_outage_stay_gated() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY_HIGH, 2900, BAND, 2000};
  const int expected[] = {MEAS, DROP, MEAS, MEAS};
  runSequence(c, inputs, expected);
}

// A drop-triggered outage shares the recovery rules: a band reading (cycle 3) restarts the
// confirmation count, so the all-clear lands three healthy cycles later (cycle 6, which is
// also a heartbeat cycle).
static void test_band_restarts_recovery_after_drop_entry() {
  AlertConfig c = makeCfg();
  const uint16_t inputs[] = {HEALTHY_HIGH, HEALTHY_LOW, HEALTHY_LOW, BAND,
                             HEALTHY_LOW,  HEALTHY_LOW, HEALTHY_LOW};
  const int expected[] = {MEAS, DROP, MEAS, MEAS, NONE, NONE, MEAS};
  runSequence(c, inputs, expected);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_reading_is_not_a_drop);
  RUN_TEST(test_drop_payload_values);
  RUN_TEST(test_gentle_drop_and_rise_stay_idle);
  RUN_TEST(test_drop_opens_outage_with_burst_and_heartbeats);
  RUN_TEST(test_drop_below_alert_threshold_is_a_normal_outage);
  RUN_TEST(test_drop_to_healthy_level_recovers_with_all_clear);
  RUN_TEST(test_baseline_advances_and_reentry_is_immediate);
  RUN_TEST(test_drops_during_outage_stay_gated);
  RUN_TEST(test_band_restarts_recovery_after_drop_entry);
  return UNITY_END();
}
