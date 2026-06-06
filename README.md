# FenceRanger Firmware

The FenceRanger monitors an electric fence and sends alerts over LoRaWAN when the fence voltage drops — indicating the energizer is off, a fault has occurred, or an animal has broken through.

Because it runs on a small solar-charged battery, it needs to balance two competing goals: **stay responsive to fence problems** while **conserving power** during bad weather or low-light periods.

---

## Alert behavior

The device measures the fence voltage every cycle (using the larger of the two polarity peaks) and classifies it against two thresholds: at or above **Recover V** is *healthy*, below **Alert V** is a *problem*, and the gap between them is a deliberate no-man's-land that counts as *not yet healthy* everywhere. This hysteresis stops the device oscillating when a fence hovers near the line.

Most of the time the fence is healthy and there is nothing to report, so the device only sends a brief heartbeat every **Idle Hb** cycles to confirm it is still alive. When the voltage drops below **Alert V**, the device immediately sends a burst of **Burst Sends** messages — making sure the network receives at least one — then settles into a faster heartbeat every **Alert Hb** cycles so the operator stays informed.

Once the voltage climbs back to **Recover V**, the device does not declare success right away: it waits for **Recovery** *consecutive* readings at or above Recover V, and **any** reading below Recover V — including one in the hysteresis band — restarts that count from zero. This filters out false all-clears during fence repair, when the energizer is switched on and off. Throughout this confirmation period the device keeps sending at the faster Alert Hb rate, so the operator is never left in silence while an outage is being resolved. When the count is finally reached, the device sends one explicit all-clear message and returns to the slow idle heartbeat.

### Rapid-drop early warning

The absolute thresholds above only react once the fence has already crossed **Alert V**. As a secondary, earlier signal, the device also watches how much the voltage drops between readings: if a reading is more than **10%** below the previous one it sends a one-off **rapid-drop** alert. The drop is measured purely against the previous reading regardless of how much time elapsed, so a gradual sag isn't hidden when the battery-throttled sleep interval stretches the time between measurements. This catches a fence that is failing (a developing short, a draining energizer, vegetation load) before it is fully down.

This only runs in the otherwise-healthy regime: if the device is already alerting (or the same drop crosses Alert V and triggers the normal burst), the rapid-drop alert stays silent so there is no duplicate. When a rapid-drop alert and a routine heartbeat fall on the same cycle, the drop alert replaces the heartbeat — it already carries the same measurement — so each wake sends at most one uplink. To stop a fence that flaps between two healthy levels from draining the battery, after one rapid-drop alert further ones are suppressed for the next **15 cycles**. The rapid-drop alert is its own message type (LoRaWAN fPort 2) carrying the previous voltage, the current voltage, and the battery voltage. These values are currently hardcoded (not configurable).

**Parameters that control this:**
- **Alert V / Recover V** — the thresholds that define "problem" and "healthy." The gap between them provides hysteresis so the device doesn't oscillate between states.
- **Burst Sends** — how many messages are sent in quick succession when an alert first triggers.
- **Idle Hb / Alert Hb** — heartbeat frequency in healthy vs. alert state (in cycles). Alert heartbeat is faster to keep the operator updated, and is also used while recovery is being confirmed.
- **Recovery** — how many consecutive readings at or above Recover V are required before declaring all-clear; any reading below Recover V restarts the count. Higher values filter out brief spikes during repair.

---

## Adaptive sleep behavior

To save power when the battery is low (e.g. several cloudy days in a row), the device automatically stretches its sleep interval. Rather than sleeping 60 seconds every cycle, it can sleep up to several minutes — reducing radio transmissions and processor wakeups. The throttling is proportional to battery voltage: a fully charged battery runs at normal speed, while a critically low battery runs at the maximum sleep interval.

