## Purpose

Defines how the device retries the cloud MQTT connection and when a prolonged outage justifies a restart, so retries stay frequent enough to resume quickly without turning a WAN outage into a reboot loop.

## ADDED Requirements

### Requirement: Bounded reconnect backoff
The device SHALL retry a failed cloud MQTT connection with exponential backoff starting at 15 s, doubling on each failure, and capped at 10 minutes. A successful connection SHALL reset the backoff.

#### Scenario: Backoff reaches the ceiling
- **WHEN** consecutive connection attempts keep failing
- **THEN** the delay between attempts grows up to 10 minutes and never exceeds it

#### Scenario: Reconnect after the outage ends
- **WHEN** the cloud becomes reachable again during a long outage
- **THEN** the device reconnects within at most 10 minutes plus one attempt's duration

### Requirement: Time-based restart after a prolonged outage
The device SHALL restart for cloud-connection reasons only when no MQTT connection has succeeded for 24 hours while the network link is up. The number of failed attempts alone SHALL NOT trigger a restart. Authorization failures SHALL keep stopping the MQTT task without a restart, as they do today.

#### Scenario: WAN down for a few hours
- **WHEN** the network link is up but the cloud is unreachable for 3 hours
- **THEN** the device keeps retrying and does not restart

#### Scenario: No connection for 24 hours
- **WHEN** no MQTT connection has succeeded for 24 hours while the network link is up
- **THEN** the device restarts with a reason identifying the prolonged cloud outage

#### Scenario: Network link down
- **WHEN** the network link itself is down
- **THEN** the cloud-outage timer does not trigger a restart (link recovery is handled by the network layer)
