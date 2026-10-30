## MODIFIED Requirements

### Requirement: Primitive results
On success the device SHALL publish `SUCCEEDED` with a `result` map. LAN payload bytes (`http_request` `body`, `udp_send` `reply`) SHALL use `s` with the payload as a string when it is non-empty, strict UTF-8 (no overlong sequences, surrogates or code points above U+10FFFF) and contains no C0 control character other than tab, CR and LF; otherwise they SHALL use `bin` (base64), and an empty payload SHALL be `s: ""`. When a payload was cut by its cap and ends inside a multibyte character, the incomplete trailing sequence SHALL be dropped before the check and `truncated` SHALL remain `true`. Structured lists SHALL use `s` with compact JSON, and numbers SHALL use `s` with their decimal string. Transport failures SHALL publish `FAILED` with a reasonCode: `TIMEOUT`, `CONNECT_FAILED`, `NO_REPLY`, `MODBUS_EXCEPTION` (with the exception code in reasonDescription), `BAD_RESPONSE` (a malformed HTTP header or a Modbus reply that does not match the request), `NETWORK_DOWN`, or `NO_MEMORY`.

- `ping` `{ip, count (1-5, default 3)}` returns `sent`, `received`, and `rtt_avg_ms` when at least one reply arrived. It succeeds even with zero replies, and `received: "0"` is the answer.
- `net_scan` `{mdns_service (optional, e.g. "_http._tcp")}` returns `hosts` (`[{"ip","mac"}]`), `host_count`, `truncated`, and, when requested, `mdns` (`[{"name","ip","port"}]`). Lists are cut to fit the MQTT buffer, which sets `truncated: true`.
- `http_request` `{ip, port (default 80), method (GET|POST|PUT|PATCH|DELETE, default GET), path (default "/"), headers (optional string map), body (optional string) or body_b64, timeout_ms}` returns `status_code`, `body` (s when text, else bin), `truncated` (b). The request is sent as HTTP/1.1 with `Connection: close`; a chunked reply is decoded.
- `modbus_read` `{ip, port (default 502), unit (default 1), function (1|2|3|4), address, count (1-125 for registers, 1-2000 for bits)}` returns `values` (JSON array of integers).
- `modbus_write` `{ip, port, unit, function (5|6|15|16), address, values (array of integers, 1-123 registers or 1-1968 bits)}` returns `written` with the count.
- `udp_send` `{ip, port, payload_b64 (1-1460 bytes, one datagram), wait_ms (default 0)}` returns `sent: true`, `reply` (s when text, else bin) and `truncated` (replies are cut at 1460 bytes) when a datagram from the target arrives within `wait_ms`, and `FAILED NO_REPLY` when none does. Datagrams from other hosts are ignored. Without a wait, it returns `sent: true`.

#### Scenario: Shelly switched through http_request
- **WHEN** an enabled device runs `http_request` with `{ip:"192.168.2.72", path:"/rpc/Switch.Set?id=0&on=true"}`
- **THEN** the Shelly turns on and the result carries `status_code: "200"` and the Shelly JSON reply as `body` with the `s` attribute

#### Scenario: Binary reply stays base64
- **WHEN** an `http_request` or `udp_send` reply contains a NUL byte, an invalid UTF-8 sequence or a control character other than tab, CR and LF
- **THEN** the payload is published with the `bin` attribute

#### Scenario: Text cut inside a multibyte character
- **WHEN** an `http_request` reply of UTF-8 text exceeds 4096 bytes and byte 4096 falls inside a multibyte character
- **THEN** `body` is an `s` string without the incomplete character and `truncated` is `true`

#### Scenario: Scan finds LAN hosts
- **WHEN** `net_scan` runs on a /24 with live Shelly and Broadlink devices
- **THEN** `hosts` lists their IP and MAC addresses

#### Scenario: Unreachable host times out
- **WHEN** `http_request` targets a private address with no host
- **THEN** the device publishes `FAILED` with reasonCode `CONNECT_FAILED` or `TIMEOUT` within the clamped timeout

#### Scenario: Modbus exception is reported
- **WHEN** a Modbus server answers `modbus_read` with exception code 2
- **THEN** the device publishes `FAILED` with reasonCode `MODBUS_EXCEPTION` and reasonDescription naming code 2
