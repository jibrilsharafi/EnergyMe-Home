## Purpose

Core LAN executor and protocol modules shared by one-shot gateway commands and scheduled collection: one request/result contract, canonical native addresses used the same way for read and write, a template expansion stage, and device-side write grants that keep every LAN target read-only unless a write was explicitly granted.

## ADDED Requirements

### Requirement: One module interface with a structured result
Every LAN protocol SHALL be implemented as a module behind one executor interface. A request SHALL carry `module` (`http`, `https`, `modbus_tcp`, `tcp`, `udp`), `target` `{ip, port}` with an optional `mac` (see `Target MAC binding`), an optional `access` (`read` or `write`, which can only raise the class, see `Every operation is classed read or write`), `writes` (`allow`, the default, or `deny`; see `Read-only callers`), `declared_read` (false by default; see `Read-only callers`), `timeout_ms` (100-10000, one absolute deadline covering connect, TLS handshake, send and the whole reply), `max_bytes` (1-131072) and the module's own fields. The executor SHALL return a structured result with `status` (`OK`, `FAILED` or `REJECTED`), `reason_code` and `reason_detail` (empty on `OK`), `proto_status` (HTTP status code, Modbus exception code, otherwise 0), `headers` (raw HTTP status line and header block, at most 2048 bytes, empty for other modules), exactly one payload (`body` bytes for `http`, `https`, `tcp`, `udp`; `registers` values for a Modbus read; `written` count for a Modbus write), `readback` (Modbus write with `verify`), `truncated`, `bytes_tx`, `bytes_rx`, `connected` (true once the TCP connect completed for `http`, `https`, `modbus_tcp` and `tcp`, and once the datagram was sent for `udp`; false for any result that ended before that, whatever its reason code), `address` (the canonical address actually used; for `http` and `https` it SHALL be the method and the path template text before `?`, unexpanded and with the query dropped, so it never holds an expanded value) and `timing` `{connect_ms, first_byte_ms, total_ms}`. A chunked HTTP reply SHALL be decoded while it is received, straight into the `max_bytes` body buffer: `max_bytes` and `truncated` SHALL apply to decoded body bytes only, chunk-size lines SHALL NOT count against `max_bytes`, and a chunk-size line (size, extensions and CRLF) longer than 128 bytes or not valid hex SHALL be `FAILED` with `BAD_RESPONSE`. Modules SHALL contain protocol logic only, never knowledge of a vendor, model or product. Result buffers SHALL be allocated in PSRAM.

#### Scenario: HTTP result keeps the header block
- **WHEN** an `http` request gets `200` with `Set-Cookie: SSES=abc` and a 900-byte body
- **THEN** the result has `status: OK`, `proto_status: 200`, `headers` holding the status line and every header line including `Set-Cookie`, the 900-byte `body` and `truncated: false`

#### Scenario: Body larger than max_bytes
- **WHEN** a request with `max_bytes: 4096` gets a 10 KB body
- **THEN** `body` holds the first 4096 bytes, `truncated` is true and `bytes_rx` counts every byte read before the cut

#### Scenario: Small chunks just under max_bytes
- **WHEN** a request with `max_bytes: 131072` gets a chunked reply of 64-byte chunks totalling 131071 decoded bytes
- **THEN** `body` holds all 131071 bytes and `truncated` is false, although the chunk-size lines add about 10 KB on the wire

#### Scenario: Small chunks just over max_bytes
- **WHEN** the same request gets 64-byte chunks totalling 131073 decoded bytes
- **THEN** `body` holds the first 131072 decoded bytes and `truncated` is true

#### Scenario: Oversized chunk-size line
- **WHEN** a chunked reply sends a chunk-size line of 200 bytes of extensions
- **THEN** the result is `FAILED` with `BAD_RESPONSE`

#### Scenario: Header block larger than 2048 bytes
- **WHEN** an `http` target sends a 3 KB header block
- **THEN** the result is `FAILED` with `BAD_RESPONSE` and no body

