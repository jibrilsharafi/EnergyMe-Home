# Tasks

Phase 0 is a throwaway instrumented build, preserved on branch `feat/bench-apsta-probe` at tag `bench/apsta-probe-v1`.

**Revised sequencing.** The original plan gated all implementation on the Phase 0 bench results. At the user's direction (no hardware available), implementation proceeds ahead of the bench and everything is tested together at the end. D13 in `design.md` records the two decisions that were narrowed so a bench answer cannot invalidate built code. Bench-2 remains the one gate that can invalidate the architecture rather than adjust it.

Phase 2 was moved ahead of Phase 1: `pio test -e native` is the only verification available without hardware, so the phase that produces runnable tests came first. Every phase after that is **compile-verified only** until the bench runs.

Bench hardware: dev device **192.168.2.174** (`admin` / `energyme00`), authorized for config/NVS/OTA experiments.

**Instrumentation note:** during AP-only provisioning the device is off the LAN, so neither the UDP log listener nor OTA can reach it, and `CustomLog` drops queued entries while `!isFullyConnected()`. **Phase 0 requires the device on USB with serial monitor.** Normal UDP capture resumes from Phase 1 onward.

```
pio run -e esp32s3-dev                          # Windows PowerShell, from source/
pio run -e esp32s3-dev -t upload -t monitor     # USB flash + serial
```

---

## Phase 0: Bench validation (throwaway, gates everything)

- [x] 0.1 Create `feat/bench-apsta-probe` off this branch. Throwaway, never merged.
- [x] 0.2 Instrumented build: raise SoftAP via `softAPConfig(172.31.42.1, ...)` then `softAP(ssid, pw, ch)`, wait `ARDUINO_EVENT_WIFI_AP_START`, call `enableDhcpCaptivePortal()`, start `DNSServer::start(53, "*", WiFi.softAPIP())`. Log `ESP.getMaxAllocHeap()` and `ESP.getFreeHeap()` every 5 s to serial.
  - `include/bench_probe.h`, `src/bench_probe.cpp`, `[env:esp32s3-bench]`, `main.cpp` swap, `setRestartSystem()` suppressed in `utils.cpp`. Builds clean (RAM 19.7%, flash 54.5%).
  - Heap line logs `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` as the **gated** number, with `maxalloc` / `free` / `minfree` / PSRAM alongside. The pinned-platform regression was internal PBUF starvation, so the internal-only figure is the one the threshold applies to.

### Runbook

Flash over USB. **Never OTA this build.**

```
pio run -e esp32s3-bench -t upload -t monitor
```

Boot is deliberately **recoverable**: AP up *and* STA started from stored credentials, so the LAN stays reachable. The Bench-2 no-route configuration is entered only by the `bench2` command, and its NVS flag is one-shot, so any reset restores LAN access.

AP: `EnergyMe-<DEVICE_ID>`, password `benchprobe`, `172.31.42.1`.

| Command | Purpose |
|---|---|
| `state` / `heap` | channels, STA/AP/DNS status; immediate heap snapshot |
| `apch <N>` | Bench-1 in-place, `softAP(ssid,pw,N)` on the live AP |
| `apre <N>` | Bench-1 / D2, stop -> `softAPConfig` -> `softAP(ssid,pw,N)` |
| `apstop` / `apstart <N>` | tear down / raise |
| `sta <ssid>\|<password>` | `WiFi.begin()`, pipe separator so SSIDs may contain spaces |
| `stastop` | STA down, credentials kept |
| `dns on\|off` | catch-all responder |
| `scan <max_ms_per_chan>` | async scan, for task 0.14 |
| `bench2` | set the one-shot no-STA flag and restart |
| `recover` | clear the flag and restart onto stored credentials |
| `erasecreds` | `esp_wifi_restore()` and restart, genuinely unprovisioned |
| `restart` | bare `ESP.restart()` |

Grep the capture for `[BENCH]`. Every probe line carries that prefix; AdvancedLogger output from the other tasks interleaves and is not prefixed.

**Order to run.** Take the STA-only baseline first, with the same binary, or a Bench-2 failure cannot be attributed:

