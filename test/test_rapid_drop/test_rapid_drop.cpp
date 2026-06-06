// Host-side unit tests for the rapid relative-drop early warning
// (detectRapidDrop, src/alert.cpp). Run with: pio test -e native
//
// detectRapidDrop fires when a reading is at least a configured percentage below the previous
// one, while the fence is still healthy, with a cooldown (in cycles) against a flapping fence.
// These tests pin the README "rapid drop" rules.

#include <unity.h>

#include "alert.h"

static RapidDropConfig makeConfig() {
  RapidDropConfig config;
  config.dropPercent = 10;    // fire at >= 10% drop vs the previous reading
  config.cooldownCycles = 3;  // suppress further alerts for 3 cycles after one fires
  return config;
}

void setUp(void) {}
void tearDown(void) {}

// The very first reading has no predecessor, so it can never fire.
static void test_first_reading_is_silent() {
  RapidDropState state;
  RapidDropConfig config = makeConfig();
  TEST_ASSERT_FALSE(detectRapidDrop(8000, true, config, state));
}

// A drop of more than 10% from the previous reading fires.
static void test_drop_above_threshold_fires() {
  RapidDropState state;
  RapidDropConfig config = makeConfig();
  detectRapidDrop(8000, true, config, state);                    // baseline
  TEST_ASSERT_TRUE(detectRapidDrop(7000, true, config, state));  // -12.5% -> fire
}

// A gentle drop under the threshold does not fire.
static void test_gentle_drop_does_not_fire() {
  RapidDropState state;
  RapidDropConfig config = makeConfig();
  detectRapidDrop(8000, true, config, state);
  TEST_ASSERT_FALSE(detectRapidDrop(7800, true, config, state));  // -2.5% -> no
}

// A rising voltage never fires.
static void test_rise_does_not_fire() {
  RapidDropState state;
  RapidDropConfig config = makeConfig();
  detectRapidDrop(7000, true, config, state);
  TEST_ASSERT_FALSE(detectRapidDrop(8000, true, config, state));
}

// While not healthy (the FSM is not Idle -- i.e. still alerting or confirming recovery after an
// outage) a qualifying drop is suppressed. The baseline still advances, so once healthy again a
// fresh qualifying drop fires normally.
static void test_not_healthy_gates_firing() {
  RapidDropState state;
  RapidDropConfig config = makeConfig();
  detectRapidDrop(8000, true, config, state);
  TEST_ASSERT_FALSE(
      detectRapidDrop(7000, false, config, state));  // qualifying drop, not Idle -> no
  // baseline is now 7000; a further drop above threshold while healthy fires (-14%)
  TEST_ASSERT_TRUE(detectRapidDrop(6000, true, config, state));
}

// After firing, further alerts are suppressed for cooldownCycles even if the fence keeps
// bouncing; once that many cycles pass a new qualifying drop fires again.
static void test_cooldown_suppresses_then_rearms() {
  RapidDropState state;
  RapidDropConfig config = makeConfig();  // cooldownCycles = 3
  detectRapidDrop(8000, true, config, state);
  TEST_ASSERT_TRUE(detectRapidDrop(7000, true, config, state));  // fire, start cooldown
  detectRapidDrop(8000, true, config, state);                    // bounce up (cooldown ticking)
  TEST_ASSERT_FALSE(
      detectRapidDrop(7000, true, config, state));               // qualifying drop, within cooldown
  detectRapidDrop(8000, true, config, state);                    // bounce up (cooldown elapses)
  TEST_ASSERT_TRUE(detectRapidDrop(7000, true, config, state));  // qualifying drop, re-armed
}

// On a firing cycle the reported info matches the readings.
static void test_info_payload_values() {
  RapidDropState state;
  RapidDropConfig config = makeConfig();
  RapidDropInfo info = {};
  detectRapidDrop(8000, true, config, state);
  TEST_ASSERT_TRUE(detectRapidDrop(6800, true, config, state, &info));  // -15%
  TEST_ASSERT_EQUAL_UINT16(8000, info.previousVoltage);
  TEST_ASSERT_EQUAL_UINT16(6800, info.currentVoltage);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_reading_is_silent);
  RUN_TEST(test_drop_above_threshold_fires);
  RUN_TEST(test_gentle_drop_does_not_fire);
  RUN_TEST(test_rise_does_not_fire);
  RUN_TEST(test_not_healthy_gates_firing);
  RUN_TEST(test_cooldown_suppresses_then_rearms);
  RUN_TEST(test_info_payload_values);
  return UNITY_END();
}
