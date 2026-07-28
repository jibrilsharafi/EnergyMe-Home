> Revision 2. Rewritten after four critical reviews. Every decision below is either verified against the
> pinned toolchain or explicitly marked as pending a bench result. Evidence and the refutation history for
> revision 1 are in `review-findings.md`.

## Context

### Current state

```
main.cpp:145  CustomWifi::begin()
main.cpp:151  while (!CustomWifi::isFullyConnected()) delay(1000);   <- blocks setup()
main.cpp:176  CustomServer::begin()                                  <- unreachable until WiFi up

wifi task (customwifi.cpp:526)
  └─ WiFiManager::autoConnect(ap, pw)            BLOCKING
       ├─ fail  -> startConfigPortal()           BLOCKING, up to 5 min, then restart
       └─ save  -> setRestartSystem()            mandatory reboot
```

The meter is **not** blocked. `Ade7953::begin()` (`main.cpp:131`) starts five tasks before the WiFi wait, and `main.cpp:148-150` says so. What blocks is `setup()`, whose task is deleted at `main.cpp:214`. The real cost of the block is that `CustomServer`, MQTT, Influx and Modbus never start, plus the WiFi task itself is stalled and cannot service its own event loop.

### Toolchain facts, verified against arduino-esp32 3.3.2 / ESP-IDF 5.5.1 / ESPAsyncWebServer 3.10.0

| Fact | Evidence | Consequence |
|---|---|---|
| `AsyncWebServer` binds `0.0.0.0` | existing behaviour | The current server already serves the AP netif. No second HTTP stack |
| `.setFilter()` + `.skipServerMiddlewares()` bypasses the whole server middleware chain | `WebRequest.cpp:877-891`; in-tree precedent `customserver.cpp:866` | This is the auth carve-out mechanism |
| Middleware `next()` has **no** skip primitive | `Middleware.cpp:56-71` | A middleware cannot skip `digestAuth`. Revision 1 was wrong |
| Filters run before middleware, and before `canHandle()` | `WebServer.cpp:145-154`, `WebRequest.cpp:868` | Filters select the handler; they run on the AsyncTCP task for every request |
| `onNotFound` ignores filters and **does** run middleware | `WebServer.cpp:153`, `ESPAsyncWebServer.h:1315` | Captive probes cannot go through `onNotFound`; they need explicit routes |
| `DNSServer::processNextRequest()` is a no-op stub; AsyncUDP-backed | `DNSServer.h:116`, `DNSServer.cpp:35-38` | No polling task |
| `DNSServer` binds `INADDR_ANY:53`, replies with a fixed `_resolvedIP` | `DNSServer.cpp:38,173-176` | Must not run while STA is up |
| `DNSServer::start()` sets `_resolvedIP` only when it is 0 | `DNSServer.cpp:21-32` | Always use `start(53, "*", WiFi.softAPIP())` |
| A live SoftAP **cannot** be moved to another channel | `wifi_softap_set_config` callee set has no channel setter; `esp_wifi.h:773` | D2 must stop and restart the AP |
| SoftAP follows the STA channel at association | `esp_wifi.h:987-988` | Unavoidable. One radio, one home channel |
| mDNS responds on the SoftAP netif | `CONFIG_MDNS_PREDEF_NETIF_AP=1`, `sdkconfig.h:1805` | `energyme.local` works over the AP |
| `enableDhcpCaptivePortal()` sets DHCP option 114, requires `started()` | `AP.cpp:309-346` | Call after `ARDUINO_EVENT_WIFI_AP_START` |
| `lwIP ip4_route` returns the **first** matching netif; `netif_add` **prepends** | disassembly of `liblwip.a` | An AP raised after STA wins every ambiguous match |
| AP netif and DHCP server already exist in STA-only mode | `WiFiGeneric.cpp:290-292` | Incremental APSTA heap cost is smaller than revision 1 claimed |
| `NETWORK_EVENTS_MUTEX` is never defined; `_cbEventList` is unlocked | `NetworkEvents.cpp:122-147,170,183` | Register all handlers at boot; `removeEvent()` at runtime is the hazard |
| `AsyncRateLimitMiddleware` is global, not per-IP, and inert below 600 s uptime | `Middleware.cpp:272-300` | Revision 1's D6 was a fabricated risk. Deleted |

### Hard constraints inherited from this codebase

