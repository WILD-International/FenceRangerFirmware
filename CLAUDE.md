# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build commands

PlatformIO must be activated before using `pio`. On macOS with fish shell use `activate.fish`; for bash/zsh use `activate`:

```bash
# fish
source ~/.platformio/penv/bin/activate.fish && pio run -e heltec_wifi_lora_32_V4

# bash (also works from Claude Code's Bash tool)
bash -c "source ~/.platformio/penv/bin/activate && pio run -e heltec_wifi_lora_32_V4"
```

The primary target is `heltec_wifi_lora_32_V4`. A second variant `heltec_wifi_lora_32_V4_GC1109` adds `-DUSE_GC1109_PA` for boards with the GC1109 PA (V4.2); the default target uses the KCT8103L PA (V4.3+). The `heltec_wifi_lora_32_V3` environment is legacy.

Generate `compile_commands.json` for IDE/language server support:
```bash
pio run -e heltec_wifi_lora_32_V4 -t compiledb
```

Flash and monitor:
```bash
pio run -e heltec_wifi_lora_32_V4 -t upload && pio device monitor -e heltec_wifi_lora_32_V4
```

Host-side unit tests (run on the laptop, no board needed):
```bash
pio test -e native
```
The `native` env (`platformio.ini`) compiles only the pure modules it tests (via `build_src_filter`, with `DISABLE_LOGGING` so no Arduino/Serial is pulled in) and runs them under Unity. Only hardware-free logic is testable this way — `src/alert.cpp` is covered (`test/test_alert/`); modules that touch ADC/radio/NVS need stubbing or on-target tests.

## Architecture

The device is an ESP32-S3 (Heltec LoRa V4) that measures electric fence voltage, then deep-sleeps between cycles. Every wake is a full reset — `setup()` runs, does work, then calls `gotoSleep()`. There is no `loop()`.

### Deep sleep and RTC memory

All state that must survive deep sleep must be declared `RTC_DATA_ATTR`. Regular globals, class members, and `static` locals are re-initialised on every wake. Almost all such state is centralized in a single `RTC_DATA_ATTR RtcState rtc` (`src/rtc_state.h` / `src/rtc_state.cpp`) — alert state machine, slow battery filter, boot counters, LoRaWAN session, button flag, device mode, and the radio-config trial. Other modules read and write fields on `rtc` rather than declaring their own `RTC_DATA_ATTR` variables.

### Main cycle (`src/main.cpp`)

Each wake:
1. Checks wakeup reason — button press (`ESP_SLEEP_WAKEUP_EXT0`) enters config mode
2. Runs `MeasureProgram` — samples ADC for fence voltage and battery voltage
3. Calls `state.updateSlowBatt()` — updates the slow-tracking battery filter
4. Calls `processMeasurement()` (`src/alert.cpp`) — decides whether to transmit this cycle
5. If transmitting: initialises radio, calls `comms.sendMeasurement()`, handles any downlink
6. Saves LoRaWAN session to RTC memory, puts radio to sleep, calls `gotoSleep(adaptedSleepMs)`

The sleep duration is `state.computeAdaptedSleepMs()` — a value between `config.getSleepIntervalMs()` and `sleepInterval × battSleepMult` depending on battery voltage.

Two branches divert from this normal flow early in `setup()`: a button press / missing AppKey / fPort-10 downlink enters config mode (`runConfigMode`, then a short sleep), and an active radio-config trial (`rtc.radioTrial.active`) runs `Comms::runRadioTrialCycle` instead of a normal send (see the trial-and-rollback section).

### Transmission policy (`src/alert.cpp`)

`processMeasurement()` (declared in `src/alert.h`) is a free function over `(fenceVoltage, AlertConfig, AlertState)` kept in `rtc.alertState` so it survives deep sleep. It is a **two-state** machine — `Idle` (healthy, slow heartbeat) and `Outage` (fence problem) — where Outage covers both active alerting and recovery-confirmation, distinguished only by `recoveredCycles` (0 = alerting, >0 = confirming). **The authoritative, unambiguous ruleset is the "Alert behavior" section of `README.md`** — keep it and the code in sync. Non-obvious invariants worth knowing before touching it:

- Two thresholds (alert < recover) give hysteresis; the band between them counts as **not-healthy** everywhere.
- One outage counter runs the whole Outage state, so the alert-rate heartbeat keeps firing right through recovery (the device is never silent mid-outage), and a dip back below threshold during recovery does **not** restart the burst.
- Recovery needs *consecutive* readings ≥ recover threshold; any lower reading (band included) restarts the count. On confirm it sends one explicit all-clear and returns to Idle.

All parameters live in NVS and are configurable via the web UI and OLED menu.

A rapid relative drop is the **second way into Outage**: a reading at least **Drop %** below the previous one (no time normalisation) while still above the alert threshold opens the same Outage state, with the opening uplink sent on fPort 2 instead of as a plain measurement — everything after (burst, alert heartbeat, recovery, all-clear) is shared. Detection runs only from Idle and a coincident threshold crossing takes precedence, so one event never produces both message types. There is deliberately no re-arm window: the recovery-confirmation period is the only damping against a flapping fence.

