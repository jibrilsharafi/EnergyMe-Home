## 1. Accuracy fix (independent, lands first)

- [x] 1.1 Apply Eq.36 `+1` to `_readGridFrequency()` and any other PERIOD-to-Hz conversion; adjust `GRID_FREQUENCY_CONVERSION_FACTOR` usage comments
- [x] 1.2 Verify on device (.174, UDP logs): reported frequency drops ~11 mHz, still validates; commit `fix(ade7953): apply datasheet eq.36 +1 to period-to-frequency conversion`

## 2. Pure-logic EMA (host-testable first)

- [x] 2.1 Add `lib/` pure module: Q24.8 EMA update (alpha=1/8 shift form), seed-on-first-valid, 45-65 Hz range validation hook, monotonic update counter, Eq.36 readout conversion
- [x] 2.2 Unity tests in `test/` (WSL `pio test -e native`): seed behavior, out-of-range rejection, convergence to a dithering input's mean, sub-LSB resolution retention, counter monotonicity
- [x] 2.3 Commit `feat(lib): grid frequency EMA pure logic + unit tests`

## 3. Interrupt demux rework (metering-critical)

- [x] 3.1 Make ISR pure: remove `_interruptHandledChannelA/B` clearing from `_isrHandler()`; keep semaphore give + counters; add separate ZXV statistics counter
- [x] 3.2 Rework `_handleInterrupt()`: single `RSTIRQSTATA` snapshot, independent `if` per bit (ZXV -> CYCEND -> RESET -> CRC), guard flags cleared inside the CYCEND branch only; remove the single-type enum dispatch
- [x] 3.3 Add `_handleZxvInterrupt()`: PERIOD read -> lib EMA update (validated); wire `getGridFrequency()` to the EMA readout, keep `_updateSampleTime()` on the raw read
- [x] 3.4 Extend `DEFAULT_IRQENA_REGISTER` (bit 15 ZXV) and `DEFAULT_CONFIG_REGISTER` (ZX_EDGE=10b, bit 13)
- [x] 3.5 Bench test (.174 + UDP logs, listener started BEFORE flashing): meter heartbeat unchanged, energy values sane, ZXV counter ~50/s, no scheduler starvation, no double-read guard trips; commit demux + EMA wiring as separate commits

## 4. Grid sampler task

- [ ] 4.1 New sampler task (PSRAM stack): boundary-derived delay loop (`500 - now%500`, <20 ms guard), gates (NTP sync -> `send_grid_data` -> update-counter freshness), builds `{t,f,v}` with cached `_meterValues[0].voltage`, `xQueueSend` no-block
- [ ] 4.2 Graceful shutdown via task notification, consistent with existing task patterns
- [ ] 4.3 Bench test: points land within a few ms of .000/.500, gaps appear when flag off / line events; commit `feat(grid): 500ms wall-clock-aligned grid sampler task`

## 5. Report flag

- [ ] 5.1 Add `send_grid_data` (NVS-persisted, default off) mirroring `send_power_data`: getter/setter, preferences load/save
- [ ] 5.2 Expose in `system` shadow (reported + writable delta handling)
- [ ] 5.3 Bench test flag flip via shadow (cloud -> device -> persisted across reboot); commit `feat(mqtt): cloud-settable send_grid_data flag`

## 6. Grid topic + publish path

- [ ] 6.1 Add `MQTT_TOPIC_GRID "grid"` + `AWS_IOT_CORE_RULE_GRID` Basic Ingest topic construction (name synced with infra handoff)
- [ ] 6.2 Grid queue (128 x 16 B, drop-oldest on overflow) + publish-cadence drain into sorted bare-array JSON `[[t,f,v],...]` (f 4-dec, v 1-dec; SpiRamAllocator, snprintf conventions)
- [ ] 6.3 Bench e2e (.174): enable flag, verify batched arrays arrive on the grid topic via MQTT test client, meter payload byte-identical to before; commit `feat(mqtt): dedicated batched grid telemetry topic`

## 7. Verification + handoff

- [ ] 7.1 Soak test on .174 (>=1 h): heartbeats, heap, `getDebugCount()` rate, energy continuity vs. pre-change baseline
- [ ] 7.2 Confirm `infra-handoff.md` names/schema match the shipped firmware literals; hand to energyme-infra
- [ ] 7.3 Open PR to `development` with `Closes #157`
