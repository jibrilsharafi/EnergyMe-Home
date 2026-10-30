## Purpose

Cloud-relayed TCP tunnel from an operator's machine to one private IPv4 host:port on the device's LAN, carried as binary frames over the device's existing AWS IoT Core MQTT connection, opened and closed through the `gateway` IoT Command.

## ADDED Requirements

### Requirement: Session opened and closed through the gateway command
The device SHALL open a tunnel session on a `gateway` IoT Command with `op: "tunnel_open"` and args `{sid, ip, port, max_duration_s?, idle_s?}`, where `sid` is exactly 16 lowercase hex characters chosen by the operator. The command SHALL stay `IN_PROGRESS` for the whole session and end with a terminal status when the session closes. Open SHALL be rejected, with no subscription, socket or buffer left behind, in this order: `TUNNEL_DISABLED` (gateway or tunnel switch off), `TUNNEL_BUSY` (a session is already open), `OTA_IN_PROGRESS`, `BAD_ARGS` (sid format, missing or ill-typed fields), `TARGET_NOT_PRIVATE` / `TARGET_NOT_ALLOWED` (the same target rules as the gateway primitives), `PORT_NOT_ALLOWED`, `NO_MEMORY` (free internal heap below 45 KB, largest internal block below 16 KB, or the session buffers could not be allocated, reported as `FAILED`). A `gateway` command with `op: "tunnel_close"` and args `{sid}` SHALL close the matching open session with reason `CLOUD_CLOSED` and itself end `SUCCEEDED`; a sid that matches no open session SHALL be rejected with `NO_SUCH_SESSION`. Tunnel ops SHALL NOT occupy the gateway primitive slot, so primitives keep running during a session.

#### Scenario: Session opens
- **WHEN** an enabled device receives `tunnel_open` with sid `0123456789abcdef`, ip `192.168.2.80` and port 80
- **THEN** it subscribes to `.../tunnel/0123456789abcdef/d`, publishes an `OPENED` frame on `.../tunnel/0123456789abcdef/u` and the command stays `IN_PROGRESS`

#### Scenario: Second session is rejected
- **WHEN** `tunnel_open` arrives while a session is open
- **THEN** the device publishes `REJECTED` with `TUNNEL_BUSY` and the open session is unaffected

#### Scenario: Port outside the allowlist
- **WHEN** `tunnel_open` targets port 3389 with the default allowlist
- **THEN** the device publishes `REJECTED` with `PORT_NOT_ALLOWED`

#### Scenario: Out-of-band close
- **WHEN** `tunnel_close` arrives with the open session's sid
- **THEN** the close command ends `SUCCEEDED` and the session's own command ends `SUCCEEDED` with reason `CLOUD_CLOSED`

#### Scenario: Primitive during a session
- **WHEN** a `ping` gateway command arrives while a session is open
- **THEN** the ping runs and completes normally

### Requirement: Tunnel topics
Frames SHALL travel on `energyme/<product>/v1/<thing>/tunnel/<sid>/d` (operator to device) and `energyme/<product>/v1/<thing>/tunnel/<sid>/u` (device to operator), with `<product>` the device's topic product segment. The device SHALL subscribe to its `/d` topic only while a session is open, re-subscribe after an MQTT reconnect, and unsubscribe when the session closes. Device publishes SHALL be QoS0 and never retained. Frames SHALL never be logged, parsed as text or handed to the JSON command path.

#### Scenario: Idle device has no tunnel subscription
- **WHEN** no session is open
- **THEN** the device holds no subscription under `.../tunnel/`

#### Scenario: Binary payload with NUL bytes
- **WHEN** a downlink DATA frame carries bytes containing `0x00`
- **THEN** every byte reaches the LAN socket unchanged

### Requirement: Frame format
Every frame SHALL start with a 16-byte little-endian header: `ver` (u8, 1), `type` (u8), `ch` (u8, 0 for session control, 1-4 for channels), `flags` (u8, bit0 ACK_VALID, bit1 RETX), `seq` (u32), `ack` (u32), `sack` (u16), `len` (u16, payload bytes, at most 4096), followed by exactly `len` payload bytes. Types SHALL be DATA 1, ACK 2, CH_OPEN 3, CH_OK 4, CH_ERR 5, CH_CREDIT 6, FIN 7, RST 8, OPENED 9, CLOSE 10, CLOSED 11, PING 12, PONG 13. A frame with an unknown `ver` or `type`, `ch` above 4, `len` above 4096, a `len` that does not match the MQTT payload length, a control payload of the wrong size, or a channel type on channel 0 SHALL be dropped and counted. Fixed payloads: `CH_OPEN {u32 credit}`, `CH_ERR {u8 reason}`, `CH_CREDIT {u32 cumulative_grant}`, `RST {u8 reason}`, `OPENED {u16 frame_max, u8 window, u8 channels_max, u32 channel_credit, u32 max_duration_s, u32 idle_s}`, `CLOSE {u8 reason}`, `CLOSED {u8 reason}`; CH_OK, FIN, ACK, PING and PONG carry none. Golden frames shared by the firmware and tool tests SHALL pin the encoding.

