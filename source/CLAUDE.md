# EnergyMe-Home firmware (CLAUDE.md)

Firmware for the EnergyMe Home energy monitor. ESP32-S3-N16R2 (16 MB flash, 2 MB PSRAM), Arduino 3.x framework via PlatformIO (pioarduino fork). Talks to an ADE7953 energy-measurement IC over SPI; 1 direct channel + up to 16 multiplexed channels (74HC4067). Primary interfaces: web UI, MQTT (AWS IoT + local broker), InfluxDB, Modbus TCP.

## Build

- `pio run -e esp32s3-dev` - debug, `-O0`, verbose logging.
- `pio run -e esp32s3-prod` - release, `-Os`, warnings+errors only.
- Partition: `partitions_esp32s3_n16r2.csv`. Don't touch `[common]` platform / memory / flash settings - they are the only combination that works on this SoC variant.
- Always use the full path to `pio` / `platformio` when invoking.
- `src/main.cpp::setup()` must call `initHardwareProfile()` first (pins, mux layout, voltage divider come from `globalHwProfile`); never hard-code pins.

## Hardware profile

- `src/hardware_profile.cpp::PCB_PROFILES[]`, one entry per PCB revision. Index 0 is "latest" and the ultimate fallback.
- Selection: NVS `factory_ns::pcb_revision` (string `"vMAJOR.MINOR"`) -> matching entry. Missing / malformed / unknown -> `globalCommunityMode = true`, fall back to `-DPCB_VERSION_FALLBACK` if defined else `PCB_PROFILES[0]`.
- Factory NVS keys in `include/factory_keys.h` - keep in sync with provisioning firmware and the Python `FactoryProvisioningPayload` pydantic model. Never commit real certs / serials.

## Coding rules

### Style & structure

- Favour simplicity and readability over cleverness. Defensive but not over-engineered.
- Constants in `include/constants.h` (global) or module headers (local); group by prefix.
- Naming: `camelCase` variables/functions, `UPPER_SNAKE_CASE` constants, `_prefix` for private, `PascalCase` for classes/namespaces.
- One-line `if (!x) return;` is preferred for simple guards.
- Use `delay(X)`, not `vTaskDelay(pdMS_TO_TICKS(X))`.
- Comment tags: `TODO` (improvement), `FIXME` (broken), `HACK` (temp workaround).

### Memory & strings

- Avoid `String` in new code - use `char[]` buffers with named size constants + `sizeof(buffer)`.
- Use `snprintf()` only; never `sprintf()` or `strncpy()`.
- `JsonDocument` with `SpiRamAllocator`; stream directly into `deserializeJson()`. Never `containsKey()` (deprecated); validate with `is<type>()`. `isNull()` only at doc level.

### Control flow & errors

- Every `while(...)` must be bounded by `MAX_LOOP_ITERATIONS` (in `constants.h`), except task loops gated by a `_taskShouldRun` flag, `while(true)` task bodies, or loops already bounded by a size variable.
- No try/catch (Arduino framework).
- Fail-fast: return early on bad input. Log via AdvancedLogger (no module/function name in the message - the logger adds it). Levels: FATAL / ERROR / WARN / INFO / DEBUG / VERBOSE.

### Storage

- Preferences (NVS) for configuration; LittleFS for historical data and logs. See namespace defines in `include/constants.h`.
- Timestamps: `uint64_t`, Unix seconds (ms only where timing matters). UTC at API boundary; `%llu` for printf.

**Config module pattern** (see `src/ade7953.cpp`, `src/influxdbclient.cpp`):
`getConfiguration / setConfiguration / configurationToJson / configurationFromJson(partial=false)`, single `_validateJsonConfiguration(doc, partial)`. Modules with tasks guard shared state with a `SemaphoreHandle_t _configMutex` + `CONFIG_MUTEX_TIMEOUT_MS`.

### FreeRTOS tasks

- Mandatory mutex on any non-atomic shared state; go through getters/setters.
- Keep critical sections short. Slow work (NVS, LittleFS, MQTT publish, JSON serialization) runs **unlocked** - copy what you need into a local under the mutex (a struct, or a single decision bool), release, then act on the local. Avoids deadlock with callees that re-acquire the same mutex via getters.
- Graceful shutdown via task notifications (`ulTaskNotifyTake(pdTRUE, timeout)`); task self-deletes with `vTaskDelete(NULL)` after setting its handle to `NULL`.
- Stack placement: **PSRAM** for non-flash-I/O tasks (WiFi, LED, button, crash monitor, ADE7953 meter); **internal RAM** for tasks that touch flash (NVS, LittleFS, OTA) - flash cache disable makes PSRAM unreadable and crashes PSRAM-stack tasks.

## Git

- Branch off `development`; `main` is release-only. Feature branches follow Conventional Commits (`feat/*`, `fix/*`, ...).
- Commit messages: Conventional Commits, lowercase, imperative, no trailing period. Types: `feat / fix / docs / style / refactor / test / chore`.
- One concern per commit. Never mega-commits. Commits are human-authored - do not mention AI tooling in the message.
- Tested before commit (unit + hardware/e2e if applicable); pure test-only changes excepted.

## Agent behaviour

- Be concise; stick to the request.
- **Never create new files (especially Markdown / docs) unless explicitly asked.** Propose first.
- Don't remove TODO/FIXME comments unless the underlying task is actually done.
