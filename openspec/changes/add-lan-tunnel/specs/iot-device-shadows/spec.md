## ADDED Requirements

### Requirement: System shadow exposes the tunnel switch and allowlist
The `system` shadow SHALL expose `tunnel_enabled` (writable boolean) and `tunnel_ports` (writable array of at most 16 integers 1-65535, empty meaning any port) with the same delta-apply-persist-ack pattern as `gateway_enabled`. An invalid value SHALL be ignored with a warning and leave the stored and reported state unchanged. The reported state SHALL also carry the reported-only `tunnel_active`, `{sid, ip, port, since}` while a session is open and `null` otherwise, refreshed when a session opens or closes.

#### Scenario: Tunnel switch applied from the cloud
- **WHEN** a `system` delta sets `tunnel_enabled: true`
- **THEN** the device persists it and publishes `reported.tunnel_enabled=true` and `desired:{tunnel_enabled:null}` in the same message

#### Scenario: Invalid allowlist ignored
- **WHEN** a `system` delta sets `tunnel_ports: [22, 70000]`
- **THEN** the device logs a warning and the stored and reported allowlist stay unchanged

#### Scenario: Open session is visible
- **WHEN** a tunnel session opens
- **THEN** the `system` shadow reports `tunnel_active` with its sid, target and start time, and `null` again after it closes