1. **Heap.** A ~50 KB internal-heap regression previously caused silent LWIP/AsyncTCP PBUF failures under load. Measure `getMaxAllocHeap()` (largest contiguous block), not `getFreeHeap()`.
2. **Never reconfigure a live netif.** `customwifi.h:56-60`: `WiFi.config(0,0,0)` races lwIP and asserts in `ip4_route`. Static-IP recovery is always restart-onto-DHCP.
3. **Event callbacks do nothing but `xTaskNotify`.** They run on the `arduino_events` task with a 4096 B stack and a 32-deep queue; blocking there stalls the esp_event loop.
4. **Never block in an AsyncWebServer handler.** AsyncTCP is single-threaded; `API_MUTEX_TIMEOUT_MS` is 2000 with a WDT warning at `customserver.h:64`.
5. No `String` in our code, `char[]` + `snprintf`, no try/catch, bounded `while` loops, task stacks in internal RAM.

## Goals / Non-Goals

**Goals**
- The WiFi task is never blocked by provisioning, so it can service its own events, the AP lifecycle and the grace timer.
- `CustomServer` and the local integrations come up even when there is no upstream network.
- One UI and one API surface for setup and daily use.
- A credential change does not reboot the device.
- WiFiManager and its synchronous web stack are gone.
- Provisioning decision logic is host-testable.

**Non-Goals**
- BLE provisioning, always-on AP, multi-SSID lists.
- Reboot-free static IP changes (constraint 2).
- A captive-portal popup that persists during APSTA (needs a netif-aware DNS responder; deferred).
- Keeping the user's phone associated across the STA transition. Verified impossible, see D2.

## Decisions

### D1: On-demand AP, bounded lifetime

Raise the AP when: no credentials in NVS, or STA has failed `WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS` consecutive times, or the user asks (button, or a UI toggle while connected).

Tear it down when: 5 minutes have elapsed after a successful STA association initiated from the AP (the grace window), **or** `WIFI_AP_MAX_LIFETIME` (30 min) has elapsed with no client ever connecting, **or** the user dismisses it.

The `AP_ASSIST` lifetime bound is not optional. Today `startConfigPortal()` times out after `WIFI_PORTAL_TIMEOUT_SECONDS` and `customwifi.cpp:694` restarts, so the AP window is bounded at 5 minutes per boot. An unbounded `AP_ASSIST` would leave a device whose router died, or whose owner moved house, broadcasting indefinitely. Preserve the bound.

*Rejected: always-on AP.* Permanent radio and RAM cost, and on community devices the AP PSK falls back to `DEVICE_ID`, which is the SSID suffix.

### D2: AP restart on the target channel (REWRITTEN)

One radio, one home channel. In `WIFI_AP_STA` the AP follows the STA channel at association (`esp_wifi.h:987-988`), and a **live AP with clients cannot be moved**: `wifi_softap_set_config` has no channel-setting callee, and `esp_wifi_set_channel()` is documented as unsafe with connected STAs (`esp_wifi.h:773`).

So the client drops. The question is only *when*, and whether it drops twice.

```
naive:     client on ch1 -> WiFi.begin() -> AP yanked to ch11 at the moment of success
           unpredictable drop exactly when the user is waiting on the result

chosen:    scan cache gives target channel N
           softAPdisconnect() -> softAPConfig(...) -> softAP(ssid, pw, N)
           client drops HERE, early, before the user submits anything sensitive
           client re-associates to the same SSID/BSSID on N within a few seconds
           WiFi.begin() -> STA joins on N -> AP does NOT move again
           one predictable early drop, then stable through the result
```

The client-side poller must survive a multi-second gap and retry rather than reporting failure.

For a manually typed SSID the channel is unknown; skip the re-raise and accept the late drop.

`esp_wifi_set_config` persists to NVS on every call (`esp_wifi.h:989`), so do not re-pin the channel repeatedly.

**Pending Bench-1.** Static call-graph evidence, not runtime. If Bench-1 shows `WiFi.softAP(ssid,pw,N)` *does* move a live AP, revert to the simpler in-place re-raise.

### D3: Auth carve-out via filter + skipServerMiddlewares (REWRITTEN)

`next()` cannot skip the chain. The working mechanism, already used at `customserver.cpp:866`:

```cpp
// open handler registered FIRST, wins by insertion order (WebServer.cpp:145-154)
server.on(path, method, cb)
      .setFilter(isProvisioningOrigin)
      .skipServerMiddlewares()
      .addMiddleware(&rateLimit);      // restore what skip dropped

// authenticated twin, registered SECOND
server.on(path, method, cb);
```

`isProvisioningOrigin(request)` must be an atomic state read plus an IP compare, nothing else. It runs on the AsyncTCP task for every request to every path (constraint 4): no mutex, no NVS, no logging.

```cpp
// shape only
return _provisioningState == UNPROVISIONED
    && request->client()->localIP() == WiFi.softAPIP();
```

Do **not** use `ON_AP_FILTER`. It is `WiFi.localIP() != client()->localIP()` (`WebServer.cpp:28-34`), which compares against the SYN destination, not the arrival netif. It returns true for the device's own `127.0.0.1` health probe (`customserver.cpp:484`), and true for everything whenever `WiFi.localIP()` is `0.0.0.0`. Compare against `WiFi.softAPIP()` explicitly, and treat the **state check as carrying the safety**.

**Unresolved, Bench-4:** if lwIP's weak host model accepts packets destined to the AP IP arriving on the STA netif, a LAN host with a static route could satisfy the IP compare. Must be tested before shipping.

### D4: DNS server runs only while STA is down

```
AP raised, STA down  ->  dnsServer.start(53, "*", WiFi.softAPIP())
STA connected        ->  dnsServer.stop()
grace expires        ->  AP down
```

Always use the three-argument form. The no-arg `start()` sets `_resolvedIP` only when it is currently 0 (`DNSServer.cpp:21-32`), so after D5's backstop changes the AP IP it would answer with a stale address.

Rationale for the lifecycle bound: `DNSServer` binds `INADDR_ANY:53` and replies with a fixed IP regardless of arrival interface, so running it during APSTA makes the device an open resolver on the customer's LAN answering every name with the AP IP.

**Known cost, and the reason A1 below matters:** stopping DNS removes the AP's resolver, which is one of the signals an OS uses to decide a network has no internet.

**A1, unresolved, Bench-3.** The grace window exists so the user can read their new LAN address. But once STA connects and DNS stops, iOS re-runs `captive.apple.com`, fails, and may auto-rejoin the remembered home SSID; Android offers to switch to cellular. If the phone leaves, the grace window delivers nothing. Bench-3 decides whether the window is worth keeping, or whether the correct UX is to show the LAN address and `energyme.local` *immediately on submit*, before the transition, and treat any post-connect view as a bonus.

`AsyncUDPPacket` exposes `localIP()` and `interface()` (`AsyncUDP.h:79-81`), so the deferred netif-aware responder is a one-line filter in a custom handler, not the ~60 lines previously estimated. Stock `DNSServer` offers no hook.

### D5: AP subnet selection

lwIP does **not** longest-prefix match. `ip4_route` walks `netif_list` and returns the **first** netif satisfying `(dest ^ ip_addr) & netmask == 0`, and `netif_add` **prepends**. An AP raised after STA is up therefore sits at the head and **wins every ambiguous match**: LAN-destined traffic exits the AP.

- Default the AP to `172.31.42.1/24`. Consumer routers essentially never use it; the ESP default `192.168.4.1` does collide with some ISP CPE.
- Runtime backstop: at AP-raise time compare the candidate against the live STA subnet, or against `WifiConfiguration.ip`/`subnet` when STA is down. On overlap, advance to the next candidate.
- Late collision (AP raised while unprovisioned, STA later gets a colliding lease): log WARNING and tear the AP down early. Do not re-raise.

Constraints from `NetworkInterface::config`: **CIDR must be /24 to /28** (`NetworkInterface.cpp:440-443`, hard fail outside), and the DHCP pool is **10 addresses** starting at `ap_ip + 1` (`:448-453`).

Ordering matters. `softAPConfig()` calls `AP.begin()` -> `enableAP(true)` (`WiFiAP.cpp:71-73`), raising the AP interface **before** SSID and channel are set, briefly beaconing the previous or NVS-stored config:

```
softAPConfig(ip, gw, mask, leaseStart, dns)   // raises netif, sets ip_info + dhcps
softAP(ssid, pw, channel)                     // wifi_softap_start applies the channel
wait ARDUINO_EVENT_WIFI_AP_START
enableDhcpCaptivePortal()                      // requires started()
```

