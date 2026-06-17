# Progress - shadow v2.1.0

Legend: ⬜ not started · 🟡 in progress · ✅ done · ⛔ blocked

_Last updated: 2026-06-17 (session 3: real broker-delivered shadow e2e via admin-dev
`desired` writes - full writable matrix + bulk buffer stress + reject paths verified on
.174; surfaced the asymmetric-null cloud-contract finding below)._

## Update 2026-06-17 (session 3 - REAL broker-delivered shadow e2e via admin-dev)

Closed the gap #190 listed as cloud-gated: drove `desired` writes against the **dev
shadows** with `aws iot-data update-thing-shadow` (admin-dev) - the real AWS->broker
->device delta path the dev `inject-delta` shim could not exercise. UDP-correlated on
.174 (build `c509e555`).

- **Writable matrix (system/meter/channels), real deltas:** brightness, send_power_data,
  log levels, sample_time, calibration (phCalB, aIRmsOs/bIRmsOs), channel label/active/
  role, multi-field + multi-channel deltas - all apply -> persist -> report -> null
  `desired`, ~1 s round trip. Each delta `routeMessage`-queued then `_applyDelta`'d in
  the **task body** (deferred, not the callback) - confirmed in UDP.
- **Reject paths all run real validation (not silent drops):** WARN observed for
  led_brightness 200 (range), mqtt_log_level BOGUS (unknown), sample_time 50 (<min),
  channel 0 disable (blocked), channel key 99 (invalid) - each still completes the ack
  that nulls `desired` (no stuck delta / loop).
