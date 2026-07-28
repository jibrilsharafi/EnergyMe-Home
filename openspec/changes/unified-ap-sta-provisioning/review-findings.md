# Review findings

Four independent critical reviews of `proposal.md` / `design.md`. All complete.

**Status: `design.md` decisions D2, D3, D6, D7 are refuted. Migration step 4 and one Goals bullet are factually false. Do not implement from the current design.**

---

## Showstoppers

### S1. AP-only provisioning triggers reboot loop, then firmware rollback and NVS factory wipe

Found independently by two reviewers. Verified against constants.

```
_performHealthCheck gates on CustomWifi::isFullyConnected()   customserver.cpp:474
  isFullyConnected() is STA-only                              customwifi.cpp:137-148
  -> false for the entire AP-only provisioning window
5 failures x HEALTH_CHECK_INTERVAL_MS 30s -> setRestartSystem  customserver.h:51,53
  reboot at ~150 s uptime
  150 s > QUICK_RESTART_THRESHOLD 60 s   -> safe mode never arms      crashmonitor.h:34
  150 s < COUNTERS_RESET_TIMEOUT 180 s   -> _consecutiveResetCount never resets  crashmonitor.h:31
  -> counter climbs every boot                                crashmonitor.cpp:175
  -> at MAX_RESET_COUNT (10 prod / 30 dev) -> Update.rollBack() + user-NVS wipe  crashmonitor.cpp:315-343
```

Approximately 25 minutes in provisioning mode destroys user configuration on a production build.

Currently masked only because `main.cpp:151` blocks in `setup()` until STA is up, so `CustomServer::begin()` (`main.cpp:176`), which starts the health-check task (`customserver.cpp:151`), is never reached with STA down. **D7 removes that mask.** The design's Impact section does not mention `_performHealthCheck`.

Fix options: introduce `CustomWifi::isNetworkServiceable()` (STA connected OR AP raised and serving) for the health check to call, or defer `_startHealthCheckTask()` to the STA-connected transition. The self-probe itself is fine either way: it targets `127.0.0.1` and `/api/v1/health` already carries `.skipServerMiddlewares()` (`customserver.cpp:866`).

### S2. D3's auth-bypass mechanism does not work

`AsyncMiddlewareChain::_runChain` (`Middleware.cpp:56-71`) builds one `next` closure over an iterator; each `next()` takes `*it`, increments, and runs it. **There is no skip-to-finalizer primitive.** A middleware placed ahead of `digestAuth` that calls `next()` runs `digestAuth` next. The pseudocode in `design.md` D3 is wrong.

The working mechanism is already in this codebase. `AsyncWebServerRequest::_runMiddlewareChain` (`WebRequest.cpp:877-891`) honours `mustSkipServerMiddlewares()`, and `/api/v1/health` uses `.skipServerMiddlewares()` at `customserver.cpp:866` (the only use in the project).

Correct pattern, open handler registered first, authenticated twin second:

```cpp
server.on(path, HTTP_GET, cb).setFilter(isProvisioningOrigin).skipServerMiddlewares();
server.on(path, HTTP_GET, cb);   // authenticated fallback
```

Sound because `_attachHandler` (`WebServer.cpp:145-154`) walks `_handlers` in insertion order taking the first that passes `filter() && canHandle()`, and `_handlers` is a `std::list` (`ESPAsyncWebServer.h:1479`) with stable order.

Absorbed costs: `skipServerMiddlewares()` also drops statistics and rate limiting, so re-add per handler via `handler.addMiddleware(&rateLimit)`. Also, the actual current order is `customMiddleware -> digestAuth -> rateLimit` (`customserver.cpp:202,231,240`), not what D6 assumed.

### S3. The WiFi task cannot host the D8 state machine in its current shape

