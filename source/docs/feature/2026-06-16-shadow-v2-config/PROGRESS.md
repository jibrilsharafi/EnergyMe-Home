# Progress - shadow v2.1.0

Legend: ⬜ not started · 🟡 in progress · ✅ done · ⛔ blocked

_Last updated: 2026-06-17 (all 8 phases implemented + committed; MQTT storm
root-caused + fixed; cutover build hardware-verified stable on dev .174)._

## RESOLVED - MQTT reconnect storm root cause (2026-06-17)

A persistent **MQTT reconnect storm** (broker closes the session ~30 ms after
CONNACK, connect/drop every ~1.2 s, zero stable session) was traced - via the
`connectivity-handler` Lambda's IoT lifecycle events - to
`disconnectReason: CLIENT_ERROR`, `clientInitiatedDisconnect: false`. That is a
**client-side protocol/limit violation**, which ruled out dup-client-id, auth
(policy is permissive), keep-alive and network. Bench device .155 (different
identity, non-shadow firmware) holds 1 connection for 40 h with 0 errors, so the
broker/account/network are healthy - the fault was **.174-firmware-specific**.

**Cause:** the device subscribed to the `$aws/commands/things/<id>/executions/+/request/+`
reserved topic, but **AWS IoT Commands is not provisioned** in this account (no
dispatcher; `list-commands` is not even a valid API here). Subscribing to an
unrecognized reserved topic makes the broker reject the session with CLIENT_ERROR.
A secondary `AdvancedLogTask` panic crash-loop was just the log firehose from the
storm (stopped the instant cloud was disabled).

**Fixes (both hardware-verified on .174):**
- `fix(mqtt)` `2205b5c` - gate the IoT Commands subscription behind
  `MQTT_IOT_COMMANDS_SUBSCRIBE_ENABLED` (OFF until provisioned end-to-end). The
  handler + ENV_DEV injection stay compiled/tested; only the broker subscribe is
  withheld.
- `fix(shadow)` `0a14b4d` - `requestReport` before `Shadow::begin` is a benign
  no-op (was 16 "unknown shadow" warnings/boot from the ADE7953 channel load).

Process note: the earlier "hardware-verified 01-07" claims had been made on
**intermediate** builds; the committed cutover binary (`41f8bf8`, 9 KB buffer)
only reached hardware during this session (build `2573984c`). The 9 KB buffer did
not fix the storm (the storm was the commands subscribe, not buffer size), but it
is retained as correct sizing for the worst-case inbound shadow delta.

## Verified on the cutover build (`2573984c`, dev .174)

- Single stable MQTT connection: device `connections=1` held > 5 min; broker
  lifecycle since re-enable = 1 connected / 0 disconnected / **0 CLIENT_ERROR**.
- Telemetry flowing: 90 publishes, **0 publish errors**, no crash.
- All 5 shadows report end-to-end (AWS versions incrementing: info, issues,
  system, meter, channels).
- Inbound apply round-trip: ENV_DEV `inject-delta` (`system.led_brightness`
  75 -> 30 -> 75) applied on-device, reported reached AWS, `desired` backstop
  cleared. (Real broker-delivered delta still needs a cloud desired-writer; not
  testable read-only.)

| # | Phase | Code | Verified on cutover build (.174) | Notes |
|---|-------|------|----------------------------------|-------|
| 01 | scaffold `shadow` module | ✅ | 25 native tests + stable on-device | core protocol |
| 02 | `info` shadow | ✅ | AWS read: identity + sketch_md5; version inc | - |
| 03 | `issues` shadow | ✅ | registry observer + active_count; version inc | ack path wired later |
| 04 | `system` shadow | ✅ | inject led_brightness -> applied -> AWS | 5 fields + mqtt_log_level auto-revert |
| 05 | `meter` shadow | ✅ | reports; version inc | ADE7953 calibration |
| 06 | `channels` shadow | ✅ | reports; version inc | dropped `channel` topic |
| 07 | Commands | ✅ (subscribe gated OFF) | native tests; broker e2e blocked on provisioning | gate flips ON when IoT Commands is live |
| 08 | config-topic retirement | ✅ (`41f8bf8`) | telemetry stable, no config topics published | 9 KB buffer + removals; NO fw version bump here |

## Decisions log (resolved)

1. **`system` scope** - ship 5 configurable fields (`led_brightness`,
   `send_power_data`, `mqtt_log_level`, `log_level_print`, `log_level_save`).
   `ntp`/`timezone` out (auto via cloud/NTP, device is UTC). `modbus_*` out
   (hardcoded, not configurable).
