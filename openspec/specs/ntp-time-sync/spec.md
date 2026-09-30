# ntp-time-sync Specification

## Purpose
TBD - created by archiving change configurable-ntp-server. Update Purpose after archive.
## Requirements
### Requirement: Gateway is tried first for NTP sync
The device SHALL include its current default-gateway IP address as the first NTP server slot on every `configTime()` call (initial sync in `CustomTime::begin()` and the periodic re-sync in `_checkAndSyncTime()`), read fresh from the active network interface at the time of the call rather than cached. The one exception is the single immediate retry after an answer rejected by the time floor, which uses only the public fallback servers.

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

### Requirement: Time floor
The device SHALL NOT let an NTP answer step the clock when it is more than 60 seconds earlier than the time floor, or when it carries an all-zero transmit timestamp (RFC 4330; lwIP decodes it as 2036-02-07T06:28:16Z) or a time outside the valid range (2001-09-09 to 2100-01-01; lwIP decodes answers up to 2104), whatever the floor. The floor at boot SHALL be the later of the firmware's source commit time (build floor) and the floor persisted in NVS; with neither, no floor applies. An accepted answer SHALL raise the floor to the time it carries, but never above a ceiling of the boot floor plus the uptime. A raised floor SHALL be persisted by the energy save task within its save interval; while the meter has failed to start (no energy save task) it is not persisted, and the next boot starts from the last persisted floor. A manual time set SHALL only lower the floor, never raise it, and SHALL keep the ceiling at or below the time set. A factory reset SHALL clear the persisted floor.

#### Scenario: Wrong-but-plausible past answer is rejected
- **WHEN** the floor is in 2026 and an NTP server answers with a 2023 time
- **THEN** the clock is not stepped, and the device retries once right away without the gateway

#### Scenario: Zero transmit timestamp is rejected
- **WHEN** an unsynchronized server answers with an all-zero transmit timestamp
- **THEN** the clock is not stepped, even when no floor applies

#### Scenario: Answer past 2100 is rejected
- **WHEN** an NTP server answers with a time lwIP decodes past 2100-01-01
- **THEN** the clock is not stepped, even when no floor applies

#### Scenario: A consistently fast source cannot poison the floor
- **WHEN** every NTP answer is ahead of real time by the same amount, across several syncs
- **THEN** the persisted floor stays at or below real time, and after a reboot a genuine answer is accepted

#### Scenario: A single future answer heals at the next sync
- **WHEN** one NTP answer is far in the future and the next one is genuine
- **THEN** the genuine answer is accepted and steps the clock back

#### Scenario: Manual set undoes a wrong floor
- **WHEN** the floor is later than real time and the time is set manually to real time
- **THEN** the floor drops to the time set and genuine NTP answers are accepted again; a build floor later than real time applies again after a reboot

#### Scenario: No build floor and nothing persisted
- **WHEN** the firmware has no commit time and NVS holds no floor
- **THEN** every answer in the valid range except the zero transmit one steps the clock, as before the floor existed

