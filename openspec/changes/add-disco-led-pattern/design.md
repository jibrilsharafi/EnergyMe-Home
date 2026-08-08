## Context

See `proposal.md` for the motivation and `specs/led-indicator/spec.md` for the delta contract. The engine this builds on is `source/lib/led_state/` (archived change `2026-08-08-redesign-led-control`, decisions D1-D10).

Constraints that shape the approach:

- `led_state` is host-compilable and must stay Arduino-free and deterministic. `random()`, `millis()` and any retained render state are unavailable by construction (D2: the renderer is a pure function of `nowMs - setAtMs`).
- The render tick is 25 ms (`LED_TASK_DELAY_MS`). Any disco step shorter than ~100 ms would alias.
- `render()` has one brightness multiply (`_scale`), and D6 requires brightness be applied exactly once. Disco must not add a second one.
- The LED task has a 2 KB internal-RAM stack, no logging and no allocation in the render path.
- `Pattern` values are not persisted anywhere, so the enum can be extended.
- The API handler runs under `_apiMutex`; `_sendSuccessResponse`/`_sendErrorResponse` release it, so every exit path must go through one of them.

## Goals / Non-Goals

**Goals:**

- A visibly fun pattern that is fully reproducible from `(seed, elapsedMs)`, so it is unit-testable on the host like every other pattern.
- Zero new lifecycle code: disco is a pattern on the `user` layer and expires through the existing duration mechanism.
- One button in the existing configuration page - the first browser-side use of the LED colour API.

**Non-Goals:**

- No colour picker or general LED colour UI. That remains the open follow-up from the previous change; this adds one button, not a control panel.
- No new layer, no new endpoint, no persistence, no shadow field.
- No smooth colour interpolation or brightness modulation. Disco is a step sequence.
- No disco on any layer other than `user` in practice; nothing forbids it, but no firmware call site sets it.

## Decisions

### D1: Palette walk, not free RGB (superseded by the even/odd split below)

Each 120 ms step picks one entry from the existing `LedState::Colors` vivid palette (`RED, GREEN, BLUE, YELLOW, PURPLE, CYAN, ORANGE, WHITE` - 8 entries) rather than generating three random channel bytes.

*Why:* random RGB spends most of its range on muddy low-saturation values that read as "the LED is broken", and it can land on near-black. A fixed saturated palette always looks deliberate, costs 8 `constexpr Rgb` already defined in the header, and makes the unit tests assert on exact colours instead of statistical properties.

The first implementation removed consecutive repeats by advancing the index by `1 + (hash % (PALETTE_SIZE - 1))` from the previous step's index, walked from step 0 on every render call (bounded by a `DISCO_SEQUENCE_STEPS` wraparound so an indefinite disco couldn't make the walk grow with uptime). Review before merge found two problems with that: the walk was O(steps) on the LED render task's hot path, and - more importantly - a hash collision two steps back could cascade, so the "no consecutive repeat" guarantee it was written to provide could actually be violated right at the `DISCO_SEQUENCE_STEPS` wrap. Replaced by the even/odd split below, which fixes both: it's O(1) per call and the no-repeat property holds by construction, not by walking and hoping.

**Even/odd palette split:** the 8-entry palette is split into two disjoint halves, indices `{0,2,4,6}` and `{1,3,5,7}`. Step *n* draws from the half selected by `n`'s parity: `index = 2 * (hash(seed, n) % 4) + (n % 2)`. Consecutive steps always draw from different halves, so `index(n) != index(n-1)` unconditionally - no comparison to the previous step's value is needed, so there is nothing to walk and nothing that can cascade. `DISCO_PALETTE_SIZE` must be even for the split to exist; a `static_assert` enforces it.

*Consequence:* `discoColor()` is a single hash call regardless of `elapsedMs`, so there is no `DISCO_SEQUENCE_STEPS` bound to keep in step with the API's duration cap (D5) - the cross-layer coupling that constant introduced is gone entirely.

### D2: xorshift32 keyed on `(seed, stepIndex)`

```
uint32_t h = seed ^ (stepIndex * 0x9E3779B9u);  // decorrelate adjacent steps
h ^= h << 13; h ^= h >> 17; h ^= h << 5;        // xorshift32
```

Seeded per step rather than iterated across steps, so a colour can be computed for any step in O(1) - each step's hash stands on its own, and D1's even/odd split (not this hash) is what guarantees adjacent steps differ.

`seed == 0` is a valid seed here because the multiply-and-xor gives a non-zero state for every step index except one. The tests cover `seed = 0`.

### D3: The seed lives in the slot; `resolve()` resolves the colour

`Slot` gains `uint32_t seed = 0`, written by `set()` through a new trailing parameter defaulted to `0`, so no existing caller changes.

`resolve()` substitutes the disco colour into the `Active` it returns:

```cpp
active.color = slot.pattern == Pattern::DISCO
                   ? discoColor(slot.seed, active.elapsedMs)
                   : slot.color;
```

`render()` then needs no seed at all: `DISCO` falls into the same branch as `SOLID` (always on, `FULL_PERMILLE`, one brightness multiply).

*Why:* the alternative - threading `seed` through `render()` - would put a pattern-specific parameter into the generic signature and leave `GET /api/v1/led` reporting the stored placeholder colour instead of the colour actually lit. Resolving in `resolve()` means the REST snapshot, `isLit`, and the pins all read the same value with no extra plumbing.

*Consequence:* `Active.color` for disco changes on every step, which is exactly what the spec's "reported colour is the colour being shown" requires. `Slot.color` is left untouched and unused for disco.

`isOn(DISCO, _)` returns `true` unconditionally.

### D4: 120 ms step

