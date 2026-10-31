## Why

The LAN gateway is one hand-written function per IoT Command op (`_runHttp`, `_runModbus`, `_runUdp` in `source/src/gateway.cpp`), each building its own JSON reply and throwing away what the next consumer needs: `_runHttp` discards the response header block, so a session cookie or token can never be captured, and nothing can be reused by a scheduled poller. Periodic LAN acquisition (add-lan-collector) and stored credentials (add-gateway-secrets) both need one executor with a stable request/result contract, canonical native addresses that read and write the same way, and a device-side guard so the cloud cannot write to LAN equipment unless that write was explicitly granted, with the read/write class decided by stored configuration rather than by a flag on each request.

## What Changes

- Split the gateway into a core executor plus protocol modules behind one interface: a templated request in, a structured result out (status, protocol status, raw header block, body or registers, truncated, timing, error reason). Modules now: `http`, `modbus_tcp`, `tcp`, `udp`; `https` behind a bench feasibility gate.
- Define a canonical native address grammar per module (Modbus `u<unit>/<table><addr>`, HTTP `<METHOD> <path>`), used symmetrically for read and write, matching the `source_device/native_point` tail of the ADR-034 timeseries id for Modbus.
- Add the template expansion stage (placeholder grammar, encodings `|url |json |b64`, size re-check after expansion, scratch zeroized). This change ships the engine with no resolvers and no resolver registry; each caller passes its resolver chain on every `expand()` call. add-gateway-secrets provides `${secret:x}`, which the command adapter passes alone, and add-lan-collector passes `${now}`, `${cursor}`, `${var:x}`, `${cap:x}` per call, falling through to `${secret:x}`.
- Add per-target write grants, default read-only: every module op is classed read or write, and a write without a matching grant is refused with `REJECTED` / `WRITE_NOT_ALLOWED` before any socket opens. A non-GET HTTP request is read only when a stored read declaration lists its exact method and path (`POST /cgi-bin/datachart.cgi`), never on its body, so an endpoint whose body selects the operation (JSON-RPC style) is never declared read, and a raw `tcp`/`udp` payload only when its SHA-256 is declared read; a per-request `access` can raise a request to write, never lower it, and an internal `writes: "deny"` request field (set by the collector on every job step, never by a command) refuses anything classed write regardless of grants. Grants and read declarations share one list in NVS (`gateway_ns`), set through the `system` shadow field `gateway_write_grants` and through `PUT /api/v1/gateway`, with Modbus address ranges and raw value bounds, HTTP path lists and raw payload hashes. Entries for on-link targets carry the target MAC, checked against ARP before the first byte, so a device that takes a renumbered IP does not inherit a grant. **BREAKING**: `modbus_write`, `udp_send` and non-GET HTTP requests now need a grant or a read declaration.
- New one-shot ops: `tcp_request` (raw TCP), `https_request` and `tls_probe` (only when the HTTPS gate passes, otherwise `MODULE_UNAVAILABLE`; raw `mbedtls_ssl` with the handshake bounded by the request deadline; refused with `NO_MEMORY` / `TUNNEL_ACTIVE` during a tunnel session). Optional `access`, `return_headers` (HTTP) and `verify` (Modbus write read-back) arguments.
- The one-shot IoT Command path becomes an adapter over the executor that keeps today's result keys, attribute rules and 4096-byte cap byte for byte when the new optional arguments are absent. **BREAKING (edge)**: a literal `${` in a templated field is now a placeholder; send `$${` or use `body_b64`.
- No vendor logic on the device: modules know protocols, never equipment.

## Capabilities

### New Capabilities
- `gateway-modules`: core executor and protocol module contract, canonical addresses, template expansion, write grants, HTTPS module gate.

### Modified Capabilities
- `lan-gateway`: command envelope gains `tcp_request`, `https_request`, `tls_probe` and the write-policy reject codes; primitive results gain the optional arguments and the new ops while staying byte-identical by default.
- `iot-device-shadows`: the `system` shadow gains the writable `gateway_write_grants` list.

## Dependencies

- None. add-lan-tunnel (implemented, not archived) modifies the same `Gateway command envelope` requirement; this delta carries its tunnel wording and must be archived after it.
- Consumed by add-gateway-secrets (uses the executor and the expansion stage) and add-lan-collector (depends on this change and on add-gateway-secrets). add-lan-collector must check POST steps and raw payload steps against the read declarations of this change, not against a per-step `access` flag, and passes `writes: "deny"` on every step so the executor refuses a step classed write at run time whatever the grants.

## Impact

- Firmware: `source/src/gateway.cpp` split into executor and module files, `source/include/gateway.h`, `source/lib/gateway_logic` (address grammar, access classification, grant and read-declaration matching including payload digests, MAC binding decisions, incremental chunk decoder, template engine, TCP read termination, pin parsing), `source/src/mqtt.cpp` (system shadow field), `source/src/customserver.cpp` (`/api/v1/gateway`), `source/resources/swagger.yaml`.
- Unit tests in `source/test/test_gateway_logic` (native, WSL); dev e2e on Home .174 and Home Pro .80.
- Cloud: no infra change required for the device side; the shadow handler archives the new `system` field like any other. Cloud code that sends `modbus_write`, `udp_send` or non-GET HTTP must first set grants (with `mac` for on-link targets) or read declarations.
- Memory: HTTPS only if the bench gate passes; the module body ceiling is PSRAM-backed.
