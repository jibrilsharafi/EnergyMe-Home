## Why

WiFi provisioning today runs through `tzapu/WiFiManager`, which blocks the WiFi task for up to 5 minutes while its synchronous portal is open, forces a reboot on every credential save, and drops the user into a second, unrelated UI with its own hand-rolled diagnostic page. The user experience is two disjoint interfaces and a mandatory restart; the engineering cost is a duplicate synchronous `WebServer` stack, ~235 lines of string-built HTML, and an `#include`-ordering landmine between WiFiManager and ESPAsyncWebServer.

Everything needed to do this properly already exists in the pinned toolchain: the AsyncWebServer binds `0.0.0.0` and therefore already serves the SoftAP netif, `ON_AP_FILTER`/`ON_STA_FILTER` ship with ESPAsyncWebServer 3.10.0, and `DNSServer` in arduino-esp32 3.3.2 is AsyncUDP-backed with `processNextRequest()` reduced to a no-op stub. The remaining work is a provisioning state machine and an auth carve-out, not a new web stack.

## What Changes

- Replace the blocking `WiFiManager::autoConnect()` / `startConfigPortal()` flow with an on-demand SoftAP raised by the existing WiFi task, running concurrently with STA (`WIFI_AP_STA`).
- Serve WiFi provisioning from the existing `AsyncWebServer` on the AP netif. One server, one design system, one API surface. The captive portal lands the user on a WiFi setup page; the user is free to navigate to the rest of the UI from there.
- Add a provisioning state machine (`UNPROVISIONED` / `STA_CONNECTING` / `AP_ASSIST` / `STA_ONLY`) as pure logic in `source/lib/` with native Unity tests.
- Pin the SoftAP to the target network's channel before `WiFi.begin()` so AP clients survive STA association, then keep the AP alive for a 5-minute grace window after a successful connect so the user can read their new LAN address.
- Carve provisioning routes out of the server-global digest-auth middleware, gated on AP-origin plus unprovisioned state, so captive-portal webviews can reach them.
- Run the catch-all `DNSServer` only while the AP is up and STA is down; stop it on STA connect. The pinned `DNSServer` binds `INADDR_ANY:53` and answers with a fixed `_resolvedIP`, so leaving it running during APSTA would make the device a rogue open resolver on the customer's LAN.
- **BREAKING**: remove the `tzapu/WiFiManager@2.0.17` dependency. Devices no longer expose the WiFiManager portal, its `/diagnostic` page, or its OTA menu. Diagnostic information moves into the main UI.
- **BREAKING**: changing WiFi SSID/password no longer reboots the device. Changing the static IP configuration still does, deliberately (reconfiguring a live netif races lwIP).

## Capabilities

### New Capabilities
- `wifi-provisioning`: how the device acquires, validates, and persists WiFi credentials; when the SoftAP is raised and torn down; the captive-portal contract; the provisioning REST surface and its authentication rules.

### Modified Capabilities
- None. There is no existing `openspec/specs/` entry covering WiFi connectivity or the web server; this change introduces the first one.

## Impact

**Code**
- `source/src/customwifi.cpp` / `include/customwifi.h`: remove `_setupWiFiManager`, `_setupDiagnosticEndpoint`, `_appendToPageBuffer` (~`customwifi.cpp:165-400`); add SoftAP lifecycle, async scan cache, DNS lifecycle, and the state-machine adapter. Static-IP machinery (`_applyNetworkConfiguration`, `_serviceStaticIpHealth`, boot-fail backstop) must remain behaviourally unchanged.
- `source/src/customserver.cpp`: insert a provisioning auth-bypass middleware ahead of `digestAuth` (`customserver.cpp:231`); add scan/status/connect endpoints under `/api/v1/network/wifi/`; add captive-probe handling via `onNotFound` filtered by `ON_AP_FILTER`. Review `rateLimit` (`customserver.cpp:240`) against burst captive-detection probes.
- `source/src/main.cpp`: replace the blocking `while (!CustomWifi::isFullyConnected())` at `main.cpp:151` with a wait on "STA connected OR AP raised"; drop the WiFiManager `#include` ordering comment at `main.cpp:18`.
- `source/lib/wifi_provisioning/` + `source/test/test_wifi_provisioning/`: new pure state machine and its Unity tests.
- `source/html/`, `source/js/`: WiFi setup page and client-side connect/poll flow.

**Dependencies**
- Removed: `tzapu/WiFiManager@2.0.17` and the synchronous `WebServer` it pulls in.
- Added: none. `DNSServer` and the WiFi AP API are part of the pinned arduino-esp32 core.

**Memory**
- The SoftAP `esp_netif` and its DHCP server object **already exist in STA-only mode** (`WiFiGeneric.cpp:290-292` calls `esp_netif_create_default_wifi_ap()` unconditionally), so the incremental cost of enabling the AP is smaller than it first appears: the lwIP `netif_add`, WiFi-driver AP state, the DHCPS pcb and up to 8 lease nodes, and second-interface buffer pressure. Unbudgeted addition: `DNSServer` would be the firmware's first AsyncUDP user, creating a global `async_udp` task with a 4096 B internal stack and a 32-entry queue.
- Given the documented regression where a ~50 KB internal-heap loss silently broke LWIP/AsyncTCP under load, this must still be measured before the change is considered viable. The metric is the largest contiguous **internal** block, `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`, not `getFreeHeap()` and not `getMaxAllocHeap()`; the historical failure was internal PBUF allocation. Pre-committed thresholds are in `design.md`, and the measurement must use the unprovisioned-first-boot configuration, where MQTT-with-TLS, InfluxDB, Modbus and NTP all start with no route.

**Security**
- The auth carve-out applies only while `UNPROVISIONED` and only on the SoftAP netif; an in-service device that lost its network keeps full digest auth. OTA requires authentication in every state, which is stronger than the current WiFiManager portal, whose auth is hard-disabled (`WiFiManager.cpp:1312-1313`) and which serves an unauthenticated firmware-upload page.
- The AP password falls back to `DEVICE_ID` on community devices, which is the visible SSID suffix; factory devices carry a real PSK on a case sticker. The bounded AP lifetime, not the PSK, is what limits exposure.

**Related**
- Supersedes and widens GitHub issue #109 ("Replace blocking WiFiManager portal with async WiFi provisioning"), which only asked for a non-blocking portal.
