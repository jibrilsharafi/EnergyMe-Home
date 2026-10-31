## Purpose

Core LAN executor and protocol modules shared by one-shot gateway commands and scheduled collection: one request/result contract, canonical native addresses used the same way for read and write, a template expansion stage, and device-side write grants and read declarations that keep every LAN target read-only unless a write was explicitly granted.

## ADDED Requirements

### Requirement: One module interface with a structured result
Every LAN protocol SHALL be implemented as a module behind one executor interface. A request SHALL carry `module` (`http`, `https`, `modbus_tcp`, `tcp`, `udp`), `target` `{ip, port}`, an optional `access` (`read` or `write`, which can only raise the class, see `Every operation is classed read or write`), `writes` (`allow`, the default, or `deny`; see `Read-only callers`), `timeout_ms` (100-10000, one absolute deadline covering connect, TLS handshake, send and the whole reply), `max_bytes` (1-131072) and the module's own fields. The executor SHALL return a structured result with `status` (`OK`, `FAILED` or `REJECTED`), `reason_code` and `reason_detail` (empty on `OK`), `proto_status` (HTTP status code, Modbus exception code, otherwise 0), `headers` (raw HTTP status line and header block, at most 2048 bytes, empty for other modules), exactly one payload (`body` bytes for `http`, `https`, `tcp`, `udp`; `registers` values for a Modbus read; `written` count for a Modbus write), `readback` (Modbus write with `verify`), `truncated`, `bytes_tx`, `bytes_rx`, `connected` (true once the TCP connect completed for `http`, `https`, `modbus_tcp` and `tcp`, and once the datagram was sent for `udp`; false for any result that ended before that, whatever its reason code), `address` (the canonical address actually used; for `http` and `https` it SHALL be the method and the path template text before `?`, unexpanded and with the query dropped, so it never holds an expanded value) and `timing` `{connect_ms, first_byte_ms, total_ms}`. A chunked HTTP reply SHALL be decoded while it is received, straight into the `max_bytes` body buffer: `max_bytes` and `truncated` SHALL apply to decoded body bytes only, chunk-size lines SHALL NOT count against `max_bytes`, and a chunk-size line (size, extensions and CRLF) longer than 128 bytes or not valid hex SHALL be `FAILED` with `BAD_RESPONSE`. Modules SHALL contain protocol logic only, never knowledge of a vendor, model or product. Result buffers SHALL be allocated in PSRAM.

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
The executor SHALL class each request after template expansion and before any socket opens, from the module rules and the stored read declarations (entries with `kind: "read"`, see `Write grants and read declarations`), never from the caller's word alone. `modbus_tcp` functions 1-4 SHALL be read and 5, 6, 15, 16 write, and an `access` that contradicts the function SHALL be rejected with `BAD_ARGS`. `http` and `https`: a GET whose expanded `body` and decoded `body_b64` are both absent or empty SHALL be read, and a GET carrying a non-empty `body` or `body_b64` SHALL be write, since a server may take the operation from the body whatever the method; POST, PUT, PATCH and DELETE SHALL be read only when a read declaration with the same `ip`, `port` and `module` lists the canonical address `<METHOD> <path>` of the request exactly, the request method and the expanded path before `?`, and write otherwise; another method to a declared path SHALL be write. The request body SHALL NOT take part in the match. A request carrying a header named `X-HTTP-Method-Override`, `X-HTTP-Method` or `X-Method-Override` (case-insensitive) SHALL be refused with `BAD_ARGS` at argument validation, next to the framing headers `lan-gateway` refuses, because such a header lets the server run another method than the one classed. `tcp`: an empty payload SHALL be read (banner read); a non-empty payload, and every `udp` payload, SHALL be read only when the SHA-256 of the exact bytes to be sent (after expansion or base64 decoding) equals a hash in a read declaration for the same `ip`, `port` and `module`, and write otherwise. A request with `access: "write"` SHALL be classed write whatever the rules above give; `access: "read"` SHALL be accepted and SHALL NOT lower the class. A GET that changes state (for example Shelly `GET /rpc/Switch.Set?on=false`) is classed read unless the caller sends `access: "write"`; this residual is a documented cloud responsibility, and the cloud control path always sends `access: "write"`. A declared method and path is classed read whatever body it carries, so an endpoint whose request body selects the operation (JSON-RPC style, for example Shelly Gen2 `POST /rpc` with `{"method":"Switch.Set"}`) SHALL NOT be declared read; this residual is also a cloud responsibility, enforced by the tool that writes read declarations. `ping`, `net_scan` and `tls_probe` are diagnostics and SHALL NOT be classed.

