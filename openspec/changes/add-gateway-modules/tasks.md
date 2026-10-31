## 1. Golden baseline (before any refactor, dev only)

- [ ] 1.1 Add `source/utils/gateway_golden.py` (dev AWS only, refuses `--env prod` and the PROD bench address): runs a fixed list of `gateway` commands through StartCommandExecution and stores each command response as JSON
- [ ] 1.2 PC test targets for the golden set: `mock_server.py` cases (text, binary with NUL, empty 200, chunked with few large chunks, 10 KB UTF-8 with a multibyte char at byte 4096, header trickle, 3 KB header block, 3xx), a pymodbus server (FC1-4 reads, FC5/6/15/16 writes, exception 2), a UDP echo, a closed port, an unused private IP
- [ ] 1.3 Capture the golden set on Home .174 and Home Pro .80 with the current development firmware and commit it under `source/utils/gateway_golden/`

## 2. HTTPS feasibility bench (early, gates task 5.6)

- [ ] 2.1 Throwaway dev build with the D9 raw `mbedtls_ssl` path (non-blocking socket, deadline-checked handshake loop, `mbedtls_ssl_get_peer_cert` pin before the first write) that runs 20 TLS handshakes per certificate type (RSA-2048, ECDSA P-256, PC test server) from the gateway task and logs over UDP: internal heap minimum free, largest internal block, gateway stack high-water, MQTT disconnects, meter publish gaps
- [ ] 2.2 Stall case: PC server that accepts TCP and never answers the ClientHello; the step ends with `TIMEOUT` within `timeout_ms` at 1000 and 10000 ms and internal heap returns to its pre-handshake level. If the deadline-bounded loop cannot be made to work, the module stays `MODULE_UNAVAILABLE` (no fallback to `NetworkClientSecure`, whose 120 s handshake timeout breaks the deadline)
- [ ] 2.3 Run on Home .174 and Home Pro .80 against the D9 thresholds (internal floor >= 24 KB, largest block >= 8 KB, stack <= 12 KB with >= 1024 B free, no disconnect or publish gap, stall case passed); record the numbers here and set `GATEWAY_HTTPS_MIN_INTERNAL_HEAP` per product, or mark the product `MODULE_UNAVAILABLE`

## 3. Pure logic in `lib/gateway_logic` with unit tests (`pio test -e native`, WSL)