#### Scenario: Oversized frame dropped
- **WHEN** a downlink frame declares `len` 5000
- **THEN** the device drops it without touching any channel and counts it

#### Scenario: Golden vectors agree
- **WHEN** the firmware unit tests and the tool tests run
- **THEN** both encode and decode every shared golden frame identically

### Requirement: Reliable ordered delivery
Every frame type except ACK, PING and PONG SHALL carry a per-direction sequence number starting at 1, and SHALL be delivered to the receiver's channel logic exactly once and in order. ACK, PING and PONG SHALL carry `seq` 0 and SHALL NOT be sequenced. `ack` SHALL be the highest in-order sequence received (0 before any); bit `i` of `sack` SHALL mean sequence `ack + 2 + i` was received out of order. Sequence comparisons SHALL use 32-bit serial arithmetic. The sender SHALL keep at most 8 unacknowledged frames, retransmit on an RFC 6298 RTO (initial 1000 ms, clamped 300-5000 ms, exponential backoff, no samples from retransmitted frames) and on a SACK showing two later frames received, and SHALL close the session with `LINK_FAILED` after 8 consecutive timeouts while the cloud connection is up. The receiver SHALL buffer up to 8 frames beyond `ack`, drop duplicates and anything further ahead, and SHALL acknowledge within 40 ms or after 2 unacknowledged frames, and at once on a duplicate or out-of-order frame.

#### Scenario: Reordered frames
- **WHEN** downlink frames 2, 3 and 1 arrive in that order
- **THEN** the device delivers 1, 2, 3 in order and acknowledges 3

#### Scenario: Lost uplink frame
- **WHEN** an uplink DATA frame is dropped by the broker
- **THEN** the device retransmits it after the RTO and the operator receives the byte stream intact

#### Scenario: Duplicate frame
- **WHEN** a downlink frame with an already delivered seq arrives again
- **THEN** it is not delivered twice and the device answers with an ACK

### Requirement: Per-channel credit flow control
A sender SHALL NOT put more DATA bytes on a channel than the receiver's cumulative grant for it. The device's initial grant per channel SHALL be its channel buffer size, advertised in `OPENED.channel_credit`; the operator's initial grant SHALL be the `CH_OPEN.credit` value. Grants SHALL be raised with `CH_CREDIT` carrying the absolute cumulative byte count, sent when at least 2048 bytes were freed or the buffer became empty. A LAN host that stops reading SHALL stall only its own channel; the device SHALL stop reading a LAN socket while its channel has no operator credit or the send window is full.

#### Scenario: Slow LAN reader
- **WHEN** a LAN host on channel 1 stops reading while channel 2 is busy
- **THEN** channel 1 stops receiving operator data once its credit is used and channel 2 keeps flowing

### Requirement: Channels
The operator SHALL open a channel with `CH_OPEN` on a free channel id 1-4. The device SHALL connect to the session target with a 5 s deadline and answer `CH_OK`, or `CH_ERR` with reason `CONNECT_FAILED` (1), `TIMEOUT` (2), `REFUSED` (3), `NO_SOCKET` (4) or `BUSY` (5). `FIN` from the operator SHALL half-close the LAN socket once the channel's buffered data is written; LAN end-of-stream SHALL be sent as `FIN`. A channel SHALL be freed once both sides sent `FIN` and its buffer is empty, or at once on `RST` either way. A LAN reset, a LAN write stalled for 120 s, or a data or `FIN` frame for a channel that is not open SHALL produce `RST`. The operator SHALL reuse a channel id only after the device sent `FIN` or `RST` for it and, after a `FIN`, granted credit for every byte the operator sent.

#### Scenario: Browser loads a page over several channels
- **WHEN** the operator opens 4 channels to an HTTP server and requests a page and its assets
- **THEN** every response arrives complete and each channel closes cleanly