#### Scenario: Declared-read POST to a declared path
- **WHEN** a read declaration `{ip:"192.168.2.40", port:80, module:"http", kind:"read", mac:"00:11:22:33:44:55", allow:["POST /cgi-bin/login.lua","POST /cgi-bin/datachart.cgi"]}` is stored and an `http` POST to `192.168.2.40:80` `/cgi-bin/datachart.cgi` arrives from that MAC
- **THEN** it is classed read, needs no write grant and runs

#### Scenario: POST to an undeclared path claims read
- **WHEN** the same declaration is stored and an `http` POST to `/cgi-bin/setpoint.cgi` carries `access: "read"`
- **THEN** it is classed write and, with no write grant, returns `REJECTED` with `WRITE_NOT_ALLOWED` and opens no socket

#### Scenario: Undeclared POST
- **WHEN** no read declaration covers the target and an `http` POST carries no `access`
- **THEN** it is classed write

#### Scenario: Another method to a declared path
- **WHEN** the declaration above is stored and an `http` DELETE, PUT or PATCH goes to `/cgi-bin/datachart.cgi` with no `access`
- **THEN** it is classed write and, with no write grant, returns `REJECTED` with `WRITE_NOT_ALLOWED` and opens no socket

#### Scenario: Body is not part of the match
- **WHEN** the declaration above is stored and a POST to `/cgi-bin/datachart.cgi` carries a body different from every earlier one
- **THEN** it is classed read; the body is never inspected, which is why an operation-in-body endpoint must not be declared read

#### Scenario: GET with a body
- **WHEN** an `http` GET to a Shelly target `/rpc` carries the body `{"method":"Switch.Set","params":{"id":0,"on":false}}` and `writes: "deny"`
- **THEN** it is classed write and returns `REJECTED` with `WRITE_NOT_ALLOWED` and `WRITES_DENIED`, and no socket opens

#### Scenario: Method-override header on a declared POST
- **WHEN** the declaration above is stored and a POST to `/cgi-bin/datachart.cgi` carries the header `X-HTTP-Method-Override: DELETE`
- **THEN** the executor returns `REJECTED` with `BAD_ARGS` and opens no socket

#### Scenario: Declaration for another port or module
- **WHEN** the declaration above is stored and a POST to `/cgi-bin/datachart.cgi` targets port 8080, or uses module `https` on port 80
- **THEN** it is classed write

#### Scenario: Query string outside the match
- **WHEN** the declaration above is stored and a POST goes to `/cgi-bin/datachart.cgi?lang=en`
- **THEN** it is classed read

#### Scenario: GET raised to write
- **WHEN** an `http` GET to `/rpc/Switch.Set?id=0&on=false` carries `access: "write"`
- **THEN** it is classed write and needs a write grant

#### Scenario: Modbus write declared read
- **WHEN** a `modbus_tcp` request with function 6 carries `access: "read"`
- **THEN** the executor returns `REJECTED` with `BAD_ARGS`

#### Scenario: Empty TCP payload
- **WHEN** a `tcp` request has no payload
- **THEN** it is classed read and only reads what the peer sends

#### Scenario: Fixed UDP query declared read
- **WHEN** a read declaration for `192.168.2.60:47808` `udp`, bound to the target's MAC, holds the SHA-256 of a 17-byte query frame and a `udp` send carries exactly those 17 bytes
- **THEN** it is classed read; the same frame with one byte changed is classed write

#### Scenario: Templated TCP payload
- **WHEN** a `tcp` payload contains `${now}` and a read declaration holds the hash of one earlier expansion
- **THEN** the expansion differs, the request is classed write and, without a write grant, returns `WRITE_NOT_ALLOWED`