- [ ] 3.1 Canonical Modbus address parse/format, table and access to function code, `addr + count` bound, round-trip tests and every malformed form in the spec
- [ ] 3.2 HTTP canonical address format; read/write classification for every module: GET without a body read, GET with a non-empty `body` or `body_b64` write (refused with `WRITES_DENIED` under `writes: "deny"`), non-GET read only on an exact read-declaration `<METHOD> <path>` match (query ignored, body ignored, other port or module not matching, DELETE/PUT/PATCH to a path declared for POST classed write), `access: "read"` never lowering and `access: "write"` raising, `tcp`/`udp` payload SHA-256 against read declarations, empty `tcp` payload read, Modbus contradiction case; `writes: "deny"` refuses every request classed write with `WRITES_DENIED` even when a matching grant (path `["*"]`, `udp` `["*"]`, Modbus range) is stored, and leaves read requests unchanged
- [ ] 3.3 Grant model (64-hex parse and 32 B digest compare only; SHA-256 itself is computed on device with mbedTLS): 208 B binary entry layout (kind, flags, MAC, 32 B hash slots), validation (count, private IP, port, module, `kind` default `write`, no `read` for `modbus_tcp`, no `*` in `read` entries, HTTP `read` strings with a `POST`/`PUT`/`PATCH`/`DELETE` method and one space (none, `GET` or lowercase refused), `allow` grammar per module, 48 B strings, 64-hex hashes, `["*"]` alone, `mac` format and broadcast/multicast/zero refusal, `min <= max`, 3072 B JSON, 1665 B blob), matching (exact and `*` prefix paths, exact read `<METHOD> <path>` strings with the method and space counted in the 48 B, unit, table, range end, value bounds, payload hash), and refusal of dot segments, `//`, backslash and `%2e`/`%2f`/`%5c` in any case for every non-GET path
- [ ] 3.4 Template engine: tokenizer, `$${` escape, name and arg charset, up to 3 encodings, `url`/`json`/`b64` encoders against RFC vectors, resolver chain passed per `expand()` call with no global registry (a resolver passed by one caller is `BAD_TEMPLATE` in another caller's expansion), sensitive flag, cap overflow
- [ ] 3.5 Post-expansion checks reusing `isValidHttpPath`, `isValidHeaderValue` and the 5760 B request limit; reserved header names extended with `x-http-method-override`, `x-http-method` and `x-method-override` (`BAD_ARGS`, case-insensitive, a declared POST carrying one included)
- [ ] 3.6 TCP read termination decision (close, cap, idle, deadline) and `truncated` rule
- [ ] 3.7 `cert_sha256` parsing (64 lowercase hex) and constant-time digest compare
- [ ] 3.8 Legacy command decisions: key presence, `s` vs `bin`, multibyte trim, 4096 B cap, optional keys only when requested
- [ ] 3.9 Incremental `ChunkDecoder` state machine fed in arbitrary splits: 64-byte chunks totalling `max_bytes - 1` give `truncated=false`, `max_bytes + 1` give `truncated=true`, chunk-size line over 128 B and non-hex size give `BAD_RESPONSE`, chunk extensions and trailers accepted, byte-at-a-time feeding equals one-shot feeding
- [ ] 3.10 MAC binding decisions: on-link test from interface address and netmask, `MAC_REQUIRED` for an on-link entry without `mac`, `TARGET_MAC_CHANGED` for different, missing or off-link-with-`mac`, no check for requests that need no entry

## 4. Executor and modules (firmware)

- [ ] 4.1 `ExecRequest`/`ExecResult` types (including `connected`, set when the TCP connect completes or the `udp` datagram is sent, so a timed-out connect reads `TIMEOUT` with `connected` false), module table and executor with the check order of the spec (validation, target, module availability, expansion, limits, class, `writes: "deny"`, grant, `MAC_REQUIRED`; MAC compare after connect), INFO/WARNING audit lines without expanded values; result `address` and the `WRITE_NOT_ALLOWED`/`WRITE_OUT_OF_RANGE` reason_detail use the HTTP path template before `?`, tested with a refused POST to `/x?k=${secret:t}` through a stub sensitive resolver
- [ ] 4.2 Executor ordering test with a stub resolver in a dev-only build path: a templated path expanding outside a path grant is refused with `WRITE_NOT_ALLOWED`, and sensitive values never appear in logs
- [ ] 4.3 `http` module from `_runHttp`, keeping the header block in the result and decoding chunked replies during receive into the single `max_bytes` buffer (no raw cap, no second buffer)
- [ ] 4.3a ARP check helper under `LOCK_TCPIP_CORE` (route lookup with `ip4_route`, `etharp_find_addr` after connect, `etharp_request` plus bounded poll for `udp`), called by every module before its first byte
- [ ] 4.4 `modbus_tcp` module from `_runModbus` with canonical addresses, `write_multiple` and `verify` read-back
- [ ] 4.5 `udp` module from `_runUdp`
- [ ] 4.6 `tcp` module
- [ ] 4.7 Command adapter: legacy ops through the executor, new ops `tcp_request`, `https_request`, `tls_probe` in `parseOp`, optional `access`, `return_headers`, `verify`; `writes` always `allow`, never read from command args; `https_request` and `tls_probe` refused with `NO_MEMORY` / `TUNNEL_ACTIVE` while `Tunnel::isActive()`
- [ ] 4.8 Grant storage in `gateway_ns` (`wgrants`), setter shared by shadow and REST, cleared by factory reset, WARNING log on change
- [ ] 4.9 `system` shadow field `gateway_write_grants` (apply, persist, ack, reported on reconnect, drift detection)
- [ ] 4.10 `GET/PUT /api/v1/gateway` `write_grants` (with `kind`, `mac`, hash `allow` strings) and `swagger.yaml` (CRLF file, stage with autocrlf off)
- [ ] 4.11 `pio run` for the dev Home and Pro environments and `pio check -e esp32s3-dev` clean; one commit per task in this group

## 5. Dev end-to-end on Home .174 and Home Pro .80

- [ ] 5.1 Replay the golden set: every response equal to its capture apart from timing values
- [ ] 5.2 Grants: no grant refuses `modbus_write`, `udp_send` and undeclared POST with `WRITE_NOT_ALLOWED`; granted range passes; range end, other unit, other port and out-of-bounds value refused; POST with `access: "read"` to an undeclared path refused with `WRITE_NOT_ALLOWED`; POST to a path listed as `POST <path>` in a read declaration passes without a write grant, and a DELETE to the same path is refused with `WRITE_NOT_ALLOWED`; `udp_send` whose payload hash is declared read passes, one byte changed is refused; revoking the declaration makes the same POST fail with `WRITE_NOT_ALLOWED`; with a dev-only caller passing `writes: "deny"`, a granted Modbus write and a granted `udp_send` are refused with `WRITES_DENIED` and the PC servers see no byte
- [ ] 5.2a MAC binding: grant bound to the pymodbus PC's MAC passes; the same grant after moving the server to another PC at the same IP (or a wrong `mac`) is refused with `TARGET_MAC_CHANGED` and the server log shows a connect but no Modbus byte; on-link grant without `mac` refused with `MAC_REQUIRED`; `udp_send` to a bound IP after flushing the ARP table resolves and sends
- [ ] 5.3 Grant configuration: shadow apply and ack, invalid list ignored, REST revoke reflected in the shadow, reboot keeps grants, factory reset clears them; a maximal 8-entry list (8 `udp` write entries with `mac` and four 64-hex hashes each, 2977 B) is delivered, applied and acked through the dev `system` shadow, and the whole `system` document stays under 8 KB with that list in reported plus the same list pending in desired, 16 `tunnel_ports` and the scalar fields; the combined worst case with 16 secret slots is re-measured by add-gateway-secrets, which lands after this change
- [ ] 5.4 `verify` against the pymodbus server: match, forced mismatch (`READBACK_MISMATCH`), read-back timeout
- [ ] 5.5 `tcp_request` echo, silent peer (`NO_REPLY`), 6000 B payload (`BAD_ARGS`); `${foo}` in a body (`BAD_TEMPLATE`) and `$${` literal
- [ ] 5.6 HTTPS, only where task 2.3 passed: correct pin, replaced certificate (`TLS_PIN_MISMATCH`, no request byte on the server log), stalled handshake (`TIMEOUT` within `timeout_ms`), `tls_probe`, `https_request` during an open tunnel (`NO_MEMORY` / `TUNNEL_ACTIVE`, tunnel stays open); elsewhere `MODULE_UNAVAILABLE`
- [ ] 5.7 One hour of mixed ops at one per 10 s: no MQTT disconnect, no meter publish gap, gateway stack high-water and internal heap floor logged and within the 2.3 limits

## 6. Review and merge

- [ ] 6.1 Code-review agents on the branch diff, one adversarial on the request path (grant and read-declaration bypass by template or path encoding, MAC check ordering, address parser, header injection, chunk decoder, buffer limits); reproduce every finding before accepting or dismissing it
- [ ] 6.2 Simplification agent; re-run unit tests and the golden replay after fixes
- [ ] 6.3 PR to `development` with `Closes #` of the tracking issue
