## MODIFIED Requirements

### Requirement: Gateway command envelope
The device SHALL accept an IoT Command with `operation: "gateway"`, an `op` string naming one primitive (`ping`, `net_scan`, `http_request`, `modbus_read`, `modbus_write`, `udp_send`) or a tunnel op (`tunnel_open`, `tunnel_close`, specified in the `lan-tunnel` capability), and `args` given either as a JSON object or as a string holding base64-encoded JSON. It SHALL reject the command with `REJECTED` and reasonCode `UNKNOWN_OP` for an unknown `op`, and `BAD_ARGS` when `args` is missing, not decodable, not a JSON object, or fails the primitive's argument validation.

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

### Requirement: Gateway disabled by default with local and cloud switch
The gateway SHALL be disabled on a fresh device. Its state SHALL be persisted in NVS and exposed as the writable `gateway_enabled` boolean of the `system` shadow, and as `GET/PUT /api/v1/gateway` on the authenticated local REST API (an emergency switch; the web UI SHALL NOT expose it, since the gateway is a cloud feature). Both SHALL use the same setter. While disabled, every `gateway` command SHALL be rejected with reasonCode `GATEWAY_DISABLED`, except `tunnel_open`, which is rejected with `TUNNEL_DISABLED`. Disabling SHALL NOT abort a primitive already running, and SHALL close an open tunnel session at once.

#### Scenario: Fresh device rejects gateway commands
- **WHEN** a `gateway` command arrives on a device that never enabled the gateway
- **THEN** the device publishes `REJECTED` with reasonCode `GATEWAY_DISABLED`

#### Scenario: Cloud enables the gateway
- **WHEN** a `system` shadow delta sets `gateway_enabled: true`
- **THEN** the device persists it, reports it with `desired:{gateway_enabled:null}`, and accepts subsequent `gateway` commands

#### Scenario: Local disable is reflected in the shadow
- **WHEN** the gateway is disabled through `PUT /api/v1/gateway`
- **THEN** the setting persists across reboot and the `system` shadow reported state shows `gateway_enabled: false`

#### Scenario: Disabling closes a tunnel
- **WHEN** the gateway is disabled while a tunnel session is open
- **THEN** the session closes at once with `TUNNEL_DISABLED`
