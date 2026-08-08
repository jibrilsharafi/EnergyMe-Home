## 0. Branch

- [x] 0.1 Create `feat/led-priority-layers` off `development`

## 1. Pure `led_state` module (host-testable)

- [x] 1.1 `source/lib/led_state/led_state.h`: `Layer` enum (`STATUS`, `USER`, `NETWORK`, `ALERT`, `CRITICAL` — reordered from the original plan, see design.md D9), `Pattern` enum, `Rgb`, `Slot`, `Table`. No Arduino / FreeRTOS / Preferences includes
- [x] 1.2 `set()` / `release()` / `releaseAll()` - a write touches only its own slot
- [x] 1.3 `resolve()` / `expire()`, composed by `step()` so no caller can skip a step (design.md D3/D9 follow-up)
- [x] 1.4 `render()` with waveforms per spec; brightness applied exactly once
- [x] 1.5 `effectiveBrightness()` with a per-layer floor table (`LAYER_MIN_BRIGHTNESS_PERCENT[]`), `static_assert`ed to cover every layer

## 2. Host unit tests

- [x] 2.1 `source/test/test_led_state/test_led_state.cpp`
- [x] 2.2 Layer precedence
- [x] 2.3 No-drop under an indefinite critical (regression test for the reported bug)
- [x] 2.4 Same-layer write replaces rather than queues
- [x] 2.5 Release reveals the layer below; no-op on a free layer; all released renders off
- [x] 2.6 Expiry measured from set time, including while masked
- [x] 2.7 Waveform boundaries at 25 ms sampling, incl. long-uptime phase and simultaneous multi-layer expiry
- [x] 2.8 Brightness applied once; pulse peak == solid at same brightness; per-layer floors, incl. the truncation-to-zero edge case
- [x] 2.9 `pio test -e native` (WSL): 41/41 led_state tests, 475/475 native suite total

## 3. `Led` adapter rewrite

- [x] 3.1 `include/led.h` rewritten around layers; `LED_TASK_DELAY_MS` = 25; no `PRIO_USER` (unreachable through the numeric scale by design, see D9) - USER is reached via the `Layer` overload of `setPattern()`
- [x] 3.2 `_layerForPriority()` (kept file-static, only used within `led.cpp`)
- [x] 3.3 Queue replaced by `LedState::Table` behind a mutex created in `begin()`, never deleted; every entry point no-ops when absent
- [x] 3.4 `setPattern()` / `clearPattern()` rewritten onto the table (`clearAllPatterns()` removed - zero callers)
- [x] 3.5 `_ledTask()`: single wait per iteration via `LedState::step()`
- [x] 3.6 `getState()` returns a `Snapshot` (embeds `LedState::Active`) with a `valid` flag distinct from "LED is off"
- [x] 3.7 No-op clamp fixed; brightness applied once in `render()`
- [x] 3.8 TODO comment removed
- [x] 3.9 `pio run -e esp32s3-dev` clean (RAM 20.2%, flash 57.1%). `pio check` could not run: cppcheck aborts on an ArduinoJson preprocessor construct in `.pio/libdeps`, pre-existing and unrelated to this branch

## 4. Bench verification of the engine

- [x] 4.1 Boot colour walk - confirmed by Jibril
- [x] 4.2 WiFi disconnect/reconnect visual - confirmed by Jibril
- [x] 4.3 Button press-and-hold ladder - confirmed by Jibril on the flashed fix (see 4.6)
- [x] 4.4 Reported symptom gone: verified via API (`GET /api/v1/led` reflects `status` changes correctly while `user`/other layers are set) and by the no-drop unit test; physical confirmation still pending
- [x] 4.5 Task timing/stack checked via `/api/v1/system/info` on the bench device (192.168.1.82, chipId 273201871555672): 26.4 ms/loop (target ~25 ms; old firmware measured 98.8 ms/loop this session), stack 43.0% used vs 42.6% before - no regression
- [x] 4.6 **Pre-existing bug found during 4.3, unrelated to the LED redesign**: pressing the button panicked `button_task` (`Exception/panic`, `_svfprintf_r`). Root-caused with `crash_dump_analyzer.py` (rewritten this session to decode in-process, see below) down to `buttonhandler.cpp:160` - `BUTTON_TASK_STACK_SIZE` at 3 KB had no headroom for `LOG_DEBUG`'s message buffer plus newlib's `vfprintf` internals on the very first log call of every press. Fixed by growing the stack to 4 KB, matching every other logging task in the firmware. Confirmed via a full GDB-backed thread dump (registers + locals), not just the address-only backtrace. Native suite unaffected (a `#define` change). Fix flashed and confirmed by Jibril - 4.3 now passes

## 5. REST API (#105)

- [x] 5.1-5.6 implemented, incl. `AsyncURIMatcher::exact()` fix and swagger docs
- [x] 5.7 Verified live against the bench device: 29/29 checks in `verify_led.sh` - GET/PUT/DELETE, all validation 400s, wrong-method 405, duration expiry, `pattern: off` suppressing ambient status, unauthenticated 401s, and the brightness-route-not-shadowed regression. Reboot-persistence of the user layer (should be gone after restart) was not exercised - would require a device restart mid-session

## 6. Shadow

- [x] 6.1 **Reverted, not implemented** - see design.md D9: reporting LED state would republish the near-static system shadow on every WiFi flap/button press. Spec updated accordingly
- N/A 6.2 (nothing to verify)

## 7. Call-site cleanup

- [x] 7.1 `customwifi.cpp:587` hack comment removed
- [x] 7.2 Redundant re-assert dropped
- [x] 7.3 The three `setBrightness(max(...,1))` sites removed; also found and fixed a fourth failure mode (indefinite critical left occupied in `performNvsRestore()`) and a button-feedback ordering bug, both found during review
- [x] 7.4 Re-verification of button/factory-reset visibility at brightness 0 - confirmed by Jibril, both stay visible (unit-tested: `test_button_feedback_is_visible_at_zero_brightness`, `test_critical_stays_visible_at_zero_brightness`). Unrelated finding during this pass: factory reset itself does not erase the WiFi driver's persisted association (`esp_wifi_get_config`/`nvs.net80211`), so the device rejoins its old network after a "reset" - pre-existing bug, outside LED scope, tracked separately

## 8. Review, simplify, merge

- [x] 8.1 Two independent review agents (adversarial + correctness/spec-compliance) run against the branch diff; every finding verified by reading the actual library/framework source before acting, not taken on the agent's word
- [x] 8.2 `simplify` skill: 4 parallel agents (reuse/simplification/efficiency/altitude - efficiency agent hit a session limit and did not complete); findings verified against source before applying
- [x] 8.3 Every finding triaged: fixed, or the reasoning for not fixing recorded in design.md D9/D10
- [x] 8.4 Re-ran `pio test -e native` (41/41, 475/475) and `pio run -e esp32s3-dev` (clean) after every fix round
- [ ] 8.5 PR not yet opened - waiting on Jibril's physical LED review first, per the original request
