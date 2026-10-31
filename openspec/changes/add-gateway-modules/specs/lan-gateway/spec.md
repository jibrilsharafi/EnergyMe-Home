## MODIFIED Requirements

### Requirement: Gateway command envelope
The device SHALL accept an IoT Command with `operation: "gateway"`, an `op` string naming one primitive (`ping`, `net_scan`, `http_request`, `https_request`, `tls_probe`, `modbus_read`, `modbus_write`, `udp_send`, `tcp_request`) or a tunnel op (`tunnel_open`, `tunnel_close`, specified in the `lan-tunnel` capability), and `args` given either as a JSON object or as a string holding base64-encoded JSON. It SHALL reject the command with `REJECTED` and reasonCode `UNKNOWN_OP` for an unknown `op`, and `BAD_ARGS` when `args` is missing, not decodable, not a JSON object, or fails the primitive's argument validation. Primitives other than `ping` and `net_scan` SHALL run through the `gateway-modules` executor, so they SHALL also be rejected with `WRITE_NOT_ALLOWED` or `WRITE_OUT_OF_RANGE` (write grants and read declarations), `BAD_TEMPLATE` (template expansion) and `MODULE_UNAVAILABLE` (`https_request` and `tls_probe` on a product where the HTTPS gate has not passed), all before any byte reaches the target; `WRITE_NOT_ALLOWED` with reason_detail `TARGET_MAC_CHANGED` is the one refusal decided after the TCP connect, still before any byte is sent.

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

#### Scenario: Tunnel op is routed to the tunnel
- **WHEN** a `gateway` command arrives with `op: "tunnel_open"`
- **THEN** the device handles it as a tunnel session open and does not hand it to the primitive task

#### Scenario: Ungranted write is rejected
- **WHEN** a `modbus_write` arrives for a target with no matching write grant
- **THEN** the device publishes `REJECTED` with reasonCode `WRITE_NOT_ALLOWED` and opens no connection

#### Scenario: HTTPS op before the gate passed
- **WHEN** an `https_request` arrives on a product where the HTTPS module is disabled
- **THEN** the device publishes `REJECTED` with reasonCode `MODULE_UNAVAILABLE`

### Requirement: Primitive results
On success the device SHALL publish `SUCCEEDED` with a `result` map. LAN payload bytes (`http_request` and `https_request` `body`, `udp_send` and `tcp_request` `reply`) SHALL use `s` with the payload as a string when it is non-empty, strict UTF-8 (no overlong sequences, surrogates or code points above U+10FFFF) and contains no C0 control character other than tab, CR and LF; otherwise they SHALL use `bin` (base64). An empty payload SHALL omit the key, because AWS IoT rejects the whole command response when any result value is empty. When a payload was cut by its cap and ends inside a multibyte character, the incomplete trailing sequence SHALL be dropped before the check and `truncated` SHALL remain `true`. Structured lists SHALL use `s` with compact JSON, and numbers SHALL use `s` with their decimal string. Transport failures SHALL publish `FAILED` with a reasonCode: `TIMEOUT`, `CONNECT_FAILED`, `NO_REPLY`, `MODBUS_EXCEPTION` (with the exception code in reasonDescription), `BAD_RESPONSE` (a malformed HTTP header or chunk-size line, or a Modbus reply that does not match the request), `NETWORK_DOWN`, `NO_MEMORY` (reasonDescription `TUNNEL_ACTIVE` when `https_request` or `tls_probe` arrives during a LAN tunnel session), `READBACK_MISMATCH` (Modbus write with `verify`), `TLS_FAILED` or `TLS_PIN_MISMATCH`.