- `WIFI_EVENT_DISCONNECTED` does `delay(WIFI_DISCONNECT_DELAY)` = **15 s blocking** (`customwifi.cpp:665`, `customwifi.h:36`). Every failed STA attempt from the setup page stalls the task 15 s: no AP-client events, no DNS lifecycle, no grace-window arithmetic.
- The periodic-check `else` branch (`customwifi.cpp:742-747`) sees `isFullyConnected()` false every 30 s with no STA and calls `_forceReconnectInternal()`, which **increments `_reconnectAttempts`** (`customwifi.cpp:969`). That is the exact counter D1 uses as its AP-raise predicate. It only resets after 5 minutes of `isFullyConnected()` true (`customwifi.cpp:736`), so while the AP is up it grows monotonically from two independent sources and the predicate can never settle.

### S4. D2's channel-pinning mechanism does not exist

Verified against arduino-esp32 3.3.2 / ESP-IDF v5.5.1 binaries.

**`WiFi.softAP(ssid, pw, N)` on an already-running AP does not move it to channel N.**

- `APClass::create` (`AP.cpp:208-258`) calls `begin()` (a no-op when the AP is already enabled, `WiFiGeneric.cpp:647-659`) then `esp_wifi_set_config(WIFI_IF_AP, &conf)`.
- Objdump of `libnet80211.a` shows `wifi_softap_set_config`'s complete callee set contains no channel-setting function: no `chm_set_home_channel`, no `chm_set_current_channel`, no `ieee80211_update_channel`.
- Callers of `ieee80211_update_channel` are exactly `wifi_softap_start`, `cnx_connect_to_bss`, and the CSA/HT-coex paths. The AP's operating channel is applied at **AP start** and thereafter follows the STA at association.
- The door is closed from the other side too: `esp_wifi.h:773` states `esp_wifi_set_channel()` "should not be called when softAP has connected to external STAs". **There is no supported way to move a live SoftAP that has clients attached.**

The underlying claim that the AP follows the STA channel is VERIFIED (`esp_wifi.h:987-988`) and cannot be avoided: one radio, one home channel.

**Consequence: "phone stays associated" is false and must be removed from the design.** The salvageable form of D2 is stop-AP -> `softAPConfig` -> `softAP(ssid, pw, N)` (a fresh `wifi_softap_start` applies N) -> `WiFi.begin()`. The client still drops, but re-associates to the same SSID/BSSID on N within a few seconds, and crucially the AP does **not** move again when STA associates. That converts an unpredictable drop at the moment of success into a predictable early drop followed by stability. Smaller benefit than claimed, still a real one.

Note `esp_wifi_set_config` persists to NVS on every call (`esp_wifi.h:989`, `_persistent = true` at `WiFiGeneric.cpp:369`), so repeated channel re-pinning means repeated flash writes.

**Discriminating bench test, ~5 minutes:** raise AP, call `WiFi.softAP(ssid, pw, N)` with N != current, read `esp_wifi_get_channel()`. Unchanged primary confirms the refutation. (Evidence above is static call-graph, not runtime.)

---

## Radio findings that IMPROVE the picture

### mDNS over SoftAP works. Retire that risk row.

`CONFIG_MDNS_PREDEF_NETIF_AP=1` (`sdkconfig.h:1805`). Disassembly of `mdns_preset_if_handle_system_event` shows the `WIFI_EVENT` branch dispatching `WIFI_EVENT_AP_START` (12) to `post_mdns_enable_pcb(MDNS_IF_AP, ip4)` and `AP_STOP` (13) to disable. `mdns_init` also enables pcbs for netifs already holding an IP, so `MDNS.begin()` ordering relative to AP raise does not matter. **`energyme.local` will resolve over the AP.**

### The event-registration risk row is backwards

`NETWORK_EVENTS_MUTEX` is never `#define`d in the framework and is not set in `platformio.ini`. So `NetworkEvents::_cbEventList` (a `std::vector`) is mutated by `onEvent()`/`removeEvent()` from arbitrary tasks with **no lock**, while the `arduino_events` task iterates it (`NetworkEvents.cpp:122-147,170,183`), on a dual-core SoC. A `push_back` reallocation mid-iteration is a use-after-free.

That is the most plausible explanation for the historical crash noted at `customwifi.cpp:588-590`, and it means **registering all handlers at boot, before any event can be posted, removes the race rather than creating one.** The genuinely dangerous call is `WiFi.removeEvent(_onWiFiEvent)` at `customwifi.cpp:893` during shutdown, while events still flow. This is a latent defect in current code.