### Requirement: Read-only callers
A request with `writes: "deny"` SHALL be refused with `REJECTED`, reasonCode `WRITE_NOT_ALLOWED` and reason_detail `WRITES_DENIED` whenever the executor classes it write, after classification and before the grant check, whatever write grants are stored and before any socket opens. A request with `writes: "deny"` that is classed read SHALL run as usual. The scheduled jobs of add-lan-collector SHALL set `writes: "deny"` on every login and data step, so classification at run time (after expansion, against the read declarations stored at that moment) decides, not only the check made when the job set was applied. The one-shot command adapter SHALL use `writes: "allow"`, and SHALL NOT read `writes` from IoT Command arguments.

#### Scenario: Job step expands to a granted write path
- **WHEN** a write grant bound to the target's MAC allows `["*"]` on an `http` target and a request with `writes: "deny"` POSTs to an undeclared path that a `${var:p}` placeholder expanded at run time
- **THEN** it is classed write and returns `REJECTED` with `WRITE_NOT_ALLOWED` and `WRITES_DENIED`, and no socket opens

#### Scenario: Read declaration revoked after apply
- **WHEN** a job step POSTs to `/cgi-bin/datachart.cgi` with `writes: "deny"`, the read declaration listing `POST /cgi-bin/datachart.cgi` is removed and a write grant `["*"]` covers the target
- **THEN** the next run classes the step write and refuses it with `WRITES_DENIED`

#### Scenario: Whole-target UDP grant and a job
- **WHEN** a `udp` write grant `["*"]` covers a target and a request with `writes: "deny"` sends a payload whose hash is not declared read
- **THEN** it returns `REJECTED` with `WRITE_NOT_ALLOWED` and `WRITES_DENIED` and no datagram is sent

#### Scenario: Read request with writes denied
- **WHEN** a request with `writes: "deny"` is a Modbus read, a GET without a body, or a POST whose method and path a MAC-bound read declaration lists
- **THEN** it runs as it would with `writes: "allow"`

#### Scenario: Command carries a writes argument
- **WHEN** an `http_request` IoT Command carries `writes: "deny"` or `writes: "allow"` in its args
- **THEN** the argument has no effect, the adapter passes `writes: "allow"` and the result is the same as without it

### Requirement: Write grants and read declarations
Every target SHALL be read-only unless a write grant matches. Write grants and read declarations SHALL share one list of at most 8 entries, replaced as a whole, holding no secrets, at most 3072 bytes as compact JSON and persisted as one fixed-layout NVS blob of at most 1665 bytes in the gateway namespace. An entry SHALL hold `ip` (private IPv4 literal), `port` (1-65535, exact), `module`, `kind` (`write`, the default when absent, or `read`), an optional `mac` (see `Target MAC binding`) and an `allow` list of 1-4 strings:
- `http`, `https`: strings of at most 48 bytes, matched against the expanded request path before `?`. In a `write` entry each string is a path that matches exactly, or as a prefix when it ends in `*`, and `["*"]` alone grants the whole target. In a `read` entry each string SHALL be a canonical HTTP address `<METHOD> <path>`, with METHOD one of `POST`, `PUT`, `PATCH` or `DELETE` (a GET without a body is read already, and a GET with a body cannot be declared read) and one space; it SHALL match the request method and path exactly and SHALL NOT contain `*`.
- `modbus_tcp`: `write` entries only, each string `u<unit>/<table><start>:<count>` with an optional `=<min>..<max>` suffix, at most 48 bytes, `table` `co` or `hr`, `start + count` at most 65536, `min` and `max` raw 0-65535 (coils always 0-1); every written address SHALL fall inside one range of the same unit and table, and every written value inside its bounds. A `modbus_tcp` entry with `kind: "read"` is invalid, since function codes already class Modbus.
- `tcp`, `udp`: each string the 64-lowercase-hex SHA-256 of an exact payload, stored as 32 bytes binary. In a `write` entry `["*"]` alone grants any payload to the target; a `read` entry SHALL hold hashes only. Only payloads that expand to identical bytes can match a hash, so in practice only fixed frames do; BACnet/IP writes wait for a dedicated `bacnet` module.

