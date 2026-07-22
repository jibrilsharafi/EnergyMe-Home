## Why

Meter data to AWS IoT Core is gated by two compile-time constants: a 5 KB queue-size threshold (`AWS_IOT_CORE_MQTT_PAYLOAD_MINIMUM_BILLABLE`, chosen to match AWS IoT Core's per-message billing floor) and a 60 s fallback ceiling (`MQTT_MAX_INTERVAL_METER_PUBLISH`). Changing publish cadence today means editing a `#define` and reflashing. There's a real need to dial this per situation without a reflash - e.g. batch more aggressively overnight for cost efficiency, or batch far less during a live demo (upcoming trade fair) so remote viewers see near real-time data. The device should stay a dumb, stateless executor of whatever cadence the cloud currently wants; scheduling *when* to change cadence (day/night, demo mode, etc.) is a cloud-side concern, out of scope here.

## What Changes

- Add two new fields to the existing cloud-settable `system` shadow, alongside `send_power_data`/`send_grid_data`: a byte-size threshold and a max-interval-ms ceiling for meter publishing.
- Both fields are persisted to NVS (survive reboot) and applied at runtime through the existing shadow delta-apply path (same pattern as `send_power_data`).
- `_checkIfPublishMeterNeeded()` reads the two values from config instead of the hardcoded constants. The OR logic (size trigger OR time trigger) is unchanged - no new "mode" branching on-device.
- Firmware defaults are unchanged (5 KB / 60 s) so fleet behavior is identical until the cloud explicitly writes a `desired`.
- Add clamp/validation bounds in the `system` shadow's apply path so a bad or accidental write (e.g. `0`) can't cause a publish storm or exceed the AWS message size limit.

## Capabilities

### New Capabilities
- `meter-publish-cadence`: the device-side mechanism that decides when to flush the queued meter payload to AWS IoT Core, driven by a configurable byte threshold and a configurable max-interval ceiling.

### Modified Capabilities
- `iot-device-shadows`: the `system` shadow gains two new writable fields (meter publish threshold bytes, meter publish max interval ms), following the same delta-apply, persist, and ack-with-desired-null pattern as existing `system` fields, with added validation/clamping.

## Impact

- `source/include/mqtt.h`: two constants become defaults instead of fixed values; new NVS preference keys.
- `source/src/mqtt.cpp`: `_checkIfPublishMeterNeeded()` reads config values; add getter/setter pair (mirroring `getSendPowerData`/`setSendPowerData`).
- `source/src/shadow.cpp`: `_reportSystem`/`_applySystem` gain the two new fields with validation.
- No change to the byte-threshold-vs-interval OR logic, no change to grid publishing (`MQTT_GRID_PUBLISH_ALIGN_SECONDS` untouched - possible future follow-up, not in scope), no cloud/infra-side changes (scheduling automation lives in energyme-infra, out of scope for this repo).