### Adaptive sleep (`src/state.cpp`)

Two methods on `State` implement battery-based sleep throttling:

- `state.updateSlowBatt()` — called after each measurement. `slowBattMv` drops instantly when `batt_max_mv` falls below it, but rises at only `1/battRecoveryN` of the gap per cycle. This asymmetric filter ensures throttling is applied immediately but lifted gradually.
- `state.computeAdaptedSleepMs()` — pure computation (no side effects). Lerps between `minSleep` and `minSleep × battSleepMult` based on where `slowBattMv` falls between `battLowMv` and `battFullMv`.

`slowBattMv` survives sleep as `rtc.slowBattMv` (`src/rtc_state.h`).

### LoRaWAN (`src/comms.cpp`)

Uses RadioLib with OTAA on Region `EU868` / `subBand` 0 (`src/comms.h`). The session is persisted in `RTC_DATA_ATTR` (`rtc.lwSession`) and nonces in NVS (`Preferences`). Join is attempted with backoff: every 5th boot for the first 30 boots, then every 10th boot. Normal uplinks are unconfirmed (no ACK).

Downlinks are dispatched by fPort in `Comms::send()` — for a config fPort, an empty payload is a query (the device replies on the same fPort via `sendConfigReport()`) and a non-empty payload sets values. fPort 10 triggers OTA/config mode. For the authoritative port map and payload layouts, read the `switch` in `src/comms.cpp` and `responseFPort()`; `kMaxDownlinkChain` (`src/comms.h`) bounds how many queued downlinks one wake processes.

The public send API is `comms.sendMeasurement(battMv, fenceV, display, downlink)` — it encodes the 3-byte payload internally and calls the private `send()`. The raw `send()` method is private.

#### Radio-config trial-and-rollback (commit-on-confirm)

A `SetRadioConfig` downlink (fPort 16) does **not** write NVS directly — that could lock the device out of downlinks (e.g. RX LNA off, scanGuard too small, or a low DR) with no way to send a fix. Instead the candidate is **staged in volatile RTC** (`rtc.radioTrial`, see `src/rtc_state.h`) and only **committed to NVS once a confirmed-uplink ACK proves the link still works both ways**. Timeout, crash, or power loss all fall back to the last confirmed-good NVS config automatically.

Each trial wake (`Comms::runRadioTrialCycle`, driven from `main.cpp` when `rtc.radioTrial.active`) sends one **confirmed** uplink (`sendReceive() > 0` = ACK). A phase passes once it collects its required ACKs (1 for a forced-DR phase, `ADR_PHASE_CONFIRMS` = 3 for the ADR phase); `RADIO_TRIAL_ATTEMPTS` (3) consecutive misses revert it (a success refills that budget). If the candidate **enables ADR**, the trial first probes **DR0** once and then runs several confirmed round-trips **with ADR actually enabled** (one per wake, to respect EU868 duty cycle). The extra ADR confirms guard against a fluke ACK on a marginal link; they don't exercise ADR's own rate loop (that needs far more frames than a trial can run). DR0 is singled out because it was found to need a larger `scanGuard` than the higher data rates (DR2–DR5) — a value that works at those can silently lose DR0 downlinks — and ADR can push the device down to DR0 with no way back (the climb-up commands are downlinks). So we confirm DR0 works under the candidate before committing. `Comms::effectiveRadioConfig()` resolves which settings `begin()`/`lwActivate()` apply: the NVS config normally, or the staged candidate (ADR forced off, DR pinned to the phase) during a trial. After a trial concludes, the device re-sends the fPort 16 radio-config report once (`rtc.trialResultPending`) so the server can tell commit (new settings) from revert (old). Only the LoRaWAN path is trial-gated; OLED/Web/AT radio changes still commit immediately, and entering config mode cancels any active trial.

### Uplink payloads

**Measurement — fPort 1 (3 bytes)**

| Byte | Content |
|------|---------|
| 0 | Battery voltage compressed: `(mv - 1950) / 10`, covering 1950–4500 mV in 10 mV steps |
| 1–2 | Fence voltage in volts, big-endian uint16 |

Encoding lives in `Comms::sendMeasurement()`. Value selection (which min/max reading to use) stays in `main.cpp`.

**Rapid-drop early warning — fPort 2 (5 bytes)**

| Byte | Content |
|------|---------|
| 0 | Battery, same compression as fPort 1 |
| 1–2 | Previous fence voltage, big-endian uint16 |
| 3–4 | Current fence voltage, big-endian uint16 |

Encoding lives in `Comms::sendRapidDropAlert()`; the trigger is the rapid-drop path of `processMeasurement()` (see the transmission-policy section).

### Config mode (`src/config_mode.cpp`)

Config mode is a single free-function event loop (`runConfigMode`, `src/config_mode.cpp`) with its collaborators passed in explicitly. It is entered on button press, when no AppKey is stored, or by a WiFi/OTA downlink — which stages a one-shot request in `rtc` that routes config mode straight to a WiFi screen on the next wake.