Every non-GET HTTP request path, every HTTP request path sent with `access: "write"`, and every HTTP `allow` path (the part after the method in a `read` entry) SHALL contain no `.` or `..` segment, no `//`, no backslash and no `%2e`, `%2f` or `%5c` in any case; a request path that violates this SHALL be refused with `BAD_ARGS` before classification, so a declaration or prefix grant cannot be escaped by dot segments or encoded separators. A request classed write with no matching `write` entry SHALL be refused with `REJECTED` and reasonCode `WRITE_NOT_ALLOWED` before any socket opens; a matching Modbus range with a value outside its bounds SHALL be refused with `WRITE_OUT_OF_RANGE`. The reason_detail of either refusal SHALL name the target and the result `address` (for `http` and `https` the template text before `?`), never the expanded path or query. Entries SHALL be checked at request start; a change does not abort a running request. The list SHALL be cleared by factory reset and SHALL NOT bypass the target rules of `lan-gateway` (private IPv4, not the device itself). It protects against cloud bugs, wrong templates and fat-fingers, not a hostile cloud principal, which can change it; every change is logged at WARNING and visible in the archived `system` shadow history.

#### Scenario: Modbus write without a grant
- **WHEN** an enabled device with no grants receives a write to `192.168.2.50:502` `u1/hr100`
- **THEN** it returns `REJECTED` with `WRITE_NOT_ALLOWED` naming the target and the canonical address, and opens no socket

#### Scenario: Refused POST with a sensitive value in its query
- **WHEN** a caller's chain resolves `${secret:t}` with a sensitive resolver and an `http` POST to the undeclared path `/x?k=${secret:t}` arrives with no write grant
- **THEN** it returns `REJECTED` with `WRITE_NOT_ALLOWED`, `address` is `POST /x`, and neither `address` nor reason_detail contains the resolved value

#### Scenario: Write inside a granted range
- **WHEN** a grant has `{ip:"192.168.2.50", port:502, module:"modbus_tcp", mac:"00:11:22:33:44:66", allow:["u1/hr100:2=0..400"]}`, the target answers ARP with that MAC and a write sets `u1/hr100` to `[215, 300]`
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
- **WHEN** a write grant bound to the target's MAC has `allow: ["/rpc/Switch.*"]` and a POST goes to `/rpc/Switch.Set?id=0&on=true`
- **THEN** the request is sent; a POST to `/rpc/Sys.Reboot` is refused with `WRITE_NOT_ALLOWED`

#### Scenario: Prefix escape attempt
- **WHEN** the same grant is in place and a POST, or a GET with `access: "write"`, goes to `/rpc/Switch./../Sys.Reboot`, `/rpc/Switch.%2e%2e/Sys.Reboot`, `/rpc//Switch.Set` or a path containing a backslash
- **THEN** the request is `REJECTED` with `BAD_ARGS` and no socket opens

#### Scenario: Read declaration escape attempt
- **WHEN** a read declaration lists `POST /cgi-bin/datachart.cgi` and a POST goes to `/cgi-bin/datachart.cgi/../setpoint.cgi`
- **THEN** the request is `REJECTED` with `BAD_ARGS` and no socket opens

#### Scenario: UDP payload grant
- **WHEN** a write grant for `192.168.2.61:4210` `udp`, bound to the target's MAC, holds the hash of one 12-byte command frame and a `udp_send` carries that frame
- **THEN** the datagram is sent; any other payload is refused with `WRITE_NOT_ALLOWED`

#### Scenario: Whole-target raw grant
- **WHEN** a write grant for an on-link `udp` target, bound to its MAC, has `allow: ["*"]`
- **THEN** any payload to that target is sent

#### Scenario: Reads need no grant
- **WHEN** no grant exists and a request is classed read
- **THEN** it runs subject only to the target rules

#### Scenario: Invalid grant list
- **WHEN** a new list has 9 entries, a public IP, a `di` range, a `modbus_tcp` entry with `kind: "read"`, a `read` entry containing `*`, an HTTP `read` string without a method or with method `GET`, a `tcp` entry with a 63-hex hash or with `"*"` next to a hash, a path without a leading `/` or with a `..` segment, an HTTP or Modbus `allow` string over 48 bytes, a range past address 65535, `min` above `max`, an unknown `kind`, a malformed `mac`, or more than 3072 bytes of compact JSON
- **THEN** the whole list is refused and the stored list stays unchanged

