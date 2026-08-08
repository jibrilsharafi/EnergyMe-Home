## 0. Branch

- [x] 0.1 Create `feat/disco-led-pattern` off `development`

## 1. `led_state` engine

- [x] 1.1 `led_state.h`: `Pattern::DISCO` appended after `DOUBLE_BLINK` (before `Count`); `constexpr uint64_t DISCO_STEP_MS = 120;` next to the other period constants
- [x] 1.2 `led_state.h`: `Slot` gains `uint32_t seed = 0`; `set()` gains a trailing `uint32_t seed = 0` parameter so existing callers are untouched
- [x] 1.3 `led_state.h/.cpp`: `Rgb discoColor(uint32_t seed, uint64_t elapsedMs)` - xorshift32 over `(seed ^ stepIndex * 0x9E3779B9)`, folding `1 + (h % (PALETTE_SIZE - 1))` across steps so no two consecutive steps repeat (design D1/D2)
- [x] 1.4 `led_state.cpp`: disco palette table built from the existing `Colors` constants, `static_assert`ed non-empty and > 1 entry (the fold divides by `size - 1`)
- [x] 1.5 `led_state.cpp`: `resolve()` substitutes `discoColor(slot.seed, elapsedMs)` into `Active.color` for `DISCO` only
- [x] 1.6 `led_state.cpp`: `isOn(DISCO, _) == true`; `render()` treats `DISCO` like `SOLID` (full factor, single brightness multiply) - no change needed in `render()`, only `PULSE` was ever special-cased
- [x] 1.7 `led_state.cpp`: `PATTERN_NAMES[]` gains `"disco"`; the existing `static_assert` against `PATTERN_COUNT` still holds
- [x] 1.8 **Found during implementation, not in the plan**: the fold restarts from step 0 on every render, so an *indefinite* disco would grow the render path linearly with uptime (tens of thousands of iterations 40x/second after a few hours). `led_state` allows indefinite disco even though the API caps it, so the fold length is bounded by `DISCO_SEQUENCE_STEPS = 256` (~31 s, twice the API cap - never observed by a caller). design.md D1 updated

## 2. Host unit tests

- [x] 2.1 `source/test/test_led_state/test_led_state.cpp`: same seed + same elapsed times -> identical sequence
- [x] 2.2 Different seeds diverge within the first few steps
- [x] 2.3 Colour is constant inside one 120 ms step and different in the next, sampled at the 25 ms tick
- [x] 2.4 No two consecutive steps are equal, over a full 15 s run (across 8 seeds)
- [x] 2.5 `seed = 0` behaves like any other seed - asserts at least 5 of the 8 palette entries are used
- [x] 2.6 Disco never renders dark: `isOn` true and output != `Colors::OFF` at every tick for the whole duration, with the requested colour set to `{0,0,0}`
- [x] 2.7 Brightness applied once: disco at 50% equals the same step's colour rendered solid at 50%
- [x] 2.8 Disco on `user` expires like any timed indication and reveals `status` beneath it
- [x] 2.9 `patternName`/`patternFromName` round-trip `"disco"` (plus the existing enum-walking round-trip test, which now covers `DISCO` automatically)
- [x] 2.10 Register every new test in the hand-written `main()` (RUN_TEST list) - two places per test
- [x] 2.11 `pio test -e native` green from WSL: **487/487** across the full suite; `test_led_state` 51/51, all 10 new disco tests passing

## 3. `Led` adapter

- [x] 3.1 `include/led.h`: `setPattern(Layer, ...)` gains a trailing `uint32_t seed = 0`; the `LedPriority` overload is unchanged
- [x] 3.2 `src/led.cpp`: pass `seed` through to `LedState::set()` under the existing mutex; no other change to the task

## 4. REST API

- [x] 4.1 `customserver.cpp` `_serveLedEndpoints()`: parse `pattern` **before** the colour channels
- [x] 4.2 Require `red`/`green`/`blue` only when `pattern != DISCO`; ignore them when supplied with disco
- [x] 4.3 Parse optional `seed`: `is<int64_t>()`, range `0..UINT32_MAX`, else 400 "Invalid seed parameter". Absent -> `(uint32_t)millis()`
- [x] 4.4 Disco duration policy: absent or 0 -> 15000 ms; > 60000 -> clamped to 60000 (design D5). Other patterns unchanged
- [x] 4.5 `DISCO_DEFAULT_DURATION_MS` / `DISCO_MAX_DURATION_MS` in `include/customserver.h`, next to the other API limits
- [x] 4.6 Every new exit path goes through `_sendErrorResponse`/`_sendSuccessResponse` (they release `_apiMutex`) - the two new 400s and the single success path
- [x] 4.7 `HTTP_MAX_CONTENT_LENGTH_LED_COLOR` (128) still fits: the largest legal disco body incl. redundant channels is ~93 bytes. Unchanged

## 5. Swagger

- [x] 5.1 `resources/swagger.yaml` `PUT /api/v1/led/color`: `disco` added to the `pattern` enum, `seed` property added, disco-specific `duration_ms` default and cap documented, `required: [red, green, blue]` replaced by a prose statement of the conditional requirement
- [x] 5.2 Line endings verified: the diff is 26 lines under the repo's own settings, not a whole-file rewrite

## 6. Web UI

- [x] 6.1 `js/api-client.js`: generic `delete(endpoint)` helper alongside `put`/`patch`
- [x] 6.2 `js/api-client.js`: `setLedDisco(durationMs)` and `clearLedColor()`
- [x] 6.3 `html/configuration.html`: section heading is now **LED**, `🪩 Disco mode` button added after the brightness button, reusing `buttonForm` + `showStatus()`
- [x] 6.4 `html/configuration.html`: `startDisco()` - disables the button and counts down in its label for the duration, then restores; on error shows the message and re-enables immediately
- [x] 6.5 No new page load request: disco is not added to `loadConfigurationData()`

## 7. Review and verification

- [ ] 7.1 Code-review agent(s) over the branch diff, per the project PR gate
- [ ] 7.2 `simplify` skill pass; triage every finding
- [x] 7.3 Re-run `pio test -e native` after every fix round
- [ ] 7.4 `pio run -e esp32s3-dev` clean (only when asked - Jibril builds)
- [x] 7.5 First bench test by Jibril: LED behaviour confirmed working
- [ ] 7.6 Check the LED task stack high-water via `/api/v1/system/info` for no regression from the per-render fold
- [ ] 7.7 Open PR to `development` with `Closes #224`, labelled `enhancement` + `ux`

## 8. Follow-up after the first bench test (2026-08-08)

- [x] 8.1 Backend `DISCO_MAX_DURATION_MS` raised to 60000; default stays 15000
- [x] 8.2 `DISCO_SEQUENCE_STEPS` raised 256 -> 1024 (~123 s). It has to stay above the API cap: the walk restarts at the wrap, which is the one place the no-repeat guarantee can break, and 256 steps was only ~31 s
- [x] 8.3 `test_disco_never_repeats_consecutive_colours` extended to the full 60 s ceiling
- [x] 8.4 Frontend duration cut to 10 s
- [x] 8.5 Full-page disco effect: fixed `pointer-events: none` overlay that darkens and cycles tints. Steps at 400 ms (2.5 Hz), **not** the LED's 120 ms - a full-screen flash in the 3-60 Hz band is a photosensitivity risk a pinpoint LED is not. `prefers-reduced-motion` holds a static tint
- [x] 8.6 `pio test -e native -f test_led_state` green after the changes
- [ ] 8.7 Second bench test by Jibril
