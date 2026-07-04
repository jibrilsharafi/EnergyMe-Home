## Why

Firmware has a high-quality grid frequency source (ADE7953 PERIOD register, ~0.8 mHz resolution after ~300 ms averaging) that is currently read once per linecyc, unfiltered (~11 mHz quantization dither) and with a known ~11 mHz absolute bias (missing the datasheet Eq.36 `+1`). Issue #157 defines the per-cycle EMA that fixes both; the grid-telemetry ADR (cloud side, energyme-infra) needs the edge to ship anonymizable 500 ms frequency + voltage points on a dedicated topic so the cloud can build a per-zone consensus dataset. This change is the firmware (edge) half of that contract.

## What Changes

- **ADE7953 interrupt demux rework**: ZXV (voltage zero-crossing, ~50 Hz) is enabled alongside CYCEND on the shared IRQ pin. `_handleInterrupt()` changes from "return one interrupt type" to "service every set bit of one `RSTIRQSTATA` snapshot" (independent `if`s, not `else if`). The ISR becomes pure (semaphore give + counter only); the `_interruptHandledChannelA/B` double-read guard flags move from the ISR into the task, cleared only when the CYCEND bit is actually set. This strengthens the existing energy read-with-reset guard.
- **Per-cycle frequency EMA** (issue #157): each ZXV reads PERIOD, validates 45-65 Hz, updates a Q24.8 fixed-point EMA (alpha = 1/8, shift-only) plus a monotonic update counter. `getGridFrequency()` returns the filtered value.
- **Absolute accuracy fix**: frequency conversion becomes `f = 223750 / (PERIOD + 1)` per datasheet Eq.36 everywhere (raw path included) - removes a ~11 mHz systematic high bias from all consumers (REST, Modbus, HA, shadow, issues).
- **New 500 ms grid sampler task**: wall-clock-aligned to absolute .000/.500 boundaries (boundary-derived `vTaskDelay`, true `getUnixTimeMilliseconds()` stamps), gated on NTP sync, cloud-set report flag, and EMA freshness (update counter). Emits `{timestamp_ms, frequency, voltage}` (voltage = cached channel-0 RMS) into a FreeRTOS queue.
- **New dedicated grid MQTT topic** (Stream A of the ADR): publish task drains the queue and ships batched point arrays on a new topic, separate from the meter payload, on the existing publish cadence. Meter payload unchanged.
- **New cloud-settable report flag** (mirrors the existing meter power-flag pattern) gating payload contribution only - the EMA always runs locally.

## Capabilities

### New Capabilities
- `grid-frequency-measurement`: per-cycle ZXV-driven EMA of the ADE7953 PERIOD register, datasheet-correct conversion, validation, freshness tracking, and the filtered `getGridFrequency()` surface.
- `grid-telemetry-stream`: the 500 ms wall-clock-aligned frequency+voltage sampler, its gates (NTP / report flag / freshness), queue handoff, and the dedicated batched MQTT grid topic (edge side of the cloud contract).

### Modified Capabilities
<!-- none: iot-commands and iot-device-shadows requirements are unchanged; the report flag rides existing shadow/config machinery as an implementation detail -->

## Impact

- Firmware: `source/src/ade7953.cpp` / `include/ade7953.h` (ISR, demux, EMA, Eq.36 fix, IRQENA/CONFIG init constants), new grid sampler task, `source/src/mqtt.cpp` (new topic + batching), config surface for the report flag.
- Existing behavior shift: `getGridFrequency()` moves ~11 mHz lower (bias fix) and becomes low-noise filtered - visible to REST/Modbus/HA/shadow consumers, imperceptible in practice.
- Metering path: interrupt demux touched - guard semantics preserved-or-strengthened; energy reads, linecyc timing, and CYCEND handling functionally unchanged (datasheet-confirmed ZX_EDGE does not affect linecyc/energy accumulation).
- Cross-repo contract (energyme-infra, out of scope here): topic name, payload schema, report-flag mechanism; ingestion + IoT Core policy changes handled in the infra repo (handoff brief in `infra-handoff.md`).
- No new hardware, no NTP cadence change, no platform/partition changes.
