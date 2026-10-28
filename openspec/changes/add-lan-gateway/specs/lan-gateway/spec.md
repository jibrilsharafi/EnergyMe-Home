## ADDED Requirements

### Requirement: Gateway command envelope
The device SHALL accept an IoT Command with `operation: "gateway"`, an `op` string naming one primitive (`ping`, `net_scan`, `http_request`, `modbus_read`, `modbus_write`, `udp_send`), and `args` given either as a JSON object or as a string holding base64-encoded JSON. It SHALL reject the command with `REJECTED` and reasonCode `UNKNOWN_OP` for an unknown `op`, and `BAD_ARGS` when `args` is missing, not decodable, not a JSON object, or fails the primitive's argument validation.

#### Scenario: base64 args are decoded
- **WHEN** a `gateway` command arrives with `op: "ping"` and `args` set to the base64 encoding of `{"ip":"192.168.2.72"}`
- **THEN** the device runs a ping to 192.168.2.72

#### Scenario: object args are accepted
- **WHEN** a `gateway` command arrives with `args` as the JSON object `{"ip":"192.168.2.72"}`
- **THEN** the device treats it the same as the base64 form

#### Scenario: Unknown op is rejected
- **WHEN** a `gateway` command arrives with `op: "telnet"`
- **THEN** the device publishes `REJECTED` with reasonCode `UNKNOWN_OP` and runs nothing

#### Scenario: Undecodable args are rejected
- **WHEN** a `gateway` command arrives with `args` that is neither a JSON object nor valid base64 JSON
- **THEN** the device publishes `REJECTED` with reasonCode `BAD_ARGS`

### Requirement: Targets limited to private IPv4 literals
Every primitive that takes a target SHALL require `ip` to be a strict dotted-quad IPv4 literal inside 10.0.0.0/8, 172.16.0.0/12 or 192.168.0.0/16. Hostnames, other ranges (including loopback, link-local, multicast and limited broadcast), and malformed literals SHALL be rejected with reasonCode `TARGET_NOT_PRIVATE` before any network activity. The device's own addresses on any active interface, and the network and directed-broadcast addresses of those interfaces' subnets, SHALL be rejected with reasonCode `TARGET_NOT_ALLOWED`, so the cloud can never reach the device's own local API through loopback or flood a subnet. The firmware SHALL build the request line and the `Host`, `Connection` and `Content-Length` headers itself from `ip`, `port`, `path` and `body`, SHALL reject cloud-supplied framing headers (`Host`, `Content-Length`, `Transfer-Encoding`, `Connection`, `Expect`, `TE`, `Upgrade`, `Keep-Alive`, `Proxy-Connection`, `Trailer`, case-insensitive) with `BAD_ARGS`, and SHALL NOT follow HTTP redirects.

#### Scenario: Public address is rejected
- **WHEN** an `http_request` targets `8.8.8.8`
- **THEN** the device publishes `REJECTED` with reasonCode `TARGET_NOT_PRIVATE` and opens no connection

#### Scenario: Loopback is rejected
- **WHEN** any primitive targets `127.0.0.1`
- **THEN** the device rejects it with `TARGET_NOT_PRIVATE`

#### Scenario: Hostname is rejected
- **WHEN** a primitive targets `router.local` or `192.168.1.1.evil.com`
- **THEN** the device rejects it with `TARGET_NOT_PRIVATE`

#### Scenario: The device itself is rejected
- **WHEN** any primitive targets one of the device's own addresses, or the network or broadcast address of its subnet (e.g. `192.168.2.255` on a /24)
- **THEN** the device rejects it with `TARGET_NOT_ALLOWED` and opens no connection

#### Scenario: Framing header is rejected
- **WHEN** an `http_request` sets a `content-length` or `Transfer-Encoding` header
- **THEN** the device rejects it with `BAD_ARGS`

#### Scenario: Redirect is not followed
- **WHEN** a private target answers an `http_request` with a 3xx redirect
- **THEN** the device returns the 3xx status and its body, and makes no further request

### Requirement: Gateway disabled by default with local and cloud switch
The gateway SHALL be disabled on a fresh device. Its state SHALL be persisted in NVS and exposed as the writable `gateway_enabled` boolean of the `system` shadow, and as `GET/PUT /api/v1/gateway` on the authenticated local REST API (an emergency switch; the web UI SHALL NOT expose it, since the gateway is a cloud feature). Both SHALL use the same setter. While disabled, every `gateway` command SHALL be rejected with reasonCode `GATEWAY_DISABLED`. Disabling SHALL NOT abort a request already running.

#### Scenario: Fresh device rejects gateway commands
- **WHEN** a `gateway` command arrives on a device that never enabled the gateway
- **THEN** the device publishes `REJECTED` with reasonCode `GATEWAY_DISABLED`

#### Scenario: Cloud enables the gateway
- **WHEN** a `system` shadow delta sets `gateway_enabled: true`
- **THEN** the device persists it, reports it with `desired:{gateway_enabled:null}`, and accepts subsequent `gateway` commands

#### Scenario: Local disable is reflected in the shadow
- **WHEN** the gateway is disabled through `PUT /api/v1/gateway`
- **THEN** the setting persists across reboot and the `system` shadow reported state shows `gateway_enabled: false`