To avoid overshooting in the other direction — waking up too frequently the moment the sun comes back — the device tracks battery voltage through a slow filter. The slow-tracked voltage drops instantly when the battery falls (to apply throttling immediately) but climbs back up gradually over many cycles. This means the device eases back to normal speed slowly, rather than snapping back the moment the battery ticks up slightly.

**Parameters that control this:**
- **Batt Full mV / Batt Low mV** — define the voltage range over which throttling scales. Above Full: no throttling. Below Low: maximum throttling. In between: linear interpolation.
- **Sleep Mult** — the maximum multiplier for the sleep interval at minimum battery. E.g. 60 s × 30 = up to 30 minutes per cycle.
- **Recovery N** — controls the speed of the gradual return to normal. The slow-tracked voltage rises by 1/N of the gap to the real voltage each cycle. N = 50 means it takes roughly 50+ cycles (and often much longer wall-clock time due to the stretched sleep intervals) to fully recover.

---

## Configuration

Parameters can be configured via four interfaces:

- **OLED menu** — single-button interface on the device (long-press on the info screen to enter the menu). Besides the config groups, the menu has two on-device bench tests — **Voltage Test** (live fence and battery readings) and **LoRa Test** (sends a confirmed uplink and shows join status plus signal quality — RSSI/SNR) — and a **WiFi AP** screen that turns the access point on
- **Web UI** — the WiFi AP is off by default in config mode; open the **WiFi AP** menu screen to switch it on (the AP runs only while that screen is shown), then connect to the device's AP and open `http://192.168.4.1`
- **LoRaWAN** — send a downlink from your network server; non-empty payload sets the values, empty payload queries current values and the device responds on the same fPort
- **AT commands** — serial interface over USB in config mode (115200 baud, `AT+HELP` for a command list)

Config mode is entered by pressing the boot button on startup, or via a LoRaWAN downlink on fPort 10. The fPort-10 path opens directly on the WiFi AP screen with the access point already on, so a remotely-triggered session is ready for a web update.

The fPort-10 payload is optional and lets each remote activation pick its own AP password and how long the AP stays up — so a leaked password can't be reused on another activation, and a remotely-woken device returns to monitoring on its own:

| Byte | Meaning |
|------|---------|
| 0 | AP active timeout in minutes — after this the device stops the AP and resumes normal monitoring (`0` or omitted = the default 1-hour config-mode timeout) |
| 1..N | AP password, ASCII — must be ≥8 chars (WPA2 minimum); empty or shorter falls back to the fixed default password |

An empty payload behaves exactly as before: fixed default password, 1-hour timeout. A button/menu-launched AP always uses the fixed default password.

### Joining a field-updater access point over LoRaWAN (fPort 9)

(Not to be confused with a LoRaWAN gateway — this is a plain WiFi access point.) Instead of becoming an AP, the device can be told to connect as a **station** to a **field-updater AP** — a box the crew brings into range that acts as the access point and pushes config/OTA to every monitor that joins it (far faster than walking up to each unit). The device connects, advertises itself (DHCP hostname **and** mDNS `FR-<DevEUI>` / `_http._tcp`, so the updater resolves `FR-<DevEUI>.local`), and serves the same web UI on its station IP. The OLED shows the SSID, connection status, and assigned IP.

Because the updater may be up to an hour from arriving, the device **waits at low power**: each cycle it scans once for the updater then deep-sleeps ~20 s before retrying, instead of staying awake. Normal fence monitoring is paused for the duration of the wait. Once the updater is in range it connects and serves; if the retry deadline passes first, it gives up and resumes monitoring.

The updater's SSID is baked into the firmware (default `FenceUpdater` — a network name, not a secret). The WPA2 **password is the per-activation secret carried in the downlink**: there is no fixed fallback, so firmware extracted from one device yields no reusable credentials. The operator sets the updater's password to match each activation.