Supporting facts: callbacks run on the dedicated `arduino_events` task (4096 B internal stack), not the WiFi/lwIP task (`NetworkEvents.cpp:15,66-77`). `postEvent` does `new arduino_event_t` per event and `xQueueSend(..., portMAX_DELAY)` into a 32-deep queue (`NetworkEvents.cpp:84-101`), so a blocking callback stalls the esp_event loop task. Notify-only is correct and load-bearing. `ARDUINO_EVENT_WIFI_AP_STACONNECTED`/`_STADISCONNECTED` are cheap. Do not unmask `AP_PROBEREQRECVED` (masked by default, `esp_wifi.h:1154`).

### APSTA heap cost is smaller than the proposal implies

`proposal.md`'s "SoftAP netif plus its DHCP server consume internal RAM" is partly wrong: **both already exist in STA-only mode.** `wifiLowLevelInit` calls `esp_netif_create_default_wifi_ap()` unconditionally (`WiFiGeneric.cpp:290-292`), and `esp_netif_new_api` calls `dhcps_new()`. Incremental cost of enabling the AP is the lwIP `netif_add`, WiFi-driver AP state (beacon buffer <= 752 B), the DHCPS udp_pcb plus up to 8 lease nodes (`CONFIG_LWIP_DHCPS_MAX_STATION_NUM=8`), and second-interface buffer pressure.

**Unbudgeted cost to add to Spike 1:** the firmware currently uses `WiFiUDP` (`customlog.cpp:15`), so `DNSServer` would be the **first AsyncUDP user**, creating the global `async_udp` task: 4096 B internal stack + 32-entry queue + per-packet heap (`AsyncUDP.cpp:182-196`).

**Measure `getMaxAllocHeap()`, not `getFreeHeap()`.** The historical failure was PBUF allocation, i.e. largest contiguous free block, which fragments independently of total free.

---

## Refuted design decisions

### D6 is dead twice over

`WEBSERVER_MAX_REQUESTS 6000` / `WEBSERVER_WINDOW_SIZE_SECONDS 600` (`customserver.h:35-36`) = 10 req/s sustained. Captive-detection bursts are 4-6 requests.

Additionally `AsyncRateLimitMiddleware` is **global, not per-IP** (one `std::list<uint32_t>` per instance, no client key, `Middleware.cpp:272-300`), and `Middleware.cpp:275` computes `now - _windowSizeMillis` on `uint32` `millis()`. Below 600 s uptime this underflows to ~4.29e9 and the prune loop empties the list on every call, so **the limiter is inert during exactly the window provisioning occupies**.

Delete D6 entirely.

Unrelated pre-existing note: above 600 s uptime under sustained load the list can hold 6000 nodes, >100 KB of heap. Worth knowing given the heap constraint.

### `ON_AP_FILTER` is not an origin check

`WebServer.cpp:28-34` is `WiFi.localIP() != request->client()->localIP()`. `AsyncClient::localIP()` (`AsyncTCP.cpp:1341-1352`) returns `_pcb->local_ip`, the **destination address of the incoming SYN**, not the arrival netif. The real predicate is "destination IP is not the STA IP".

- **Loopback trips it today.** `_performHealthCheck` connects to `127.0.0.1` (`customserver.cpp:484`), so `ON_AP_FILTER` returns true for the device's own probe.
- **STA without an IP trips it.** `WiFi.localIP() == 0.0.0.0` makes it true for everything, including STA-arriving requests. This is not incidental: `STA_CONNECTING` and the DHCP-pending window inside `AP_ASSIST` are both states the design spends real time in.
- **IPv6 latent.** `CONFIG_LWIP_IPV6=y` and the listener is dual-stack (`AsyncTCP.cpp:1472-1506`); an IPv6 `localIP()` vs IPv4 `WiFi.localIP()` is always `!=`. Disarmed only because `enableIPv6()` is never called in `source/`.
- **Cross-netif destination: UNRESOLVED.** If lwIP's weak host model accepts packets destined to any local address regardless of arrival netif (`CONFIG_LWIP_IP_FORWARD=y`), a LAN host routing to the AP IP via the STA address gets the filter to return true, bypassing auth from the LAN. Needs a bench test: APSTA up, static route to the AP subnet via the STA IP, `curl http://<AP_IP>/`, log `client()->localIP()` inside the filter.