### D6: DELETED

Revision 1 claimed captive-detection bursts would trip the rate limiter. `WEBSERVER_MAX_REQUESTS 6000` / `WINDOW 600 s` is 10 req/s sustained, and `Middleware.cpp:275` underflows `uint32` below 600 s uptime so the limiter is inert during exactly the window provisioning occupies. No change needed.

### D7: Boot order, gated on fixing the health check first (REWRITTEN)

`main.cpp:151` becomes a wait on "STA connected **OR** AP raised and serving". `CustomServer::begin()` stays at `main.cpp:176`.

**This change is unsafe until S1 is fixed.** `_performHealthCheck` gates on `CustomWifi::isFullyConnected()` (`customserver.cpp:474`), which is STA-only. With the AP up and STA down it fails every 30 s, and 5 failures trigger `setRestartSystem` at ~150 s. That uptime is above `QUICK_RESTART_THRESHOLD` (60 s) so safe mode never arms, and below `COUNTERS_RESET_TIMEOUT` (180 s) so `_consecutiveResetCount` never clears. At `MAX_RESET_COUNT` (10 prod) the device performs `Update.rollBack()` and a user-NVS wipe (`crashmonitor.cpp:315-343`). Roughly 25 minutes in provisioning destroys the user's configuration.

Masked today only because `main.cpp:151` blocks before `CustomServer::begin()` runs. **D7 removes the mask, so S1 must land first, in its own commit, with its own test.**

Fix: introduce `CustomWifi::isNetworkServiceable()` (STA connected OR AP raised and serving) and have the health check call that. The self-probe targets `127.0.0.1` and `/api/v1/health` already carries `.skipServerMiddlewares()`, so it works fine on the AP netif.

Downstream behaviour with no upstream network, all verified, none fatal:
- `CustomLog` safe, but UDP log entries are dropped from the queue front, so UDP debugging does not work during provisioning. Use serial for Bench tests.
- `CustomTime::begin()` passes `WiFi.gatewayIP()` = `0.0.0.0` as the primary NTP server (`customtime.cpp:242-244`) and that stale value persists until the next sync interval after STA comes up. Needs a re-trigger on the STA-connected transition.
- `IssueRegistry` raises `ntp_not_synced`, `cloud_mqtt_disconnected`, `custom_mqtt_connect_failed` for the whole window, plus undeliverable raise-edge publishes. Suppress while unprovisioned.
- `ModbusTcp` binds all interfaces (`modbustcp.cpp:22`), unauthenticated on the AP. Filter it off the AP netif or accept explicitly.
- `Mqtt` / `CustomMqtt` / `InfluxDbClient` are safe; all gate on `isFullyConnected()` before connecting.
- `startMaintenanceTask` heap threshold is 1 KB (`utils.h:59`), so it provides **no** guard against the APSTA heap risk. That failure mode is silent PBUF starvation, not a restart.

### D8: State machine in `source/lib/`, and the WiFi task rework it requires

```
                       no creds in NVS
   BOOT ──────────────────────────────────────► UNPROVISIONED (AP up, DNS on)
     │                                                  │
     │ creds present                          creds submitted, AP re-raised on ch N
     ▼                                                  ▼
  STA_CONNECTING ◄──────────────────────────────  STA_CONNECTING
     │        │                                         │
  success   N failures                              success │ fail
     │        ▼                                         │   │
     │   AP_ASSIST (APSTA, DNS on until STA up) ◄────────┘   │
     │        │  bounded by WIFI_AP_MAX_LIFETIME             │
     │     success                                    stay, surface reason
     ▼        ▼                                              │
  STA_ONLY ◄── grace window (5 min, AP up, DNS off) ─────────┘
```

The current WiFi task cannot host this:

- `WIFI_EVENT_DISCONNECTED` does `delay(WIFI_DISCONNECT_DELAY)` = **15 s blocking** (`customwifi.cpp:665`). Every failed attempt stalls the task: no AP-client events, no DNS lifecycle, no grace timer. Replace with a deadline checked in the notify loop.
- The periodic-check `else` branch (`customwifi.cpp:742-747`) calls `_forceReconnectInternal()` every 30 s while STA is down, which **increments `_reconnectAttempts`** (`:969`), the exact counter D1 uses as its AP-raise predicate. It only resets after 5 minutes of `isFullyConnected()` true (`:736`), so while the AP is up it grows from two sources and the predicate never settles. Separate "STA retry attempts" from "AP-raise trigger count", and suppress forced reconnects while in `UNPROVISIONED`.

