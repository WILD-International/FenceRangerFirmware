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

## Architecture

The device is an ESP32-S3 (Heltec LoRa V4) that measures electric fence voltage, then deep-sleeps between cycles. Every wake is a full reset — `setup()` runs, does work, then calls `gotoSleep()`. There is no `loop()`.

### Deep sleep and RTC memory

All state that must survive deep sleep must be declared `RTC_DATA_ATTR`. Regular globals, class members, and `static` locals are re-initialised on every wake. The pattern is used across `state.cpp`, `comms.cpp`, and `helpers.cpp`.

### Main cycle (`src/main.cpp`)

Each wake:
1. Checks wakeup reason — button press (`ESP_SLEEP_WAKEUP_EXT0`) enters config mode
2. Runs `MeasureProgram` — samples ADC for fence voltage and battery voltage
3. Calls `state.updateSlowBatt()` — updates the slow-tracking battery filter
4. Calls `processMeasurement()` (`src/alert.cpp`) — decides whether to transmit this cycle
5. If transmitting: initialises radio, calls `comms.sendMeasurement()`, handles any downlink
6. Saves LoRaWAN session to RTC memory, puts radio to sleep, calls `gotoSleep(adaptedSleepMs)`

The sleep duration is `state.computeAdaptedSleepMs()` — a value between `config.getSleepIntervalMs()` and `sleepInterval × battSleepMult` depending on battery voltage.

### Transmission policy (`src/alert.cpp`)

`processMeasurement()` is a free function (declared in `src/alert.h`) that takes a `fenceVoltage` and an `AlertConfig` struct populated from `Config`. It implements a three-state machine stored in `RTC_DATA_ATTR` globals:

- **Idle**: healthy fence. Sends on the first cycle after boot/recovery, then heartbeat every `idleHeartbeatCycles` (default 60).
- **Alerting**: fence voltage below `alertThresholdV`. Sends a burst of `burstSends` (default 3) messages immediately, then heartbeat every `alertHeartbeatCycles` (default 15).
- **Recovering**: voltage came back up but not yet confirmed. Requires `recoveryConfirmCycles` (default 30) consecutive above-threshold readings before returning to Idle. Filters brief spikes during fence repair. Bouncing back to Alerting during recovery does **not** restart the burst.

The alert and recover thresholds are separate (`alertThresholdV` < `recoverThresholdV`) to provide hysteresis. All parameters are stored in NVS and configurable via the web UI and OLED menu.

### Adaptive sleep (`src/state.cpp`)

Two methods on `State` implement battery-based sleep throttling:

- `state.updateSlowBatt()` — called after each measurement. `slowBattMv` drops instantly when `batt_max_mv` falls below it, but rises at only `1/battRecoveryN` of the gap per cycle. This asymmetric filter ensures throttling is applied immediately but lifted gradually.
- `state.computeAdaptedSleepMs()` — pure computation (no side effects). Lerps between `minSleep` and `minSleep × battSleepMult` based on where `slowBattMv` falls between `battLowMv` and `battFullMv`.

`slowBattMv` is stored as `RTC_DATA_ATTR` in `state.cpp`.

### LoRaWAN (`src/comms.cpp`)

Uses RadioLib with OTAA. The session is persisted in `RTC_DATA_ATTR LWsession[]` and nonces in NVS (`Preferences`). Join is attempted with backoff: every 5th boot for the first 30 boots, then every 10th boot. Uplinks are unconfirmed (no ACK). Downlinks are handled on fPorts 10 (trigger OTA) and 11 (update sleep interval and alert thresholds).

The public send API is `comms.sendMeasurement(battMv, fenceV, display, downlink)` — it encodes the 3-byte payload internally and calls the private `send()`. The raw `send()` method is private.

### Uplink payload (3 bytes)

| Byte | Content |
|------|---------|
| 0 | Battery voltage compressed: `(mv - 1950) / 10`, covering 1950–4500 mV in 10 mV steps |
| 1–2 | Fence voltage in volts, big-endian uint16 |

Encoding lives in `Comms::sendMeasurement()`. Value selection (which min/max reading to use) stays in `main.cpp`.

