## ADDED Requirements

### Requirement: Unstable boots are counted before anything that touches flash state

The device SHALL classify the previous boot at the very start of `setup()`, right after the LED is initialised and before LittleFS is mounted, a pending NVS restore runs or the logger starts. A boot SHALL count as unstable when it ended through a panic, a watchdog, a brownout or any other fault reset, or through a software restart that was not flagged as deliberate, before reaching `COUNTERS_RESET_TIMEOUT` of uptime. Power-on, USB, JTAG and reset-pin resets SHALL leave the count unchanged. The count SHALL be persisted in NVS, so power cycles and brownouts neither clear it nor reset the ladder, and SHALL be cleared once the device reaches `COUNTERS_RESET_TIMEOUT` of uptime.

#### Scenario: A fault before the crash monitor is counted
- **WHEN** the device crashes right after LittleFS mounts, before `CrashMonitor::begin()`, on three boots in a row
- **THEN** the third crash is followed by a boot that escalates the ladder

#### Scenario: Deliberate restarts are not counted
- **WHEN** the user restarts the device ten times in a row, each after less than three minutes of uptime
- **THEN** the unstable-boot count stays at zero and no recovery action runs

#### Scenario: Power cycles do not reset the ladder
- **WHEN** a looping device is power cycled
- **THEN** the unstable-boot count and the stage persisted before the power cycle are kept

### Requirement: Three unstable boots escalate one stage, immediately

On the boot that finds three consecutive unstable boots, the device SHALL reset the count and escalate one stage, acting during that same early step with no uptime gate:

| Stage | Escalation |
| --- | --- |
| Normal | Roll back to the passive image if it validates and no automatic rollback happened since the last stable boot, then restart. Otherwise factory reset and enter Wiped. |
| Wiped | Enter Recovery |
| Recovery | Enter Minimal |
| Minimal | Stay in Minimal |

The factory reset SHALL clear the application NVS namespaces, the web password, the gateway secrets and the WiFi driver's stored credentials, and SHALL format LittleFS, without needing WiFi or the filesystem to be started. It SHALL keep `factory_ns`, the recovery namespace and the coredump partition. Restarts made by the ladder SHALL be flagged as deliberate.

#### Scenario: Factory firmware crash loop
- **WHEN** a device on its factory image (no rollback target) crashes within seconds of every boot
- **THEN** on the fourth boot it factory resets and restarts at once, without waiting for any uptime

#### Scenario: The crash survives the wipe
- **WHEN** the device keeps crashing after the factory reset
- **THEN** after three more unstable boots it boots in Recovery, and after three more in Minimal

### Requirement: Degraded stages keep the device reachable

In Recovery the device SHALL keep the meter passive (no ADE7953 communication, interrupts or meter tasks) and SHALL NOT start Modbus TCP, while WiFi (station and SoftAP), the web server, the issue registry and cloud MQTT keep running. In Minimal the device SHALL also run as in community mode: no cloud MQTT, gateway, collector or tunnel. In both stages the LED SHALL blink purple fast on the critical layer, and the issue `recovery_mode_active` (error) SHALL be active.

#### Scenario: Recovery boot of an unconfigured device
- **WHEN** an unconfigured device boots in Recovery
- **THEN** its provisioning SoftAP and web server come up and stay up

### Requirement: The stage is sticky per firmware and can be exited

The stage SHALL persist across restarts and power cycles while the running firmware (app ELF SHA-256) is unchanged, and SHALL reset to Normal, together with the unstable-boot count, when the firmware changes. `GET /api/v1/system/recovery` SHALL report the stage, the unstable-boot count and whether the boot is degraded. `POST /api/v1/system/recovery/exit` SHALL set the stage to Wiped and restart, so a normal boot is retried and a repeated failure returns to Recovery without a second factory reset.

#### Scenario: Power cut in Recovery
- **WHEN** a device in Recovery loses power and comes back
- **THEN** it boots straight into Recovery, with no crash and no second wipe

#### Scenario: An OTA fixes the firmware
- **WHEN** a device in Recovery or Minimal is updated to another firmware
- **THEN** the next boot is a Normal boot

### Requirement: The ladder can be exercised on development builds only

Development builds SHALL provide `POST /api/v1/debug/boot-crash` with a stage threshold (0 disarms). While armed, every boot SHALL abort right after LittleFS mounts as long as the current stage is below the threshold. The setting SHALL live in the recovery namespace so it survives the ladder's factory reset. Production builds SHALL contain neither the endpoint nor the crash.

#### Scenario: Whole ladder on the bench
- **WHEN** the boot crash is armed with threshold Minimal on a dev build
- **THEN** the device escalates Normal, Wiped, Recovery and Minimal in order and then boots without crashing
