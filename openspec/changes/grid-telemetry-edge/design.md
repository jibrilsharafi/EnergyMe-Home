## Context

The ADE7953 exposes grid frequency via the 16-bit PERIOD register (0x10E), updated every line cycle. Today it is read once per linecyc inside the channel-0 meter path (`_readGridFrequency()`), unfiltered (quantization step ~11 mHz at 50 Hz) and converted without the datasheet's `+1` term (Eq.36, Rev. C p.36: `f = 223750 / (PERIOD + 1)`), giving a ~11 mHz systematic high bias.

Issue #157 (supersedes #93) defines a per-cycle EMA that turns this into a ~0.8 mHz-resolution signal. The grid-telemetry ADR (energyme-infra) consumes it: anonymized 500 ms frequency+voltage points per device, per-zone consensus via cloud-side median over a rotating quorum. This change is the edge half only; the cloud contract items (topic, payload schema, report flag) are fixed here and handed to the infra repo (`infra-handoff.md`).

Key existing machinery this builds on:

- One shared active-low IRQ pin (GPIO from `globalHwProfile->ade7953InterruptPin`); ISR gives a binary semaphore; the ADE7953 task reads `RSTIRQSTATA_32` (read-with-reset: clears ALL bits) and dispatches.
- `_interruptHandledChannelA/B`: double-read guard for read-with-reset energy registers, currently cleared in the ISR (assumes every IRQ = new linecyc).
- Meter/log topics use Basic Ingest (`_constructMqttTopicWithRule`); payload batching over FreeRTOS queues.
- `send_power_data`: NVS-persisted boolean, cloud-controlled via `system` shadow delta - the pattern the grid report flag mirrors.

## Goals / Non-Goals

**Goals:**

- Low-noise, absolutely-accurate grid frequency (EMA + Eq.36) exposed to all existing consumers.
- 500 ms wall-clock-aligned `{t, f, v}` points on a dedicated batched grid topic, gated (NTP / report flag / freshness), with honest gaps (skipped ticks, never stale or fabricated points).
- Zero regression to the metering path: energy reads, linecyc timing, and guard semantics preserved or strengthened.

**Non-Goals:**