#### Scenario: Connect refused
- **WHEN** the target port is closed
- **THEN** the result is `FAILED` with `CONNECT_FAILED`, `connected` false, `timing.connect_ms` set, `first_byte_ms` 0 and empty payload

#### Scenario: Connect times out
- **WHEN** the target host never answers the TCP SYN and the deadline passes during the connect
- **THEN** the result is `FAILED` with `TIMEOUT` (the reason code commands already get today), `connected` false, `bytes_tx` 0 and empty payload

#### Scenario: Reply times out after connect
- **WHEN** a `modbus_tcp` connect completes but the unit sends no reply before the deadline
- **THEN** the result is `FAILED` with `TIMEOUT` and `connected` true

#### Scenario: max_bytes above the ceiling
- **WHEN** a request asks for `max_bytes: 200000`
- **THEN** the executor returns `REJECTED` with `BAD_ARGS` and opens no socket

### Requirement: Canonical native addresses
Each module SHALL accept addresses in one canonical grammar, the same string for read and write. `modbus_tcp`: `u<unit>/<table><addr>` with `unit` 0-255, `table` one of `co` (coils), `di` (discrete inputs), `ir` (input registers), `hr` (holding registers), and `addr` the zero-based protocol address in decimal 0-65535 without leading zeros (not the 0xxxx/4xxxx notation); the item count is a separate field (`count` for reads, length of `values` for writes) and `addr + count` SHALL NOT exceed 65536. The function code SHALL follow from table and access: `co` read 1, write 5 (one value) or 15; `di` read 2; `ir` read 4; `hr` read 3, write 6 (one value) or 16; `write_multiple: true` forces 15 or 16 for one value. `http` and `https`: `<METHOD> <path>` with METHOD in GET, POST, PUT, PATCH, DELETE and path in origin form (query included); the result `address` echoes the method and the unexpanded path template without the query (see "One module interface"). `tcp` and `udp` address the whole endpoint and echo an empty address. For Modbus the canonical address is also the `source_device/native_point` tail of the external point timeseries id (ADR-034, e.g. `u3/hr3000`); for HTTP, TCP and UDP point identity is assigned by the cloud decoder and is not the module address.

#### Scenario: Holding register read
- **WHEN** a `modbus_tcp` read has address `u3/hr3000` and `count: 2`
- **THEN** the device sends function 3, unit 3, address 3000, quantity 2 and echoes `address: "u3/hr3000"`

#### Scenario: Same address written
- **WHEN** a granted `modbus_tcp` write has address `u3/hr3000` and `values: [215]`
- **THEN** the device sends function 6 to unit 3, address 3000

#### Scenario: Read-only table written
- **WHEN** a write targets `u1/ir10` or `u1/di0`
- **THEN** the executor returns `REJECTED` with `BAD_ARGS` and opens no socket

#### Scenario: Malformed address
- **WHEN** an address is `u256/hr1`, `u1/hr65536`, `u1/hr007`, `u1/xx5` or `U1/HR5`, or a read with `count: 0`
- **THEN** the executor returns `REJECTED` with `BAD_ARGS`

#### Scenario: Range runs past the address space
- **WHEN** a read has `u1/hr65500` and `count: 100`
- **THEN** the executor returns `REJECTED` with `BAD_ARGS`

