## Purpose

New non-community devices run with cloud services on from their first boot, so they receive essential updates and can be configured and supported remotely; keeping data local stays possible as an explicit choice.

## ADDED Requirements

### Requirement: Cloud services on by default for new devices
A device that is not in community mode and has no stored cloud services value SHALL run with cloud services enabled. A stored value SHALL always win over the default; devices that already booted earlier firmware hold the value that firmware stored and keep it. The default SHALL be applied without being written back, so only an explicit choice stores a value. An explicit choice (REST, web UI, setup page) SHALL always be stored, even when it equals the current value. A device in community mode SHALL keep cloud services off, whatever the default. A device that has not completed setup (`phase-check`, setup page) SHALL NOT start cloud services, so no cloud update restarts it while the installer is still configuring it; completing or skipping the setup SHALL store the choice made there and start cloud services if it is on. A device upgraded with a non-default password counts as set up and is not held back.

#### Scenario: Factory-fresh device
- **WHEN** a non-community device boots with no stored cloud services value and the installer completes the setup without changing the cloud choice
- **THEN** cloud services stay off until the setup completes, and the device then stores them enabled and connects to the cloud

#### Scenario: No update during setup
- **WHEN** a factory-fresh device joins Wi-Fi and the installer is on the circuits step
- **THEN** the device has not connected to the cloud and cannot receive an update

#### Scenario: Existing device keeps its value
- **WHEN** a non-community device that booted earlier firmware with cloud services off is updated to this firmware
- **THEN** cloud services stay off

#### Scenario: Community device
- **WHEN** a community-mode device boots with no stored value
- **THEN** cloud services stay off, `GET /api/v1/mqtt/cloud-services` reports `enabled: false`, and enabling them is refused with 403


### Requirement: Remote access stays opt-in
Turning cloud services on by default SHALL NOT turn on the LAN gateway, the LAN tunnel or any other remote access to the local network; those keep their own off default and their own explicit enable.

#### Scenario: New device on the default
- **WHEN** a factory-fresh device connects to the cloud on the default
- **THEN** the LAN gateway and the LAN tunnel are off
