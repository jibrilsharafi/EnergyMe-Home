# Add a temporary disco mode LED pattern

## Why

Issue #224: a fun/diagnostic "disco mode" - rapid pseudo-random colour changes for a fixed, short duration. It is also the first LED feature that reaches the web UI: the previous LED change (#111/#105) deliberately shipped the REST surface with no web control, so today the only way to drive the LED is with a hand-written HTTP request. A one-click button that visibly does something is both the feature and the proof the colour API works from the browser.

## What Changes

**Firmware - `led_state`**

- New `Pattern::DISCO`: colour changes every ~120 ms, chosen from the existing vivid palette by a seeded xorshift32 keyed on `(seed, elapsedMs / 120)`. No `random()`, no retained state, so it stays a pure function of its inputs and is host-testable.
- `Slot` gains a `seed` field, written by `set()`. Only `DISCO` reads it.
- `resolve()` substitutes the current disco colour into the returned `Active`, so `render()` treats `DISCO` exactly like `SOLID` and `GET /api/v1/led` reports the colour that is actually lit rather than a stored placeholder.
- `patternName()` / `patternFromName()` gain `"disco"`.

**REST API**

- `PUT /api/v1/led/color` accepts `pattern: "disco"` with an optional `seed` (uint32) and `duration_ms`.
- With `disco`, `red`/`green`/`blue` become optional (they are ignored), `duration_ms` defaults to 15000 ms when absent or 0, and is clamped to 15000 ms. Disco is never indefinite.
- Omitted `seed` is derived from the device clock, so two presses do not replay the same sequence.
- No new route, no new auth or rate-limit surface: the existing `user` layer, its expiry, and `DELETE /api/v1/led/color` cover the whole lifecycle.
- `resources/swagger.yaml` updated.

**Web UI**

- A "🪩 Disco mode" button in the existing **LED Brightness** block of `configuration.html`, next to the brightness slider. One click fires the 15 s disco; the button is disabled with a countdown until it expires.
- `energyApi.setLedDisco(durationMs)` / `clearLedColor()` added to `js/api-client.js`.

**Tests**

- Host unit tests: same seed produces the same sequence, different seeds diverge, colour holds for one 120 ms bucket and then changes, disco expires like any other timed indication, `"disco"` name round-trips.

## Capabilities

### New Capabilities

_None._

### Modified Capabilities

- `led-indicator`: adds `disco` to the supported pattern table with its determinism and cadence contract; extends the `PUT /api/v1/led/color` contract with `seed`, disco-specific `duration_ms` defaulting and capping, and optional colour channels for that pattern.

## Impact

- **Code**: `source/lib/led_state/led_state.{h,cpp}`, `source/include/led.h` + `source/src/led.cpp` (seed passthrough on `setPattern()`), `source/src/customserver.cpp` (`_serveLedEndpoints()`), `source/test/test_led_state/test_led_state.cpp`, `source/html/configuration.html`, `source/js/api-client.js`, `source/resources/swagger.yaml`.
- **API**: `PUT /api/v1/led/color` gains optional `seed` and a new `pattern` value. Existing bodies keep their exact meaning - no **BREAKING** change.
- **Persistence**: none. No NVS key added; the `user` layer stays volatile.
- **Resources**: one `uint32_t` per layer slot (5 slots), a small `constexpr` palette index table, no new task or timer. The render tick is unchanged at 25 ms, which samples a 120 ms bucket ~5 times.
- **Issues**: closes #224.