### Requirement: Every operation is classed read or write
The executor SHALL class each request after template expansion and before any socket opens, from the module rules and the caller's `declared_read` (see `Read-only callers`), never from `access: "read"`. `modbus_tcp` functions 1-4 SHALL be read and 5, 6, 15, 16 write, and an `access` that contradicts the function SHALL be rejected with `BAD_ARGS`. `http` and `https`: a GET whose expanded `body` and decoded `body_b64` are both absent or empty SHALL be read, and a GET carrying a non-empty `body` or `body_b64` SHALL be write, since a server may take the operation from the body whatever the method; POST, PUT, PATCH and DELETE SHALL be write unless the caller sets `declared_read`. `tcp`: an empty payload SHALL be read (banner read); a non-empty `tcp` payload and every `udp` payload SHALL be write unless the caller sets `declared_read`. A request with `access: "write"` SHALL be classed write whatever the rules above give; `access: "read"` SHALL be accepted and SHALL NOT lower the class. A GET that changes state (for example Shelly `GET /rpc/Switch.Set?on=false`) is classed read unless the caller sends `access: "write"`; this residual is a documented cloud responsibility, and the cloud control path always sends `access: "write"`. `ping`, `net_scan` and `tls_probe` are diagnostics and SHALL NOT be classed.

#### Scenario: POST without a declaration
- **WHEN** an `http` POST to `/cgi-bin/datachart.cgi` arrives from a one-shot command, with or without `access: "read"`
- **THEN** it is classed write and, with no write grant, returns `REJECTED` with `WRITE_NOT_ALLOWED` and opens no socket

#### Scenario: GET with a body
- **WHEN** an `http` GET to a Shelly target `/rpc` carries the body `{"method":"Switch.Set","params":{"id":0,"on":false}}` and `writes: "deny"`
- **THEN** it is classed write and returns `REJECTED` with `WRITE_NOT_ALLOWED` and `WRITES_DENIED`, and no socket opens

#### Scenario: GET raised to write
- **WHEN** an `http` GET to `/rpc/Switch.Set?id=0&on=false` carries `access: "write"`
- **THEN** it is classed write and needs a write grant

#### Scenario: Modbus write declared read
- **WHEN** a `modbus_tcp` request with function 6 carries `access: "read"`
- **THEN** the executor returns `REJECTED` with `BAD_ARGS`

#### Scenario: Empty TCP payload
- **WHEN** a `tcp` request has no payload
- **THEN** it is classed read and only reads what the peer sends

#### Scenario: TCP payload or UDP send
- **WHEN** a one-shot `tcp` request carries a 12-byte payload, or a `udp_send` carries any payload
- **THEN** it is classed write and needs a write grant

### Requirement: Read-only callers
A request with `writes: "deny"` SHALL be refused with `REJECTED`, reasonCode `WRITE_NOT_ALLOWED` and reason_detail `WRITES_DENIED` whenever the executor classes it write, after classification and before the grant check, whatever write grants are stored and before any socket opens. A request with `writes: "deny"` that is classed read SHALL run as usual. A caller MAY set `declared_read`, asserting that the literal request is a read of its own target: a POST, PUT, PATCH or DELETE (`http`, `https`) or a `tcp`/`udp` payload is then classed read without any grant lookup. `declared_read` SHALL NOT lower a Modbus write function, a GET with a body or `access: "write"`. The scheduled jobs of add-lan-collector SHALL set `writes: "deny"` and `declared_read` on every login and data step; the collector refuses at apply time any step that cannot be a literal read (see add-lan-collector). The one-shot command adapter SHALL use `writes: "allow"` and SHALL NOT set `declared_read`, and SHALL NOT read either from IoT Command arguments.

#### Scenario: Job step classed write
- **WHEN** a write grant allows `["*"]` on an `http` target and a request with `writes: "deny"` sends a GET with a body
- **THEN** it is classed write and returns `REJECTED` with `WRITE_NOT_ALLOWED` and `WRITES_DENIED`, and no socket opens

#### Scenario: Declared-read POST
- **WHEN** a collector step with `declared_read` and `writes: "deny"` POSTs to `192.168.2.40:80` `/cgi-bin/datachart.cgi` with the target's `mac`
- **THEN** it is classed read, needs no write grant and runs

#### Scenario: Declared-read UDP query
- **WHEN** a collector step with `declared_read` sends a fixed 17-byte frame to an on-link `udp` target with its `mac`
- **THEN** it is classed read and runs; the same request without `declared_read` is a write