2. **Configurable state = shadow-only** - device drops `channel` and
   `system/static` publishes; `channels`/`info` shadows are canonical. Telemetry
   topics carry only measurements/statistics/dynamic runtime. WiFi creds stay
   local-only (never in a shadow).
3. **`issues` ack** - via shadow desired/delta (`desired.ack=[codes]`), not a
   Command (ack is idempotent state intent).
4. **Identity hash** - `sketch_md5` (`ESP.getSketchMD5()`), NOT git commit: the
   binary digest proves published-vs-modified build; a git SHA lies on
   dirty/uncommitted trees.
5. **No `/update/accepted` subscription** - its full-state+metadata echo is the
   largest inbound message; `version` is sourced from the delta payload instead.
6. **No topic-version bump** - `MQTT_TOPIC_VERSION` stays `v1`; no surviving topic
   changes payload. Migration is additive (`$aws/...` shadows/Commands) +
   subtractive (3 config topics stop publishing). Avoids rule duplication, rollout
   window, policy-ARN migration, and the delete-v1-early footgun. Zero ingest gap.
   `system/dynamic`->`system` rename dropped (cosmetic).
7. **Deltas applied in the MQTT task body, NOT the RX callback** - callback copies
   the delta out + flags; apply (blocking SPI/NVS + mutex up to 1s) runs in
   `_checkPublishShadows()` so it can't stall `loop()`/keepalive (#138). Same rule
   for the `mqtt_log_level` revert timer and `issues` tick: flag, never publish
   cross-task (PubSubClient not thread-safe).
8. **Cloud-writer contract constraints** - backend chunks `channels` `desired` to
   <= N channels/update (RX buffer), and clears all `desired` on `factory_reset`.

## Deploy sequencing (hard)

Do NOT ship the `channel`/`system/static` **publish removals** (08) to the fleet
until the cloud `channels`/`info` shadow-ingestion is live - else the
`channel_handler`/`system_static` Lambdas lose input and cloud state goes stale.
Shadow publishes (02-06) are safe to ship earlier; only removals are order-dependent.

## Cloud-side dependencies (EXTERNAL - energyme-infra, not this repo)

Reference only; implemented + tracked in the cloud repo. Listed so the firmware
contract is validated end-to-end.

| Item | Status | Blocks |
|------|--------|--------|
| `$aws/commands/things/<thing>/*` policy statement | ✅ present in policy | - |
| **IoT Commands feature provisioned** (account not set up; `list-commands` invalid) | ⬜ | **device commands subscribe is gated OFF until this exists** |
| Shadow ingestion Lambda + rule -> existing `device-ops` table (no new table) | ⬜ | reading reported cloud-side |
| Desired-state writer ("Intelligence" backend) | ⬜ | real broker-delivered delta test (inject path already proven) |
| 3 command templates + `StartCommandExecution` dispatcher | ⬜ | 07 end-to-end test |
| Retire idle `system/static`/`command`/`channel` rules (whenever) | ⬜ | cleanup only |

## Hardware-verify items

- ✅ `MQTT_BUFFER_SIZE` 5 KB -> 9 KB: TLS handshake fine, free internal heap
  ~65 KB during steady cloud operation on `2573984c`. No esp-aes alloc failures.
- ⬜ `channels` worst-case **inbound delta** (bulk `desired`, 64-char labels +
  metadata, ~2x state) fits `MQTT_BUFFER_SIZE` 9 KB; else cloud must chunk. Not
  yet exercised at worst case (no cloud desired-writer). See 06.
- ⬜ (Final review) Commands request carries a server timestamp for the staleness
  guard; backend clears `desired` on `factory_reset`. Blocked on IoT Commands
  provisioning. See 07.

## Final gate + post-MVP

- **Final payload-shape review** (firmware + cloud together) before "done":
  confirm every shadow/command payload has exactly what's needed. See 00.
- **Post-MVP** (next iteration, not in 01-08): WiFi info (SSID/RSSI/IP, non-secret)
  + WiFi actions (reconnect/rescan) as shadow/Commands; remaining REST config not
  yet mirrored; `issues` ack wiring. Creds stay local-only.

## Next step

Firmware MVP is implemented + committed + hardware-stable on dev .174. Remaining:
1. Push `feat/iot-shadow-config` + open PR -> `development` (gated on Jibril's go-ahead).
2. Cloud side (energyme-infra): shadow ingestion + desired-state writer to test the
   real broker-delivered delta; provision IoT Commands, then flip
   `MQTT_IOT_COMMANDS_SUBSCRIBE_ENABLED` ON for the commands e2e.
3. Separate release step on `development`: bump firmware version to 2.1.0 (NOT in
   this branch).