### Requirement: Target MAC binding
An entry MAY carry `mac` (six lowercase hex pairs separated by `:`; broadcast, multicast and all-zero values are invalid). The executor SHALL decide whether a target is on-link at run time from the interface lwIP routes the target through (`ip4_route`) and that interface's address and netmask, so Home Pro's Ethernet and WiFi interfaces are handled alike. When a `write` entry admits a write, or a `read` entry classes a non-GET HTTP request or a raw payload read, and the target is on-link, the entry SHALL carry `mac`; otherwise the request SHALL be refused with `REJECTED`, reasonCode `WRITE_NOT_ALLOWED` and reason_detail `MAC_REQUIRED` before any socket opens. With `mac` present the executor SHALL compare it with the target's ARP entry under the lwIP core lock: for `http`, `https`, `modbus_tcp` and `tcp` after the TCP connect and before the first byte (for `https` before the ClientHello); for `udp` before the send, first sending one ARP request and polling for at most `min(500 ms, remaining deadline)` when no entry exists. A missing or different MAC, or an entry with `mac` whose target is off-link at run time, SHALL be refused with `REJECTED`, reasonCode `WRITE_NOT_ALLOWED` and reason_detail `TARGET_MAC_CHANGED`, closing the socket without sending a byte. Off-link (routed) targets cannot be MAC-bound: their entries omit `mac` and match by IP, port and module only. Requests that need no entry (GET, Modbus reads, empty `tcp` payloads) SHALL NOT be MAC-checked against a grant or read declaration; a resolver MAY still require the same ARP compare at the same point (after connect, before the first byte) for the request it expanded, which add-gateway-secrets uses for MAC-bound secrets on any request, GET included. The setter SHALL NOT decide on-link status, because the network can change after a list is stored.

#### Scenario: Different device at a granted IP
- **WHEN** a grant binds `192.168.2.50:502` to `00:11:22:33:44:66`, the controller is renumbered and another device with MAC `00:aa:bb:cc:dd:ee` takes `192.168.2.50` and answers on 502
- **THEN** a write to `u1/hr100` connects, finds the different ARP entry, returns `REJECTED` with `WRITE_NOT_ALLOWED` and `TARGET_MAC_CHANGED`, and sends no Modbus byte

#### Scenario: On-link grant without a MAC
- **WHEN** a write grant for on-link `192.168.2.50` has no `mac` and a write arrives
- **THEN** the request is `REJECTED` with `WRITE_NOT_ALLOWED` and `MAC_REQUIRED` and no socket opens

#### Scenario: UDP target not yet in the ARP table
- **WHEN** a granted `udp` send targets a bound on-link IP with no ARP entry and the target answers the ARP request within 500 ms with the bound MAC
- **THEN** the datagram is sent; with no ARP answer in time it is refused with `TARGET_MAC_CHANGED`

#### Scenario: Routed target
- **WHEN** a write grant without `mac` covers `10.20.0.5`, which the device reaches through its default gateway
- **THEN** the write runs on IP, port and module matching alone

#### Scenario: Bound entry, target now routed
- **WHEN** an entry with `mac` covers a target that a subnet change has made off-link
- **THEN** a request relying on it is refused with `TARGET_MAC_CHANGED`

#### Scenario: Read declaration on a swapped device
- **WHEN** the XWEB read declaration is bound to the XWEB MAC and another device now answers at its IP
- **THEN** a POST to `/cgi-bin/datachart.cgi` is refused with `WRITE_NOT_ALLOWED` and `TARGET_MAC_CHANGED` before any byte is sent

### Requirement: Modbus write verification
A `modbus_tcp` write with `verify: true` SHALL, after the write succeeds and within the same deadline, read the same address and count back (function 1 for `co`, 3 for `hr`) and return the values in `readback`. A mismatch SHALL produce `FAILED` with reasonCode `READBACK_MISMATCH`, the written count and the read-back values in `reason_detail`. `verify` SHALL default to false.

#### Scenario: Verified write
- **WHEN** a write granted by a MAC-bound entry sets `u1/hr100` to `[215]` with `verify: true` and the server stores it
- **THEN** the result is `OK` with `written: 1` and `readback: [215]`

