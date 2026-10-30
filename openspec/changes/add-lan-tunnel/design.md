## Context

The approved design study (2026-10-30, side-session note `lan-tunnel-design.md`) drives this change. The constraints that shape it, all from the current code:

- PubSubClient 2.8.0 publishes QoS0 only, reads one packet per `loop()`, and the MQTT task sleeps up to 100 ms between iterations. Inbound packets above the 9 KB buffer vanish silently.
- `_subscribeCallback` copies every payload into a 32 KB PSRAM buffer with `"%.*s"`, which truncates binary at the first NUL, and nothing may publish inside the callback (single PubSubClient buffer, QoS1 PUBACK after return).
- The gateway primitive slot is single (`_busy` CAS), so a session cannot live there.
- lwIP: 16 sockets, 16 active PCBs shared with AsyncTCP/Modbus/MQTT, TCP send buffer and window 5760 B, MSS 1436. mbedTLS buffers are internal, so a second TLS session (AWS Secure Tunneling) does not fit the Home's ~41 KB internal-heap floor.
- The dev `gateway` command templates take a free-string `op`, and the deployed dev device policies allow Publish/Subscribe/Receive on `energyme/<product>/v1/${thing}/*` (checked with `aws iot get-command` / `get-policy`, 2026-10-30).

## Goals / Non-Goals

**Goals:**
- Interactive and bulk TCP (HTTP web UI, SSH, Modbus TCP, telnet) to one LAN host per session, surviving loss, reordering and short MQTT drops.
- Zero infra change for the first phase.
- No measurable effect on metering and telemetry cadence.

**Non-Goals:**
- End-to-end encryption inside the broker, SOCKS mode, a web UI banner, per-device IAM narrowing, sessions over 55 minutes, resume across device reboot. All deferred (see Open Questions / later tasks).

## Decisions

### Ride the existing `gateway` command (`op: tunnel_open|tunnel_close`)
No new command template, audit in `command_events` for free. Alternative: a dedicated `energyme-<product>-<env>-tunnel` template, which lets IAM separate tunnel rights from gateway rights. Deferred to infra; the device code only needs `parseOp` to accept a second spelling later.

### The operator picks the sid
64-bit random, 16 hex chars, in the open args. The tool subscribes to `/u` before dispatching, so there is no race and no polling to discover it. The sid is a correlation id, not a credential: authority is IAM on `/d`.

### Binary frames, own ARQ, per-channel credit
JSON/base64 would inflate data by a third and add a parse per frame. The ARQ is session-level (window 8 frames, cumulative ack + 16-bit SACK, RFC 6298 RTO 300-5000 ms, fast retransmit on 2 SACKed later frames, `LINK_FAILED` after 8 consecutive RTOs). SACK bit `i` means `ack + 2 + i`, since `ack + 1` is by definition missing. Unsequenced frames (ACK/PING/PONG) carry `seq` 0. Credit is SSH-style with absolute cumulative grants, so a duplicate or reordered `CH_CREDIT` is harmless; every non-ACK/PING/PONG frame is sequenced, so a lost credit or FIN cannot deadlock.

Channel-id reuse: in-order delivery guarantees the device processes the operator's `FIN` before a later `CH_OPEN` on the same id, but the device may still be draining that channel's buffer. The operator therefore reuses an id only after the device's `FIN`/`RST` and, after a `FIN`, a grant covering every byte it sent (the device always flushes its credit when a buffer empties). A `CH_OPEN` on a still-busy id resets the old channel and answers `CH_ERR BUSY`.

Close handshake: the closing side sends `CLOSE`/`CLOSED` (sequenced) and lingers up to 2 s for its ack, then tears down. The operator tool exits on `CLOSED` or after the linger and reads the terminal status from `GetCommandExecution`.