`DISCO_STEP_MS = 120`, exported as a `constexpr` next to the other period constants so tests and callers can reference it.

*Why:* the issue asks for 100-150 ms. 120 ms gives ~8 changes per second (fast enough to read as disco, slow enough that each colour registers) and is a clean multiple of the 25 ms tick minus one - each step is sampled 4 or 5 times, never fewer than 4, so no step can be skipped.

### D5: Disco policy lives in the API handler, not in `led_state`

The 15 s default and the 60 s cap are enforced in `_serveLedEndpoints()`, not in the engine. `led_state` will happily run disco indefinitely if asked.

The cap is the API's ceiling for an automation that wants a long attention signal; the web button asks for far less (D7). Default and maximum are separate constants precisely because they answer different questions - "what does a caller who said nothing get" versus "how long may a caller insist on".

*Why:* `led_state` is a mechanism (what does layer X show at time T); "disco is a novelty and must not be left running" is a policy about the public API. Putting the cap in the engine would also make the pure module carry a magic number no unit test of the mechanism cares about.

Out-of-range `duration_ms` is **clamped, not rejected**, matching the issue's "capped". Rejecting would be inconsistent: no other pattern has an upper bound, so a 400 on a long `duration_ms` would surprise a caller who read the existing contract. Malformed values (negative, non-integer) still 400 through the existing check.

### D6: Validation order changes so colour can be optional for disco

The handler currently reads `red`/`green`/`blue` before `pattern`. It is reordered to parse `pattern` first, then require the channels only when `pattern != DISCO`.

`seed` and `duration_ms` are both optional integers with a range check, so their validation shares a `_readOptionalRangedInt()` helper next to `_readColorChannel` rather than repeating the same `is<int64_t>()` / range / error-response shape twice. `seed`'s range is `0 .. 4294967295`. When absent, the firmware passes `esp_random()` (not `millis()` - two requests inside the same millisecond would otherwise get the same seed), so two presses differ.

`HTTP_MAX_CONTENT_LENGTH_LED_COLOR` is 128 bytes. A maximal disco body (`{"pattern":"disco","seed":4294967295,"duration_ms":15000}`, 56 bytes) fits with room to spare, so the limit is unchanged.

### D7: Web UI is one button, no new page or picker

A `🪩 Disco mode` button is added to the existing **LED Brightness** `section-box` in `configuration.html`, reusing `buttonForm`, the `loading` class and `showStatus()` exactly as `setLedBrightness()` does. The section heading becomes **LED** since it now holds more than brightness.

The button asks for 10 s, well under the API's 60 s ceiling: it exists to answer "which meter am I looking at", and a browser trigger should not be able to take the LED for a minute. It disables itself for that period and shows a seconds countdown in its label, then restores. On error it re-enables immediately. No polling of `GET /api/v1/led` - the duration is known client-side and a poll would add request load for nothing.

While it runs, a fixed full-page overlay darkens the page and steps through tinted colours, so the browser visibly joins in. It is `pointer-events: none`, so nothing on the page becomes unclickable, and it steps at **400 ms (2.5 Hz), not the LED's 120 ms**: a full-screen flash in the 3-60 Hz band is a photosensitivity risk in a way a pinpoint LED is not. `prefers-reduced-motion` holds a static tint instead.

`api-client.js` gains `setLedDisco(durationMs)` (a `put('led/color', {pattern: 'disco', duration_ms})`) and `clearLedColor()`. `clearLedColor()` needs a `delete()` helper, which the client does not have yet; it is added alongside `put()`/`patch()` in the same shape.

*Why include `clearLedColor()` when the button does not use it:* the DELETE route is the documented way to stop the pattern early, and the helper is three lines. It is the natural pair for a future colour control.

### D8: `seed` is not exposed in the UI

The browser sends no `seed`, so the device picks one. Reproducibility is an API/test affordance (the issue asks for it so the unit tests can assert determinism), not something a user wants from a button.

## Risks / Trade-offs

- **Confirm the LED task stack high-water** via `/api/v1/system/info` after the change, as the previous LED change did - `discoColor()` is O(1) (D1), so this is a formality rather than a real concern.
- **A disco left running masks the ambient status colour for up to 60 s** → same exposure as any other timed `user` indication, and strictly bounded by the cap. Every layer above `user` still overrides it, so a fault is never hidden.
- **Adding an enum value shifts `Pattern::Count`** → nothing persists a pattern value and the wire names are strings, so the only coupling is `PATTERN_NAMES[]`, which is already `static_assert`ed against `PATTERN_COUNT`. Append `DISCO` after `DOUBLE_BLINK`, before `Count`.
- **`elapsedMs` clamps a future `setAtMs` to 0** (existing `_elapsed()` behaviour) → disco restarts its sequence rather than misbehaving. No new failure mode.
- **The UI countdown drifts from the device** if the request is slow or a higher layer takes over → cosmetic only; the button is a fire-and-forget trigger, and the device releases the layer on its own schedule regardless of what the button shows.

## Migration Plan

Nothing to migrate. No NVS key, no persisted pattern value, no changed default. An older firmware flashed over this one simply does not know the `disco` name and returns 400, which is the correct answer for it.

Sequencing (one concern per commit):

1. `led_state`: `DISCO` pattern, `Slot.seed`, `discoColor()`, wire name - plus its host tests.
2. `led.cpp` / `led.h`: `seed` passthrough on the `Layer` overload of `setPattern()`.
3. `customserver.cpp`: validation reorder, `seed` parsing, disco duration policy.
4. `swagger.yaml`.
5. Web UI: `api-client.js` helpers, then the button in `configuration.html`.
6. Review + simplify pass, then bench verification by Jibril.