#### Scenario: Server ignores the write
- **WHEN** the server acknowledges the write but reads back `[200]`
- **THEN** the result is `FAILED` with `READBACK_MISMATCH`

#### Scenario: Read-back times out
- **WHEN** the write succeeds and the read-back gets no reply before the deadline
- **THEN** the result is `FAILED` with `TIMEOUT` and `reason_detail` states that the write was acknowledged

### Requirement: Template expansion before send
Templated fields (`http`/`https` path, header values and text `body`; `tcp` text `payload`) SHALL be expanded after validation and before classification and the grant check, so declarations and grants always match the expanded request, and before any socket opens. A placeholder SHALL be `${name}` or `${name:arg}` (`name` `[a-z_]{1,16}`, `arg` `[A-Za-z0-9_.-]{1,32}`) followed by at most 3 encodings applied left to right: `|url` (RFC 3986 percent-encoding of everything but unreserved characters), `|json` (JSON string escaping without quotes), `|b64` (standard base64 with padding). `$${` SHALL produce a literal `${`. Names SHALL resolve only through the resolver chain the caller passes to that `expand()` call. The engine SHALL hold no global or persistent resolver registry, so a resolver passed by one caller is never visible to an expansion made by another caller; this capability passes none. Unknown names, malformed placeholders, unknown encodings or a resolver failure SHALL return `REJECTED` with `BAD_TEMPLATE`. The expanded request SHALL be re-validated against every limit (path rules, header value rules, the one-send request limit); a violation SHALL return `REJECTED` with `BAD_ARGS`. Values marked sensitive by their resolver SHALL never be logged or echoed in a result, and the expansion scratch buffers SHALL be zeroized before release. Untemplated fields (`ip`, `port`, `method`, header names, `body_b64`, `payload_b64`, Modbus fields, `cert_sha256`) SHALL be taken literally.

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

#### Scenario: Declaration matched on the expanded path
- **WHEN** a read declaration lists `POST /cgi-bin/datachart.cgi` and a POST has path `${var:p}` that expands to `/cgi-bin/setpoint.cgi`
- **THEN** the request is classed write and, without a write grant, `REJECTED` with `WRITE_NOT_ALLOWED`

#### Scenario: Untemplated field
- **WHEN** `body_b64` decodes to bytes containing `${secret:x}`
- **THEN** the bytes are sent unchanged

### Requirement: Raw TCP module
The `tcp` module SHALL connect to the target, send the payload (`payload` text or `payload_b64`, 0-5760 bytes, one non-blocking write) and read until the peer closes, `max_bytes` is reached, `idle_ms` (default 300, clamped 50-5000) passes without a byte after the first one, or the deadline expires. Zero bytes received SHALL be `FAILED` with `NO_REPLY`. `truncated` SHALL be true when `max_bytes` was hit or the deadline cut a reply that was still arriving.

#### Scenario: Request and reply
- **WHEN** a `tcp` request whose 12-byte payload is granted or declared read by an entry bound to the target's MAC sends it and the peer answers 80 bytes and goes quiet
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
The executor SHALL apply, in order, argument and address validation, target rules, module availability (including the HTTPS tunnel and heap checks), template expansion, the post-expansion limit and path checks, read/write classification, the `writes: "deny"` check, the grant check on the expanded request and the `MAC_REQUIRED` check, and SHALL open a socket only when all pass. The only checks after the socket opens SHALL be the MAC comparisons of `Target MAC binding` (for the admitting entry, and any compare a resolver required), which run before the first byte is sent. Every request that passes SHALL be logged at INFO with caller id, module, access class, the entry that admitted it (if any), target and canonical address (template text, never expanded values); every refusal or failure SHALL be logged at WARNING with its reasonCode and reason_detail.

#### Scenario: Write to a public address
- **WHEN** a write without a grant targets `8.8.8.8`
- **THEN** the result is `REJECTED` with `TARGET_NOT_PRIVATE`, since target rules run before the grant check

#### Scenario: Audit of a granted write
- **WHEN** a granted Modbus write runs
- **THEN** an INFO line names the caller id, `modbus_tcp`, `write`, the grant entry, the target and `u1/hr100`
