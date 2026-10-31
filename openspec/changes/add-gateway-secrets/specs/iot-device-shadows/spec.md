## ADDED Requirements

### Requirement: System shadow reports gateway secret metadata
The `system` shadow reported state SHALL carry the reported-only object `secrets`: `{kid, hwm, slots}` where `kid` is the 64-hex SHA-256 of the device certificate DER, `hwm` the secret high-water mark (Unix seconds, 0 when none), and `slots` an array sorted by name with one positional array per stored secret, `[name, "<ip>:<port>", set_at, m]`, where `m` is 1 when the slot is bound to a MAC and 0 otherwise; `slots` is empty when no secret is stored. The slot count (16) and the bound MAC itself are not reported in the shadow; the local REST API returns both. The positional form is mandatory so the object stays at most 1200 bytes as compact JSON with 16 slots of 24-character names, which keeps the whole `system` state under the 8 KB AWS limit next to a full `gateway_write_grants` list reported and pending in `desired` (design D8). It SHALL never contain a value, a value length or a hash of a value, so it stays consistent with the rule that secrets stay out of shadows. It SHALL be built from the in-RAM metadata cache, never from NVS reads, and a set or delete through any path SHALL reach the cloud through the existing drift detection. A delta carrying `secrets` SHALL be nulled with a WARNING and nothing applied.

#### Scenario: Remote set is visible
- **WHEN** `secret_set` stores `xweb_admin` bound to `192.168.1.50:80` and a MAC
- **THEN** within the drift-check interval the `system` reported state lists `["xweb_admin","192.168.1.50:80",<set_at>,1]`, and `hwm` equals that `set_at`

#### Scenario: Local delete is visible
- **WHEN** a secret is deleted through `DELETE /api/v1/secrets`
- **THEN** the next `system` report no longer lists it and no `desired` is written

#### Scenario: Cloud tries to write secrets
- **WHEN** a `system` delta sets `secrets: {slots: []}`
- **THEN** the device publishes `desired:{secrets:null}`, logs a WARNING and the store is unchanged

#### Scenario: Fresh store
- **WHEN** a fresh or factory-reset device reports
- **THEN** the report carries `secrets` with `hwm: 0` and `slots: []`

#### Scenario: Full store next to full grants
- **WHEN** 16 secrets with 24-character names, 15-character IPs and 5-digit ports are stored, the reported `gateway_write_grants` list is at its 3072-byte maximum, a second 3072-byte list is pending in `desired`, 16 `tunnel_ports` are set and a tunnel is active
- **THEN** the `secrets` object is at most 1200 bytes and the `system` state document (`desired` plus `reported`) stays under 8192 bytes, so AWS accepts both the cloud grant write and the device's reported update