- Cloud side: anonymization, zone resolution, consensus, quorum manager, dashboard, API (energyme-infra).
- RoCoF / event-triggered raw capture, sag/swell, per-cycle min/max (deferred in #157).
- Phase-angle / PMU claims (hardware cannot).
- Stream B (user ~30 s house voltage on the meter path) - unchanged, out of scope.

## Decisions

### D1. Interrupt demux: one snapshot, service every set bit

The ADE7953 IRQ pin is level-active-low; the ESP32 interrupt is edge-triggered (FALLING). A bit asserting while the pin is already low produces no new edge - so co-pending bits are routine once ZXV (~50 Hz) joins CYCEND. `RSTIRQSTATA` is read-with-reset, so the current single-type dispatch (`if/else if` returning one enum) silently discards co-pending bits.

Rework: read `RSTIRQSTATA_32` once per wake, then independent `if`s over the snapshot - ZXV first (PERIOD is transient, overwritten every cycle), then CYCEND, RESET, CRC. Never re-read status to check another bit (second read returns zeros). No drain loop: anything asserting after the read-and-clear raises a fresh edge and a new wake; the existing timeout wake is the backstop.

*Rejected:* per-type ISR dispatch (can't know cause without SPI); drain-until-clean loop (spins at 50 Hz ZXV; unbounded).

### D2. ISR becomes pure; energy guard flags move into the task

`_interruptHandledChannelA/B` protect read-with-reset energy registers from double reads within one frozen linecyc window. The ISR currently clears them on every interrupt - encoding the assumption "every IRQ is a new linecyc". ZXV at 50 Hz breaks that assumption and would re-arm the guard every 20 ms, making it decorative and reopening the double-read window (second read returns ~0, destroying that cycle's energy).

Fix: ISR does semaphore give + statistics counter only. The task clears the flags iff the CYCEND bit is set in the snapshot, immediately before `_handleCycendInterrupt()`. Strictly stronger than today: the guard re-arms only on a true new linecyc.

### D3. Frequency: per-cycle Q24.8 EMA in the period domain (issue #157)

- ZXV handler: read PERIOD (16-bit), validate via existing `_validateGridFrequency` (45-65 Hz) - out-of-range cycles are dropped, never fed to the EMA; first in-range read seeds it (never 0).
- `ema_q8 += (x_q8 - ema_q8) >> 3` (alpha = 1/8): ~15-cycle / ~300 ms window, ~1.06 Hz cutoff, ~0.8 mHz floor (Allan-optimal per #157). Q8 keeps sub-LSB resolution through the shift; int32 read/write is atomic on Xtensa.
- Monotonic `_emaUpdateCount` incremented on each accepted update - the freshness signal (D5).
- Conversion at readout only: `f = 223750 / (ema/256 + 1)`. The `+1` (datasheet Eq.36) is applied fleet-wide including the raw path `_readGridFrequency()` - fixes the ~11 mHz absolute bias. Filtering in the period domain is deliberate: reciprocal nonlinearity bias is sub-uHz at <1% period swing.
- `getGridFrequency()` returns the EMA value. `_updateSampleTime()` (50/60 Hz bootstrap for linecyc) stays on the direct raw read - it runs before the EMA seeds.
- ZXV misses under load are best-effort by design: bits are flags not counters, PERIOD holds only the latest cycle; a missed cycle just shrinks effective N (~15 -> ~13 worst case, sub-mHz effect). CYCEND is never best-effort - its bit rides every snapshot.

*Rejected:* histogram telemetry (superseded in #157 - no useful sub-mHz structure beyond ~320 ms averaging); float/divide on hot path; timestamping ZXV events (sampler stamps snapshots instead).

### D4. Register init: extend constants, no RMW

Init is the only writer of `CONFIG_16` / `IRQENA_32`, so extend the existing constants: `DEFAULT_IRQENA_REGISTER` gains bit 15 (ZXV); `DEFAULT_CONFIG_REGISTER` gains ZX_EDGE = `10b` (bits 13:12, positive-going only -> 1 IRQ/cycle). Datasheet-confirmed safe: "changing the ZX_EDGE bits affects only the ZX status bits and interrupts" - linecyc half-line-cycle counting and energy accumulation are unaffected; the waveform capture routine counts zero crossings in software and is independent.

### D5. Sampler: dedicated task, absolute wall-clock alignment, three gates

Dedicated task (PSRAM stack; RAM-only work - no SPI, no flash, no JSON). Each iteration targets the next absolute .000/.500 boundary derived from the wall clock:

```c
uint64_t now = CustomTime::getUnixTimeMilliseconds();
uint32_t delayMs = 500 - (now % 500);
if (delayMs < 20) delayMs += 500;      // degenerate-sleep / backward-NTP-step guard
vTaskDelay(pdMS_TO_TICKS(delayMs));
```

Boundary-derived delay (not `vTaskDelayUntil`): re-targets the absolute grid every tick, so crystal drift never accumulates and NTP steps self-heal with no re-phasing logic. `vTaskDelayUntil` gives perfect spacing but in the tick domain, which slides off the wall-clock grid.

Why absolute alignment matters (and is required by the ADR): consensus quality during grid events. Apparent disagreement between healthy devices = RoCoF x time skew. Normal RoCoF (~13 mHz/s p99.9) x 250 ms = ~3 mHz (noise-level); event RoCoF (0.5-2 Hz/s) x 250 ms = 125-500 mHz - which would make cloud outlier-rejection reject healthy devices exactly when the data matters. Alignment collapses skew to NTP error (~±50 ms), a 5-10x improvement for four lines of code. Staggered ("sparse") sampling was considered and rejected: the EMA band-limits each device to ~1 Hz (nothing above Nyquist to recover), NTP jitter scrambles sub-50 ms interleave order, and it destroys same-instant cross-validation. Alignment is best-effort, not correctness-bearing: stamps are always the true `getUnixTimeMilliseconds()` read, so a misaligned device degrades bucket tightness, never data integrity.

Gates, in order - all must pass or the tick is skipped (a real timestamp gap, visible to the cloud):

1. Time is NTP-synced (existing `CustomTime` gate).
2. Grid report flag enabled (D7).
3. `_emaUpdateCount != lastSeen` - the EMA actually updated since the last emit. Line-dead => no ZXV => no points. Single-writer counter, no clearing, no race (elegant replacement for a dirty flag).

On pass: emit `{uint64_t timestamp_ms; float frequency; float voltage}` - frequency from the EMA (D3), voltage = cached `_meterValues[0].voltage` (refreshed each CYCEND, <= `_sampleTime` stale; RMS moves slowly; keeps the sampler SPI-free). `xQueueSend` with zero block time.

*Rejected:* esp_timer callback (gating logic in a timer cb, no fresh-read option ever); folding into an existing task (couples grid cadence to unrelated timing); fresh VRMS SPI read (forces SPI into the sampler for negligible gain).

### D6. Transport: dedicated Basic Ingest topic, batched

New `MQTT_TOPIC_GRID "grid"` via `_constructMqttTopicWithRule(AWS_IOT_CORE_RULE_GRID, ...)` - same Basic Ingest pattern as meter/log (halves messaging cost at this cadence). The publish path drains the grid queue on the existing publish cadence (~30 s) and ships one JSON array of points per publish. Queue: 128 x 16 B = 2 KB, ~64 s buffer (2x publish period); on overflow drop oldest (grid telemetry is redundant fleet data, not billing data - recency beats completeness). Meter payload untouched.

Payload schema (cross-repo contract, v1): bare top-level array of positional triplets `[[<t unix_ms>, <f hz>, <v volt>], ...]` - same compact style as the meter power points. `t` is the true read at the tick (never the nominal boundary); array sorted ascending; gaps explicit (missing boundaries). Serialization precision: `f` 4 decimals (0.1 mHz - preserves the 0.8 mHz EMA floor; the existing `GRID_FREQUENCY_DECIMALS`=3 would quantize it), `v` 1 decimal.

*Rejected:* piggybacking on the meter payload (mixes the anonymous-at-rest stream into the attributed user path - breaks the ADR's load-bearing privacy boundary); per-point publish (60 msgs/min, cost).

### D7. Report flag: `send_grid_data`, mirrors `send_power_data` exactly

NVS-persisted boolean, default off; cloud flips it via the `system` shadow delta (the ADR's quorum rotation mechanism). Gates payload contribution only - the EMA always runs (it feeds `getGridFrequency()` for REST/Modbus/HA/issues regardless). Persistence means quorum membership survives reboot; no requirement change to the `iot-device-shadows` spec (new key rides the existing writable-shadow machinery).

## Risks / Trade-offs

- [Task wake rate 3-5/s -> ~50/s] -> ~100 us SPI per ZXV wake (~0.5% CPU); FreeRTOS handles this trivially. Verify with the task profiler + `getDebugCount()` rate after flashing; acceptance criterion: no measurable meter-heartbeat impact.
- [ZXV floods during degraded line (brownout flicker)] -> validation drops out-of-range PERIODs; freshness gate stops emission; existing timeout/`_recordCriticalFailure` path unchanged.
- [`statistics.ade7953TotalInterrupts` inflates ~10x] -> add a separate ZXV counter; keep total meaningful.
- [`getGridFrequency()` shifts ~11 mHz lower + becomes filtered] -> correct per datasheet; imperceptible to users (3-decimal display); note in release notes.
- [Backward NTP step could emit two points near one boundary] -> `delayMs < 20` guard absorbs small steps; stamps are honest so the cloud dedupes by bucket; harmless.
- [Queue overflow during long MQTT outage] -> drop-oldest by design; grid data is fleet-redundant.
- [Cross-repo drift: topic/rule/schema/flag names] -> single source of truth is this design + `infra-handoff.md`; both repos must reference it before shipping either side.

## Migration Plan

Pure firmware addition behind the `send_grid_data` flag (default off): deploy order-independent. Flash dev device (.174), verify metering unchanged (UDP logs + heartbeats), enable flag manually, verify points on the topic with MQTT test client before any infra work. Rollback = flag off or previous OTA image; no data-format or NVS migrations.

## Open Questions

- Final topic/rule literal (`AWS_IOT_CORE_RULE_GRID` name) - confirm against infra naming conventions at infra-PR time; placeholder fixed in `infra-handoff.md`.
- QoS for grid publishes: QoS0 suggested (redundant data, cheapest); meter uses QoS1 today - decide at implementation.
