## Why

The LAN gateway runs one bounded primitive per IoT Command, which cannot drive an interactive session such as a web UI with many requests, SSH or a Modbus master. The first pilot needs remote access to a Dixell XWEB EVO web UI (plain HTTP on :80) on a site LAN, so the meter needs to relay a whole TCP session ("ssh -L" through the cloud) before more gateway primitives are added (#275).

## What Changes

- `gateway` IoT Command gains `op: tunnel_open` and `op: tunnel_close`. No new command template: the existing `energyme-<product>-<env>-gateway` templates already take a free-string `op`, and the session audit lands in `command_events` as for any command.
- Tunnel frames travel on `energyme/<product>/v1/<thing>/tunnel/<sid>/d` (operator to device) and `.../u` (device to operator). The deployed device policies already cover them through the `energyme/<product>/v1/<thing>/*` wildcard and no IoT rule matches them, so no infra change is needed for the first phase.
- Binary frames with a 16-byte header and at most 4096 data bytes, a session-level ARQ (sequence numbers, cumulative ack plus SACK bitmap, window 8, adaptive RTO) and per-channel byte credit, because the device can only publish QoS0 and the broker promises no ordering.
- One session per device, one private IPv4 `ip:port` per session, up to 4 TCP channels per session, with duration, idle, operator-silence, heap, OTA and cloud-disconnect limits.
- New `tunnel_enabled` switch (NVS + writable `system` shadow field + REST), effective only while `gateway_enabled` is also true. Turning either off closes a running session at once. Writable `tunnel_ports` allowlist in the `system` shadow, default `22, 23, 80, 443, 502, 8080, 8443`.
- **BREAKING (spec only)**: disabling the gateway now aborts a running tunnel session. The rule that disabling lets a running request finish keeps applying to the one-shot primitives.
- MQTT task: a binary receive branch ahead of the text-oriented `_subscribeCallback` copy (which truncates at the first NUL), a budgeted uplink drain, and a shorter loop interval while a session is active.
- Python/uv operator tool (`source/utils/lan_tunnel/`) that opens `localhost:<port>` and relays it over MQTT/WSS with SigV4 credentials, plus host chaos tests (loss, reordering, duplication, reconnect, abrupt close).

## Capabilities

### New Capabilities
- `lan-tunnel`: session open/close through the gateway command, topics, wire format, reliability and flow control, limits and close reasons, enable switches, port allowlist, MQTT task integration and the operator tool contract.

### Modified Capabilities
- `lan-gateway`: the command envelope accepts the two tunnel ops; the enable-switch requirement states that disabling aborts a running tunnel.
- `iot-device-shadows`: the `system` shadow exposes `tunnel_enabled`, `tunnel_ports` and the reported-only `tunnel_active`.

## Impact

- Firmware: new `source/lib/tunnel_logic/` (pure, Unity-tested), `source/src/tunnel.cpp` + `source/include/tunnel.h` (task, sockets, PSRAM buffers), hooks in `mqtt.cpp` (RX branch, uplink drain, gateway op routing, OTA close, reconnect), `shadow.cpp`, `customserver.cpp` (`/api/v1/gateway`), `gateway.cpp` (shared target validator), `utils.cpp` restart path, `main.cpp` boot, `swagger.yaml`.
- Memory: about 6 KB internal stack (task created at boot) and about 180 KB PSRAM allocated only while a session is open.
- Tooling: `source/utils/lan_tunnel/` and pytest tests; `awsiotsdk` added to `source/pyproject.toml`.
- Later, in energyme-infra (not part of this change): operator IAM permission set scoped to tunnel topics, optional dedicated tunnel command template, a CDK test asserting no rule matches `.../tunnel/...`, requester audit.
