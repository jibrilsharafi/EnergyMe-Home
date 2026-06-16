# PCB Version Handling Design

**Date:** 2026-03-08
**Status:** Approved
**Firmware Version:** 2.0.0 (breaking change from 1.x)

---

## Context

The EnergyMe-Home firmware v1.x hardcodes all hardware config for PCB v5 in `pins.h`. v6.1 PCB has different GPIO pins, different voltage transformer ratios, and will have factory calibration data (ADE7953 gains/offsets) embedded in a dedicated NVS partition at production time. Certs are provisioned into the existing `certificates_ns` NVS namespace via the existing provisioning API.

v5 devices are few in number and will receive only critical bugfixes on a `legacy/v5` branch. v2.0.0 targets v6.1+ exclusively.

This is an open source project — the firmware must function gracefully even without eFuse provisioning (community builders who don't use the commercial provisioning pipeline).

---

## Strategy

**Branching:**
- Create `legacy/v5` branch from current `main` — v5 devices stay on v1.x, critical fixes cherry-picked
- `main` becomes v2.0.0 — v6.1+ only, no v5 compatibility code

**PCB Versions Supported:**
- v5 → `legacy/v5` branch only (eFuse value: 5)
- v6.1 → v2.0.0 `main` (eFuse value: 61)
- Future versions → add new `HardwareProfile` entry

**Version Detection:** Runtime, via eFuse `hardwareVersion` field (already in `EfuseProvisioningData` struct in `structs.h`, read by `readEfuseProvisioningData()` in `utils.cpp:932`)

**Two operational modes:**

| Mode | Trigger | Behavior |
|---|---|---|
| **Provisioned** | eFuse `isProvisioned == 0x01` | Full features. Uses eFuse `hardwareVersion` → exact PCB profile. Certs from NVS. Cloud enabled. |
| **Community** | eFuse not programmed | Log INFO. v6.1 default profile. Standard calibration. Cloud (MQTT/AWS) disabled. Web UI, Modbus, InfluxDB still work. |

---

## Architecture

### 1. Hardware Profile System

**New file:** `include/hardware_profile.h`

```cpp
struct HardwareProfile {
    uint16_t version;  // PCB version (61=v6.1, ...)

    // RGB LED
    uint8_t ledRedPin, ledGreenPin, ledBluePin;
    // Button
    uint8_t buttonPin;
    // Multiplexer (74HC4067)
    uint8_t muxS0Pin, muxS1Pin, muxS2Pin, muxS3Pin;
    // ADE7953 SPI
    uint8_t ade7953SsPin, ade7953SckPin, ade7953MisoPin;
    uint8_t ade7953MosiPin, ade7953ResetPin, ade7953InterruptPin;
    // Voltage measurement
    float voltageDividerR1;
    float voltageDividerR2;
    // Feature flags (additive for future versions)
    bool hasFactoryPartition;  // true when factory NVS partition is present and populated
};
```

**New file:** `src/hardware_profile.cpp`
- Defines `PCB_PROFILES[]` with one entry per supported PCB version (currently: v6.1 only)
- `initHardwareProfile()`: reads eFuse via existing `readEfuseProvisioningData()`, selects profile, sets `g_hwProfile` and `g_communityMode`
- Exposes `const HardwareProfile* getHardwareProfile()` accessor

**To add a new PCB version:** add one entry to `PCB_PROFILES[]`. No other changes needed.

`pins.h` is **deleted** — all usages replaced with `g_hwProfile->...` fields.

### 2. Boot Sequence

`setup()` in `main.cpp` calls `initHardwareProfile()` first, before any hardware init:

```
setup()
  └─ initHardwareProfile()             ← eFuse read, profile + mode selected
  └─ initLed(...)                      ← uses g_hwProfile->ledRedPin etc.
  └─ SPI.begin(sck, miso, mosi, ss)    ← uses g_hwProfile ADE7953 pins
  └─ ade7953.begin(...)                ← uses g_hwProfile reset/interrupt pins
  └─ loadCalibration()                 ← reads calibration_ns (factory-seeded or user-set)
  └─ initMqtt()                        ← skipped if g_communityMode == true
  └─ ... rest of setup
```

`SPI.begin(sck, miso, mosi, ss)` overload takes explicit pins — runtime selection works cleanly.

### 3. Community Mode — Cloud Gating

A global `bool g_communityMode` (set in `initHardwareProfile()`):
- If `true`: log `INFO: eFuse not provisioned — running in community mode, cloud disabled`
- MQTT/AWS initialization is skipped entirely
- All local integrations (web UI, Modbus, InfluxDB) remain unconditional

If eFuse is programmed but `hardwareVersion` is unknown: log `WARN`, use v6.1 profile, community mode stays `false` (device is provisioned but on unknown PCB — unusual, proceed carefully).

### 4. Factory Calibration (ADE7953 Gains/Offsets) — DEFERRED

**Deferred:** Factory NVS partition implementation is deferred. TODO comments mark all relevant locations.

**Intended design when implemented:**

The factory partition (`factory_data, data, nvs`) stores original ADE7953 calibration gains and offsets written at production time. The runtime `calibration_ns` NVS namespace (existing) always holds the *active* calibration — factory-seeded initially, then user-overridable via the web UI calibration page.

**Calibration flow:**
1. First boot / after reset-to-factory: if `hasFactoryPartition` → read gains/offsets from factory partition → write to `calibration_ns` as starting point
2. Normal operation: ADE7953 reads from `calibration_ns` (existing behavior, unchanged)
3. User adjusts via calibration UI: values saved to `calibration_ns` (existing behavior, unchanged)
4. "Reset to factory": if `hasFactoryPartition` → reload from factory partition → overwrite `calibration_ns`; else → apply hardcoded defaults from `ade7953.h` (`DEFAULT_CONFIG_AV_GAIN` etc.)

This means the existing calibration read/write code in `ade7953.cpp` and the calibration web API **do not change** — they always operate on `calibration_ns`. Factory partition is only touched during first-boot seeding and reset-to-factory.

**Existing default constants to preserve:** `DEFAULT_CONFIG_AV_GAIN`, `DEFAULT_CONFIG_AI_GAIN`, `DEFAULT_CONFIG_BI_GAIN`, etc. in `ade7953.h` — these remain the fallback when no factory partition exists.

**TODO locations to mark:**
- `src/ade7953.cpp` — calibration loading: note factory partition seeding should happen here
- `src/customserver.cpp` — reset-to-factory endpoint (or add one): note factory partition reset path
- `partitions.csv` — note factory_data partition to be added
- `include/hardware_profile.h` — `hasFactoryPartition` flag with TODO explaining intent
- MQTT provisioning code (`src/mqtt.cpp`, provisioning request handling) — note factory cert/data delivery path is deferred

### 5. Secrets & Certs — NVS Only

`HAS_SECRETS` removed entirely. Certs always from NVS `certificates_ns` via existing Preferences API. Existing provisioning API (`/api/provisioning/certificate`) remains for manual cert delivery. For provisioned v6.1+ devices: certs pre-programmed into NVS at factory.

### 6. Build Environments

Simplified from 4 to 2:

| Environment | Purpose |
|---|---|
| `esp32s3-dev` | Development: debug symbols, verbose logging |
| `esp32s3-prod` | Production: size optimized, minimal logging |

Remove embedded secret files from `board_build.embed_txtfiles`. Remove `-DHAS_SECRETS`.

---

## Files to Create

| File | Description |
|---|---|
| `include/hardware_profile.h` | `HardwareProfile` struct, extern `g_hwProfile`, `g_communityMode`, function declarations |
| `src/hardware_profile.cpp` | Profile table (v6.1 only for now), `initHardwareProfile()`, `getHardwareProfile()` |

## Files to Modify

| File | Change |
|---|---|
| `include/pins.h` | Delete — all usages replaced with `g_hwProfile->...` |
| `src/main.cpp` | Call `initHardwareProfile()` first; explicit SPI pins; gate MQTT on `g_communityMode` |
| `src/mqtt.cpp` | Remove `HAS_SECRETS` blocks; always use NVS certs; add TODO for factory provisioning path |
| `include/mqtt.h` | Remove `HAS_SECRETS` cert logic |
| `src/ade7953.cpp` | Use `g_hwProfile` for voltage divider ratios; add TODO for factory partition calibration seeding |
| `src/led.cpp` | Use `g_hwProfile` for LED pins |
| `src/customserver.cpp` | Remove `HAS_SECRETS` TODOs; add TODO for reset-to-factory factory-partition path |
| `platformio.ini` | Simplify to 2 envs, remove secrets embedding, remove `HAS_SECRETS` |
| `include/constants.h` | Bump to version 2.0.0 |
| `partitions.csv` | Add TODO comment for `factory_data` partition (deferred) |

---

## Verification

1. **Community mode:** Flash to unprogrammed device → INFO log, MQTT not started, web UI/Modbus/InfluxDB functional
2. **v6.1 provisioned:** Flash to eFuse-programmed device (`hardwareVersion=61`) → correct pins, certs from NVS, MQTT connects
3. **Unknown eFuse version:** WARN log, v6.1 profile used, boots safely
4. **Calibration override:** Adjust calibration via web UI → saved to `calibration_ns` → survives reboot
5. **Reset to factory (pre-partition):** Reset-to-factory applies hardcoded defaults from `ade7953.h`
6. **Build:** Both envs build cleanly, no `HAS_SECRETS` errors, no `pins.h` references