- `ping` `{ip, count (1-5, default 3)}` returns `sent`, `received`, and `rtt_avg_ms` when at least one reply arrived. It succeeds even with zero replies, and `received: "0"` is the answer.
- `net_scan` `{mdns_service (optional, e.g. "_http._tcp")}` returns `hosts` (`[{"ip","mac"}]`), `host_count`, `truncated`, and, when requested, `mdns` (`[{"name","ip","port"}]`). Lists are cut to fit the MQTT buffer, which sets `truncated: true`.
- `http_request` `{ip, port (default 80), method (GET|POST|PUT|PATCH|DELETE, default GET), path (default "/"), headers (optional string map), body (optional string) or body_b64, timeout_ms, access (optional, read|write; `write` raises the request to write, `read` never lowers it, a non-GET is read only when a stored read declaration lists its method and path), return_headers (optional, default false)}` returns `status_code`, `body` (s when text, else bin), `truncated` (b), and, only when `return_headers` is true, `headers` (s, the raw status line and header block, at most 2048 bytes, omitted when empty). The request is sent as HTTP/1.1 with `Connection: close`; a chunked reply is decoded while it is received and the 4096-byte cap counts decoded bytes.
- `https_request` takes the `http_request` args plus `cert_sha256` (required) and port default 443, and returns the same keys.
- `tls_probe` `{ip, port (default 443), timeout_ms}` returns `cert_sha256`, `subject_cn` (omitted when empty) and `not_after`.
- `modbus_read` `{ip, port (default 502), unit (default 1), function (1|2|3|4), address, count (1-125 for registers, 1-2000 for bits)}` returns `values` (JSON array of integers).
- `modbus_write` `{ip, port, unit, function (5|6|15|16), address, values (array of integers, 1-123 registers or 1-1968 bits), verify (optional, default false)}` returns `written` with the count and, only when `verify` is true, `readback` (JSON array of integers).
- `udp_send` `{ip, port, payload_b64 (1-1460 bytes, one datagram), wait_ms (default 0)}` is classed write unless the payload's SHA-256 matches a read declaration, and returns `sent: true`, `reply` (s when text, else bin) and `truncated` (replies are cut at 1460 bytes) when a datagram from the target arrives within `wait_ms`, and `FAILED NO_REPLY` when none does. Datagrams from other hosts are ignored. Without a wait, it returns `sent: true`.
- `tcp_request` `{ip, port (required), payload (optional string) or payload_b64 (0-5760 bytes), idle_ms (default 300, 50-5000), timeout_ms}` is read with an empty payload, otherwise classed like `udp_send`, and returns `reply` (s when text, else bin) and `truncated` (replies are cut at 4096 bytes), and `FAILED NO_REPLY` when no byte arrives.

#### Scenario: Shelly switched through http_request
- **WHEN** an enabled device holding a grant `{ip:"192.168.2.72", port:80, module:"http", mac:"c4:5b:be:00:11:22", allow:["/rpc/Switch.Set"]}` runs `http_request` with `{ip:"192.168.2.72", path:"/rpc/Switch.Set?id=0&on=true", access:"write"}` and the Shelly answers ARP with that MAC
- **THEN** the Shelly turns on and the result carries `status_code: "200"` and the Shelly JSON reply as `body` with the `s` attribute

#### Scenario: Write declared without a grant
- **WHEN** the same request arrives on a device with no grant for 192.168.2.72
- **THEN** the device publishes `REJECTED` with `WRITE_NOT_ALLOWED` and the Shelly is not contacted

#### Scenario: POST claiming read without a declaration
- **WHEN** an `http_request` POST carries `access: "read"` and no read declaration lists `POST` with its path
- **THEN** the device publishes `REJECTED` with `WRITE_NOT_ALLOWED` unless a write grant matches

#### Scenario: HTTPS during a tunnel session
- **WHEN** a LAN tunnel session is open and an `https_request` arrives
- **THEN** the device publishes `FAILED` with `NO_MEMORY` and reasonDescription `TUNNEL_ACTIVE`, and the tunnel stays open

#### Scenario: Binary reply stays base64
- **WHEN** an `http_request`, `udp_send` or `tcp_request` reply contains a NUL byte, an invalid UTF-8 sequence or a control character other than tab, CR and LF
- **THEN** the payload is published with the `bin` attribute

#### Scenario: Empty reply body
- **WHEN** an `http_request` target answers `200` with an empty body
- **THEN** the device publishes `SUCCEEDED` with `status_code` and `truncated` and no `body` key

#### Scenario: Text cut inside a multibyte character
- **WHEN** an `http_request` reply of UTF-8 text exceeds 4096 bytes and byte 4096 falls inside a multibyte character
- **THEN** `body` is an `s` string without the incomplete character and `truncated` is `true`

