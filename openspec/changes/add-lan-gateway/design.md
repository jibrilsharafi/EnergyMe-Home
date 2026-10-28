## Context

IoT Commands are handled in the MQTT task body (`Mqtt::_handleCommandExecution`), never in the PubSubClient callback, and PubSubClient is not thread-safe. LAN primitives can block for seconds (HTTP timeout, ARP sweep), which the MQTT task must not do: it also runs keepalive and meter publishing. MQTT buffer is 9 KB (`MQTT_BUFFER_SIZE`), which bounds the status publish carrying the result.

## Decisions

### Dedicated gateway task with a single-slot handoff both ways
- The MQTT task validates the envelope (enabled, op known, args parse, target private), publishes `IN_PROGRESS`, and posts the request to a depth-1 FreeRTOS queue. A full queue or a running request means `REJECTED GATEWAY_BUSY`.
- The gateway task (internal-RAM stack, like every task) runs the primitive and writes the outcome into a mutex-guarded result slot. The MQTT task drains the slot every loop and publishes the terminal status + `result`, the same pattern as `_pendingCmd`.
- Rejected alternative: running primitives in the MQTT task. A 10 s HTTP timeout would stall keepalive and meter publishing.

### One generic command, args as base64 JSON
- CDK template: `{"operation":"gateway","op":"<STRING>","args":"<STRING>","created_at":<LONG>}`. `args` is base64 of a JSON object, so quoting and escaping inside AWS `awsJsonSubstitution` never matter. The firmware also accepts `args` as a JSON object (inject harness and readability in tests).
- One template per product instead of one per primitive: adding a primitive is a firmware change only.

### Results
- Status and errors follow the existing reasonCode convention (`[A-Z0-9_-]+`).
- Payload bytes from the LAN (HTTP body, UDP reply) are returned as `bin` (base64), never `s`: device responses are not guaranteed UTF-8. Structured lists (scan hosts, Modbus values) are returned as `s` holding compact JSON.
- Raw response cap 4096 bytes. base64 makes it about 5.5 KB, which fits the 9 KB MQTT buffer with the envelope. Truncation is reported with `truncated: {b: true}`.

### Primitives
- `http_request`: Arduino `HTTPClient` over `WiFiClient` (lwIP sockets, so it works on Ethernet too), redirects disabled, `Connection: close`, body streamed into a PSRAM buffer up to the cap.
- `ping`: IDF `esp_ping` session (`ping_sock.h`), synchronous via a semaphore on the end callback.
- `net_scan`: ARP sweep of the active interface's subnet, limited to its /24 around the device address. Requests go out in batches of 8 under `LOCK_TCPIP_CORE()` on `netif_default` (Ethernet on Home Pro when it is the active interface), wait 300 ms, then the ARP table is read. The lwIP ARP table is small, so batching keeps entries from being evicted before they are read. Optional mDNS browse for one service type via ESPmDNS `queryService` (mDNS is already running).
- `modbus_read` / `modbus_write`: a minimal synchronous Modbus TCP client on `WiFiClient`, with frame building and response parsing as pure functions in `lib/gateway_logic`. Rejected: eModbus `ModbusClientTCP`, because it starts its own worker task and request queue for what is one blocking request at a time.
- `udp_send`: `WiFiUDP` send to ip:port, optional wait for one reply (max 5 s). This is enough for Broadlink: the cloud does auth and AES, the device only carries datagrams.

### Target restriction
- Pure `GatewayLogic::isPrivateIpv4(const char*)`: parses a dotted-quad literal strictly (no hostnames, no octal/hex forms, no leading or trailing garbage) and accepts only 10/8, 172.16/12 and 192.168/16. Loopback, link-local, multicast and broadcast are rejected.
- Port range 1-65535. HTTP path must start with `/`. The URL is built by the firmware from ip/port/path, never taken from the cloud, so the host can't be smuggled in.

### Enable switch
- `gateway_enabled` lives in the `system` shadow (writable) and in NVS, default false. Local REST `GET/PUT /api/v1/gateway` and a toggle on the configuration page call the same setter. A local change drift-detects into the shadow reported state like other local edits.
- Disabling while a request runs lets it finish (bounded by the timeout). New requests are rejected with `GATEWAY_DISABLED`.

## Risks / Trade-offs

- A compromised cloud account gets LAN reach on every enabled site. Mitigated by default-off, private-only targets, and later by cloud-side role/MFA/audit. Accepted by product decision on 2026-10-28.
- Internal heap: the HTTP and UDP clients are short-lived inside the gateway task. Plain HTTP only, no TLS. Gateway task stack is internal RAM and sized once, then checked with the task profiler.
- AWS `awsJsonSubstitution` STRING length limit is not documented. It is verified empirically in dev with a 4 KB `args` before relying on large request bodies.