Consequence: compare against `WiFi.softAPIP()` rather than using the library helper, and treat the **state gate as carrying 100% of the safety**. The design's "triple gate" is one gate.

### `onNotFound` cannot serve captive probes

`_catchAllHandler` is assigned directly by `_attachHandler` (`WebServer.cpp:153`) **without consulting `filter()` or `canHandle()`**, so filters never apply to it, and `_skipServerMiddlewares == false` means **it runs `digestAuth`**. A captive probe to `/hotspot-detect.html` returns 401 and detection fails.

Correct approach: register each probe as an explicit route with `.setFilter(apOrigin).skipServerMiddlewares()` plus an authenticated twin. Paths: `/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/library/test/success.html`, `/ncsi.txt`, `/connecttest.txt`, `/redirect`.

---

## Factual errors in the current documents

| Claim | Reality |
|---|---|
| `design.md` Goals: "Provisioning never blocks the meter, logging, or any other task" | The meter is **not** blocked today. `Ade7953::begin()` (`main.cpp:131`) starts five tasks before the WiFi wait, and `main.cpp:148-150` says so explicitly. What blocks is `setup()`, whose task is deleted at `main.cpp:214` |
| Migration step 4: diagnostic data "is already in the main UI's info page" | False. `_lastDisconnectReason` / `_lastDisconnectSSID` / `_lastDisconnectBSSID` / `_lastDisconnectRSSI` / `_getDisconnectReasonString` exist only as file-statics (`customwifi.cpp:42-45,464-506`), consumed only by `/diagnostic`. No getter in `customwifi.h`; nothing in `src/`, `include/`, `html/`, `js/` references them. Removing WiFiManager destroys this data unless the change adds an API for it. Same for the log-tail download (`customwifi.cpp:339-380`), reachable today with no WiFi, whereas the main UI `/log` needs auth + STA |
| Impact list omits | `customserver.h:6` includes `<WiFiManager.h>` directly. Build break in a file the plan never mentions |
| Impact list omits | `resetWifi()` calls `wifiManager->resetSettings()` (`customwifi.cpp:772-776`), reachable from the button (`buttonhandler.cpp:282`) and `/api/v1/network/wifi/reset`. Needs an `esp_wifi_restore()` / `nvs.net80211` erase replacement |
| Impact list omits | `_lastAttemptedSSID` comes from `wifiManager->getWiFiSSID(true)` (`customwifi.cpp:550`); needs `esp_wifi_get_config()` |
| Impact list omits | `_isPowerReset()` + `WIFI_CONNECT_TIMEOUT_POWER_RESET_SECONDS` (5 min, `customwifi.h:32`) live **inside** `_setupWiFiManager` (`customwifi.cpp:170-179`), which the plan deletes wholesale. Today a power cut gives the router 5 minutes to boot. Under D1, N failures raise the AP, so **every household power blip would leave mains-powered meters broadcasting a SoftAP** |
| Risk table lists "WPA2 on the AP" as a mitigation | Contradicted by the design's own open question. Now answered: `manual/02-setup.md:14` states DEVICE_ID is printed on the device label, and `_resolveApPassword` falls back to DEVICE_ID (`customwifi.cpp:989-1003`), which is also the SSID suffix. The WPA2 story is as weak as suspected |
| Spike 1 scope | Measures "STA vs APSTA under navigation + Modbus load". The dangerous configuration is the one D7 creates: unprovisioned first boot where `CustomTime`, `IssueRegistry`, `CustomServer`, `ModbusTcp`, `Mqtt`, `CustomMqtt`, `InfluxDbClient` all start for the first time with no route, concurrent with SoftAP + DHCP + DNS. **The blocking gate as specified cannot fail in the scenario most likely to break** |