0. Type `help` and confirm the characters echo. USB CDC gives no local echo, so the probe echoes each byte itself; if nothing appears, RX is dead and every interactive test is unreachable. Fall back to `-DARDUINO_USB_CDC_ON_BOOT=0` on UART pins in that case.
1. Boot recoverable, wait 2 min, record `internal_largest`. Then `apstop`, wait 1 min, record again. That pair is the STA-only baseline and the AP delta.
2. Bench-1: `apch 6`, read the `configured_ap_channel` / `radio_channel` pair **and the drained event ids** between before/after. An `AP_STOP` + `AP_START` pair means the driver bounced the AP rather than moving it in place, which is a different answer from "channel unchanged". Then `apch 11` with a phone associated. Then `apre 6` for the D2 sequence.
3. Bench-2: `erasecreds` first (reboots into a recoverable boot that now has no credentials), then `bench2` (reboots again, this time with STA suppressed). Two reboots; the one-shot flag is written by `bench2` and consumed by the boot right after it, so this order lands correctly. Then measure per 0.8.
4. Bench-3: rejoin the AP, `sta <ssid>|<pw>`, `dns off` on STA-connected, time the association.

### Bench-1: can a live SoftAP change channel? (informational, selects the D2 implementation)

- [ ] 0.3 With the AP up and **no** clients: log `esp_wifi_get_channel()`, call `WiFi.softAP(ssid, pw, N)` with N != current, log `esp_wifi_get_channel()` again.
- [ ] 0.4 Repeat with a phone associated. Record whether the phone drops.
- [ ] 0.5 Record result in `review-findings.md`.
  - Primary channel **unchanged** confirms the refutation. D2 stays as written (stop, reconfigure, restart on N).
  - Primary channel **changed** means the in-place re-raise works. Simplify D2 and re-test whether clients survive.

### Bench-2: APSTA heap in the unprovisioned-first-boot configuration (**pass/fail, pre-committed**)

- [ ] 0.6 Erase NVS WiFi credentials so the device boots genuinely unprovisioned (`esp_wifi_restore()` in the probe build, or `pio run -t erase` then reflash).
- [ ] 0.7 Force the D7 code path: allow boot to proceed past the WiFi wait with AP raised and STA down, so `CustomTime`, `IssueRegistry`, `CustomServer`, `ModbusTcp`, `Mqtt`, `CustomMqtt`, `InfluxDbClient` all start with no route. This is the configuration that matters, not steady-state APSTA.
- [ ] 0.8 Record `internal_largest` at steady state, then during a full UI page load from a phone on the AP, then during a concurrent Modbus poll. Compare against the STA-only baseline taken in step 1 of the runbook with the same binary.
- [ ] 0.9 **Gate.** Pass requires steady state **>= 40 KB** and never below **32 KB** under load, measured as `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`. Below 32 KB, the change does not proceed in APSTA form. Record the numbers before interpreting them. Note this is an `-O0` dev build; the same-binary baseline is what makes a fail attributable to APSTA rather than to the build.
- [ ] 0.10 Note the `async_udp` task cost: `DNSServer` is the firmware's first AsyncUDP user, adding a 4096 B internal-stack task plus a 32-entry queue. Confirm it appears in the task list.

### Bench-3: does the phone stay on the AP after STA connects? (**pass/fail, decides A1**)

- [ ] 0.11 Phone on the AP with a page open polling a status endpoint. Connect STA to a real network. Stop the DNS server on STA-connected, per D4.
- [ ] 0.12 Measure how long the phone stays associated, on **iOS and Android** separately. Record whether the OS shows a no-internet prompt, and whether it auto-rejoins the remembered home SSID.
- [ ] 0.13 **Gate.** Pass requires **>= 60 s** associated without manual intervention on both platforms.
  - Fail means the 5-minute grace window is dropped from D1. Replace with: surface the LAN address and `energyme.local` **on submit**, before the transition, and treat any post-connect view as a bonus. Update D1 and D4 accordingly.
- [ ] 0.14 Also measure AP client stability during a `scanNetworks()` with `max_ms_per_chan` at the Arduino default (300 ms) versus ~120 ms. Pick the value.

### Decision gate

- [ ] 0.15 Write results into `review-findings.md`. If Bench-2 fails, stop and reopen the descoped alternative (non-blocking WiFiManager portal via `setConfigPortalBlocking(false)` + `process()`, verified at `WiFiManager.h:371`, `:272`). If Bench-3 fails, proceed with the amended UX only.
- [ ] 0.16 Delete `feat/bench-apsta-probe` **only after the bench has run**. The branch and tag `bench/apsta-probe-v1` are the reproducible history point for the probe binary; deleting them before the measurements exist would throw away the only build that can take them.

