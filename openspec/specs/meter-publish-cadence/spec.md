# meter-publish-cadence Specification

## Purpose
The `meter` MQTT topic's payload contract and publish-trigger sizing: power-only points gated by `send_power_data` and the byte/interval threshold trigger. Energy counters and voltage are published separately by `energy-telemetry-stream`.

## Requirements
### Requirement: Meter payload contains power data only
The `meter` MQTT topic SHALL carry only power points (`[unixTimeMs, channel, activePower, powerFactor]`), gated by `send_power_data` and the existing threshold/interval trigger. It SHALL NOT contain voltage or per-channel energy counter data; that data is published separately on the dedicated energy telemetry stream.

#### Scenario: Meter payload excludes energy and voltage
- **WHEN** the device publishes a meter message
- **THEN** the message contains only power point arrays, with no voltage object and no per-channel energy object present

#### Scenario: Meter size estimate reflects power-only content
- **WHEN** the device estimates the queued meter payload size to decide whether the byte threshold is reached
- **THEN** the estimate accounts only for queued power points, with no added overhead term for energy or voltage data