### Tasks and ownership
- `Tunnel` task (6 KB internal stack, priority 1, created at boot) owns every LAN socket, the ARQ and the timers. `select()` with a 10 ms cap while a session is open; blocked on a task notification when idle. eventfd is not used (linking unverified); the 10 ms cap bounds latency instead.
- The MQTT task stays the only PubSubClient user: it calls `Tunnel::open` from the gateway command handler, subscribes/unsubscribes, publishes uplink frames and the terminal status, and frees the session buffers once the tunnel task marks the session done. REST and shadow disables only flip flags; the tunnel task notices on its next pass.
- Downlink: first branch of `_subscribeCallback` compares the topic with the stored `/d` topic and memcpys the payload into a free PSRAM slot (pool of 20 x 4112 B) whose index goes on a FreeRTOS queue. No log, no publish, no allocation. Out-of-order frames keep their slot in the reorder table, so there is no second copy. A full pool drops the frame and the ARQ recovers.
- Uplink: the tunnel task builds complete frames into an outbound pool (8 x 4112 B) and queues indices; the MQTT task publishes at most 4 per iteration with the `const uint8_t*` overload (the `const char*` one uses `strlen`) and returns the slot. A failed publish just returns the slot; the ARQ resends. Retransmit copies stay in a separate ring (8 x 4112 B), so the outbound slot can be released as soon as it is published.
- While a session is open the MQTT task waits 10 ms instead of 100 ms, the tunnel task rings `MQTT_NOTIFY_WAKE_BIT` when it queues uplink, and `loop()` is called up to 8 more times per iteration while the previous packet was a tunnel frame and bytes are pending, so command and shadow single-slot handoffs keep their one-packet-per-drain guarantee.

### Device-side buffering and backpressure
Per channel only a downlink ring (8 KB PSRAM, operator to LAN), advertised as `channel_credit`. LAN to operator needs no ring: the task calls `recv()` straight into a retransmit slot only when the channel has operator credit and the window has room, otherwise lwIP's receive window closes towards the LAN host. Small reads coalesce for up to 10 ms or a full frame (`FIONREAD`). A 25 frames/s token bucket (burst 8) covers every uplink frame, acks included.

Total PSRAM while open: about 82 KB inbound pool + 33 KB outbound pool + 33 KB retransmit ring + 32 KB channel rings, about 180 KB, all freed at close.

### Enable flags and allowlist
`tunnel_enabled` lives in the gateway NVS namespace, separate from `gateway_enabled` because a tunnel is a much wider grant than the bounded primitives. `tunnel_ports` (NVS blob, at most 16 ports) defaults to `22, 23, 80, 443, 502, 8080, 8443`; an empty list allows any port. A target IP allowlist is not used: DHCP LANs make it impractical and it adds little against a cloud principal who can change it anyway. The target checks reuse the gateway's validator through a public `Gateway::validateTarget()`.

### MQTT reconnect grace
The session survives an MQTT drop of up to 30 s: RTO timers and the operator-silence timer freeze while disconnected, LAN reads stop when the window fills, and after reconnect the device re-subscribes and resends all unacknowledged frames at once. Closing on every disconnect would drop SSH at every WiFi hiccup on the pilot's weak link.

### Operator tool in this repo for now
`source/utils/lan_tunnel/` (asyncio, `awsiotsdk` MQTT5 over WSS with SigV4, boto3 for `StartCommandExecution`), run with the existing `source/pyproject.toml`. Client id `tunnel-<sid>`, never a thing name (which would kick the meter off the broker). It refuses `--env prod` without a typed device-id confirmation and has a hard block on the PROD bench address. It moves to energyme-infra with the IAM work. The protocol module is shared by the tool and a Python reference device used in the host chaos tests.

## Risks / Trade-offs

- [A blocking TLS `publish()` stalls the MQTT task on a congested uplink] → per-iteration budget and token bucket; telemetry is queued in PSRAM; worst loop time measured in the soak.
- [Broker's 100 publishes/s per connection, shared with telemetry, drops silently] → 25 frames/s each way; the ARQ absorbs drops.
- [16 active PCBs shared with web, Modbus, MQTT] → 4 channels max; `NO_SOCKET` on exhaustion.
- [Plaintext inside AWS for HTTP/telnet] → any principal allowed to subscribe to `energyme/#` can read it; SSH is end-to-end already. Encryption is a later phase.
- [Anyone allowed to run the `gateway` command can open a tunnel where `tunnel_enabled` is true] → the device switch is the only separation until the dedicated command template exists.
- [Executions over 60 minutes may lose their terminal status] → `max_duration_s` clamped to 3300 until a 65-minute dev test proves otherwise.
- [Localhost forward breaks web UIs that emit absolute URLs or check Host] → hosts-file mapping or browser proxy for now; SOCKS mode later.

## Migration Plan

Firmware only, off by default (`tunnel_enabled` false), so an OTA changes nothing until the switch is turned on. Rollback is the previous firmware; NVS keys are ignored by older builds.

## Open Questions

- Requester audit for `command_events` (CloudTrail on `StartCommandExecution` vs an ADR-010 requester column): infra, later.
- Whether a terminal status lands after 60 minutes: needs the 65-minute dev test.