### Config mode (`src/config_mode.cpp`)

Entered on button press or when no AppKey is stored. `runConfigMode(config, state, display, webUi)` is a free function that owns the entire config mode event loop — all dependencies are passed explicitly rather than accessed as globals.

Runs a WiFi AP + HTTP server (`WebUi`) and accepts AT commands over Serial. Exits when `state.at_command_mode` is set to false (short tap on info screen, or 1-hour timeout). Config is stored in NVS via `Preferences`.

#### OLED menu UX (single-button)

The menu uses four states: **INFO → BROWSE → SUBMENU → EDIT**.

- **INFO** (home): shows device name, DevEUI, firmware version. Short tap exits config mode; long press enters BROWSE.
- **BROWSE**: 4-item top-level menu — "Alert config", "Sleep config", "Debug", "Back". No scrolling needed. Short tap advances; long press activates.
  - **Alert / Sleep**: long press enters the corresponding SUBMENU.
  - **Debug**: long press toggles debug mode on/off.
  - **Back**: long press returns to INFO.
- **SUBMENU**: shows the parameters for the active group plus a Back item at the end. Short tap advances; long press enters EDIT or (on Back) returns to BROWSE. A 4px scrollbar appears on the right when the group has more items than fit (Alert group: 7 items, 6 visible).
- **EDIT**: shows the parameter name, current candidate value in large text, and `now:X` in the hint line. Short tap cycles through predefined options (plus a CANCEL slot at the end). Long press saves (hint shows `hold=save`) or goes back without saving if candidate equals current value or CANCEL is selected (hint shows `hold=back`). Returns to SUBMENU.

#### Configurable parameters (OLED and web UI)

**Alert group:**

| Parameter | Default | Description |
|-----------|---------|-------------|
| Alert V | 3000 V | Fence voltage below which an alert is triggered |
| Recover V | 3500 V | Fence voltage above which recovery begins |
| Burst Sends | 3 | Uplinks sent in quick succession on alert trigger |
| Idle Heartbeat | 60 cycles | Cycles between uplinks when fence is healthy |
| Alert Heartbeat | 15 cycles | Cycles between uplinks while fence is in alert |
| Recovery Confirm | 30 cycles | Consecutive above-threshold cycles to confirm recovery |

**Sleep group:**

| Parameter | Default | Description |
|-----------|---------|-------------|
| Cycle Sleep | 60 s | Deep sleep duration between measurements |
| Batt Full mV | 4000 mV | Battery voltage above which no sleep throttling applies |
| Batt Low mV | 3300 mV | Battery voltage at which maximum sleep throttling applies |
| Sleep Mult | 30 | Maximum sleep interval multiplier at low battery |
| Recovery N | 50 | Slow-filter N; slow voltage rises 1/N of gap per cycle |

### Web UI (`src/web_ui.cpp`, `src/ota.html`)

Three POST endpoints:
- `/device` — device name and AppKey
- `/settings` — alert parameters and cycle sleep
- `/batt-sleep` — the four adaptive sleep parameters (`battFullMv`, `battLowMv`, `battSleepMult`, `battRecoveryN`)

`/api/config` (GET) returns all parameters as JSON. `src/ota.html` is the source for the web UI — the pre-build script `scripts/embed_html.py` converts it to `src/ota_html.h` at compile time. Edit `ota.html`, not the generated header. A dev server for the web UI is in `scripts/dev_server.py`.

### OTA updates

Firmware update is handled via `/update` POST in the web UI. See `src/ota.html`.

### Logging

`LOG()`, `LOGLN()`, `LOGPRINT()` macros in `src/debug.h` gate all serial output behind `Debug::enabled()`. RADIOLIB debug output can be enabled via build flags in `platformio.ini` (`RADIOLIB_DEBUG_BASIC`, `RADIOLIB_DEBUG_PROTOCOL`).

### clangd / language server

The standard macOS clang does not understand ESP32 toolchain attributes (`RTC_DATA_ATTR`, `-mlongcalls`, etc.) — diagnostic errors from clangd on those are false positives. For proper LSP support, use the Espressif clangd fork with `--query-driver` pointing to the xtensa toolchain (see README.md).