---

### D5's routing rationale is worded wrong, and the real behaviour is worse

lwIP does **not** do longest-prefix match. `ip4_route` (disassembled from `liblwip.a`) walks `netif_list` and returns the **first** netif that is UP + LINK_UP + has a non-zero address + satisfies `(dest ^ netif->ip_addr) & netif->netmask == 0`. And `netif_add` **prepends**. So an AP raised *after* STA is up sits at the head of the list and **wins every ambiguous match**: LAN-destined traffic exits the AP. The conclusion holds and is sharper than stated.

Hard constraints on the AP subnet from `NetworkInterface::config`:
- **CIDR restricted to /24 through /28** (`NetworkInterface.cpp:440-443`); hard fail outside that range.
- DHCP lease pool is **10 addresses** starting at `ap_ip + 1` (`NetworkInterface.cpp:448-453`).

`softAPConfig()` before `softAP()` is required, but it calls `AP.begin()` -> `enableAP(true)` (`WiFiAP.cpp:71-73`), so it **raises the AP interface before SSID and channel are set**, briefly beaconing the previous or NVS-stored config. Correct sequence:

```
softAPConfig(ip, gw, mask, leaseStart, dns)   // raises AP netif, sets ip_info + dhcps
softAP(ssid, pw, channel)                     // wifi_softap_start applies the channel
wait ARDUINO_EVENT_WIFI_AP_START
enableDhcpCaptivePortal()                     // requires started()
```

### D4 x D5 interaction that neither document caught

`DNSServer::start()` (no-arg) sets `_resolvedIP` **only if it is currently 0** (`DNSServer.cpp:21-32`) and never refreshes it. If D5's runtime backstop changes the AP IP on subnet collision, a subsequent `start()` answers with the stale address.

**Fix: always call `start(53, "*", WiFi.softAPIP())`** (`DNSServer.cpp:41-56`), which unconditionally assigns `_resolvedIP`; `"*"` clears `_domainName` for catch-all mode.

Also: the deferred netif-aware responder is far cheaper than the "~60 lines" estimated. `AsyncUDPPacket` already carries the arrival interface via `localIP()` and `interface()` (`AsyncUDP.h:79-81`), so a custom handler needs one `if (pkt.localIP() != apIP) return;`. Stock `DNSServer` exposes no hook, so lifecycle-gating remains the v1 answer.

### Scan is workable but must be constrained

- `scanNetworks` calls `WiFi.enableSTA(true)` (`WiFiScan.cpp:75`). AP -> APSTA does **not** stop the AP (`wifi_stop_old_mode(old=2, new=3)` skips both stop branches).
- `esp_wifi_scan_start(&config, false)` is always non-blocking (`WiFiScan.cpp:93`); `async=false` merely blocks the caller for up to the 60 s `_scanTimeout`. Use async.
- Arduino memsets the scan config, so `home_chan_dwell_time` takes the driver default of **30 ms** (`esp_wifi.h:511`). With Arduino defaults (`max_ms_per_chan=300`, all channels) a full scan runs ~4.3 s with the AP on its home channel roughly **9% of the time**, against a 100 ms beacon interval (`AP.cpp:230`). Clients usually survive but interactive traffic stalls hard and the iOS captive assistant may bail. **Constrain `max_ms_per_chan` to ~120 ms or scan a channel subset.**
- `esp_wifi_scan_start` returns `ESP_ERR_WIFI_STATE` while STA is connecting (`esp_wifi.h:520`). Do not scan during `STA_CONNECTING`.

### `enableDhcpCaptivePortal` verified

`APClass::enableDhcpCaptivePortal` (`AP.cpp:309-346`) sets `ESP_NETIF_CAPTIVEPORTAL_URI` = **DHCP option 114** (RFC 8910), value `"http://<softAPIP>"`. It **requires `started()`** (`AP.cpp:315-318`) and stop/starts DHCPS around setting the option. Compiled in only for ESP-IDF >= 5.4.2; satisfied at 5.5.1.

---

## Additional traps for implementation

