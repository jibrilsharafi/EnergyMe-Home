## 1. Decision logic

- [x] 1.1 `lib/boot_recovery_logic`: reset classification and the escalation table, with native tests

## 2. Firmware

- [x] 2.1 `BootRecovery` module: NVS state, RTC deliberate-restart flag, early step, boot-time factory reset, stable mark
- [x] 2.2 Flag every deliberate `ESP.restart()` (restart task, failsafe timer, wrong-image rollback)
- [x] 2.3 `setup()` gates the meter, Modbus TCP and the cloud modules on the stage; recovery LED
- [x] 2.4 `CrashMonitor::_handleCounters()` loses rollback and factory reset; the reset task marks the boot stable
- [x] 2.5 `recovery_mode_active` issue
- [x] 2.6 `GET /api/v1/system/recovery`, `POST /api/v1/system/recovery/exit`, dev-only `POST /api/v1/debug/boot-crash`, swagger
- [x] 2.7 `pio test -e native`, `pio run` for Home and Pro dev and prod

- [x] 2.8 Boot history in `recovery_ns`, reported by `GET /api/v1/system/recovery`; dev boot crash takes `keepWifi` and `noRollback` so the ladder runs over the network

## 3. Bench

Run over OTA on the Home dev (.174, 2026-11-11) with `keepWifi=1&noRollback=1`, checked through the boot history. The clean-board USB run (3.2 power cycle, 3.6) was dropped on 2026-11-11 and the change merged on the remote run alone.

- [x] 3.1 Boot crash armed until Minimal: the ladder walks Normal, Wiped, Recovery, Minimal, with one factory reset (9 panics, 1 wipe)
- [ ] 3.2 Power cycle in Recovery boots straight into Recovery (software restart in Minimal verified sticky; power cycle pending)
- [x] 3.3 Recovery boot: STA and the web server up, meter passive (readings and writes refused), issue raised (LED not checked remotely)
- [x] 3.4 `recovery/exit` re-enters at Wiped and returns to Recovery, then Minimal, with no second wipe; an OTA clears the stage
- [x] 3.5 Ten deliberate restarts in a row change nothing
- [ ] 3.6 Clean board over USB: power cycle, purple LED, wipe without `keepWifi` brings up the SoftAP, rollback branch without `noRollback`