#### Scenario: Read request with writes denied
- **WHEN** a request with `writes: "deny"` is a Modbus read or a GET without a body
- **THEN** it runs as it would with `writes: "allow"`

#### Scenario: Command carries a writes argument
- **WHEN** an `http_request` IoT Command carries `writes: "deny"`, `writes: "allow"` or `declared_read` in its args
- **THEN** the argument has no effect, the adapter passes `writes: "allow"` without `declared_read` and the result is the same as without it

### Requirement: Write grants
Every target SHALL be read-only unless a write grant matches. Write grants SHALL be one list of at most 8 entries, replaced as a whole, holding no secrets, at most 3072 bytes as compact JSON and persisted as one fixed-layout NVS blob of at most 1609 bytes in the gateway namespace. An entry SHALL hold `ip` (private IPv4 literal), `port` (1-65535, exact), `module`, an optional `kind` that SHALL be `write` when present, and an `allow` list of 1-4 strings. An entry with `kind: "read"` or with a `mac` key SHALL be refused; the target MAC travels on the request (see `Target MAC binding`).
- `http`, `https`: strings of at most 48 bytes, each a path that starts with `/`, holds no `?` and has `*` only as its last byte. The expanded request path before `?` SHALL match a string by plain byte comparison, exactly or as a prefix when the string ends in `*`, without any normalization; `["*"]` alone grants the whole target.
- `modbus_tcp`: each string `u<unit>/<table><start>:<count>` with an optional `=<min>..<max>` suffix, at most 48 bytes, `table` `co` or `hr`, `start + count` at most 65536, `min` and `max` raw 0-65535 (coils always 0-1); every written address SHALL fall inside one range of the same unit and table, and every written value inside its bounds.
- `tcp`, `udp`: only `["*"]`, granting any payload to the target. Raw payloads are not matched by content; a polled raw protocol gets its own module that classes requests by its service codes, as Modbus does by function code.

A request classed write with no matching entry SHALL be refused with `REJECTED` and reasonCode `WRITE_NOT_ALLOWED` before any socket opens; a matching Modbus range with a value outside its bounds SHALL be refused with `WRITE_OUT_OF_RANGE`. The reason_detail of either refusal SHALL name the target and the result `address` (for `http` and `https` the template text before `?`), never the expanded path or query. Entries SHALL be checked at request start; a change does not abort a running request. The list SHALL be cleared by factory reset and SHALL NOT bypass the target rules of `lan-gateway` (private IPv4, not the device itself). It protects against cloud bugs, wrong templates and fat-fingers, not a hostile cloud principal, which can change it; every change is logged at WARNING and visible in the archived `system` shadow history.

#### Scenario: Modbus write without a grant
- **WHEN** an enabled device with no grants receives a write to `192.168.2.50:502` `u1/hr100`
- **THEN** it returns `REJECTED` with `WRITE_NOT_ALLOWED` naming the target and the canonical address, and opens no socket

#### Scenario: Refused POST with a sensitive value in its query
- **WHEN** a caller's chain resolves `${secret:t}` with a sensitive resolver and an `http` POST to `/x?k=${secret:t}` arrives with no write grant
- **THEN** it returns `REJECTED` with `WRITE_NOT_ALLOWED`, `address` is `POST /x`, and neither `address` nor reason_detail contains the resolved value

#### Scenario: Write inside a granted range
- **WHEN** a grant has `{ip:"192.168.2.50", port:502, module:"modbus_tcp", allow:["u1/hr100:2=0..400"]}`, the write carries `mac:"00:11:22:33:44:66"`, the target answers ARP with that MAC and the write sets `u1/hr100` to `[215, 300]`
- **THEN** the write is sent

#### Scenario: Write crosses the end of a range
- **WHEN** the same grant is in place and a write sets `u1/hr101` with two values
- **THEN** it returns `REJECTED` with `WRITE_NOT_ALLOWED`

