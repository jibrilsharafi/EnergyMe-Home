## Context

See proposal.md for motivation. Current code that shapes the design:

- One function per op, dispatched by a switch in `_run` (`source/src/gateway.cpp:940-956`); each builds the command JSON itself with `_succeed/_fail/_reject` (`gateway.cpp:48-70`). Nothing is reusable by a scheduler.
- `_runHttp` (`gateway.cpp:398`) parses the header block only for status, Content-Length and chunking, then frees it (`gateway.cpp:441-494`): no cookie or token can leave the function. Header cap 2048 B and payload cap 4096 B (`source/lib/gateway_logic/gateway_logic.h:22-23`), raw receive buffer `HTTP_RAW_CAP` (`gateway.cpp:258`).
- A request must fit one non-blocking send of `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` = 5760 B (`gateway.cpp:255`, `_connectAndSend` at `gateway.cpp:241`).
- One slot: `_busy` CAS (`gateway.cpp:41`, `1087`) and a depth-1 queue (`gateway.cpp:1013`); the gateway task has an 8 KB internal stack (`source/include/gateway.h:23`). The MQTT task validates and submits (`source/src/mqtt.cpp:1553-1580`) and publishes the result (`mqtt.cpp:1678`).
- Modbus args take `unit` 0-255, `function`, `address` 0-65535 (`gateway.cpp:663-690`); framing and parsing are already pure in `gateway_logic`.
- `udp_send` exists (`gateway.cpp:566`); there is no raw TCP op.
- mbedTLS allocations go to PSRAM from boot (`source/src/main.cpp:121`, `mbedtls_platform_set_calloc_free`). add-lan-tunnel `design.md:8` states the opposite ("mbedTLS buffers are internal"); that claim is unverified, so HTTPS fit is measured, not assumed.
- Gateway NVS namespace `gateway_ns` (`source/include/constants.h:68`), REST switch at `source/src/customserver.cpp:3794`.
- Chunked replies are received raw into `HTTP_RAW_CAP` = header cap + 4096 + `HTTP_CHUNK_OVERHEAD_BYTES` 512 (`gateway.cpp:257-258`) and then decoded into a second `RESPONSE_CAP_BYTES` buffer (`gateway.cpp:457-465`, `GatewayLogic::decodeChunked` at `gateway_logic.h:83`).
- `net_scan` already reads the lwIP ARP table with `etharp_get_entry` and sends `etharp_request` under `LOCK_TCPIP_CORE` (`gateway.cpp:798-813`, `818-851`).
- An open tunnel session is `Tunnel::isActive()` (`tunnel.cpp:197`); it closes with `LOW_MEMORY` after `LOW_HEAP_GRACE_MS` 5 s (`tunnel_logic.h:322`) below `TUNNEL_LOW_HEAP` 30 KB internal free (`tunnel.h:46`).
- `NetworkClientSecure` sets `handshake_timeout = 120000` ms (`NetworkClientSecure.cpp:41`, `:69`), `setHandshakeTimeout()` takes whole seconds (`:450-451`), and `ssl_client.cpp:315-322` loops on the handshake until that timeout; the connect timeout covers TCP only. The installed esp32s3 sdkconfig has `CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE=y`.
- `[env:native]` (`source/platformio.ini:381-387`) has no `lib_deps`, so host tests cannot use ArduinoJson: everything host-tested must be dependency-free C++ in `lib/gateway_logic`.

## Goals / Non-Goals

**Goals:**
- One executor and one result type used by gateway commands now and by add-lan-collector jobs later, with no change to command output by default.
- Symmetric, canonical addresses so cloud control (ADR-035) and later local rules can target the same strings the decoder uses.
- Writes refused by the device unless explicitly granted, with the read/write class decided by stored configuration, never by a per-request "read" flag.

**Non-Goals:**
- Scheduling, captures, sessions, the `collector` shadow and the change to `GATEWAY_BUSY` (add-lan-collector).
- Secret storage and the `${secret:x}` resolver (add-gateway-secrets).
- Digest auth (deferred until a target needs it), BACnet, SNMP, local MQTT modules.
- Any vendor knowledge on the device. The tunnel (`lan-tunnel`) stays outside the executor and outside write grants.