The WiFi + HTTP server (`WebUi`) is **off by default** — it comes up only while a WiFi screen is active, so most config sessions never bring WiFi up. AT commands over Serial (`src/at_commands.cpp`, a table-driven dispatcher; `AT+HELP` lists them) are always available. The loop exits on a user action or a timeout and returns to the normal sleep cycle. Config is stored in NVS via `Preferences`.

#### OLED menu UX (single-button)

A single-button state machine (`MenuState` in `src/config_mode.cpp`): **INFO → BROWSE → SUBMENU → EDIT**, plus two bench screens **VOLT_TEST** and **LORA_TEST** and the **WIFI_AP_SCREEN**. Throughout, a short tap advances/cycles and a long press (≥500 ms) activates/saves.

- **INFO** (home): device name, DevEUI, version. Short tap exits config mode; long press enters BROWSE.
- **BROWSE**: top-level list of the config groups followed by the two bench tests, the WiFi AP screen, and a Back item; longer than the screen, so it scrolls. Long press opens a group's SUBMENU, starts a bench test/WiFi AP, or returns to INFO.
- **SUBMENU**: the active group's items plus a trailing Back; long press edits an item or returns to BROWSE.
- **EDIT**: cycles a parameter through predefined options (plus a CANCEL slot); long press commits.

The menu is **data-driven** — do not hardcode the item list from this doc. The groups and their parameters are the `GROUPS[]` / `*Items[]` arrays at the top of `src/config_mode.cpp`; the option values for each item live in the `*_OPTIONS[]` arrays there. Three `MenuItem` kinds behave differently (see `isToggleItem` / `isActionItem`):

- **Normal**: `get`/`set` a value chosen from its `options`, via the EDIT screen.
- **Toggle** (`options == {0,1}`): rendered ON/OFF; a long press flips it directly, no EDIT screen.
- **Action** (`optCount == 0`, e.g. "Sleep now"): shows no value; a long press runs its `set()` callback. "Sleep now" exits config mode and makes that one post-config sleep last the full Cycle Sleep interval (vs the usual 100 ms) so deep-sleep current can be measured on an un-provisioned device — carried by the `bool` return of `runConfigMode()`.

**VOLT_TEST** shows live fence/battery readings (re-measuring continuously); **LORA_TEST** sends a *confirmed* uplink and shows join state plus RSSI/SNR/freq/DR/RX-window (`SignalInfo`, `src/signal_info.h`). **WIFI_AP_SCREEN** starts the WiFi AP on entry and shows the SSID + password (a password supplied over LoRa is masked, since it's a per-activation secret); the AP runs only while this screen is shown, and a long press stops it and returns to BROWSE (a tap is ignored so a stray press can't drop the AP mid-OTA).

#### Configurable parameters

The full parameter reference — every parameter, its default, and which surfaces (OLED / Web UI / AT / LoRaWAN fPort) reach it — is the table in `README.md`; that is the single source of truth, not this file. All four surfaces funnel through the `Config` setters (`src/config.{h,cpp}`), which persist to NVS via `Preferences`.

One non-obvious entry worth flagging: **Carrier Rev** lets a single firmware image run on every carrier board. The only thing it varies today is the opamp-enable pin (mapping in `src/carrier_board.h`): on v3.0 that pin is GPIO 2 — *shared with the mis-routed PA pin* — and on v3.1 it moves to GPIO 6. Both the measurement path and the deep-sleep latch (`src/sleep.cpp`, which reads the global `config`) must agree on which pin it is, and both revs float it during sleep. It is deliberately not reachable over LoRa.

### Web UI (`src/web_ui.cpp`, `src/ota.html`)

`WebUi` (`src/web_ui.cpp`) serves the config/OTA web UI over HTTP; read that file for the current route set. The same server is served two ways: the device can bring up its **own** access point, or **join an existing** AP as a station (e.g. a provisioning gateway) — both reach the identical pages. The station case waits for the AP at low power, deep-sleeping between scan attempts (so the wait spans many wakes, unlike the awake AP path) and handing off to the serve path only once it's in range (`src/main.cpp`).

`src/ota.html` is the source for the web UI — the pre-build script `scripts/embed_html.py` converts it to `src/ota_html.h` at compile time. Edit `ota.html`, not the generated header. A dev server for the web UI is in `scripts/dev_server.py`.

### OTA updates

Firmware update is handled via `/update` POST in the web UI. See `src/ota.html`.

### Logging

`LOG()`, `LOGLN()`, `LOGPRINT()` macros in `src/logging.h` wrap serial output; they compile to no-ops when `DISABLE_LOGGING` is defined (it is not set in `platformio.ini`, so logging is on by default). RADIOLIB debug output can be enabled via build flags in `platformio.ini` (`RADIOLIB_DEBUG_BASIC`, `RADIOLIB_DEBUG_PROTOCOL`).

### clangd / language server

The standard macOS clang does not understand ESP32 toolchain attributes (`RTC_DATA_ATTR`, `-mlongcalls`, etc.) — diagnostic errors from clangd on those are false positives. For proper LSP support, use the Espressif clangd fork with `--query-driver` pointing to the xtensa toolchain (see README.md).