#### Scenario: Headers returned on request
- **WHEN** an `http_request` carries `return_headers: true` and the target sets a cookie
- **THEN** the result also carries `headers` as an `s` string including the `Set-Cookie` line

#### Scenario: Verified Modbus write
- **WHEN** a `modbus_write` granted by an entry bound to the target's MAC carries `verify: true` and the server stores the values
- **THEN** the result carries `written` and `readback` with the stored values

#### Scenario: Scan finds LAN hosts
- **WHEN** `net_scan` runs on a /24 with live Shelly and Broadlink devices
- **THEN** `hosts` lists their IP and MAC addresses

#### Scenario: Unreachable host times out
- **WHEN** `http_request` targets a private address with no host
- **THEN** the device publishes `FAILED` with reasonCode `CONNECT_FAILED` or `TIMEOUT` within the clamped timeout

#### Scenario: Modbus exception is reported
- **WHEN** a Modbus server answers `modbus_read` with exception code 2
- **THEN** the device publishes `FAILED` with reasonCode `MODBUS_EXCEPTION` and reasonDescription naming code 2

## ADDED Requirements

### Requirement: Command results unchanged by the executor
The gateway command path SHALL be an adapter over the `gateway-modules` executor. For every op that existed before this change, a command that uses none of the new optional arguments (`access`, `return_headers`, `verify`) and contains no `${` in a templated field SHALL produce a command response (status, reasonCode, result keys, attribute types, values) identical to the previous firmware for the same LAN reply, with the 4096-byte payload cap. Requests classed write additionally need a matching grant (see `Write grants configured locally and from the cloud`). The intended differences are: a chunked reply whose chunk-size lines overflowed the previous firmware's 512-byte raw allowance was cut early with `truncated: true` and now returns up to 4096 decoded bytes; a malformed chunked body is `FAILED` `BAD_RESPONSE` instead of a truncated success; a Modbus request whose `address + count` exceeds 65536 is `REJECTED` `BAD_ARGS` (canonical address grammar) instead of being sent; a non-GET path, or one sent with `access: "write"` or a body, holding a dot segment, `//`, a backslash or an encoded separator is `REJECTED` `BAD_ARGS`; a request write that does not finish before the deadline is `TIMEOUT` instead of `CONNECT_FAILED`; header names that equal a reserved name once `_` is read as `-` are refused like the reserved name; and the `UNKNOWN_OP` description lists the new ops.

#### Scenario: Golden replies match
- **WHEN** the golden set captured on the previous firmware (each op, text, binary, empty, chunked with few large chunks, truncated multibyte, timeout, connect failure, Modbus exception) is replayed against the same LAN targets on the new firmware
- **THEN** every command response matches its golden capture key for key and value for value, apart from `rtt_avg_ms` and other timing values

#### Scenario: Reply larger than the command cap
- **WHEN** a command `http_request` target returns 10 KB
- **THEN** the body is cut at 4096 bytes with `truncated: true`, whatever the executor ceiling

### Requirement: Write grants configured locally and from the cloud
The grant list of the `gateway-modules` capability (write grants and read declarations) SHALL be set through the `system` shadow field `gateway_write_grants` and through `PUT /api/v1/gateway` with a `write_grants` array on the authenticated local REST API, both using the same setter, which replaces the whole list. `GET /api/v1/gateway` SHALL return `write_grants`. An invalid list SHALL be refused with HTTP 400 on REST and ignored with a warning from the shadow, leaving the stored list unchanged. `write_grants: []` SHALL clear every grant. In community mode the endpoint keeps returning 403 for PUT.

#### Scenario: Local revoke
- **WHEN** an installer sends `PUT /api/v1/gateway` with `{"write_grants": []}`
- **THEN** every later write is refused with `WRITE_NOT_ALLOWED` and the `system` shadow reports an empty `gateway_write_grants`

#### Scenario: Invalid REST list
- **WHEN** `PUT /api/v1/gateway` carries a grant with `module: "ftp"`
- **THEN** the device answers 400 and the stored grants are unchanged

#### Scenario: Grants survive reboot
- **WHEN** the device reboots with two stored grants
- **THEN** both are enforced and reported after boot