## Decisions

### D1. Executor and module table in the gateway task
`GatewayExecutor::run(const ExecRequest&, ExecResult&)` runs synchronously in the gateway task. Modules register a static table entry `{id, validate, classify, run}`; no heap-allocated objects, no virtual dispatch needed. Callers are the command adapter (this change) and the collector scheduler (add-lan-collector), both in the gateway task, so no new task stack and no locking around module state.

Contract consumed by the other changes (field names are normative, the C++ layout is not):

| Request field | Notes |
|---|---|
| `module` | `http`, `https`, `modbus_tcp`, `tcp`, `udp` |
| `target.ip`, `target.port` | validated by `Gateway::validateTarget` (`gateway.h:49`) |
| `access` | optional `read` or `write`; `write` raises the class, `read` never lowers it, see D3 |
| `writes` | `allow` (default) or `deny`; with `deny` any request classed write is refused whatever the grants, see D3. Set by the collector on every step, never by the command adapter |
| `timeout_ms` | 100-10000, one absolute deadline covering connect, TLS handshake, send and reply |
| `max_bytes` | 1-131072 (`GATEWAY_BODY_MAX`), commands use 4096 |
| `http`/`https` | `method`, `path`*, `headers` (name to value*), `body`* or `body_b64`, `cert_sha256` (https) |
| `modbus_tcp` | `address` (canonical), `count` or `values`, `write_multiple`, `verify` |
| `tcp` | `payload`* or `payload_b64`, `idle_ms` |
| `udp` | `payload_b64`, `wait_ms` |
| `caller_id` | execution id or `job/<id>/<seq>`, for logs only |

`*` marks templated fields.

| Result field | Notes |
|---|---|
| `status` | `OK`, `FAILED`, `REJECTED` |
| `reason_code`, `reason_detail` | codes of lan-gateway plus `WRITE_NOT_ALLOWED`, `WRITE_OUT_OF_RANGE`, `BAD_TEMPLATE`, `MODULE_UNAVAILABLE`, `READBACK_MISMATCH`, `TLS_FAILED`, `TLS_PIN_MISMATCH`; new `reason_detail` values `MAC_REQUIRED`, `TARGET_MAC_CHANGED`, `WRITES_DENIED` (with `WRITE_NOT_ALLOWED`) and `TUNNEL_ACTIVE` (with `NO_MEMORY`) |
| `proto_status` | HTTP status, Modbus exception code, else 0 |
| `headers`, `headers_len` | raw status line + header block, PSRAM, at most 2048 B |
| `body`, `body_len` / `registers`, `count` / `written` | one payload per module |
| `readback` | Modbus `verify` only |
| `truncated`, `bytes_tx`, `bytes_rx` | |
| `connected` | true once the TCP connect completed (`udp`, once sent); lets the collector tell an unreachable target from a silent unit, because a timed-out connect is coded `TIMEOUT` like a silent reply |
| `address` | canonical echo; for HTTP the method and the path template before `?`, never expanded values |
| `timing.connect_ms`, `timing.first_byte_ms`, `timing.total_ms` | from executor start |

The result owns its PSRAM buffers and is released with `ExecResult::release()`. Alternative considered: keep per-op functions and add a second "structured" path for the collector. Rejected, two code paths for the same socket logic would drift.

### D2. Canonical addresses
Modbus `u<unit>/<table><addr>`, zero-based decimal, tables `co di ir hr`, count outside the address. Zero-based because it is what goes on the wire and what `gateway_logic` already uses; the 4xxxx convention is ambiguous across vendors (some 1-based, some not) and is converted in the cloud driver, once. The `u<unit>` prefix keeps the address equal to `source_device/native_point` of the ADR-034 timeseries id (`home/<dev>/src/src_9c1d/u3/hr3000`), so the device, decoder and control path share strings. Function codes derive from table and access (`write_multiple` forces 15/16 for devices that reject 5/6). HTTP addresses are `<METHOD> <path>`; they identify a request, not a point, because one HTTP reply usually carries many points that only the cloud driver can name. Parser and formatter live in `gateway_logic` and round-trip (`format(parse(s)) == s`).

