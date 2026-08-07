## 1. NVS browser/editor endpoints (dev-only)

- [x] 1.1 `GET /api/v1/debug/nvs/namespaces`: enumerate every namespace in the `nvs` partition via `nvs_entry_find`/`nvs_entry_next`, returning `{namespace, entryCount}` per namespace. Bounded by `NVS_DEBUG_MAX_ENTRIES` iterations and `NVS_DEBUG_MAX_NAMESPACES` distinct namespaces.
- [x] 1.2 `GET /api/v1/debug/nvs/entries?namespace=X`: enumerate one namespace's keys with `{key, type}`, plus `value` for string/i32/u32/i64/u64 types. `cert_pem` and `key_pem` values are always `"<redacted>"` regardless of type.
- [x] 1.3 `POST /api/v1/debug/nvs/entry` `{namespace, key, type, value}`: write one key via `Preferences`. `type` one of `string` (default) | `i32` | `u32` | `i64` | `u64` | `float` | `bool`.
- [x] 1.4 `DELETE /api/v1/debug/nvs/entry?namespace=X&key=Y`: remove one key.
- [x] 1.5 `DELETE /api/v1/debug/nvs/namespace?namespace=X`: clear every key in a namespace.
- [x] 1.6 Registered in `_serveApi()` inside the existing `#ifdef ENV_DEV` block, alongside `_serveShadowDevEndpoints()` / `_serveCrashTestEndpoints()`.

## 2. Verification

- [x] 2.1 `pio run -e esp32s3-dev-v5` compiles clean.
- [x] 2.2 Hardware (bench device, `192.168.1.27`): `GET /api/v1/debug/nvs/namespaces` returned all 15 namespaces present on the device (`factory_ns`, `wifi_ns`, `channels_ns`, etc.) with plausible entry counts.
- [x] 2.3 Hardware: `GET /api/v1/debug/nvs/entries?namespace=factory_ns` showed `pcb_revision: "v5"` (the actual bug) and `cert_pem`/`key_pem` correctly redacted.
- [x] 2.4 Hardware: `POST .../entry` corrected `pcb_revision` to `"v5.0"` and wrote a freshly issued AWS IoT cert/key pair; a follow-up `GET` confirmed the write without exposing the cert/key values.
- [x] 2.5 Hardware: after restart, cloud MQTT connected (`mqtt` task went from an all-zero stat block to an active loop count) and the two crash records archived during `crash-archive-single-publish` verification published and drained from the archive (`/api/v1/crash/info` count 0), which was the point of building this tool.

## 3. Commit

- [x] 3.1 `customserver.cpp` / `customserver.h`: NVS debug endpoints.
- [x] 3.2 This openspec change.
