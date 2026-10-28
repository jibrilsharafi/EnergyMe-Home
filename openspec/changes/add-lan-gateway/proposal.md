## Why

Pilots (first one: Sapore di Mare, three stores) need the cloud to switch loads that are reachable only on the customer LAN: Shelly relays and plugs (HTTP RPC), Broadlink IR blasters (UDP + AES), Sonoff in eWeLink LAN mode (HTTP + AES), and later heat pumps, inverters and EV chargers over Modbus TCP. The bench test on 2026-10-28 confirmed every one of these can be driven locally from a PC. EnergyMe is already on that LAN with a cloud connection, so it can act as the cloud's gateway.

Decision (Notion "Home device control", 2026-10-28): the firmware exposes generic network primitives and the cloud keeps everything else - device catalogue, protocol logic (including Broadlink/eWeLink crypto), automations. A new device type never needs a firmware release.

## What Changes

- New IoT Command `gateway` carrying one primitive per execution: `ping`, `net_scan`, `http_request`, `modbus_read`, `modbus_write`, `udp_send`. Results return in the IoT Commands `result` map.
- Targets are IPv4 literals in the private ranges only (10/8, 172.16/12, 192.168/16). No hostnames, no HTTP redirects, so the device can never reach internet hosts or itself through loopback.
- Disabled by default. `gateway_enabled` is a writable field of the `system` shadow and of a local REST endpoint + web UI toggle, persisted in NVS. Either side can flip it at any time.
- Work runs in a dedicated gateway task, one request at a time, with a per-request timeout (default 5 s, max 10 s), a 4 KB response cap and a batched ARP sweep, so metering and MQTT keep running. A second request while one is running is rejected with `GATEWAY_BUSY`.
- Every executed request is logged at INFO with primitive and target, as the on-device audit trail.

Non-goals: on-device automations or schedules, TLS to LAN devices, IPv6 targets, local pairing/allow-lists, cloud-side role/MFA/audit (later, in energyme-infra).

## Capabilities

### New Capabilities
- `lan-gateway`: the cloud-driven LAN primitives, their arguments and results, target restrictions, limits, and the enable switch.

### Modified Capabilities
- `iot-commands`: the command set grows from five to six (`gateway`).
- `iot-device-shadows`: the `system` shadow gains the writable `gateway_enabled` field.

## Impact

- Firmware: new `source/src/gateway.cpp` + `include/gateway.h` (task, primitives), new host-testable `lib/gateway_logic` (private-IP check, arg parsing, Modbus TCP framing, scan range) with Unity tests, dispatch + result drain in `mqtt.cpp`, `system` shadow field in `shadow.cpp`, REST endpoint in `customserver.cpp`, toggle in `configuration.html`, `swagger.yaml`.
- Cloud (energyme-infra, separate PR): `gateway` IoT Command template for Home and Home Pro, with `op` and `args` (base64 JSON) STRING parameters plus `created_at`. Dispatchers must set `executionTimeoutSeconds` above the default 10 s for `net_scan`. No IoT policy change: the commands request/response topics are already allowed.
