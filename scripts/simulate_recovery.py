#!/usr/bin/env python3
"""
Simulates adaptive sleep interval recovery after battery voltage step.

Mirrors the logic in src/main.cpp:
  - slowBattMv drops instantly when voltage falls
  - slowBattMv recovers at 1/N of the gap per cycle
  - sleep interval is lerped between minSleep and maxSleep based on slowBattMv
"""

BATT_FULL_MV = 4000
BATT_LOW_MV = 3300
BATT_MAX_MULT = 30.0
BATT_RECOVERY_N = 50

BASE_SLEEP_S = 60
ACTUAL_BATT_MV = 4200  # instant step to this voltage after starting at worst case

min_sleep = BASE_SLEEP_S
max_sleep = BASE_SLEEP_S * BATT_MAX_MULT

slow_batt = float(BATT_LOW_MV)
elapsed_s = 0.0


def fmt_elapsed(s):
    h = int(s) // 3600
    m = (int(s) % 3600) // 60
    return f"{h}h {m:02}m"


print(f"Recovery simulation")
print(f"  Start voltage: {BATT_LOW_MV} mV  ->  step to {ACTUAL_BATT_MV} mV")
print(f"  Base sleep:    {BASE_SLEEP_S} s")
print(f"  Max sleep:     {max_sleep:.0f} s  (at {BATT_LOW_MV} mV or below)")
print(f"  N:             {BATT_RECOVERY_N}")
print()
print(f"{'Cycle':>6}  {'Elapsed':>8}  {'slowBatt':>10}  {'Sleep':>8}  {'Mult':>6}")
print("-" * 48)

for cycle in range(10000):
    batt_t = max(0.0, min(1.0, (BATT_FULL_MV - slow_batt) / (BATT_FULL_MV - BATT_LOW_MV)))
    adapted_sleep = min_sleep + batt_t * (max_sleep - min_sleep)
    mult = adapted_sleep / min_sleep

    print(f"{cycle:>6}  {fmt_elapsed(elapsed_s):>8}  {slow_batt:>8.0f} mV  {adapted_sleep:>6.1f} s  {mult:>5.2f}x")

    if adapted_sleep <= min_sleep + 0.5:
        break

    elapsed_s += adapted_sleep

    if ACTUAL_BATT_MV < slow_batt:
        slow_batt = float(ACTUAL_BATT_MV)
    else:
        slow_batt += (ACTUAL_BATT_MV - slow_batt) / BATT_RECOVERY_N

print()
print(f"Recovered to normal sleep after {cycle} cycles ({fmt_elapsed(elapsed_s)} total elapsed)")
