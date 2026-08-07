# EnergyMe-Home (CLAUDE.md)

Open-source residential energy monitor. ESP32-S3-N16R2 (16 MB flash, 2 MB PSRAM), Arduino 3.x via pioarduino + PlatformIO. Measures up to 16 circuits via ADE7953 (SPI) + 74HC4067 mux. Exposes web UI, REST API, MQTT (AWS IoT + local), InfluxDB, Modbus TCP.

## Project layout

```
source/         Firmware (C++, Arduino)
  src/          Implementation
  include/      Headers + constants
  html/css/js/  Web interface (embedded in firmware at compile time)
  resources/    swagger.yaml, favicon
  lib/          Host-compilable pure logic (unit-testable)
  test/         Unity unit tests
hardware/       Schematics, datasheets
examples/       Integration configs
manual/         User-facing docs (install, troubleshoot)
```

## Build environments

- `esp32s3-dev` - debug (`-O0`), verbose logging. Use for development.
- `esp32s3-prod` - release (`-Os`), errors/warnings only. Use for releases.
- `native` - Unity unit tests, host only. Run via `pio test -e native` **from WSL** (Windows native compile is broken).

Static analysis: `pio check -e esp32s3-dev` (cppcheck + clangtidy).

**Never bump the platform version** (`pioarduino 55.03.32` = Arduino Core 3.3.2 / ESP-IDF v5.5.1). v5.5.4 loses ~50 KB of internal heap and silently breaks networking under load (no crash, just unreachable). Don't change `[common]` platform / memory / flash settings - the `qio_qspi` PSRAM + `qio` flash combo is the only one that works on this SoC.

## Hardware profile

`src/hardware_profile.cpp::PCB_PROFILES[]` - first entry is "latest" and the ultimate fallback. Selection: NVS `factory_ns::pcb_revision` -> matching entry. Missing/malformed -> community mode (`globalCommunityMode = true`), falls back to `PCB_PROFILES[0]`.

v6.1 (current): Y1 absent on PCB - 15 mux channels + 1 direct ADE7953 input = **16 active channels**. `MAX_CHANNEL_COUNT = 17` is the compile-time array size; always iterate using `globalHwProfile->totalChannelCount`, not the constant.

**Never hard-code GPIO pins.** Always read from `globalHwProfile`.

## Coding rules

- No `String` class - use `char[]` with named size constants + `sizeof`.
- `snprintf()` only; never `sprintf()` / `strncpy()`.
- `JsonDocument` with `SpiRamAllocator`; stream into `deserializeJson()`. No `containsKey()` (deprecated); use `is<T>()`.
- Every `while(...)` bounded by `MAX_LOOP_ITERATIONS` unless it's a task loop or already bounded by a size variable.
- No try/catch (Arduino framework). Fail-fast with early returns.
- Log via AdvancedLogger - omit module/function name (logger adds it). Levels: FATAL/ERROR/WARN/INFO/DEBUG/VERBOSE.
- Timestamps: `uint64_t` Unix seconds (`%llu`), ms only where timing matters.
- Config module pattern: `getConfiguration / setConfiguration / configurationToJson / configurationFromJson(partial)` + `_validateJsonConfiguration`. Tasks guard shared state with `SemaphoreHandle_t _configMutex`.

## FreeRTOS

- Mandatory mutex on any non-atomic shared state. Keep critical sections short - copy needed data under the mutex, release, then act on the copy.
- Graceful shutdown via task notifications (`ulTaskNotifyTake`); task self-deletes with `vTaskDelete(NULL)`.
- All task stacks are **internal RAM** (plain `xTaskCreate()`), regardless of whether the task touches flash. PSRAM-backed task stacks were tried in the past and dropped due to problems; PSRAM is still used extensively for buffers/queues (`ps_malloc`, `SpiRamAllocator`), just not task stacks.

## Secrets / provisioning

Cloud certs and device serials live in `factory_ns` NVS partition, written at manufacturing. **Never commit secrets, certs, or device-specific config.** Devices without factory NVS boot in community mode (cloud disabled, local integrations work).

## Git

- Branch off `development`; `main` is release-only.
- Conventional Commits: `type(scope): description` - lowercase, imperative, no trailing period.
- One concern per commit. No mega-commits. Test before committing.

## PR review before merge to `development`

Every PR gets a round of **code-review agents** and a **simplification agent** before it is merged to `development`. Not optional, not only for large changes.

- **Code review**: spawn one or more agents to review the branch diff for correctness, security, and edge cases. For anything touching auth, networking, OTA, or the request path, at least one agent's brief is adversarial (try to break it), and findings are reproduced before being accepted or dismissed - never merge on an agent's say-so alone.
- **Simplification**: spawn an agent to strip comments that restate self-evident code and apply safe structural cleanups, keeping the comments that explain non-obvious *why*. (See the `simplify` skill.)
- Triage every finding, fix or explicitly document with the reason, then re-run the applicable tests. Only then merge.

## Agent behaviour

- **Don't compile without being asked.** `pio run` is slow and CPU-intensive; stop after code edits unless Jibril explicitly requests a build or OTA.
- Don't create new files (especially Markdown/docs) unless explicitly asked.
- Don't remove TODO/FIXME comments unless the underlying task is done.
- Be concise; stick to the request.
