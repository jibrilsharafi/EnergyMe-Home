## ADDED Requirements

### Requirement: Gateway is tried first for NTP sync
The device SHALL include its current default-gateway IP address as the first NTP server slot on every `configTime()` call (initial sync in `CustomTime::begin()` and the periodic re-sync in `_checkAndSyncTime()`), read fresh from the active network interface at the time of the call rather than cached.

#### Scenario: Router answers NTP on its gateway address
- **WHEN** the device's default gateway responds to NTP requests
- **THEN** the device synchronises its clock against the gateway, without any manual configuration

#### Scenario: Gateway changes between sync attempts
- **WHEN** the device's gateway IP changes (DHCP lease renewal, static IP reconfiguration, or reconnecting to a different network) between one sync attempt and the next
- **THEN** the next `configTime()` call uses the new gateway IP, with no stale state left over from the previous network

### Requirement: Public defaults remain as fallback
The device SHALL include two public NTP servers - `pool.ntp.org` (hostname) and a Cloudflare NTP IP address (DNS-independent) - as the second and third server slots, so that time sync continues to work on networks with normal internet access even when the gateway does not run an NTP responder, and remains resilient to a broken DNS path.

#### Scenario: Gateway does not run NTP, internet is reachable
- **WHEN** the gateway does not respond to NTP requests but the device has normal internet access
- **THEN** the device synchronises its clock against one of the two public fallback servers

#### Scenario: Unconfigured/never-changed device keeps working
- **WHEN** a device is freshly provisioned or updated to this behavior with no other changes
- **THEN** it still successfully syncs on any network with internet access, exactly as before this change

### Requirement: No configuration surface
NTP server selection SHALL NOT be exposed as user-configurable state anywhere - no NVS persistence, no REST endpoint, no web UI field, and no AWS IoT device shadow field (writable or reported).

#### Scenario: No REST endpoint exists for NTP servers
- **WHEN** a client queries the device's REST API for NTP server configuration
- **THEN** no such endpoint exists; NTP server selection is not part of any configuration API

#### Scenario: No shadow delta can alter NTP behaviour
- **WHEN** an AWS IoT shadow delta is applied for any registered shadow
- **THEN** it SHALL NOT change NTP server selection, since no shadow field exists for it