---

## Phase 1: Fix the health-check reboot loop (independent, ship regardless)

Required before D7. Without it, AP-only provisioning reboots every ~150 s and reaches firmware rollback plus NVS wipe at ~25 min. Currently masked only by the blocking wait at `main.cpp:151`.

- [x] 1.1 Add `CustomWifi::isNetworkServiceable()`: STA connected **or** AP raised and serving. Declare in `customwifi.h`. Added alongside `isApServing()` (`WiFi.AP.started()` + a non-zero `softAPIP()`); the rule itself lives in `lib/wifi_provisioning` so it stays unit-tested.
- [x] 1.2 `_performHealthCheck` (`customserver.cpp:474`) calls `isNetworkServiceable()` instead of `isFullyConnected()`.
- [x] 1.3 Unit-test the predicate in `lib/wifi_provisioning`. Done in Phase 2, which was moved ahead of Phase 1 because `pio test -e native` is the only verification available without hardware.
- [ ] 1.4 Bench: force AP-only for 30 min, confirm zero reboots and that `_consecutiveResetCount` does not climb. Capture serial. **Blocked on hardware.**
- [ ] 1.5 Commit `fix(server): keep health check green while serving on the AP netif`.

---

## Phase 2: Pure provisioning logic (`source/lib/`, no hardware)

- [x] 2.1 `source/lib/wifi_provisioning/wifi_provisioning.h` (+ `.cpp` if needed). SPDX header, `#pragma once`, `<cstdint>` / `<cstddef>` only. No Arduino, FreeRTOS or ESP headers.
- [x] 2.2 State enum and transition table per D8: `UNPROVISIONED`, `STA_CONNECTING`, `AP_ASSIST`, `GRACE`, `STA_ONLY`.
- [x] 2.3 AP-raise predicate. **Separate "STA retry attempts" from "AP-raise trigger count"** so `_forceReconnectInternal()` cannot poison it (D8).
- [x] 2.4 Grace-window and `WIFI_AP_MAX_LIFETIME` arithmetic, both bounded (D1).
- [x] 2.5 AP subnet candidate selection with overlap detection against a given STA subnet. CIDR /24 to /28 only.
- [x] 2.6 `source/test/test_wifi_provisioning/test_wifi_provisioning.cpp`. Explicit `main()` with `UNITY_BEGIN` / `RUN_TEST` / `UNITY_END`.
- [x] 2.7 Cover: unbounded-AP regression, counter-poisoning regression, subnet overlap including a foreign restored static IP, grace expiry, lifetime expiry.
- [x] 2.8 `pio test -e native` **from WSL**. No `platformio.ini` change needed.
- [x] 2.9 Commit `feat(wifi): add pure provisioning state machine with unit tests`.

---

## Phase 3: AP lifecycle in `customwifi`

**Sequencing amendment (found while implementing 3.1).** The notify loop does not exist until `autoConnect()` returns. `autoConnect()` owns the whole unprovisioned path: it raises its own AP, runs its own portal, blocks the task, and on failure calls `setRestartSystem()` then `vTaskDelete(NULL)` (`customwifi.cpp:577-587`). Consequences:

- 3.1, 3.2, 3.3 edit code that runs today and can be done in place.
- 3.4 has no `WifiProvisioning::Context` to read. Nothing in `customwifi.cpp` owns one, and neither `init()` nor `onEvent()` is called anywhere.
- 3.5 to 3.8 presuppose the device sitting in `UNPROVISIONED` with our loop running, which is unreachable while `autoConnect()` exists. Written as-is they would be dead code.

So **3.4a and 3.4b below are inserted before 3.5**, pulling the Context ownership and the non-blocking connect (previously Phase 6.1/6.2) forward. Phase 6 keeps only the deletion of WiFiManager itself. This is the D13 amendment mechanism.