1. **`_validateRequest` + `_sendJsonResponse` = permanent `_apiMutex` deadlock.** `_validateRequest` (`customserver.cpp:316-335`) ends by acquiring the mutex; it is released only in `_sendSuccessResponse` (`:264`) and `_sendErrorResponse` (`:275`), **not** `_sendJsonResponse` (`:248-254`). A `GET /wifi/scan` following that pattern wedges the whole write API. Existing GETs (e.g. `:1743`) correctly skip both.
2. **Never block in a request handler.** AsyncTCP is single-threaded; `API_MUTEX_TIMEOUT_MS` is 2000 with a comment warning of WDT crashes (`customserver.h:64`). `POST /connect` must only `xTaskNotify` the WiFi task and return. Scan must be async with cached results.
3. **`AsyncCallbackJsonWebHandler` requires `Content-Type: application/json`** (`AsyncJson.cpp:125-140`). A form-encoded POST from a captive webview misses the handler and falls to the catch-all.
4. **Filters run on the AsyncTCP task for every request to every path** (`WebServer.cpp:145-154`, before `_runMiddlewareChain` at `WebRequest.cpp:868`). The provisioning filter must be an atomic state read plus an IP compare. No mutex, no NVS, no logging. This constrains how provisioning state is exposed.
5. **Register all routes at `begin()`**, gate at request time. Adding handlers after `server.begin()` works but is unsynchronized against the AsyncTCP task.
6. **Static-asset carve-out is ~14 routes** (`customserver.cpp:762-807`). Either AP-side duplicates, a `/css/*` prefix handler registered before the exact matches, or inline the setup page's CSS.
7. **LED priority collision.** `_ledTask` accepts when `priority >= currentPriority` (`led.cpp:131`), last-write-wins on ties. In AP mode `WIFI_EVENT_DISCONNECTED` fires `pulseBlue(PRIO_MEDIUM)` repeatedly (`customwifi.cpp:660`), stomping any AP pattern at MEDIUM. URGENT collides with button feedback, CRITICAL with safe-mode purple (`main.cpp:203`) and `resetWifi` orange (`customwifi.cpp:763`). Also a lower-priority command is re-queued forever (`led.cpp:145`) against `LED_QUEUE_SIZE 10`.
8. **Button: only the `<2 s` slot is free** (`buttonhandler.cpp:179-205`, `buttonhandler.h:20-24`; 2-5 s restart, 5-10 s password reset, 10-15 s WiFi reset, 15-20 s factory reset). Practical window ~150 ms-2 s after debounce. An accidental tap would broadcast an AP whose PSK is the label value.
9. **Backup/restore can drive the device into provisioning.** `wifi_ns` (static IP, gateway, subnet, `staticFails`) is backed up and restored (`utils.cpp:1934-1945`, `:2146`), and restore runs at `main.cpp:81-85` before `CustomWifi::begin()`. A backup from a different LAN pushes a foreign static IP; D5's subnet comparison then reasons about someone else's network. STA credentials are safe (`nvs.net80211` excluded, `constants.h:57-61`).
10. **Boot-order consequences with no network** (all verified): `CustomLog` safe but UDP logs are dropped from the queue front the whole time, so UDP debugging will not work during provisioning. `CustomTime::begin()` passes `WiFi.gatewayIP()` = `0.0.0.0` as the primary NTP server (`customtime.cpp:242-244`) and that stale value persists until the next sync interval after STA comes up. `IssueRegistry` raises `ntp_not_synced`, `cloud_mqtt_disconnected`, `custom_mqtt_connect_failed` for the whole window plus undeliverable raise-edge publishes. `ModbusTcp` binds all interfaces (`modbustcp.cpp:22`), unauthenticated on the AP. `Mqtt` / `CustomMqtt` / `InfluxDbClient` are **safe**, all gate on `isFullyConnected()` before connecting. `startMaintenanceTask` heap threshold is 1 KB (`utils.h:59`), so it provides **no guard** against the APSTA heap risk; that failure mode is silent PBUF starvation, not a restart.
11. **Stale user-facing strings**: `configuration.html:315` ("with no password"), `:320`, `:449`, `:476`, `:480` (192.168.4.1); `customserver.cpp:1690`, `:1737` (both promise a restart); `manual/02-setup.md:14-18`; `resources/swagger.yaml`.
12. **`_serveNetworkEndpoints` already owns `/api/v1/network/wifi/`** (`customserver.cpp:1683-1785`), and its `credentials` handler is `new`'d without `static` (`:1695`), unlike the pattern elsewhere. Do not copy that. Decide whether `credentials` keeps save-and-reboot semantics for API compatibility.
13. **New-file plumbing for a setup page** needs three coordinated edits: `platformio.ini` `board_build.embed_txtfiles`, an `extern ... asm("_binary_html_setup_html_start")` pair in `include/binaries.h`, and a `server.on()` in `_serveStaticContent`. Flash is a non-issue (`app0` = 4.5 MB vs ~2 MB image).