#### Scenario: Value outside bounds
- **WHEN** the same grant is in place and a write sets `u1/hr100` to `[500]`
- **THEN** it returns `REJECTED` with `WRITE_OUT_OF_RANGE`

#### Scenario: Grant for another unit or port
- **WHEN** a grant covers unit 1 on port 502 and a write targets unit 2, or port 503
- **THEN** it returns `REJECTED` with `WRITE_NOT_ALLOWED`

#### Scenario: HTTP prefix grant
- **WHEN** a write grant has `allow: ["/rpc/Switch.*"]` and a POST with the target's `mac` goes to `/rpc/Switch.Set?id=0&on=true`
- **THEN** the request is sent; a POST to `/rpc/Sys.Reboot` is refused with `WRITE_NOT_ALLOWED`

#### Scenario: Whole-target raw grant
- **WHEN** a write grant for an on-link `udp` target has `allow: ["*"]` and a `udp_send` carries the target's `mac`
- **THEN** any payload to that target is sent

#### Scenario: Reads need no grant
- **WHEN** no grant exists and a request is classed read
- **THEN** it runs subject only to the target rules

#### Scenario: Invalid grant list
- **WHEN** a new list has 9 entries, a public IP, a `di` range, an entry with `kind: "read"` or an unknown `kind`, an entry with a `mac` key, a `tcp` or `udp` entry with anything but `["*"]`, `"*"` next to another string, a path without a leading `/`, with `?` or with `*` before its end, an HTTP or Modbus `allow` string over 48 bytes, a range past address 65535, `min` above `max`, or more than 3072 bytes of compact JSON
- **THEN** the whole list is refused and the stored list stays unchanged

### Requirement: Target MAC binding
A request MAY carry the target `mac` (six lowercase hex pairs separated by `:`; broadcast, multicast and all-zero values are `BAD_ARGS`). It is never stored per grant or per secret: one-shot commands take it from their `mac` argument, collector jobs from the job `target`. The executor SHALL decide whether a target is on-link at run time from the interface lwIP routes the target through (`ip4_route`) and that interface's address and netmask, so Home Pro's Ethernet and WiFi interfaces are handled alike. An on-link request SHALL carry `mac` when it is classed write, when it is classed read only because of `declared_read` (a POST, PUT, PATCH or DELETE, or a `tcp`/`udp` payload), or when it expands a sensitive placeholder (`${secret:...}`); otherwise it SHALL be refused with `REJECTED` and reason_detail `MAC_REQUIRED` after expansion and before any socket opens. A request carrying `mac` to an off-link target SHALL be refused with reason_detail `TARGET_MAC_CHANGED` before any socket opens. With `mac` present on an on-link target the executor SHALL compare it once with the target's ARP entry under the lwIP core lock: for `http`, `https`, `modbus_tcp` and `tcp` after the TCP connect and before the first byte (for `https` before the ClientHello); for `udp` before the send, first sending one ARP request and polling for at most `min(500 ms, remaining deadline)` when no entry exists. A missing or different MAC SHALL be refused with reason_detail `TARGET_MAC_CHANGED`, closing the socket without sending a byte. The reasonCode of these refusals SHALL be `SECRET_TARGET_MISMATCH` when the request needs a MAC only because it expanded a secret, and `WRITE_NOT_ALLOWED` otherwise. Off-link (routed) targets stay IP-only.

#### Scenario: Different device at a granted IP
- **WHEN** a granted write to `192.168.2.50:502` carries `mac:"00:11:22:33:44:66"`, the controller is renumbered and another device with MAC `00:aa:bb:cc:dd:ee` takes `192.168.2.50` and answers on 502
- **THEN** the write connects, finds the different ARP entry, returns `REJECTED` with `WRITE_NOT_ALLOWED` and `TARGET_MAC_CHANGED`, and sends no Modbus byte