- [x] 3.1 Register all WiFi event handlers at boot per D10, including `AP_START`, `AP_STACONNECTED`, `AP_STADISCONNECTED`. Callbacks notify-only. Delivery stays gated by `_eventsEnabled` (registration timing is the hazard, not gate timing).
- [x] 3.2 Remove `WiFi.removeEvent(_onWiFiEvent)` at `customwifi.cpp:893` (unlocked-vector use-after-free hazard).
- [ ] 3.3 Replace the 15 s blocking `delay(WIFI_DISCONNECT_DELAY)` at `customwifi.cpp:665` with a deadline checked in the notify loop.
- [ ] 3.4 Suppress `_forceReconnectInternal()` while `UNPROVISIONED` (`customwifi.cpp:742-747`). Depends on 3.4a.
- [ ] 3.4a Own a `WifiProvisioning::Context` in `customwifi.cpp`: `init()` at boot from "are there stored credentials", `onEvent()` fed from the notify loop, guarded by the existing task-state discipline. Expose the state through an atomic read for the Phase 4 filter.
- [ ] 3.4b Replace `autoConnect()` with a non-blocking connect driven by the notify loop, so `UNPROVISIONED` becomes a state the device can actually sit in. Credentials read via `esp_wifi_get_config`; no portal, no restart on save. **This is the load-bearing change of the whole proposal.**
- [ ] 3.5 AP raise/teardown in the documented order: `softAPConfig` -> `softAP(ssid, pw, ch)` -> wait `AP_START` -> `enableDhcpCaptivePortal()`.
- [ ] 3.6 DNS lifecycle per D4, always `start(53, "*", WiFi.softAPIP())`, stopped on STA-connected.
- [ ] 3.7 D2 channel handling, in whichever form Bench-1 selected.
- [ ] 3.8 Async scan with a cache, `max_ms_per_chan` per 0.14. Never scan during `STA_CONNECTING` (`ESP_ERR_WIFI_STATE`).
- [ ] 3.9 Bench: full provisioning cycle on .174. Capture serial.
- [ ] 3.10 Commits, one concern each: events, disconnect deadline, AP lifecycle, DNS lifecycle, scan cache.

---

## Phase 4: Server auth carve-out and provisioning API

- [ ] 4.1 `isProvisioningOrigin(request)` filter: atomic state read plus `client()->localIP() == WiFi.softAPIP()`. No mutex, no NVS, no logging (constraint 4). **Do not use `ON_AP_FILTER`.**
- [ ] 4.2 Twin-handler registration per D3: open handler first with `.setFilter().skipServerMiddlewares().addMiddleware(&rateLimit)`, authenticated twin second.
- [ ] 4.3 Captive probe routes as explicit handlers, not `onNotFound`: `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`, `/ncsi.txt`, `/connecttest.txt`, `/redirect`.
- [ ] 4.4 Provisioning API under `/api/v1/network/wifi/`: `GET scan` (cached), `POST connect`, `GET status`. **`POST connect` must only `xTaskNotify` and return.**
- [ ] 4.5 **Do not call `_validateRequest` in a GET that replies via `_sendJsonResponse`.** `_validateRequest` acquires `_apiMutex` and only `_sendSuccessResponse` / `_sendErrorResponse` release it. Follow the existing GET convention at `customserver.cpp:1743`.
- [ ] 4.6 Client JS must send `Content-Type: application/json`; `AsyncCallbackJsonWebHandler` requires it (`AsyncJson.cpp:125-140`).
- [ ] 4.7 Register every route at `begin()`; gate at request time. Never add handlers after `server.begin()`.
- [ ] 4.8 Disconnect-diagnostics endpoint per D11 (`_lastDisconnect*` currently have no getter and are consumed only by `/diagnostic`).
- [ ] 4.9 **Negative tests, mandatory:** no bypass in `AP_ASSIST`; no bypass on the STA netif; no bypass for `127.0.0.1` (the health probe); OTA never bypassed in any state.
- [ ] 4.10 **Bench-4:** LAN host with a static route to the AP subnet via the STA IP, `curl http://<AP_IP>/api/v1/network/wifi/scan` while `UNPROVISIONED`. **Pass requires 401.** Log `client()->localIP()` inside the filter to see what lwIP presents. Fail blocks Phase 4 until the filter is hardened.
- [ ] 4.11 Commits: filter + carve-out, captive probes, provisioning API, diagnostics endpoint.

---

## Phase 5: Web UI