### D3. Read/write classification and the threat model
Grants exist to stop cloud bugs, wrong templates and fat-fingers, not a hostile cloud principal, which can rewrite the list through the `system` shadow, the same accepted gap as add-lan-tunnel `design.md:68`. Those bugs are exactly what produces a wrong per-request flag (a job step copied from the XWEB read steps to a setpoint CGI keeps `access: "read"`), so the class never depends on the caller saying "read". The rule: Modbus by function code; HTTP GET without a body read, and a GET with a non-empty `body` or `body_b64` write (a server may take the operation from the body whatever the method, the same copy bug moved into a GET); HTTP POST/PUT/PATCH/DELETE read only when a stored read declaration (`kind: "read"`) for the same ip, port and module lists the canonical address `<METHOD> <path>` (D2) of the request exactly, method plus expanded path before `?`, else write, so a DELETE or PUT to a path declared for a POST read stays a write; TCP with an empty payload read; TCP with a payload and every UDP send read only when the SHA-256 of the exact bytes matches a read declaration, else write. Method-override headers (`X-HTTP-Method-Override`, `X-HTTP-Method`, `X-Method-Override`) are refused with `BAD_ARGS` like the framing headers, since they would let the server run a method other than the one classed. A per-request `access: "write"` raises any request to write; `access: "read"` is accepted for compatibility and ignored. Classification runs after expansion, so a templated path or payload is classed on what is actually sent.

Read declarations are exact method-and-path addresses and exact payload hashes, never prefixes or `*`, because a wildcard read declaration would reopen the per-request bypass for a whole subtree. The hash form fits fixed request/reply frames (BACnet/IP ReadProperty on 47808 before a `bacnet` module exists, proprietary TCP queries); a payload that carries time, a cursor or a secret expands differently each run, never matches, and is a write. Alternatives rejected: HTTP strictly by method (breaks both ways: the XWEB login and `datachart.cgi` are read-only POSTs, the Shelly `Switch.Set` is a mutating GET); per-request `access` as the authority (the original design, the bypass above); a read flag in the collector job document (the same bypass, set by the same cloud path); every HTTP request a write (forces grants for plain polling); tcp/udp grants for the whole target only (polling a request/reply protocol would need an unrestricted write grant that also admits any one-shot WriteProperty to a refrigeration or HVAC controller). The residual is the mutating GET: it is classed read unless the caller sends `access: "write"`, which the device cannot check; the cloud control path (ADR-035) always sends `access: "write"`. The second residual is body blindness. An HTTP read declaration binds method and path, never the body, so on a single-endpoint RPC API (Shelly Gen2 `POST /rpc` carries `Shelly.GetStatus`, `Switch.Set` and `Sys.Reboot` in the body) a declaration written for polling would class every operation read, the same copy bug as above moved into the body. Rule: a path whose request body selects the operation SHALL NOT be declared read; such a device is polled through its GET endpoints (Shelly `GET /rpc/Shelly.GetStatus`) or not at all. The device cannot enforce this, so the cloud tool that derives read declarations from a driver (the infra collector jobs tool) refuses to derive one for such an endpoint. A stricter alternative, an optional SHA-256 of the exact body in a read entry, would close the gap for fixed bodies but never matches a templated body (the XWEB `datachart.cgi` body carries the session id and the time window), so it is not the default; whether to add it is an open decision for Jibril.

