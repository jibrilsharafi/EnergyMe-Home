# wifi-provisioning Delta

## MODIFIED Requirements

### Requirement: The SoftAP is raised while the device is unreachable, and only then

The SoftAP SHALL be raised whenever the device cannot be reached over its own network, and SHALL be torn down once it can. Association, not elapsed time, is the teardown condition. On products with Ethernet, "reachable" includes the Ethernet interface: a device with an Ethernet link and an address SHALL NOT raise the SoftAP, and a serviceable Ethernet interface SHALL satisfy the teardown condition the same way an STA association does. On products without Ethernet, the conditions are unchanged.

#### Scenario: Successful provisioning

- **WHEN** STA association succeeds from a session initiated on the AP
- **THEN** the AP is torn down after the grace period, or immediately once no AP clients remain

#### Scenario: Nobody ever connects

- **WHEN** the AP is raised and no client associates
- **THEN** the AP stays raised, because a device nobody can reach is not made more useful by going quiet

#### Scenario: Device loses its network permanently

- **WHEN** the device enters AP-assist because its network is gone and the user never intervenes
- **THEN** the AP stays raised indefinitely, with full authentication still required on it, so the device remains fixable in place rather than needing a power cycle
- **AND** STA association attempts continue throughout, so the device rejoins by itself if the network returns

#### Scenario: Unprovisioned device left alone

- **WHEN** a device with no stored credentials is left running
- **THEN** the AP remains raised, because it has no other way to be reached

#### Scenario: The AP is lowered while the device is still unreachable

- **WHEN** the AP comes down for any reason while the device still cannot associate
- **THEN** it is raised again on the next lifecycle tick, with no cooldown to wait out

#### Scenario: Cabled Pro device never raises the AP

- **WHEN** a Home Pro device has an Ethernet link and an address, with or without WiFi credentials
- **THEN** no SoftAP is raised, because the device is reachable over the wire

#### Scenario: Pro device recovers over Ethernet

- **WHEN** the SoftAP is up on a Pro device and an Ethernet cable is plugged in and obtains an address
- **THEN** the AP is torn down under the same rules as a successful STA association

#### Scenario: Pro device with no interface at all

- **WHEN** a Home Pro device has no Ethernet link and cannot associate to WiFi (or has no credentials)
- **THEN** the SoftAP is raised, exactly as on a Home device that cannot associate

### Requirement: The DNS responder is confined to AP-only operation

The catch-all DNS responder SHALL run only while the SoftAP is raised and no station-side interface is serviceable - STA disconnected, and on products with Ethernet, no serviceable Ethernet interface either - and SHALL be stopped once any station-side interface comes up.

#### Scenario: STA connects while the AP is still up

- **WHEN** STA association succeeds during the grace period
- **THEN** the DNS responder stops, so the device does not answer DNS queries arriving on the LAN

#### Scenario: AP address changed by subnet-collision avoidance

- **WHEN** the SoftAP address is moved to avoid a collision with the STA subnet
- **THEN** the DNS responder answers with the current SoftAP address, not a previously cached one

#### Scenario: Ethernet becomes serviceable while the AP is up

- **WHEN** an Ethernet address is obtained during AP recovery on a Pro device
- **THEN** the DNS responder stops, because the device is now reachable on a real network and must not answer LAN DNS queries

### Requirement: The SoftAP subnet never overlaps the STA subnet

The SoftAP SHALL be assigned a subnet that does not overlap any station-side subnet - the STA subnet, and on products with Ethernet, the Ethernet subnet (live lease or configured static) - because lwIP resolves an ambiguous route to the first matching interface and a later-added interface is prepended.

#### Scenario: Router uses the default AP subnet

- **WHEN** the STA network occupies the SoftAP's default subnet
- **THEN** the SoftAP is assigned a different non-overlapping subnet before it is raised

#### Scenario: Restored backup carries a foreign static IP

- **WHEN** a configuration backup from a different LAN is restored and its static IP overlaps the SoftAP subnet
- **THEN** subnet selection accounts for the configured static IP, not only the live STA lease

#### Scenario: Radio brought up in AP+STA mode

- **WHEN** the WiFi mode is set to AP+STA at boot with persistent storage enabled
- **THEN** no SoftAP is broadcast until the device decides to raise one, because the driver restores the last SoftAP config from NVS and would otherwise beacon it on a subnet no collision check ever approved
- **AND** the address the authentication carve-out and the Modbus TCP block compare against is never left unset while an AP is reachable

#### Scenario: Collision appears after the AP is raised

- **WHEN** the AP is raised while unprovisioned and STA later obtains a lease that overlaps the AP subnet
- **THEN** the condition is logged and the AP is torn down rather than left routing ambiguously

#### Scenario: Ethernet lease overlaps the AP subnet

- **WHEN** the AP is up on a Pro device and Ethernet obtains an address that overlaps the AP subnet
- **THEN** the AP is torn down (the device is wire-reachable anyway), never left routing ambiguously
- **AND** subnet selection for a future AP raise accounts for the Ethernet subnet - lease and configured static - alongside the STA subnet
