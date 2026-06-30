## ADDED Requirements

### Requirement: Three transient IoT Commands
The device SHALL support exactly three AWS IoT Commands: `restart` (no payload), `factory_reset` (`{"confirm": "<device_id>"}`), and `energy_reset` (selective or full counter reset). Jobs remain OTA-only.

#### Scenario: restart command reboots the device
- **WHEN** a valid `restart` command is delivered
- **THEN** the device acks and reboots

#### Scenario: energy_reset clears the requested counters
- **WHEN** a valid `energy_reset` command is delivered
- **THEN** the device resets the requested channel energy counters and reports success

### Requirement: factory_reset requires device-id confirmation
The device SHALL reject a `factory_reset` whose `confirm` field does not equal the device id. Only a matching `confirm` SHALL trigger the wipe of user NVS (factory NVS preserved).

#### Scenario: Mismatched confirm is rejected
- **WHEN** a `factory_reset` arrives with `confirm` not equal to the device id
- **THEN** the device rejects it and performs no wipe

### Requirement: Only the json request topic routes to the handler
The device SHALL route only `.../request/json` to the command handler. AWS rejection echoes and any other payload-format segment SHALL be ignored, preventing an infinite status-publish loop.

#### Scenario: Rejection echo is ignored
- **WHEN** an AWS rejection echo is received on the command topic
- **THEN** the device does not route it to the handler and does not publish a status in response

### Requirement: Commands processed off the RX callback
Command handling (per-channel NVS work and status publishes) SHALL run in the MQTT task body, not in the PubSubClient callback, to avoid corrupting the QoS1 PUBACK. Emitted `reasonCode`s SHALL be uppercased to the AWS `[A-Z0-9_-]+` pattern.

#### Scenario: Status published from the task body
- **WHEN** a command requires NVS work and a status publish
- **THEN** that work runs in the MQTT task body and the reasonCode matches `[A-Z0-9_-]+`