Read-only callers. Grants authorize one-shot commands; add-lan-collector requires that they never apply to scheduled jobs, and its apply-time check alone cannot guarantee that. A step can expand differently at run time (`${var:x}` or `${cap:x}` in a path or payload), and a read declaration can be revoked or replaced after the job set was applied. Either way the step is classed write at run time, and a matching write grant (a Shelly `["*"]` path grant, a `udp` `["*"]` grant) would admit it every `interval_s`, unaudited. So the request carries `writes`: with `deny` the executor refuses any request classed write with `REJECTED` / `WRITE_NOT_ALLOWED`, reason_detail `WRITES_DENIED`, right after classification and before the grant check, so no grant is ever consulted and no socket opens. A request classed read (Modbus read, GET, POST whose method and path are declared, payload with a declared hash) runs as usual. The collector sets `deny` on every login and data step; the command adapter always uses `allow` and never reads `writes` from command args. The cloud cannot clear it for a job either, since the collector sets it in firmware and the job schema has no such key. Alternatives rejected: a caller kind (`command` / `job`) that the executor maps to a policy (same effect, but ties the executor to its callers); relying on the collector's apply-time classification (the run-time cases above).

### D4. Write grants and read declarations in `gateway_ns`, set from the `system` shadow and REST
One list holds both kinds of entry. The names `gateway_write_grants`, `write_grants` and the NVS key `wgrants` stay, deliberately: add-lan-collector and the dev tools already use them, and a write grant is still what every write needs; a read declaration is an entry with `kind: "read"`.

One NVS blob `wgrants` in `gateway_ns`: a version byte plus up to 8 fixed 208 B entries (IPv4 4 B, port 2 B, module id 1 B, kind 1 B, flags 1 B (bit 0 `mac` present, bit 1 `["*"]`), MAC 6 B, allow count 1 B, 4 x 48 B allow slots), so at most 1665 B, written in one `putBytes` so an update is atomic. An allow slot holds a normalized path or Modbus range string, or a 32 B binary SHA-256 for `tcp`/`udp`. `gateway_logic` only parses the 64-hex form and compares 32 B digests (host-tested, no dependency); the module computes the payload SHA-256 with mbedTLS on the device.