| Byte | Meaning |
|------|---------|
| 0 | retry deadline in minutes — how long to keep scanning before giving up and resuming monitoring (`0` or omitted = 60 min) |
| 1 | SSID length `L` (`0` = use the baked-in default SSID) |
| 2 .. 1+L | SSID override (`L` bytes), if any |
| 2+L .. N | WPA2 password, ASCII — required, must be ≥8 chars (absent/shorter → join refused, device resumes monitoring) |

For example, to join the default updater AP with password `passw0rd`: `[10][0]['p','a','s','s','w','0','r','d']`.

**Radio config over LoRaWAN is trial-gated.** A radio-config downlink (fPort 16) is not applied permanently right away — it is staged and tested first, so a bad combination (e.g. RX LNA off, scanGuard too small, or a low data rate) can't lock the device out of downlinks. The device sends **confirmed uplinks** at the relevant data rate(s) and only keeps the new settings if it gets an ACK; otherwise it automatically reverts to the previous working settings. If the new settings enable ADR, it first proves DR0 works — DR0 needs a larger scan guard than higher rates, and ADR could drop the device down to it with no way back — and then proves the link a few times with ADR actually enabled. After the trial it re-sends the fPort 16 report, so the server can see whether the change stuck (new values) or reverted (old values). Changes made locally (OLED/Web UI/AT) apply immediately.

### Parameter reference

| Group | Parameter | Default | OLED | Web UI | LoRaWAN | AT command |
|-------|-----------|---------|:----:|:------:|---------|------------|
| Credentials | Device name | "no name" | — | ✓ | fPort 21 | `AT+DEVNAME` |
| | App Key | — | — | ✓ | — | `AT+APPKEY` |
| | Device EUI | (chip) | — | ✓ | — | `AT+DEVEUI` |
| Alert | Alert V | 3000 V | ✓ | ✓ | fPort 11 | `AT+ALERTV` |
| | Recover V | 3500 V | ✓ | ✓ | fPort 11 | `AT+RECOVERV` |
| | Burst Sends | 3 | ✓ | ✓ | fPort 12 | `AT+BURSTSENDS` |
| | Idle Heartbeat | 60 cycles | ✓ | ✓ | fPort 12 | `AT+IDLEHBCYCLES` |
| | Alert Heartbeat | 15 cycles | ✓ | ✓ | fPort 12 | `AT+ALERTHBCYCLES` |
| | Recovery Confirm | 30 cycles | ✓ | ✓ | fPort 12 | `AT+RECOVERCYCLES` |
| Sleep | Cycle Sleep | 60 s | ✓ | ✓ | fPort 13 | `AT+SLEEPINTERVAL` |
| | Batt Full mV | 4000 mV | ✓ | ✓ | fPort 14 | `AT+BATTFULLMV` |
| | Batt Low mV | 3300 mV | ✓ | ✓ | fPort 14 | `AT+BATTLOWMV` |
| | Sleep Mult | 30 | ✓ | ✓ | fPort 13 | `AT+BATTSLEEPMULT` |
| | Recovery N | 50 | ✓ | ✓ | fPort 13 | `AT+BATTRECOVERYN` |
| Measure | Measure Duration | 3000 ms | ✓ | ✓ | fPort 15 | `AT+MEASUREDURATION` |
| | Fence Cal % | 100 | ✓ | ✓ | fPort 15 | `AT+FENCECAL` |
| | Carrier Rev | 0 (v3.0) | ✓ | ✓ | — | `AT+CARRIERREV` |
| Radio | RX LNA | enabled | ✓ | ✓ | fPort 16 | `AT+RXLNA` |
| | TX Power | 10 dBm | ✓ | ✓ | fPort 16 | `AT+TXPOWER` |
| | Data Rate | 3 | ✓ | ✓ | fPort 16 | `AT+DR` |
| | ADR | enabled | ✓ | ✓ | fPort 16 | `AT+ADR` |
| | Syncword Public | enabled | ✓ | ✓ | fPort 16 | `AT+SYNCWORD` |
| | Scan Guard | 10 ms | ✓ | ✓ | fPort 16 | `AT+SCANGUARD` |
| Info | Firmware version | — | (info screen) | (shown) | fPort 20 (query) | `AT+VERSION?` |