#### Scenario: Refused connection
- **WHEN** `CH_OPEN` targets a closed port on a live host
- **THEN** the device answers `CH_ERR` with `REFUSED` or `CONNECT_FAILED` and the channel id is free again

### Requirement: Session limits and close reasons
The device SHALL enforce: one session, 4 channels, `max_duration_s` default 3000 clamped to 60-3300, `idle_s` (no DATA on any channel) default 300 clamped to 30-1800, operator silence (no downlink frame) 30 s, at most 256 MB of channel data per session, uplink at most 25 frames/s. It SHALL close the session with a `CLOSED` frame and a terminal status: `SUCCEEDED` for `OPERATOR_CLOSED` (operator `CLOSE`), `CLOUD_CLOSED`, `IDLE_TIMEOUT`, `MAX_DURATION`; `FAILED` for `OPERATOR_LOST`, `TUNNEL_DISABLED`, `OTA_STARTED`, `LOW_MEMORY` (free internal heap below 30 KB for 5 s), `CLOUD_DISCONNECTED` (MQTT not back within 30 s, reported after reconnect), `LINK_FAILED`, `BYTE_CAP`. The terminal `result` SHALL carry `sid`, `reason`, `duration_s`, `bytes_up`, `bytes_down`, `channels_opened`, `frames_retx` and `frames_dropped`. On the restart path the device SHALL close sockets without publishing. While the MQTT connection is down for less than 30 s the session, its LAN sockets and its unacknowledged frames SHALL be kept and resent after reconnect.

#### Scenario: Operator tool killed
- **WHEN** the operator tool is killed without sending `CLOSE`
- **THEN** within 30 s the device closes the session and the command ends `FAILED` with `OPERATOR_LOST`

#### Scenario: Short MQTT drop
- **WHEN** the device's MQTT connection drops and recovers within 30 s during a session
- **THEN** the session continues and no byte is lost or duplicated on any channel

#### Scenario: OTA during a session
- **WHEN** an OTA job is accepted while a session is open
- **THEN** the session closes with `OTA_STARTED` before the download proceeds

### Requirement: Tunnel switch and port allowlist
The device SHALL keep a `tunnel_enabled` switch in NVS, default false, settable from the `system` shadow and from `PUT /api/v1/gateway` (`{"tunnel": bool}`), and SHALL accept `tunnel_open` only while both `tunnel_enabled` and `gateway_enabled` are true. Turning either off SHALL close a running session at once with `TUNNEL_DISABLED`. The device SHALL keep a port allowlist in NVS, default `22, 23, 80, 443, 502, 8080, 8443`, settable as the `tunnel_ports` array (at most 16 ports, 1-65535) of the `system` shadow, where an empty array allows any port. `GET /api/v1/gateway` SHALL report `tunnel` and, while a session is open, `tunnel_active {sid, ip, port, since}`.

#### Scenario: Gateway disabled mid-session
- **WHEN** `PUT /api/v1/gateway {"enabled": false}` arrives during a session
- **THEN** the session closes at once and its command ends `FAILED` with `TUNNEL_DISABLED`

#### Scenario: Tunnel switch off rejects opens
- **WHEN** `tunnel_open` arrives with `gateway_enabled` true and `tunnel_enabled` false
- **THEN** the device publishes `REJECTED` with `TUNNEL_DISABLED`

### Requirement: Session does not starve the device
Tunnel sockets and timers SHALL run in their own task, and session buffers SHALL be allocated in PSRAM at open and freed at close. The MQTT task SHALL publish at most 4 tunnel frames per loop iteration after its existing telemetry work, SHALL deliver downlink frames to the tunnel without the text-message copy or a per-frame log line, and SHALL keep meter, energy and shadow publishing on schedule during a saturating session.

#### Scenario: Telemetry during a bulk transfer
- **WHEN** a session moves data at the rate limit for 10 minutes
- **THEN** meter and energy publishes keep their cadence and the device does not restart

### Requirement: On-device audit log
The device SHALL log at INFO the execution id, sid, target and limits when a session opens, and the reason and counters when it closes, and SHALL log every rejected open at WARNING with its reasonCode. It SHALL NOT log per frame.

#### Scenario: Session lifecycle is logged
- **WHEN** a session opens and later closes on idle timeout
- **THEN** one INFO line names the execution id, sid and target, and one names `IDLE_TIMEOUT` with the byte counters
