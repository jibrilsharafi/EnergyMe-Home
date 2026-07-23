## Context

`CustomTime::begin()` and the periodic `_checkAndSyncTime()` (`source/src/customtime.cpp:17` and `:265`) both call `configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3)` with three compile-time macros (`source/include/customtime.h:13-15`). On a network with no path to the public internet, none of the three are reachable and the device can never NTP-sync; the only recovery today is a manual `POST /api/v1/system/time` with a Unix timestamp, repeated after every clock loss (e.g. a power outage) - see [issue #205](https://github.com/jibrilsharafi/EnergyMe-Home/issues/205).

An earlier draft of this change added a full NVS-backed, REST-managed custom-server config (mirroring `CustomWifi`/`CustomMqtt`). That was dropped in favor of a zero-configuration heuristic: most routers capable of running a LAN (OpenWrt, pfSense, and plenty of consumer routers) also answer NTP requests on their own gateway address, so trying the gateway first covers the reported scenario without asking anyone to find and enter a server address.

The Arduino core's `configTime()` (`esp32-hal-time.c`) is a thin wrapper over lwIP's SNTP client (`#include "lwip/apps/sntp.h"`): it calls `sntp_setservername(0/1/2, ...)` for exactly three slots and `sntp_setoperatingmode(SNTP_OPMODE_POLL)`. The frozen pioarduino sdkconfig for this board (`esp32s3/qio_qspi/include/sdkconfig.h`) has `CONFIG_LWIP_SNTP_MAX_SERVERS=3` (matching the fixed three-argument API).

**Incident (2026-07-23): a second draft also enabled DHCP-provided NTP (`sntp_servermode_dhcp(1)`) in `begin()`.** On hardware, this asserted immediately and consistently (`LWIP_ASSERT_CORE_LOCKED()` at lwIP's `sntp.c:788`, confirmed against upstream source and a decoded on-device backtrace) - `sntp_servermode_dhcp()` requires the lwIP core lock, which is only implicitly held on the lwIP/tcpip thread, not on Arduino's `loopTask` where `CustomTime::begin()` runs. This was never an ordering bug (an earlier hypothesis, also wrong - see git history); it's a thread-safety precondition the call site never satisfied. The device also then failed to self-heal for ~20 minutes because of a separate, unrelated flaw in `crashmonitor.cpp`'s own rollback safety net (fixed independently - see `crashmonitor.cpp` and issue notes, not part of this change's scope). Given DHCP option 42 was always the marginal "nice to have" - gateway-first alone covers the reported scenario (issue #205), and was verified independently reachable via `w32tm` on the reporter's own network - **DHCP-provided NTP acquisition is dropped from this change entirely** rather than fixed with a `LOCK_TCPIP_CORE()`/`UNLOCK_TCPIP_CORE()` wrap, to keep zero new lwIP-threading surface in a change whose whole premise was minimalism.

## Goals / Non-Goals

**Goals:**
- Let a device on a network without public-internet reachability sync its clock against its own router/gateway, with no configuration step.
- Preserve current fleet behavior as closely as possible for networks where the gateway isn't an NTP server - two of today's three public fallbacks stay available.

**Non-Goals:**
- No manual/custom NTP server configuration (REST, NVS, shadow, UI) - this was the explicit simplification requested: almost nobody needs to point the device at an NTP server that isn't its own gateway or one of the two well-known public fallbacks.
- No DHCP-advertised NTP server (option 42) acquisition - dropped after the incident above; gateway-first already covers the reported scenario, and the marginal coverage (a network where the gateway itself doesn't answer NTP but DHCP explicitly advertises a different server - mostly a managed/enterprise-network pattern, not this project's audience) isn't worth the lwIP thread-safety surface.
- No change to the manual `POST /api/v1/system/time` fallback - it stays as the immediate-recovery path.
- No attempt to detect or special-case "gateway does not run NTP" beyond relying on SNTP's own per-server retry/failover - see Risks.

## Decisions

**Server list built dynamically from `WiFi.gatewayIP()` on every sync attempt, not cached.**
Reading the gateway fresh each call means a DHCP lease renewal, static-IP reconfiguration, or network change is picked up automatically with no state to keep in sync and no invalidation logic needed.

**Gateway takes slot 0; `NTP_SERVER_1` (`pool.ntp.org`) and `NTP_SERVER_2` (the Cloudflare IP) take slots 1-2; the old `NTP_SERVER_2` (`time.google.com`) is dropped entirely.**
lwIP's SNTP client is hard-capped at 3 servers (`CONFIG_LWIP_SNTP_MAX_SERVERS=3`, matching the fixed three-argument `configTime()` signature), so gateway + all three existing defaults doesn't fit; one has to go. The old `time.google.com` is the one dropped because it's the most redundant: both it and `pool.ntp.org` are DNS-dependent public pool hostnames, covering the same failure mode. Keeping `pool.ntp.org` (hostname, widely distributed pool) alongside the Cloudflare IP (raw, no DNS dependency) instead gives the 3-slot list real diversity across the two failure axes that matter: "gateway isn't an NTP server" and "DNS isn't working." The old Cloudflare-IP macro is renumbered from `NTP_SERVER_3` to `NTP_SERVER_2` so the two remaining macros stay contiguous. The gateway slot is itself a third DNS-independent option, so the final list has one DNS-dependent fallback and two DNS-independent ones (gateway, Cloudflare IP).

**Rely on SNTP's built-in per-server retry/failover, no custom staged-retry logic.**
lwIP's SNTP client in `SNTP_OPMODE_POLL` tries the server at the current index and advances to the next configured index on repeated failure to respond - i.e. "try server 1 first, fall back to server 2, then server 3" is the client's existing behavior, not something this change has to build. Passing `[gateway, pool.ntp.org, <Cloudflare IP>]` gets the desired priority order for free. If the gateway isn't an NTP server, or the device isn't yet connected (`WiFi.gatewayIP()` returns `0.0.0.0`), that slot simply times out and SNTP moves on - no special-casing needed, consistent with "simplicity over cleverness."

**No configuration/persistence/REST/shadow surface at all.**
Nothing here is user-configurable, so none of the earlier design's shadow-placement question applies - there's no field to place anywhere.

## Risks / Trade-offs

- **Not every router runs an NTP responder on its LAN IP** (some only forward to WAN, some run nothing) → the gateway slot then simply fails to respond and SNTP falls through to `pool.ntp.org`/the Cloudflare IP, exactly like today for any network with internet access. No worse than the status quo; strictly better for the reported case.
- **Dropping the old `time.google.com` fallback removes a second well-known public server** → accepted trade-off; it was fully redundant with `pool.ntp.org` for the failure mode that matters (DNS working, server unreachable/slow), while the retained Cloudflare IP covers a failure mode `pool.ntp.org` cannot (DNS broken).
- **A device not yet connected calls `configTime()` with gateway `0.0.0.0` in slot 0** → harmless: that server fails immediately/times out and SNTP proceeds to the next slot, same cost as the existing retry cadence already tolerates. Needs on-device verification, not yet exercised (see incident note above - the crash always fired before sync logic ever ran).
- **No compile-time way to verify SNTP's failover-on-timeout behavior without hardware** → verify on a bench device (192.168.2.174) as part of implementation before merging.

## Migration Plan

No migration needed - this changes runtime behavior only, no NVS keys, no persisted state, no REST/shadow shape changes anywhere.

## Open Questions

None outstanding - scope is now small enough that implementation should surface anything left, per the tasks list.