#### Scenario: On-link write without a MAC
- **WHEN** a granted write to on-link `192.168.2.50` carries no `mac`
- **THEN** the request is `REJECTED` with `WRITE_NOT_ALLOWED` and `MAC_REQUIRED` and no socket opens

#### Scenario: Malformed MAC argument
- **WHEN** a one-shot command carries `mac:"00:11:22:33:44:AA"` or `mac:"ff:ff:ff:ff:ff:ff"`
- **THEN** it is `REJECTED` with `BAD_ARGS`

#### Scenario: UDP target not yet in the ARP table
- **WHEN** a granted `udp` send with `mac` targets an on-link IP with no ARP entry and the target answers the ARP request within 500 ms with that MAC
- **THEN** the datagram is sent; with no ARP answer in time it is refused with `TARGET_MAC_CHANGED`

#### Scenario: Routed target
- **WHEN** a write grant covers `10.20.0.5`, which the device reaches through its default gateway, and the write carries no `mac`
- **THEN** the write runs on IP, port and module matching alone

#### Scenario: MAC on a routed target
- **WHEN** a request carries `mac` and its target is off-link at run time
- **THEN** it is refused with `TARGET_MAC_CHANGED` before any socket opens

#### Scenario: Declared read on a swapped device
- **WHEN** an XWEB collector step POSTs with `declared_read` and the job target `mac`, and another device now answers at the XWEB IP
- **THEN** the POST is refused with `WRITE_NOT_ALLOWED` and `TARGET_MAC_CHANGED` before any byte is sent

#### Scenario: Secret on a GET without a MAC
- **WHEN** an on-link GET expands `${secret:t}` and carries no `mac`
- **THEN** it is refused with `SECRET_TARGET_MISMATCH` and `MAC_REQUIRED` before any socket opens

#### Scenario: Plain read with a MAC
- **WHEN** a Modbus read carries the target `mac`
- **THEN** the ARP entry is compared before the first byte, and a different MAC is refused with `TARGET_MAC_CHANGED`

### Requirement: Modbus write verification
A `modbus_tcp` write with `verify: true` SHALL, after the write succeeds and within the same deadline, read the same address and count back (function 1 for `co`, 3 for `hr`) and return the values in `readback`. A mismatch SHALL produce `FAILED` with reasonCode `READBACK_MISMATCH`, the written count and the read-back values in `reason_detail`. `verify` SHALL default to false.

#### Scenario: Verified write
- **WHEN** a granted write with the target `mac` sets `u1/hr100` to `[215]` with `verify: true` and the server stores it
- **THEN** the result is `OK` with `written: 1` and `readback: [215]`

#### Scenario: Server ignores the write
- **WHEN** the server acknowledges the write but reads back `[200]`
- **THEN** the result is `FAILED` with `READBACK_MISMATCH`

#### Scenario: Read-back times out
- **WHEN** the write succeeds and the read-back gets no reply before the deadline
- **THEN** the result is `FAILED` with `TIMEOUT` and `reason_detail` states that the write was acknowledged

### Requirement: Template expansion before send
Templated fields (`http`/`https` path, header values and text `body`; `tcp` text `payload`) SHALL be expanded after validation and before classification and the grant check, so grants always match the expanded request, and before any socket opens. A placeholder SHALL be `${name}` or `${name:arg}` (`name` `[a-z_]{1,16}`, `arg` `[A-Za-z0-9_.-]{1,32}`), with at most 3 encodings inside the braces before the closing `}` (`${name:arg|url|b64}`), applied left to right: `|url` (RFC 3986 percent-encoding of everything but unreserved characters), `|json` (JSON string escaping without quotes), `|b64` (standard base64 with padding). `$${` SHALL produce a literal `${`. Names SHALL resolve only through the resolver chain the caller passes to that `expand()` call. The engine SHALL hold no global or persistent resolver registry, so a resolver passed by one caller is never visible to an expansion made by another caller; this capability passes none. Unknown names, malformed placeholders, unknown encodings or a resolver failure SHALL return `REJECTED` with `BAD_TEMPLATE`. The expanded request SHALL be re-validated against every limit (path rules, header value rules, the one-send request limit); a violation SHALL return `REJECTED` with `BAD_ARGS`. Values marked sensitive by their resolver SHALL never be logged or echoed in a result, and the expansion scratch buffers SHALL be zeroized before release. Untemplated fields (`ip`, `port`, `method`, header names, `body_b64`, `payload_b64`, Modbus fields, `cert_sha256`) SHALL be taken literally.