---

## Confirmed as specified

- **`lib/` + `test/` shape.** `source/lib/wifi_provisioning/wifi_provisioning.h` (+ optional `.cpp`); both header-only (`lib/phase_utils`) and split (`lib/duration_format`) are precedent. SPDX header, `#pragma once`, `<cstdint>`/`<cstddef>` only, no Arduino/FreeRTOS/ESP headers. Test at `source/test/test_wifi_provisioning/test_wifi_provisioning.cpp` with explicit `main()` + `UNITY_BEGIN/RUN_TEST/UNITY_END`. **No `platformio.ini` change needed**; `[env:native]` has no `test_filter` and LDF resolves from the include. Run `pio test -e native` from WSL.
- **Flash / partitions**: non-issue.

---

## AP-side OTA exposure: reviewed and ACCEPTED

The reviews flagged this as a security issue. **The team assessed it and accepted the risk.** Recorded here so it is not re-raised.

Mechanism, for the record: `customwifi.cpp:397` puts `"update"` in the WiFiManager menu, which registers the OTA upload page (`WiFiManager.cpp:658-659`). WiFiManager's auth is hard-disabled (`WiFiManager.cpp:1312-1313`, `bool testauth = false; if(!testauth) return;`), so while the portal is up the upload page is reachable by anyone who can join the AP.

**Rationale for acceptance:**
- OTA over the AP is a deliberate **recovery path**. It is how a device with bad firmware or an unreachable network is rescued in the field without physical disassembly. The reviews scored it purely as attack surface and never weighed that.
- Exposure requires RF proximity plus, in most cases, forcing the portal up via deauth.
- Factory devices carry a real AP password on a case sticker (`manual/02-setup.md:11-13`), so the WPA2 layer is meaningful for the shipped fleet. Only community/self-built devices fall back to `DEVICE_ID` (`customwifi.cpp:1000`), which is the SSID suffix.
- The WiFiManager portal is slated for replacement, and the successor UI puts OTA behind digest auth, giving WPA2 + login rather than WPA2 alone. That is a net improvement over today.

**Design requirement this creates for the replacement:** OTA must remain reachable over the AP. It should sit behind the web password (not in any auth-bypass allowlist), which means the AP-side OTA rescue no longer covers the "user forgot the web password" case that today's unauthenticated portal happens to handle. Decide explicitly whether that case needs a separate escape hatch (the existing 5-10 s button press already resets the web password, `buttonhandler.cpp:179-205`), rather than losing it by accident.

## Remaining items, unrelated to the above

1. **`configuration.html:315` tells users the AP has "no password"**, contradicting `_resolveApPassword`. `manual/02-setup.md:11-13` documents it correctly. Straight documentation defect; fix opportunistically.
2. **Default web credentials** `admin` / `energyme` (`customserver.h:57-58`). Pre-existing, orthogonal to this change.
3. **`AP_ASSIST` has no timeout in the current design.** Today `startConfigPortal()` times out after `WIFI_PORTAL_TIMEOUT_SECONDS` and `customwifi.cpp:694` restarts, bounding the AP window to 5 minutes per boot. The design must not silently drop that bound; it is also what stops a device stuck in `AP_ASSIST` from broadcasting indefinitely after, say, the owner moves house.