- [ ] 5.1 WiFi setup page. Captive portal lands here; navigation to the rest of the UI stays available (D9).
- [ ] 5.2 Client flow: scan, select, submit, poll status across a multi-second association gap with retry (D2 guarantees at least one drop).
- [ ] 5.3 Surface the LAN address and `energyme.local` **on submit**, not only after connect. Mandatory if Bench-3 failed; good practice either way. mDNS over the AP is verified working.
- [ ] 5.4 Embed plumbing: `platformio.ini` `board_build.embed_txtfiles`, `extern ... asm("_binary_html_setup_html_start")` pair in `include/binaries.h`, `server.on()` in `_serveStaticContent`.
- [ ] 5.5 Static assets on the AP: either a `/css/*` prefix handler registered before the ~14 exact matches (`customserver.cpp:762-807`), or inline the setup page CSS.
- [ ] 5.6 LED states for AP provisioning; resolve the priority collision (open question in `design.md`).
- [ ] 5.7 Commits: setup page, client flow, embed plumbing, LED.

---

## Phase 6: Remove WiFiManager

Only after Phases 1 to 5 are green on hardware.

- [ ] 6.1 Move the power-reset extended timeout out of `_setupWiFiManager` (`customwifi.cpp:170-179`) into the STA connect path. **Without this every household power blip raises a SoftAP.**
- [ ] 6.2 Replace `resetSettings()` with `esp_wifi_restore()` / `nvs.net80211` erase. Verify from the button (`buttonhandler.cpp:282`) and `/api/v1/network/wifi/reset`.
- [ ] 6.3 Replace `getWiFiSSID(true)` with `esp_wifi_get_config(WIFI_IF_STA, ...)`.
- [ ] 6.4 Port connect retries / clean connect / duplicate-AP removal (`customwifi.cpp:179-184`).
- [ ] 6.5 Delete `_setupWiFiManager`, `_setupDiagnosticEndpoint`, `_appendToPageBuffer` (~`customwifi.cpp:165-400`).
- [ ] 6.6 Remove `#include <WiFiManager.h>` from `customserver.h:6` and `customwifi.h:14`; drop the ordering comment at `main.cpp:18`.
- [ ] 6.7 Remove `tzapu/WiFiManager@2.0.17` from `platformio.ini`.
- [ ] 6.8 D7 boot-order change at `main.cpp:151`. **Requires Phase 1.**
- [ ] 6.9 Re-trigger NTP configuration on the STA-connected transition (`customtime.cpp:242-244` caches `0.0.0.0` as the gateway NTP server).
- [ ] 6.10 Suppress `ntp_not_synced` / `cloud_mqtt_disconnected` / `custom_mqtt_connect_failed` while unprovisioned.
- [ ] 6.11 Decide Modbus on the AP netif: filter or accept (open question).
- [ ] 6.12 Commits, one concern each.

---

## Phase 7: Documentation and strings

- [ ] 7.1 `configuration.html:315` ("no password" is wrong), `:320`, `:449`, `:476`, `:480` (192.168.4.1 -> 172.31.42.1).
- [ ] 7.2 `customserver.cpp:1690`, `:1737` both promise a restart that no longer happens.
- [ ] 7.3 `manual/02-setup.md:14-18`.
- [ ] 7.4 `resources/swagger.yaml`: new scan / connect / status / diagnostics routes.
- [ ] 7.5 Release note: `/diagnostic` is gone, credential changes no longer reboot, static IP changes still do.
- [ ] 7.6 Confirm and document that the 5-10 s button press remains the escape hatch for a forgotten web password, now that AP-side OTA sits behind auth (D9).

---

## Verification summary

| Gate | Blocks | Pass criterion |
|---|---|---|
| Bench-2 | Everything after Phase 0 | `getMaxAllocHeap()` >= 40 KB steady, never < 32 KB under load, unprovisioned-first-boot config |
| Bench-3 | The grace window in D1/D4 | Phone stays associated >= 60 s after STA connect, iOS and Android |
| Bench-1 | D2 implementation choice | Informational |
| Bench-4 | Phase 4 ship | LAN host via static route gets 401, not 200 |
| Phase 1 bench | D7 / Phase 6.8 | 30 min AP-only, zero reboots, reset counter flat |
| `pio test -e native` | Phase 2 | All green, from WSL |