Pure logic (transition table, retry counters, grace and lifetime arithmetic, AP-raise predicate, subnet-candidate selection) goes in `source/lib/wifi_provisioning/` with Unity tests, following `lib/duration_format` and `lib/phase_utils`. No Arduino, FreeRTOS or ESP headers. No `platformio.ini` change needed; `[env:native]` resolves via LDF.

### D9: Auth scope on the AP

- **`UNPROVISIONED`**: the full UI is reachable from the AP without the web password. The device holds nothing worth protecting, the AP is WPA2-gated, and the user may not know the default password on first boot. This delivers "land on WiFi setup, then explore freely."
- **`AP_ASSIST`**: full digest auth. The device is an in-service meter that merely lost its network.
- **OTA is always behind auth**, in both states. Team decision: OTA over the AP is a deliberate recovery path and is acceptable behind WPA2 + digest, which is stronger than today's unauthenticated WiFiManager portal.

Consequence to handle deliberately: the AP-side OTA rescue no longer covers "user forgot the web password". The 5-10 s button press already resets the web password (`buttonhandler.cpp:179-205`), which should remain the escape hatch. Confirm and document.

Because `UNPROVISIONED` opens the whole UI, the bypass is implemented as a filter on a broad route set rather than a narrow path allowlist, and the **state check is the only real gate** (see D3). Negative tests are mandatory: no bypass in `AP_ASSIST`, no bypass on the STA netif, no bypass for `127.0.0.1`.

### D10: Register all WiFi event handlers at boot

Revision 1 listed early event registration as a hazard. It is the opposite. `NETWORK_EVENTS_MUTEX` is never defined, so `_cbEventList` (a `std::vector`) is mutated without a lock while the `arduino_events` task iterates it (`NetworkEvents.cpp:122-147,170,183`). A `push_back` reallocation mid-iteration is a use-after-free, which is the most plausible cause of the crash noted at `customwifi.cpp:588-590`.

Register everything once at boot, before any event can be posted. The genuine hazard is `WiFi.removeEvent(_onWiFiEvent)` at `customwifi.cpp:893` during shutdown, while events still flow; drop that call. Add `ARDUINO_EVENT_WIFI_AP_START`, `_AP_STACONNECTED`, `_AP_STADISCONNECTED`. Do not unmask `AP_PROBEREQRECVED`. Keep callbacks notify-only (constraint 3).

### D11: Preserve behaviour that currently lives inside WiFiManager

Deleting `_setupWiFiManager` deletes these by accident unless they are moved first:

| Behaviour | Current location | Replacement |
|---|---|---|
| Power-reset extended timeout, 5 min for the router to boot | `_isPowerReset()` + `WIFI_CONNECT_TIMEOUT_POWER_RESET_SECONDS`, `customwifi.cpp:170-179` | Move into the STA connect path. **Without this, every household power blip raises a SoftAP on mains-wired meters** |
| Credential erase | `wifiManager->resetSettings()`, `customwifi.cpp:774` | `esp_wifi_restore()` or `nvs.net80211` erase. Reachable from `buttonhandler.cpp:282` and `/api/v1/network/wifi/reset` |
| Saved SSID readback | `getWiFiSSID(true)`, `customwifi.cpp:550` | `esp_wifi_get_config(WIFI_IF_STA, ...)` |
| Connect retries, clean connect, duplicate-AP removal | `customwifi.cpp:179-184` | Explicit retry loop in the STA connect path |
| Disconnect diagnostics | `_lastDisconnect*` statics + `/diagnostic`, `customwifi.cpp:42-45,464-506` | **New REST endpoint.** These have no getter in `customwifi.h` and nothing else reads them. Revision 1 wrongly claimed the main UI already shows this |
| Log tail with no WiFi | `/diagnostic`, `customwifi.cpp:339-380` | Main UI `/log` needs auth + STA today. Reachable under D9's `UNPROVISIONED` bypass; verify |

Also: `customserver.h:6` includes `<WiFiManager.h>` directly. Removing the dependency breaks that file.

## Risks

