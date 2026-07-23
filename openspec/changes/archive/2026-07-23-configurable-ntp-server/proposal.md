## Why

The three NTP servers (`customtime.h`: `pool.ntp.org`, `time.google.com`, a Cloudflare IP) are compile-time constants. On a local-only network with no path to the public internet, none of them are reachable, so a device that loses its clock (e.g. after a power outage) can never re-sync and stays wrong until someone manually POSTs a Unix timestamp via `/api/v1/system/time`. [Issue #205](https://github.com/jibrilsharafi/EnergyMe-Home/issues/205) asks for a way to point the device at a reachable NTP server. Most routers (OpenWrt, pfSense, and many consumer routers) already answer NTP requests on their own LAN IP - so the device can just try its own gateway first, with zero configuration, instead of exposing a new settable server list that almost nobody will ever need to touch.

## What Changes

- Both `configTime()` call sites (`CustomTime::begin()` and the periodic `_checkAndSyncTime()`) build their 3-server list dynamically each time: the current DHCP/static gateway IP first, then two of today's compiled-in defaults as fallback (`pool.ntp.org` and the Cloudflare IP; `time.google.com` is dropped to stay within lwIP's fixed 3-server slot limit - see Impact).
- No new configuration surface: no NVS storage, no REST endpoint, no web UI, no shadow field. Gateway IP is read fresh from `WiFi.gatewayIP()` on every sync attempt, so it stays correct across DHCP renewal or a network change with nothing to keep in sync.

**Dropped from an earlier draft**: DHCP-provided NTP acquisition (`sntp_servermode_dhcp(1)`). On hardware this asserted immediately (`LWIP_ASSERT_CORE_LOCKED()` at lwIP's `sntp.c:788` - the call requires the lwIP core lock, which `CustomTime::begin()` doesn't hold when calling it from Arduino's `loopTask`). Gateway-first already covers issue #205's scenario, so this is dropped entirely rather than fixed with a lock wrap - see `design.md` for the full incident writeup.

## Capabilities

### New Capabilities
- `ntp-time-sync`: device-side NTP server selection (gateway-first with default fallback), applied at the two `configTime()` call sites. No configuration/persistence/REST/shadow surface.

### Modified Capabilities
(none)

## Impact

- `source/src/customtime.cpp`: both `configTime()` call sites build the server list from `WiFi.gatewayIP()` + the two remaining default macros (`NTP_SERVER_1`, `NTP_SERVER_2`), instead of passing the macros directly.
- `source/include/customtime.h`: the old `NTP_SERVER_2` (`time.google.com`) macro is removed - it was redundant with `NTP_SERVER_1` (both DNS-dependent public pools) once the gateway takes a slot, and lwIP's SNTP client is hard-capped at 3 server slots (`sntp_setservername(0/1/2, ...)` in the Arduino core's `configTime()`; matches the frozen sdkconfig's `CONFIG_LWIP_SNTP_MAX_SERVERS=3`). The old `NTP_SERVER_3` (Cloudflare IP) is renumbered to `NTP_SERVER_2` so the remaining two macros stay contiguous.
- No new buffer-size constant needed - `IP_ADDRESS_BUFFER_SIZE` (already in `constants.h`) is reused for formatting the gateway IP into a `char[]` before passing it to `configTime()`.
- No REST/NVS/UI/shadow changes anywhere - this whole area (`customserver.cpp`, `shadow.cpp`, `configuration.html`, `swagger.yaml`) is untouched by this change.
- No changes to `source/test/` beyond, if useful, a native unit test around the server-list construction (e.g. confirming the list still contains valid entries when the gateway is unset/`0.0.0.0`, since `configTime()` itself isn't host-testable).