**Carrier Rev** selects the carrier-board revision (0 = v3.0, opamp on GPIO 2; 1 = v3.1, opamp on
GPIO 6) so one firmware image runs on every revision. It is intentionally **never** settable over
LoRa — it is a physical-hardware attribute and a wrong remote value would silently break
measurements.

---

## Development

Install Platformio, the easiest method is probably using VSCode: https://platformio.org/platformio-ide

Make sure to get the git submodules, because lib/RadioBoards is a submodule:

```
git submodule update --init --recursive
```

### Without VSCode on MacOS with fish shell

Install using the script from: https://docs.platformio.org/en/latest/core/installation/methods/installer-script.html

To activate the new local environment with fish:

```
source ~/.platformio/penv/bin/activate.fish
```

Compile the firmware:

```
pio run -e heltec_wifi_lora_32_V4
```

The default target `heltec_wifi_lora_32_V4` builds for the KCT8103L PA (module V4.3). For the older GC1109 PA (module V4.2), build the `heltec_wifi_lora_32_V4_GC1109` variant (adds `-DUSE_GC1109_PA`). The `heltec_wifi_lora_32_V3` environment is legacy.

To generate compile_commands.json for other IDE support:

```
pio run -e heltec_wifi_lora_32_V4 -t compiledb
```

### Running the tests

Host-side unit tests run on your machine — no board needed — via PlatformIO's `native` environment:

```
pio test -e native
```

These cover the alert state machine (`src/alert.cpp`) under the Unity framework; see `test/test_alert/`. Only hardware-free logic is testable this way — modules that touch the ADC, radio, or NVS would need stubbing or on-target tests (`pio test -e heltec_wifi_lora_32_V4`).

### Make the language server work with Neovim

The only way I got this to work properly was by downloading the espressif clangd fork. Download the latest from here: https://github.com/espressif/llvm-project/releases. I used esp-21.1.3_20260304.

In your preferred directory:
```
git clone --depth 1 --branch esp-21.1.3_20260304 https://github.com/espressif/llvm-project.git
```

Compile it, takes a while:

```
cd llvm-project
cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra"
cmake --build build
```

Configure clangd by creating a `.clangd` file like this in this projects root directory:

```
require("lspconfig").clangd.setup({
	cmd = {
		"/Users/bart/git/espressif/llvm-project/build/bin/clangd",
		"--background-index",
		"--query-driver=/Users/bart/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++",
	},
})
```

And create a `.nvim.lua` file, also in this projects root directory:

```
-- .nvim.lua
vim.lsp.config("clangd", {
	cmd = {
		"/Users/bart/git/espressif/llvm-project/build/bin/clangd",
		"--background-index",
		"--query-driver=/Users/bart/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-g++",
	},
	root_markers = { "compile_commands.json" },
})
vim.lsp.enable("clangd")

vim.api.nvim_create_autocmd("BufWritePre", {
  pattern = { "*.cpp", "*.h", "*.hpp", "*.c" },
  callback = function()
    vim.lsp.buf.format()
  end,
})
```

Besides the language server configuration, it also configures auto-format on save.

The host-side unit tests (`test/`, built with `pio test -e native`) are not part of the firmware `compile_commands.json`, so clangd can't resolve their includes on its own and will flag `alert.h` / `unity.h` as missing. Add a `test/`-scoped fragment to your `.clangd` (YAML) so it can find the `src` and Unity headers:

```yaml
---
If:
  PathMatch: test/.*
CompileFlags:
  Add:
    - -Isrc
    - -I.pio/libdeps/native/Unity/src
    - -DDISABLE_LOGGING
```

The Unity headers only exist after running `pio test -e native` at least once.
