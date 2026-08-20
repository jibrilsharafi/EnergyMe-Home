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
