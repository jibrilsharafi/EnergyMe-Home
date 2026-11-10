## 1. Decision logic

- [ ] 1.1 `lib/boot_recovery_logic`: reset classification and the escalation table, with native tests

## 2. Firmware

- [ ] 2.1 `BootRecovery` module: NVS state, RTC deliberate-restart flag, early step, boot-time factory reset, stable mark
- [ ] 2.2 Flag every deliberate `ESP.restart()` (restart task, failsafe timer, wrong-image rollback)
- [ ] 2.3 `setup()` gates the meter, Modbus TCP and the cloud modules on the stage; recovery LED
- [ ] 2.4 `CrashMonitor::_handleCounters()` loses rollback and factory reset; the reset task marks the boot stable
- [ ] 2.5 `recovery_mode_active` issue
- [ ] 2.6 `GET /api/v1/system/recovery`, `POST /api/v1/system/recovery/exit`, dev-only `POST /api/v1/debug/boot-crash`, swagger
- [ ] 2.7 `pio test -e native`, `pio run` for Home and Pro dev and prod

## 3. Bench (dev Home over USB with serial first, pre-network boot code)

- [ ] 3.1 Boot crash armed until Minimal: the ladder walks Normal, Wiped, Recovery, Minimal, with one factory reset
- [ ] 3.2 Power cycle in Recovery boots straight into Recovery
- [ ] 3.3 Recovery boot: SoftAP or STA and the web server up, meter passive, issue raised, LED purple
- [ ] 3.4 `recovery/exit` re-enters at Wiped; an OTA clears the stage
- [ ] 3.5 Ten deliberate restarts in a row change nothing