| Risk | Severity | Status |
|---|---|---|
| A1: phone leaves the AP during the grace window once the OS sees no internet | **High** | **Unresolved.** Bench-3 decides. Fallback: surface the LAN address on submit, treat post-connect view as a bonus |
| APSTA heap regresses LWIP/AsyncTCP under load | **High** | Bench-2, in the unprovisioned-first-boot configuration. Threshold pre-committed below |
| S1 health-check reboot loop to NVS wipe | **High** | Understood, fix specified in D7. Must land first and independently |
| Bench-4 cross-netif destination bypasses the auth filter | Medium | Unresolved. Must be tested before ship |
| Hand-rolled connection manager loses WiFiManager's accumulated router/phone workarounds | Medium | Accepted with eyes open. D11 moves the known ones; the unknown tail is the residual |
| Scan disrupts AP clients | Low | Constrain `max_ms_per_chan` to ~120 ms. Full scan at Arduino defaults is ~4.3 s with the AP on-channel ~9% of the time |
| mDNS over AP | **Retired** | Verified working, `CONFIG_MDNS_PREDEF_NETIF_AP=1` |
| Early event registration | **Retired, inverted** | Boot registration is the safe option. See D10 |
| Rate limiter trips on captive probes | **Retired** | Fabricated. See D6 |

## Migration

1. Devices with valid credentials boot straight to `STA_CONNECTING` and never raise the AP. No user-visible change.
2. Credentials stay in `nvs.net80211`, written via `esp_wifi_set_config()` as today. No credential migration. Note `nvs.net80211` is excluded from backup/restore (`constants.h:57-61`), so restore cannot clobber them.
3. `wifi_ns` (static IP) **is** backed up and restored, and restore runs at `main.cpp:81-85` before `CustomWifi::begin()`. A backup from a different LAN pushes a foreign static IP and can drive the device into provisioning; D5's subnet comparison must tolerate a subnet belonging to another network.
4. The WiFiManager `/diagnostic` page disappears. Its data needs the new endpoint from D11 or it is lost. Release-note it.
5. User-facing strings to update: `configuration.html:315` ("no password"), `:320`, `:449`, `:476`, `:480` (192.168.4.1 becomes 172.31.42.1); `customserver.cpp:1690`, `:1737` (both promise a restart); `manual/02-setup.md:14-18`; `resources/swagger.yaml`.
6. Rollback is a firmware downgrade. No persisted state changes shape.

## Pre-committed thresholds

Written before measurement, per the review's objection that a gate without a threshold is not a gate.

- **Bench-2 pass:** in the unprovisioned-first-boot configuration with AP + DNS + all services started, `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` at steady state is **>= 40 KB**, and does not fall below **32 KB** during a full UI page load plus a Modbus poll. Below 32 KB, the change does not proceed in APSTA form. The gated metric is the internal-heap largest free block, not `ESP.getMaxAllocHeap()`: the regression this guards against was internal PBUF starvation, and `getMaxAllocHeap()` does not exclude PSRAM-satisfiable allocations. A same-binary STA-only baseline is taken first so a fail is attributable.
- **Bench-3 pass:** on both iOS and Android, the phone remains associated to the AP for **>= 60 s** after STA connects and DNS stops, without manual intervention. Fail means the grace window is dropped from the design and the LAN address is surfaced on submit instead.
- **Bench-1:** informational, selects the D2 implementation. No pass/fail.
- **Bench-4 pass:** a LAN host with a static route to the AP subnet via the STA IP receives **401**, not 200, on a provisioning route while in `UNPROVISIONED`. Fail blocks the auth design until the filter is hardened.

## Open questions

- Should Modbus TCP be filtered off the AP netif, or is exposure during a bounded AP window acceptable?
- Does the `UNPROVISIONED` bypass need to cover `/log` so the diagnostic log tail stays reachable without credentials (matching today's `/diagnostic` behaviour)?
- Which button duration raises the AP on demand? Only the `<2 s` slot is free (`buttonhandler.cpp:179-205`), practical window ~150 ms to 2 s after debounce, and `_updateVisualFeedback` currently shows white there.
- LED priority for AP states. `PRIO_MEDIUM` is stomped by the repeated `pulseBlue` on disconnect (`customwifi.cpp:660`); `PRIO_URGENT` collides with button feedback; `PRIO_CRITICAL` collides with safe-mode purple and `resetWifi` orange.
