## Why

Home EM-HOME-000092 (factory firmware 2.0.0, never configured) was left in a panel for some days and ended in a boot loop: white, yellow, purple, then the SoftAP blue blink, a crash about 4 s later, about 7.5 s per cycle. Power cycles and the button did nothing, and only a USB reflash can bring it back.

The trigger is still unknown, but the loop could not have broken itself on any firmware up to 2.8.1:

- The crash ladder (`CrashMonitor::_handleCounters()`) asks for its factory reset through `setRestartSystem()`, which refuses any restart before 30 s of uptime. 2.0.0 drops the request and 2.5.x+ queues it until 30 s, which a device dying at 6 s never reaches. The counters are zeroed anyway, so the ladder starts over forever.
- There is no rollback target on factory firmware, and safe mode only blocks deliberate restarts. It cannot stop a panic.
- Counting lives in RTC memory and starts after LittleFS, the NVS restore and the logger. A power cycle or brownout wipes it, and a fault in those early steps (a `LittleFS.begin` failure restarts with `ESP.restart()`) is never counted at all.
- A factory reset cannot help a device that has no configuration, like device 92. Nothing degrades the boot to keep the device reachable for an OTA.
- Holding the button (GPIO0) across a reset enters ROM download mode, and the press actions need 10 to 15 s of uptime, so the button cannot get the user out either.
- `MAX_RESET_COUNT` also counted deliberate restarts, so ten ordinary restarts inside three minutes queued a factory reset.

## What Changes

- New early boot step `BootRecovery::begin()`, run right after the LED comes up and before LittleFS, the NVS restore or the logger. It uses Serial only.
- Every boot that ends before `COUNTERS_RESET_TIMEOUT` (180 s) of uptime without a deliberate restart is an unstable boot: panic, watchdog, brownout, or a direct `ESP.restart()`. Power-on, USB, JTAG and reset-pin resets count as neither stable nor unstable. Deliberate restarts (restart task, failsafe timer, wrong-image rollback, the ladder's own restarts) set an RTC flag first and are not counted.
- The count, the stage and the firmware it belongs to live in a dedicated NVS namespace (`recovery_ns`), kept out of factory resets and backups, so power cycles and brownouts neither reset nor bypass the ladder.
- Three unstable boots in a row escalate one stage, acting immediately at boot with no uptime gate:
  1. **Normal**: roll back to the passive image if it validates and no automatic rollback was made since the last stable boot. Otherwise factory reset at boot (NVS app namespaces, WiFi driver store, LittleFS) and go to **Wiped**.
  2. **Wiped** → **Recovery**: the meter stays passive (no SPI, interrupts or meter tasks) and Modbus TCP never starts. WiFi (STA and SoftAP), the web server and cloud MQTT keep running, so the device stays configurable, visible and updatable over the air.
  3. **Recovery** → **Minimal**: like Recovery, and the device also runs as in community mode (no cloud MQTT, gateway, collector or tunnel). Only WiFi and the web server remain.
- The stage is sticky across reboots and power cycles on the same firmware and is cleared when the firmware changes (OTA or reflash). `POST /api/v1/system/recovery/exit` retries a normal boot, re-entering at Wiped so a repeated failure goes back to Recovery without a second wipe.
- Recovery and Minimal are loud: purple fast blink on the critical LED layer, a `recovery_mode_active` issue (error) mirrored in the issues shadow, and the stage in `GET /api/v1/system/recovery`.
- `CrashMonitor::_handleCounters()` no longer rolls back or factory resets. The quick-restart safe mode is unchanged.
- Dev builds only: `POST /api/v1/debug/boot-crash` arms a crash right after LittleFS mounts on every boot while the stage is below a given value. The setting survives the wipe, so the whole ladder can be exercised on the bench.

Not covered, still USB-only: a crash inside the WiFi or web server stack itself, or one before `setup()` reaches the LED.

Known limits:
- On the first release with the ladder, an automatic rollback can only land on a release without it. If that older image also loops before its own crash monitor starts, it stays there.
- Loops of deliberate restarts longer than 60 s (a health check that keeps failing) are left to safe mode and are not counted.
- If NVS cannot be opened, the ladder is disabled for that boot.
- The OTA restart from a release without the ladder is not flagged as deliberate, so the first boot of the ladder counts one unstable boot. A stable boot clears it.

## Capabilities

### New Capabilities
- `boot-loop-recovery`: early unstable-boot counting, the escalation ladder, the degraded boot stages and their exit.

### Modified Capabilities
- `crash-reporting`: the rollback and factory-reset recovery leaves `CrashMonitor::_handleCounters()`. Recovery still runs before the archive, because it now runs before LittleFS is mounted.
- `firmware-image-compatibility`: the automatic rollback now happens in the boot-loop ladder and still validates the passive slot.
