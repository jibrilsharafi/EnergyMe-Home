## Why

Recovering a bench device's factory NVS today has no tooling. The `factory_ns` namespace (pcb revision, AP password, AWS IoT cert/key) sits inside the single shared `nvs` partition (`partitions_esp32s3_n16r2.csv`) alongside WiFi credentials and every other app namespace - there is no separate manufacturing-only partition. Rebuilding and flashing a full NVS partition image to fix one field would wipe everything else in that 64 kB region, including WiFi credentials, on a device already reachable over the network.

This was hit directly during hardware verification of `crash-archive-single-publish`: the bench device's `factory_ns::pcb_revision` NVS key held the malformed value `"v5"` instead of `"v5.0"` (fails the firmware's `sscanf("v%u.%u", ...)` parse), which silently put the device into community mode - cloud/MQTT disabled - and left the two archived crash records from that verification unable to publish. There was no way to inspect or fix a single NVS field without either a serial-flashed full-partition rewrite (destructive to WiFi credentials) or new firmware.

## What Changes

- Add a dev-only (`#ifdef ENV_DEV`) HTTP API to browse and edit arbitrary NVS namespaces and keys on a live device, using the ESP-IDF `nvs_entry_find`/`nvs_entry_next` enumeration API plus the existing Arduino `Preferences` wrapper for reads/writes. Never compiled into production firmware, following the same gating already used for the shadow-injection and crash-test debug endpoints.
- List namespaces and their entry counts; list a namespace's keys with type and value; write or delete a single key; clear an entire namespace.
- Redact `cert_pem` and `key_pem` values specifically (by key name) in list responses - their presence and type are visible, their content is not, even on a dev-only endpoint.

## Capabilities

### New Capabilities
- `device-provisioning`: dev-only HTTP inspection and editing of NVS namespaces/keys for bench-device recovery, without a full-partition reflash.

### Modified Capabilities
(none)

## Impact

- `source/src/customserver.cpp` / `source/include/customserver.h`: new `_serveNvsDebugEndpoints()`, registered alongside the other `#ifdef ENV_DEV` debug endpoint groups.
- No changes to production (`esp32s3-prod`) firmware - the endpoints do not exist in that build.
- Used immediately to fix the bench device's `pcb_revision` (`"v5"` -> `"v5.0"`) and to load a freshly issued AWS IoT cert/key pair after the previous one was found stale, unblocking the crash-publish verification for `crash-archive-single-publish` (see that change's `tasks.md` 9.7).

## Non-goals

- Value redaction is name-based (`cert_pem`, `key_pem` only), not a general secrets policy - a future factory key added without updating the redaction list would be readable.
- No enumeration of NVS blob values (only key/type are listed for blobs); no bulk import/export.
