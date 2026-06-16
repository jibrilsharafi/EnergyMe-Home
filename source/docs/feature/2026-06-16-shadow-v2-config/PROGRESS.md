# Progress - shadow v2.1.0

Legend: ⬜ not started · 🟡 in progress · ✅ done · ⛔ blocked

_Last updated: 2026-06-16 (planning complete, validated with cloud side)._

## Phases (firmware)

| # | Phase | Status | PR | Provable without cloud? | Notes |
|---|-------|--------|----|-------------------------|-------|
| 01 | scaffold `shadow` module | ⬜ | - | yes (native unit tests) | core protocol; build first |
| 02 | `info` shadow | ⬜ | - | **yes** (AWS console) | retire `system/static` deferred to 08 |
| 03 | `issues` shadow | ⬜ | - | **yes** (AWS console) | ack path wired later |
| 04 | `system` shadow | ⬜ | - | no (needs desired writer) | 5 fields + mqtt_log_level auto-revert |
| 05 | `meter` shadow | ⬜ | - | no | ADE7953 calibration |
| 06 | `channels` shadow | ⬜ | - | no | drops `channel` topic |
| 07 | Commands | ⬜ | - | no (needs dispatcher) | restart/factory_reset/energy_reset |
| 08 | config-topic retirement + fw 2.1.0 | ⬜ | - | n/a | topic stays v1; remove command/static/channel, buffer bump |

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
| `$aws/commands/things/<thing>/*` policy statement | ⬜ (agreed to add) | 07 end-to-end test |
| Shadow ingestion Lambda + rule -> existing `device-ops` table (no new table) | ⬜ | 04/05/06 + reading reported |
| Desired-state writer ("Intelligence" backend) | ⬜ | 04/05/06 inbound test |
| 3 command templates + `StartCommandExecution` dispatcher | ⬜ | 07 end-to-end test |
| Retire idle `system/static`/`command`/`channel` rules (whenever) | ⬜ | cleanup only |

## Hardware-verify items

- `MQTT_BUFFER_SIZE` 5 KB -> 9 KB: re-check TLS handshake + free heap on a real
  device (known esp-aes alloc pressure at the 5 KB baseline). See 08.
- `channels` worst-case **inbound delta** (bulk `desired`, 64-char labels +
  metadata, ~2x state) fits `MQTT_BUFFER_SIZE` 9 KB; else cloud must chunk. See 06.
- (Final review verifies) Commands request carries a server timestamp for the
  staleness guard; backend clears `desired` on `factory_reset`. See 07.

## Final gate + post-MVP

- **Final payload-shape review** (firmware + cloud together) before "done":
  confirm every shadow/command payload has exactly what's needed. See 00.
- **Post-MVP** (next iteration, not in 01-08): WiFi info (SSID/RSSI/IP, non-secret)
  + WiFi actions (reconnect/rescan) as shadow/Commands; remaining REST config not
  yet mirrored; `issues` ack wiring. Creds stay local-only.

## Next step

Implement **01 - scaffold** (`lib/shadow_logic` native unit tests first).