#### Scenario: Unknown placeholder
- **WHEN** an `http` body contains `${foo}` and no resolver named `foo` is in the chain passed to `expand()`
- **THEN** the request is `REJECTED` with `BAD_TEMPLATE` and no socket opens

#### Scenario: Resolver of another caller not visible
- **WHEN** one caller expands a request with a chain that resolves `cap`, and another caller then expands a request containing `${cap:tok}` with a chain that does not
- **THEN** the second request is `REJECTED` with `BAD_TEMPLATE`, no socket opens and the value from the first caller is never expanded

#### Scenario: Escaped literal
- **WHEN** a body contains `$${x}`
- **THEN** the device sends `${x}`

#### Scenario: Expansion breaks a header value
- **WHEN** a header value expands to text containing CR or LF
- **THEN** the request is `REJECTED` with `BAD_ARGS`

#### Scenario: Expansion exceeds the send limit
- **WHEN** the expanded HTTP or HTTPS request exceeds 5760 bytes of plaintext
- **THEN** the request is `REJECTED` with `BAD_ARGS`

#### Scenario: Grant matched on the expanded path
- **WHEN** a grant allows `/rpc/Switch.Set` and a POST has path `${var:p}` that a resolver expands to `/rpc/Sys.Reboot`
- **THEN** the request is `REJECTED` with `WRITE_NOT_ALLOWED`

#### Scenario: Untemplated field
- **WHEN** `body_b64` decodes to bytes containing `${secret:x}`
- **THEN** the bytes are sent unchanged

### Requirement: Raw TCP module
The `tcp` module SHALL connect to the target, send the payload (`payload` text or `payload_b64`, 0-5760 bytes, one non-blocking write) and read until the peer closes, `max_bytes` is reached, `idle_ms` (default 300, clamped 50-5000) passes without a byte after the first one, or the deadline expires. Zero bytes received SHALL be `FAILED` with `NO_REPLY`. `truncated` SHALL be true when `max_bytes` was hit or the deadline cut a reply that was still arriving.

#### Scenario: Request and reply
- **WHEN** a granted `tcp` request with the target `mac` sends a 12-byte payload and the peer answers 80 bytes and goes quiet
- **THEN** the result is `OK` with the 80 bytes `idle_ms` after the last byte, `truncated: false`

#### Scenario: Silent peer
- **WHEN** the peer accepts the connection and never sends
- **THEN** the result is `FAILED` with `NO_REPLY` at the deadline

#### Scenario: Payload too large
- **WHEN** `payload_b64` decodes to 6000 bytes
- **THEN** the request is `REJECTED` with `BAD_ARGS`

