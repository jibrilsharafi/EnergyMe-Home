## Purpose

Detects a sustained over-temperature condition on the device's internal die sensor and surfaces it through the issue registry, so an overheating unit is visible in the REST issues list and the cloud shadow instead of failing silently.

## ADDED Requirements

### Requirement: Sustained over-temperature is raised as an issue
The system SHALL evaluate the device's internal temperature reading on the issue registry's periodic tick and SHALL raise an `over_temperature` issue when the temperature has remained at or above a raise threshold for a sustained period, using asymmetric raise/clear thresholds so the issue does not flap around a single boundary.

#### Scenario: Temperature climbs above the raise threshold and stays there
- **WHEN** the device's internal temperature is at or above the raise threshold for the sustained period required to raise
- **THEN** an `over_temperature` issue instance is raised, visible in the REST issues list and the cloud shadow, with severity `warning`

#### Scenario: Temperature briefly spikes above the raise threshold
- **WHEN** the device's internal temperature crosses the raise threshold for less than the sustained period required to raise, then drops back down
- **THEN** no `over_temperature` issue is raised

### Requirement: Over-temperature clears only after cooling below a lower threshold
Once an `over_temperature` issue is active, the system SHALL clear it only after the temperature has remained at or below a clear threshold that is strictly lower than the raise threshold, for a sustained period.

#### Scenario: Temperature drops back below the clear threshold
- **WHEN** an `over_temperature` issue is active and the temperature falls to or below the clear threshold for the sustained period required to clear
- **THEN** the `over_temperature` issue instance clears

#### Scenario: Temperature drops below the raise threshold but stays above the clear threshold
- **WHEN** an `over_temperature` issue is active and the temperature falls below the raise threshold but remains above the clear threshold
- **THEN** the `over_temperature` issue instance remains active (no flapping in the gap between the two thresholds)