### Requirement: Gateway work never blocks the MQTT task
Primitives SHALL run in a dedicated gateway task, one request at a time. The MQTT task SHALL publish `IN_PROGRESS` and hand the request off. The terminal status SHALL be published from the MQTT task once the gateway task reports a result. A `gateway` command received while another is queued or running SHALL be rejected with reasonCode `GATEWAY_BUSY`.

#### Scenario: Concurrent request is rejected
- **WHEN** a second `gateway` command arrives while a `net_scan` is running
- **THEN** the second command is rejected with `GATEWAY_BUSY` and the scan completes normally

#### Scenario: Meter publishing continues during a long request
- **WHEN** an `http_request` to an unresponsive private host waits for its full timeout
- **THEN** meter and MQTT keepalive traffic continue during the wait

### Requirement: Technical limits
Each primitive SHALL apply a timeout, `timeout_ms` from args, defaulting to 5000 and clamped to 100-10000, as one absolute deadline covering connect, send and the whole response: a peer that trickles header lines or holds the body open SHALL NOT extend it. `udp_send` waits SHALL be clamped to 0-5000 ms. LAN response payloads SHALL be capped at 4096 bytes, with `truncated: true` in the result when the cap is hit. `net_scan` SHALL sweep at most the device's /24, in batches, and SHALL stop after 30 s with `truncated: true`. Decoded `args` SHALL be at most 6 KB: the 9 KB inbound MQTT buffer is the real ceiling once base64 and the envelope are added.

#### Scenario: Oversized response is truncated
- **WHEN** an `http_request` target returns a 10 KB body
- **THEN** the result carries the first 4096 bytes and `truncated: true`

#### Scenario: Trickling server cannot hold the gateway
- **WHEN** an `http_request` target sends one header line per second and never finishes the header block
- **THEN** the device publishes `FAILED` with reasonCode `TIMEOUT` once `timeout_ms` has elapsed, and the next `gateway` command is accepted

#### Scenario: Timeout is clamped
- **WHEN** an `http_request` asks for `timeout_ms: 60000`
- **THEN** the device waits at most 10000 ms

### Requirement: Primitive results
On success the device SHALL publish `SUCCEEDED` with a `result` map. LAN payload bytes SHALL use `bin` (base64), structured lists SHALL use `s` with compact JSON, and numbers SHALL use `s` with their decimal string. Transport failures SHALL publish `FAILED` with a reasonCode: `TIMEOUT`, `CONNECT_FAILED`, `NO_REPLY`, `MODBUS_EXCEPTION` (with the exception code in reasonDescription), `BAD_RESPONSE` (a malformed HTTP header or a Modbus reply that does not match the request), `NETWORK_DOWN`, or `NO_MEMORY`.

- `ping` `{ip, count (1-5, default 3)}` returns `sent`, `received`, and `rtt_avg_ms` when at least one reply arrived. It succeeds even with zero replies, and `received: "0"` is the answer.
- `net_scan` `{mdns_service (optional, e.g. "_http._tcp")}` returns `hosts` (`[{"ip","mac"}]`), `host_count`, `truncated`, and, when requested, `mdns` (`[{"name","ip","port"}]`). Lists are cut to fit the MQTT buffer, which sets `truncated: true`.
- `http_request` `{ip, port (default 80), method (GET|POST|PUT|PATCH|DELETE, default GET), path (default "/"), headers (optional string map), body (optional string) or body_b64, timeout_ms}` returns `status_code`, `body` (bin), `truncated` (b). The request is sent as HTTP/1.1 with `Connection: close`; a chunked reply is decoded.
- `modbus_read` `{ip, port (default 502), unit (default 1), function (1|2|3|4), address, count (1-125 for registers, 1-2000 for bits)}` returns `values` (JSON array of integers).
- `modbus_write` `{ip, port, unit, function (5|6|15|16), address, values (array of integers, 1-123 registers or 1-1968 bits)}` returns `written` with the count.
- `udp_send` `{ip, port, payload_b64, wait_ms (default 0)}` returns `sent: true`, `reply` (bin) and `truncated` (replies are cut at 1460 bytes) when a datagram from the target arrives within `wait_ms`, and `FAILED NO_REPLY` when none does. Datagrams from other hosts are ignored. Without a wait, it returns `sent: true`.

#### Scenario: Shelly switched through http_request
- **WHEN** an enabled device runs `http_request` with `{ip:"192.168.2.72", path:"/rpc/Switch.Set?id=0&on=true"}`
- **THEN** the Shelly turns on and the result carries `status_code: "200"` and the Shelly JSON reply as `body`

#### Scenario: Scan finds LAN hosts
- **WHEN** `net_scan` runs on a /24 with live Shelly and Broadlink devices
- **THEN** `hosts` lists their IP and MAC addresses

#### Scenario: Unreachable host times out
- **WHEN** `http_request` targets a private address with no host
- **THEN** the device publishes `FAILED` with reasonCode `CONNECT_FAILED` or `TIMEOUT` within the clamped timeout

#### Scenario: Modbus exception is reported
- **WHEN** a Modbus server answers `modbus_read` with exception code 2
- **THEN** the device publishes `FAILED` with reasonCode `MODBUS_EXCEPTION` and reasonDescription naming code 2

### Requirement: On-device audit log
Every gateway command that passes validation SHALL be logged at INFO with execution id, primitive and target. Every rejection or failure SHALL be logged at WARNING with its reasonCode.

#### Scenario: Executed request is logged
- **WHEN** a valid `http_request` to 192.168.2.72 runs
- **THEN** an INFO log line names the execution id, `http_request` and `192.168.2.72`