The JSON form is deliberately flat (`allow` is a list of strings, Modbus ranges reuse the address grammar as `u1/hr100:2=0..400`, hashes are 64-hex strings) because an AWS delta adds a `metadata` timestamp object per leaf and the inbound MQTT buffer is 9 KB with oversized packets dropped silently. Worst case entry: a `udp` write entry with `mac` and four 64-hex hashes, 371 B compact; 8 of them are 2977 B, so the cap is 3072 B. Leaves: 8 x (5 scalars `ip`, `port`, `module`, `kind`, `mac` + 4 allow strings) = 72; metadata about 72 x 30 B = 2.2 KB, envelope about 0.3 KB, so a full-list delta stays under 5.6 KB. A nested object per range would have been about 27 leaves per entry and over the 9 KB limit. The reported `system` state carries the list once (at most 3072 B) next to `tunnel_ports`, the `secrets` metadata of add-gateway-secrets and the other fields, and a pending full-list delta adds another copy in desired; e2e task 5.3 measures grants in reported and desired plus `tunnel_ports` against the 8 KB shadow limit, and add-gateway-secrets sizes and re-measures the whole `system` document once its slots exist. JSON exists only at the edges (shadow, REST). Same setter for both sources, mirroring `gateway_enabled` and `tunnel_ports`. Cleared by factory reset with the namespace. Matching is pure (`gateway_logic`): exact IP, exact port, same module, same kind, then path rule, range plus value bounds, or payload hash. Non-GET HTTP paths and paths sent with `access: "write"` that contain dot segments, `//`, backslashes or encoded `.`, `/`, `\` are refused before matching, because LAN web servers often normalize them and a prefix grant would otherwise be escapable.

Worked example, the XWEB at Sapore di Mare (read-only polling, no write grant at all): one read entry `{ip:"192.168.1.20", port:80, module:"http", kind:"read", mac:"<xweb mac>", allow:["POST /cgi-bin/login.lua","POST /cgi-bin/datachart.cgi"]}`. Both POSTs are classed read; a copied step that posts to any other CGI on the XWEB, or sends another method to one of the two paths, is a write and is refused with `WRITE_NOT_ALLOWED`. The allow string is the canonical HTTP address of D2 and is stored as is in a 48 B slot, so the method and its space count toward the 48 bytes. The IP is illustrative.

MAC binding. IP-keyed entries fail open after DHCP renumbering: whatever device later takes the old address, often with the same port 502 and unit 1, inherits the grant. So an entry that admits a write, or classes a non-GET or payload request read, must carry `mac` when the target is on-link, and the executor compares the ARP entry under `LOCK_TCPIP_CORE` (the `net_scan` pattern, `gateway.cpp:818-851`): after the TCP connect and before the first byte for TCP-based modules (the connect guarantees a resolved entry), and before the send for UDP after one `etharp_request` and a poll of at most `min(500 ms, remaining deadline)`. On-link is decided at run time from the routing interface (`ip4_route`, address and netmask), not at set time, because the network can change; an entry with `mac` whose target became off-link fails closed (`TARGET_MAC_CHANGED`). Routed targets cannot be MAC-bound and match by IP only; that residual is documented, not solved. ARP spoofing on the LAN is out of scope, as for the rest of the threat model. Secrets bound to ip:port have the same renumbering exposure on any request, GET included; add-gateway-secrets owns that binding (a `mac` on the slot) and reuses the ARP compare helper of this change (task 4.3a) for every request that expanded a secret, whatever its class.

Alternatives: grants only through local REST (stronger, but every cloud-controlled point would need a site visit); grants inside jobs in the `collector` shadow (do not cover one-shot commands, and that shadow belongs to add-lan-collector); a gateway op `grant_set` (adds a command op for what is configuration; the `system` shadow also gives an archived history of every grant change in `device_shadow_history` for free).

### D5. Buffers and ceilings
The executor allocates `headers` (2048 B) and `body` (exactly `max_bytes`) in PSRAM per request and frees them on release; nothing is preallocated. Chunked replies are decoded during receive by a small chunk-header state machine in `gateway_logic` (a `ChunkDecoder` fed with each socket read: size line, data, CRLF, trailer), writing data bytes straight into `body`; `max_bytes` and `truncated` count decoded bytes only, and a chunk-size line over 128 B is `BAD_RESPONSE`. This replaces today's raw buffer plus second decode buffer (`gateway.cpp:257-258`, `457-465`) and the fixed 512 B chunk allowance, which does not scale: at 128 KB a CGI flushing 100-byte chunks adds about 9 KB of size lines, the raw cap fills before the decoded body reaches `max_bytes`, the reply is flagged truncated and a cursor job halves its window forever. The single buffer also halves the per-step PSRAM peak (about 130 KB instead of about 260 KB). The state machine is host-tested (task 3.9). Ceiling 131072 B: above the ~48 KB collector chunk so one reply can span chunks, small against 2 MB PSRAM, and the collector's bytes/hour budget narrows it further. Allocation failure is `FAILED NO_MEMORY`, as today.

### D6. Command adapter, proven by golden replies
The adapter maps each legacy op to an `ExecRequest` (`modbus_read`/`modbus_write` turn `unit`, `function`, `address` into the canonical address; `function` 5/6 vs 15/16 sets `write_multiple`) and maps `ExecResult` back with today's helpers (`_setPayload`, `_setNumber`, ...). `max_bytes` is fixed at 4096 for commands. Because the native env has no ArduinoJson, the host tests cover the decisions (key presence, `s` vs `bin`, multibyte trim, cap), and byte-for-byte equality of the JSON is proven on the meters: a golden set is captured on the current firmware before the refactor and diffed after it (task group 1 and 6). Timing fields such as `rtt_avg_ms` are excluded from the diff. The one intended difference is a chunked reply whose size lines overflowed today's 512 B slack (D5); the golden chunked case uses few, large chunks so it stays identical.

### D7. Template engine in `gateway_logic`
Tokenizer and expander are pure: `expand(const char* tmpl, Resolver*, uint8_t* out, size_t cap, size_t* len, bool* sensitive)`, with `Resolver` a C function pointer plus context, passed by the caller on every call (a chain the caller builds, never a global registration), so add-gateway-secrets and add-lan-collector plug in without touching the engine and a resolver one caller passes (the collector's `${cap:x}` or `${cursor}`) can never expand inside another caller's request, such as a one-shot command. Encoders (`url`, `json`, `b64`) are implemented in the lib so host and device produce identical bytes. The engine holds no resolver registry and this change passes no resolver, so any placeholder is `BAD_TEMPLATE`. Output goes to a PSRAM scratch buffer sized to the module's limit (5760 B for HTTP and TCP), zeroized with `mbedtls_platform_zeroize` before free, whether or not anything sensitive was expanded. Expansion runs before classification and the grant check so a templated path or payload cannot slip past a read declaration or a grant. Alternative: expand only when a request opts in with a flag. Rejected, a flag is easy to forget and the escape `$${` keeps literals possible.

### D8. TCP module
Same connect and one-write rule as HTTP (`_connectAndSend`, `gateway.cpp:241`). Termination by close, cap, `idle_ms` after the first byte, or deadline; no framing knowledge. `idle_ms` default 300 ms suits request/response LAN protocols; banner reads use an empty payload.

### D9. HTTPS gated on a bench measurement
Implementation: raw `mbedtls_ssl` over the module's own non-blocking lwIP socket (`mbedtls_ssl_set_bio` with send/recv wrappers), `MBEDTLS_SSL_VERIFY_NONE`, no SNI, TLS 1.2 minimum. The handshake loop calls `mbedtls_ssl_handshake` and, on every `WANT_READ`/`WANT_WRITE`, checks the executor's absolute deadline and waits on the socket with `select` for at most the remaining time; past the deadline it frees the context and returns `TIMEOUT`. The TCP connect and every later read and write use the remaining deadline too. After the handshake and before the first `mbedtls_ssl_write`, the leaf from `mbedtls_ssl_get_peer_cert` (kept because `CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE=y`) is hashed and compared in constant time with `cert_sha256`; no TOFU. `tls_probe` uses the same path and sends nothing.

`NetworkClientSecure` with `setInsecure()` was the first candidate and is rejected: its handshake timeout defaults to 120 s (`NetworkClientSecure.cpp:41`, `:69`), the loop in `ssl_client.cpp:315-322` honours only that, and `setHandshakeTimeout()` takes whole seconds (`:450-451`). A LAN device that accepts TCP and stalls TLS (a busy single-thread embedded TLS stack, or the wrong port) would hold the gateway task for two minutes, break the 100-10000 ms deadline of D1 and the at-most-one-step command wait of add-lan-collector, and keep the TLS buffers allocated meanwhile; whole seconds cannot honour `timeout_ms` below 1000 either.

HTTPS commands and `tls_probe` are refused with `NO_MEMORY` / `TUNNEL_ACTIVE` while `Tunnel::isActive()`. The collector already pauses during a tunnel, but one-shot commands do not, and an HTTPS session held for up to 10 s next to the tunnel's sockets could push internal free heap under `TUNNEL_LOW_HEAP` long enough (5 s) to close the support tunnel, or under a floor the gate measured without the tunnel. Refusing needs no extra gate run, and `NO_MEMORY` is already in the transport failure list add-lan-collector handles.

Gate, measured per product on Home .174 and Home Pro .80 with MQTT connected, meter publishing and no tunnel session (HTTPS is refused during one), 20 handshakes each against a PC test server with an RSA-2048 and an ECDSA P-256 self-signed certificate:
- `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)` never below 24 KB (today's floor is about 41 KB per add-lan-tunnel `design.md:8`), largest internal free block never below 8 KB;
- gateway task stack high-water at least 1024 B free with the stack at most 12 KB (from 8 KB today; every KB comes from internal RAM);
- no MQTT disconnect, no missed meter publish, no OTA regression in one OTA run with the module idle.

The gate also runs the stall case: a PC server that accepts TCP and never answers the ClientHello must end the step with `TIMEOUT` within `timeout_ms` (tested at 1000 and 10000 ms) and return internal heap to its pre-handshake level.

Pass sets the runtime pre-handshake threshold to the measured internal-heap peak cost plus 8 KB and enables the module for that product. Fail on a product: the module stays compiled out for it, `https_request` and `tls_probe` return `MODULE_UNAVAILABLE`, and HTTPS LAN targets are reachable only interactively through the LAN tunnel. Results are recorded in tasks.md.

### D10. What does not move
`ping` and `net_scan` stay executor diagnostics outside the module table. `tunnel_open`/`tunnel_close` keep their own path (`mqtt.cpp:1553`). The one-slot `GATEWAY_BUSY` behaviour is unchanged here.

## Risks / Trade-offs

- [A mutating GET is classed read] → only GETs rely on the caller; non-GET HTTP and raw payloads are write unless a stored read declaration matches; cloud control always sends `access: "write"`, documented as a cloud-side responsibility.
- [A read declaration ignores the request body, so on a JSON-RPC style endpoint (one path, operation in the body) it would class every operation read] → such paths are never declared read and the infra jobs tool refuses to derive a declaration for them; GET polling covers those devices. A body hash in read entries is the stricter option, left as a decision for Jibril because it cannot match templated bodies.
- [Read declarations are a second kind of entry to maintain] → needed only for POST-based read APIs and fixed raw frames; Modbus and GET polling need none. They share the 8-entry budget with write grants.
- [A collector step becomes a write after apply (run-time expansion, revoked declaration)] → the collector passes `writes: "deny"`, so the executor refuses it with `WRITES_DENIED` whatever the grants.
- [Cloud can grant itself writes] → accepted threat model; local REST revoke, WARNING log and shadow history give audit and a kill switch.
- [Grants keyed by IP fail open after a DHCP renumbering: the device that takes the old address inherits them] → `mac` is mandatory on entries for on-link targets and checked against ARP before the first byte (`TARGET_MAC_CHANGED`); routed targets stay IP-only, documented.
- [A legitimate NIC or controller replacement changes the MAC] → writes and declared reads stop with `TARGET_MAC_CHANGED` until the entry is updated; failing closed is the intent.
- [Refactor changes command output] → golden diff on both meters before merge.
- [BREAKING for existing cloud write callers] → set grants before the OTA; the dev tools and docs are updated in the same PR.
- [A literal `${` in a one-shot body is now rejected] → `$${` or `body_b64`.
- [HTTPS does not fit] → gate with a defined fallback; no partial enablement.
- [A LAN device stalls the TLS handshake] → raw `mbedtls_ssl` loop bounded by the executor deadline; gate stall test.
- [HTTPS during a tunnel session] → refused with `TUNNEL_ACTIVE`.
- [Incremental chunk decoding changes legacy output in one case] → a chunked command reply whose size lines exceeded today's 512 B slack was cut early and now decodes up to 4096 B; the golden chunked case stays low-overhead and the lan-gateway delta names this difference.
- [`return_headers` puts session cookies or tokens into `command_events`, which is archived] → off by default; add-lan-collector keeps captured session values on the device and never publishes the header block of a login step it captured from.
- [HTTPS plaintext at the 5760 B limit needs several TCP sends once TLS framing is added] → allowed inside the deadline; the one-send rule protected naive HTTP parsers, and TLS reassembles records anyway.
- [128 KiB PSRAM allocations fragment PSRAM] → per-request alloc and free, ceiling only reached by collector jobs that ask for it.

## Migration Plan

1. Capture the golden set on the current development firmware on both meters.
2. Ship the firmware with an empty list. Cloud scripts that write (`modbus_write`, `udp_send`, non-GET HTTP) first set `gateway_write_grants` in the dev `system` shadow, with `mac` for on-link targets (taken from `net_scan`), and POST-based read polling first gets its read declaration.
3. Rollback: previous firmware ignores the `wgrants` NVS key; it leaves a `gateway_write_grants` desired key unacknowledged in the `system` shadow, which the cloud clears by writing `null`.