### Requirement: HTTPS module behind a bench gate with certificate pinning
The `https` module SHALL be enabled on a product only after the bench feasibility measurement passes on that product. While disabled, `https` requests and `tls_probe` SHALL return `REJECTED` with `MODULE_UNAVAILABLE`. When enabled it SHALL run HTTP over TLS 1.2 or later to the IP literal without SNI and without CA or hostname validation, SHALL accept at most 5760 bytes of plaintext request (TLS records may take several sends, all inside the deadline), and SHALL require `cert_sha256` (64 lowercase hex, SHA-256 of the leaf certificate DER); a missing or malformed value SHALL be `REJECTED` with `BAD_ARGS`, a leaf that does not match SHALL be `FAILED` with `TLS_PIN_MISMATCH` before any request byte is sent, and a handshake failure SHALL be `FAILED` with `TLS_FAILED`. There is no trust on first use. The TCP connect, the whole TLS handshake and every later socket wait SHALL be bounded by the remaining executor deadline: the handshake loop SHALL check the absolute deadline on every `WANT_READ`/`WANT_WRITE` and end with `FAILED` `TIMEOUT` when it passes, so an `https` step never runs longer than its `timeout_ms`. While a LAN tunnel session is active, `https` requests and `tls_probe` SHALL return `FAILED` with `NO_MEMORY` and reason_detail `TUNNEL_ACTIVE` without starting a handshake. Before each handshake the module SHALL check the internal heap against the threshold fixed by the bench task and return `FAILED` with `NO_MEMORY` below it. `tls_probe` SHALL complete a handshake, send nothing, and return the leaf `cert_sha256`, subject CN (at most 64 bytes) and `not_after` (Unix seconds).

#### Scenario: Gate not passed
- **WHEN** the bench measurement has not passed for the product and an `https_request` arrives
- **THEN** the device publishes `REJECTED` with `MODULE_UNAVAILABLE`

#### Scenario: Pin matches
- **WHEN** an enabled `https` request carries the leaf's correct `cert_sha256`
- **THEN** the request is sent and the result has the same shape as `http`

#### Scenario: Certificate replaced
- **WHEN** the LAN device presents a different self-signed certificate
- **THEN** the result is `FAILED` with `TLS_PIN_MISMATCH` and no request byte leaves the device

#### Scenario: Server stalls the handshake
- **WHEN** a server accepts the TCP connection and never answers the ClientHello, and the request has `timeout_ms: 3000`
- **THEN** the result is `FAILED` with `TIMEOUT` within 3000 ms plus one poll interval, and the TLS context is freed

#### Scenario: Tunnel session open
- **WHEN** a LAN tunnel session is active and an `https_request` or `tls_probe` arrives
- **THEN** the result is `FAILED` with `NO_MEMORY` and `TUNNEL_ACTIVE`, no handshake starts and the tunnel session continues unaffected

#### Scenario: Low internal heap
- **WHEN** internal free heap is below the bench threshold at request start
- **THEN** the result is `FAILED` with `NO_MEMORY` and no handshake starts

#### Scenario: Commissioning probe
- **WHEN** `tls_probe` targets a self-signed LAN device on 443
- **THEN** the result carries its `cert_sha256`, subject CN and `not_after`

### Requirement: Checks complete before any byte is sent
The executor SHALL apply, in order, argument and address validation, target rules, module availability (including the HTTPS tunnel and heap checks), template expansion, the post-expansion limit checks, read/write classification, the `writes: "deny"` check, the grant check on the expanded request and the `MAC_REQUIRED`/off-link MAC check, and SHALL open a socket only when all pass. The only check after the socket opens SHALL be the one MAC comparison of `Target MAC binding`, which runs before the first byte is sent. Every write that passes SHALL be logged at INFO with caller id, module, the entry that admitted it, target and canonical address (template text, never expanded values); reads, which include every collector poll, SHALL be logged the same way at DEBUG. A refusal or failure SHALL be logged at DEBUG with its reasonCode and reason_detail; the caller reports it (the command status, the collector health), so a failing poll never logs every interval.

#### Scenario: Write to a public address
- **WHEN** a write without a grant targets `8.8.8.8`
- **THEN** the result is `REJECTED` with `TARGET_NOT_PRIVATE`, since target rules run before the grant check

#### Scenario: Audit of a granted write
- **WHEN** a granted Modbus write runs
- **THEN** an INFO line names the caller id, `modbus_tcp`, `write`, the grant entry, the target and `u1/hr100`
