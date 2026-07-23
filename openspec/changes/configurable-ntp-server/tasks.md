## 1. Server list construction

- [x] 1.1 Remove the old `NTP_SERVER_2` macro (`time.google.com`) from `source/include/customtime.h` (redundant with `NTP_SERVER_1`, both DNS-dependent); renumber the Cloudflare IP macro from `NTP_SERVER_3` to `NTP_SERVER_2` so the remaining two stay contiguous
- [x] 1.2 Add a small helper (`_configureNtpServers()`) in `source/src/customtime.cpp` that formats `WiFi.gatewayIP()` into a `static char[IP_ADDRESS_BUFFER_SIZE]` buffer (static, not stack-local - the SNTP client only stores the pointer it's given, not a copy, so the buffer must outlive the `configTime()` call) and calls `configTime()` with `[gateway, NTP_SERVER_1, NTP_SERVER_2]`

## 2. Apply gateway-first list

- [x] 2.1 ~~Call `sntp_servermode_dhcp(1)` in `CustomTime::begin()`~~ - **dropped**. Tried this (both call orderings), and it asserted on hardware both times: `LWIP_ASSERT_CORE_LOCKED()` at lwIP's `sntp.c:788`, since `sntp_servermode_dhcp()` requires the lwIP core lock and `CustomTime::begin()` runs on Arduino's `loopTask`, not the lwIP/tcpip thread. Confirmed via decoded on-device backtrace against upstream lwIP source (2026-07-23). Removed the call and the now-unused `#include "lwip/apps/sntp.h"` entirely rather than fixing with `LOCK_TCPIP_CORE()`/`UNLOCK_TCPIP_CORE()` - gateway-first alone covers issue #205, DHCP option 42 was always marginal.
- [x] 2.2 Both `configTime()` call sites (`begin()` and `_checkAndSyncTime()`) now call `_configureNtpServers()` instead of passing the macros directly

## 3. Tests

- Native unit test skipped: `_configureNtpServers()` has no branching or edge-case logic to test in isolation (a direct `WiFi.gatewayIP()` read + `snprintf` + `configTime()` call), and its dependencies (`WiFi`, `configTime`) aren't available in the host/native build. The behavior worth verifying (gateway tried first, fallback on failure) only exists at the SNTP-client level and is covered by hardware verification below.

## 4. Hardware verification (bench device, before commit per CLAUDE.md testing rule)

**Done for 4.2, on 192.168.2.174 (2026-07-23) after removing the DHCP call.** OTA-flashed via local REST; device rebooted clean (`lastResetWasCrash: false`, no crash-analysis dump on boot), and logged `Initial time sync successful` ~6.1s after boot with no assert. Confirmed via `w32tm /stripchart /computer:192.168.2.1` that this bench network's gateway (192.168.2.1) does answer NTP, so this sync exercised the gateway-first path specifically (not just fallback). The 180s crash-monitor stability reset (`_crashResetTask`) also fired cleanly on this boot with no errors.

- [ ] 4.1 Verify a device on a normal internet-connected network still syncs (gateway likely doesn't answer NTP, falls through to `pool.ntp.org`/Cloudflare IP) - no regression. Not directly exercised - the bench network's gateway does answer NTP (see 4.2), so this fallback path hasn't been forced. Low risk: unchanged from prior behavior once the gateway slot times out, per SNTP's existing failover.
- [x] 4.2 Verify a device on a network whose router does answer NTP requests on its gateway IP syncs against the gateway (confirmed reachable on the bench network via `w32tm /stripchart /computer:192.168.2.1`)
- [ ] 4.3 Verify a device not yet WiFi-connected when `begin()` first runs (gateway `0.0.0.0`) degrades gracefully to the fallback servers instead of misbehaving. Not directly exercised - would need a boot sequence where `begin()` runs before WiFi association completes.
- [ ] 4.4 Verify gateway-IP change (e.g. reconnect to a different bench network) is picked up on the next sync attempt with no stale state. Not directly exercised - would need a mid-session network switch on the bench device.