- **Bulk buffer stress (06's worst case):** 16-channel delta, 63-char label+groupLabel
  each, arrived as a single **3848-byte** inbound message and applied (<9 KB
  `MQTT_BUFFER_SIZE`). Single-shadow channels path confirmed feasible; no cloud chunking
  needed for label+groupLabel.
- **Concurrency + lifecycle paths all verified:** (a) local+cloud interplay - drift(local)
  + delta(cloud) on different fields converge, cloud-after-local wins; (b) idle 30 s - 0
  spurious version bumps; (c) reported-only shadow write (`wifi`) inert (writable-gated
  subscribe, confirmed `shadow.cpp:191` + UDP); (d) NVS persistence across reboot
  (brightness/levels/sample_time/calib/labels/role); (e) transient `mqtt_log_level` full
  lifecycle - cloud-set DEBUG -> survives reboot (restored + 5-min timer restarted,
  baseline untouched) -> auto-reverted to INFO at exactly 5 min; (f) **offline->reconnect-
  apply** - `desired` written while device off-broker (cloud disabled via API) applied on
  reconnect via reported-first->re-delta (cloud-wins-on-reconnect, 00 line 82); (g) **409
  optimistic-concurrency** forced via rapid double-writes - device re-publishes reported
  *without* version, fresh delta applies, converges to latest; 1 conflict/round, **no loop**
  (00 lines 96-100). All UDP-correlated.

### FINDING (no firmware change) - asymmetric-null seam needs the cloud contract

A cloud `desired` written **equal to** `reported` (a no-op write, or re-asserting an
already-satisfied value) produces **no delta** (AWS only deltas on `desired != reported`).
A no-GET device nulls `desired` only in the delta-ack, so it never sees or clears that
`desired`. The stuck `desired` then **reverts the next local edit** to the field.
*Reproduced on .174:* `desired.led_brightness=44` when reported already 44 -> local REST
88 -> reverted to 44 in ~2 s. Resolution is the **cloud contract** (cloud owns clearing
`desired`: write-to-change then `desired:null` after convergence; never write
`desired==reported`/re-assert) - documented in `00-overview-and-contract.md`. A
device-side drift-null was considered and rejected (trades it for a coincident-write
race; needs the avoided GET). **Not a #190 blocker** - the trigger needs the (unbuilt)
cloud writer; firmware is correct under the contract.

## Update 2026-06-17 (session 2 - post-MVP hardening, committed + verified on .174)

On top of the 01-08 MVP (now **6 shadows**):
- **Local-change propagation is source-agnostic:** `checkPublish()` drift-detects
  any writable-shadow change every 3 s and republishes, replacing the per-REST
  `publishLocalEdit` hook (removed). Local edits reach the cloud within ~3 s; they
  publish reported-only (no `desired:null`) - see 00 for the conflict-policy note.
- **Transient `mqtt_log_level` survives reboot:** a separate NVS marker
  (`mqtt_ns/transient_log`) restores the VERBOSE/DEBUG level + restarts the 5-min
  timer on boot, so a debug session keeps boot-time logs. Persistent-set and
  auto-revert clear it. Restore + clear both verified on .174.
- **`wifi` shadow (6th, reported-only):** connected/ssid/ip/gateway/subnet/dns/mac +
  static_ip/fallback_to_dhcp; read back from AWS (version 1). No writable network
  config, no creds, RSSI stays on `system/dynamic`.
- **`issues` boot-race fixed:** `IssueRegistry::begin()` moved before
  `CustomServer::begin()`, and `issuesToJson` returns empty-200 on a null mutex
  (was HTTP 500 on early boot). Verified: 40/40 200s post-reboot, 0 mutex errors.
- **Command `reasonCode` casing fixed:** uppercased to AWS `[A-Z0-9_-]+` (resolves
  the known follow-up noted below).
- **Command rejected-echo loop fixed (found during command testing on .174):** the
  MQTT RX router treated *any* `/commands/things/.../executions/...` topic as a
  command, so AWS's `.../response/rejected/json` echo of a rejected status publish
  was reprocessed (no `operation` -> re-rejected -> echoed -> **infinite ~110 ms
  publish loop** hammering the broker). Now only `.../request/json` (the subscribed
  topic) routes to the handler; other `/commands/things/` topics are logged +
  ignored. Verified on .174: each injected command processes exactly once, 14
  publishes / 14 ignores (1:1, bounded), no repeats. NB: a lowercase reasonCode (the
  bug above) would have *triggered* this loop in production - both fixes needed.
  Real broker-delivered command on `/request/json` still pending the cloud dispatcher
  (inject bypasses the RX router, so that match condition is not yet hardware-exercised).
- **Commands now processed in the task body, not the PubSubClient callback:**
  `_subscribeCallback` queues the request (`_queueCommand`, single mutex-guarded slot,
  warn-on-overwrite); `_drainPendingCommand()` in the MQTT loop runs the apply +
  status publishes. Avoids blocking the callback on per-channel NVS writes (#138) and
  the QoS1-PUBACK buffer corruption from publishing inside `loop()`. The dev inject
  now shares this queue+drain, so the inject suite exercises the real processing path
  (only the RX-callback enqueue itself stays cloud-gated). factory_reset verified
  end-to-end on .174 (IN_PROGRESS->SUCCEEDED->wipe->reboot; confirm-guard rejects).

## Update 2026-06-17 (corrected root cause + topic fix - SUPERSEDES the banner below)

The CLIENT_ERROR storm was NOT caused by IoT Commands being unprovisioned. Real
cause: the device subscribed to `.../executions/+/request/+` - a `+` at the
PAYLOAD-FORMAT segment, an unsupported AWS reserved-topic subscribe (AWS docs:
"Unsupported ... subscribe operations to reserved topics can result in a terminated
connection"). The documented form allows `+` only at the execution-id position. The
"not provisioned" reading was an artifact of the local AWS CLI 2.15.38 lacking
`list-commands`; the cloud side confirmed IoT Commands IS available in eu-west-1.

Fix `a9fbce8`: subscribe to `.../executions/+/request/json` (flag stays ON). Coupling:
the cloud dispatcher must create commands with contentType application/json.

Verified on dev .174 (build `e338ab8f`, commands subscription ACTIVE): single stable
MQTT connection >4 min, 68 telemetry publishes / 0 errors, **0 CLIENT_ERROR** at the
connectivity-handler since connect. Storm fix proven; the round-trip (device RECEIVES
a command) is pending the cloud CreateCommand + StartCommandExecution dispatcher.

~~Known follow-up~~ **RESOLVED (session 2):** device reject/fail `reasonCode`s
uppercased to AWS `[A-Z0-9_-]+` (BAD_PAYLOAD, MISSING_OPERATION, STALE_COMMAND,
CONFIRM_MISMATCH, BAD_CHANNELS, UNKNOWN_OPERATION). Happy path
(IN_PROGRESS -> SUCCEEDED) sends no statusReason, so was already unaffected.

## RESOLVED - MQTT reconnect storm root cause (2026-06-17)

A persistent **MQTT reconnect storm** (broker closes the session ~30 ms after
CONNACK, connect/drop every ~1.2 s, zero stable session) was traced - via the
`connectivity-handler` Lambda's IoT lifecycle events - to
`disconnectReason: CLIENT_ERROR`, `clientInitiatedDisconnect: false`. That is a
**client-side protocol/limit violation**, which ruled out dup-client-id, auth
(policy is permissive), keep-alive and network. Bench device .155 (different
identity, non-shadow firmware) holds 1 connection for 40 h with 0 errors, so the
broker/account/network are healthy - the fault was **.174-firmware-specific**.

**Cause (SUPERSEDED - see Update at top):** the device subscribed to
`$aws/commands/things/<id>/executions/+/request/+`. The original reading below blamed
"IoT Commands not provisioned" (an artifact of the local CLI lacking `list-commands`);
the real cause is the `+` at the payload-format segment - an unsupported reserved-topic
subscribe that makes the broker terminate the session with CLIENT_ERROR. IoT Commands
IS available in eu-west-1.
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
| 07 | Commands | ✅ subscribe fixed (`a9fbce8`, /request/json) + verified | storm gone on .174 (0 CLIENT_ERROR, sub ACTIVE); round-trip pending cloud dispatcher | reasonCode casing follow-up |
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
| **IoT Commands available in eu-west-1** | ✅ confirmed cloud-side (`list-commands` responds) | resolved - was a topic-shape bug, not provisioning |
| Shadow ingestion Lambda + rule -> existing `device-ops` table (no new table) | ⬜ | reading reported cloud-side |
| Desired-state writer ("Intelligence" backend) | ⬜ | real broker-delivered delta test (inject path already proven) |
| Command(s) + `StartCommandExecution` dispatcher (contentType application/json) | ⬜ | 07 round-trip test (storm fix already proven on .174) |
| Retire idle `system/static`/`command`/`channel` rules (whenever) | ⬜ | cleanup only |

## Hardware-verify items

- ✅ `MQTT_BUFFER_SIZE` 5 KB -> 9 KB: TLS handshake fine, free internal heap
  ~65 KB during steady cloud operation on `2573984c`. No esp-aes alloc failures.
- ⬜ `channels` worst-case **inbound delta** (bulk `desired`, 64-char labels +
  metadata, ~2x state) fits `MQTT_BUFFER_SIZE` 9 KB; else cloud must chunk. Not
  yet exercised at worst case (no cloud desired-writer). See 06.
- ⬜ (Final review) Commands request carries a server timestamp for the staleness
  guard; backend clears `desired` on `factory_reset`. Blocked on the cloud command
  dispatcher. See 07.

## Final gate + post-MVP

- **Final payload-shape review** (firmware + cloud together) before "done":
  confirm every shadow/command payload has exactly what's needed. See 00.
- **Post-MVP** (next iteration, not in 01-08): WiFi info (SSID/RSSI/IP, non-secret)
  + WiFi actions (reconnect/rescan) as shadow/Commands; remaining REST config not
  yet mirrored; `issues` ack wiring. Creds stay local-only.

## Next step

Firmware MVP + session-2 hardening implemented + committed + hardware-stable on
dev .174. Remaining:
1. PR `feat/iot-shadow-config` -> `development` opened (not merged - awaiting review).
2. Cloud side (energyme-infra): shadow ingestion + desired-state writer to test the
   real broker-delivered delta; build the `restart` command + StartCommandExecution
   dispatcher (contentType application/json) for the commands round-trip. The subscribe
   topic fix (`a9fbce8`) is done + verified on .174; the flag is already ON.
3. Separate release step on `development`: bump firmware version to 2.1.0 (NOT in
   this branch).
