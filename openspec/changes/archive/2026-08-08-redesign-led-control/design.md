## Context

See `proposal.md` - Why for the motivation, and `specs/led-indicator/spec.md` for the behaviour contract.

Constraints that shape the approach:

- `Led::setPattern()` is called from at least five tasks (main/setup, WiFi task, button task, web server handlers, restart/factory-reset task) and from paths that run while other services are being torn down. Every call site is a task; none is an ISR, so blocking primitives are permitted.
- The LED task runs at FreeRTOS priority 1 with a 2 KB internal-RAM stack and no logger. Nothing in the render path may allocate or log.
- ~40 existing call sites use the `PRIO_NORMAL`/`PRIO_MEDIUM`/`PRIO_URGENT`/`PRIO_CRITICAL` constants and the `setRed`/`blinkBlueFast`/… helpers. Rewriting all of them in the same change would produce exactly the mega-commit the project forbids.
- Host unit tests build without Arduino or FreeRTOS. Anything to be tested must live in `source/lib/` with no framework includes, as `web_auth_gate` and `wifi_provisioning` already do.

## Goals / Non-Goals

**Goals:**

- One authoritative resolution rule for "what colour is the LED right now", living in one place, testable on the host.
- Make layer release a first-class operation so no caller ever has to re-assert what should come next.
- Keep the existing call sites compiling unchanged, so the behavioural rewrite and the call-site cleanup are separate commits.

**Non-Goals:**

- No new patterns beyond the six already in `LedPattern`.
- No persistence for the `user` layer; a reboot clears it. Persisting a user override risks a device booting into a colour that hides a real fault.
- No per-layer brightness. Brightness stays a single global percentage, with per-layer render-time floors.
- Nothing in the shadow, in either direction. It does not accept a desired colour (which would be re-applied on every reconnect, outliving the reason it was set) and it does not report the rendered one (see D9).
- No web UI control for the user colour in this change.

## Decisions

### D1: Priority layer table, not a command queue

Replace `QueueHandle_t _ledQueue` + `LedState _state` with a fixed array of five layer slots, each holding `{occupied, pattern, color, setAtMs, durationMs}`. Rendering resolves to the highest-priority occupied slot.

*Why:* the queue conflates two different things - transporting a command across a task boundary, and arbitrating between simultaneous indications. Arbitration over a FIFO is where every current bug lives: the loser is re-queued and retried forever, the queue fills at 10 entries and silently drops, and a re-queued command's duration restarts on acceptance. A layer table has none of those failure modes because there is nothing to arbitrate at write time - a write lands in its own slot and cannot collide.

*Alternative considered:* keep the queue but fix arbitration (drop the loser instead of re-queueing). Rejected: it fixes the spin but not "release reveals the layer below", which is the requirement that forces every caller to re-assert state. The `customwifi.cpp:587` hack would survive.

*Alternative considered:* keep the queue purely as a transport (LED task is the sole owner of the table, commands are "set slot"/"clear slot"). Rejected: it reintroduces a bounded queue that can drop under burst, for no benefit over a mutex whose critical section is a 16-byte struct copy.

### D2: Stateless renderer, not an enter/update/exit FSM

Issue #111 proposes a named-state FSM with per-state enter/update/exit hooks. This design instead makes the renderer a pure function:

```
resolve(layers[], nowMs) -> ActiveIndication   // which layer wins, or none
render(indication, nowMs, brightness) -> Color // what the pins should be driven to
```

Each pattern's waveform is a function of `nowMs - setAtMs` alone. There is no retained render state, so there is nothing to enter or exit.

*Why:* the enter/exit hooks in a classic FSM exist to keep retained state consistent across transitions. Here the only retained state is the layer table, which D1 already makes the single source of truth. Adding hooks would create a second, derived copy of "what is showing" that can drift from the table - the exact class of bug being removed. The named states #111 asks for still exist and are explicit (the five layers, the six patterns); they are just data rather than control flow. Note this deviation when closing #111.

*Consequence:* pattern phase restarts whenever a layer is written, because `setAtMs` is rewritten. This matches the spec ("pattern phase restarts on set") and is what makes a blink visibly acknowledge a repeated event.

### D3: `source/lib/led_state/` holds the resolver and the renderer

New host-compilable module with no Arduino, FreeRTOS or `Preferences` includes:

- `LedState::Layer` enum (`STATUS`, `USER`, `NETWORK`, `ALERT`, `CRITICAL`), `Pattern` enum, `Rgb` struct.
- `Table` holding `LAYER_COUNT` slots plus `set()`, `release()`, `releaseAll()`.
- `step(Table&, nowMs, brightness) -> Frame` - the one entry point a caller needs. It composes `expire()`, `resolve()`, `effectiveBrightness()` and `render()`, so no caller can get the order wrong or skip a step, and the host tests exercise the same composition the render task runs rather than a re-implementation of it. The pieces stay exposed for tests and single-purpose callers.
- Per-layer brightness floors as a `LAYER_MIN_BRIGHTNESS_PERCENT[]` table indexed by `Layer`, `static_assert`ed to cover every layer, so a new layer cannot forget to state one.

`Led` in `src/led.cpp` becomes the adapter: owns the pins, the task, the mutex and NVS brightness; calls into `LedState` and writes the result with `ledcWrite()`.

*Why:* mirrors the `web_auth_gate` precedent, and makes every scenario in the spec a unit test rather than a bench observation. Layer precedence, fallback-on-release, expiry timing and each waveform are all pure functions of inputs.

### D4: Mutex-guarded table, single reader

A module-lifetime `SemaphoreHandle_t` created in `begin()` and never deleted guards the table. Writers (`setPattern`, `clearPattern`, `clearAllPatterns`, the API handlers) take it, mutate one slot, release. The LED task takes it, copies the table, releases, then renders from the copy - matching the project's "copy under the mutex, act on the copy" rule.

If the mutex does not exist (called before `begin()` or after `stop()`), every entry point returns without touching the table. Teardown paths must stay safe: `_restartTask()` sets a critical indication and then stops services, and `_factoryReset()` runs with the logger already gone.

`getState()` - the snapshot used by `GET /api/v1/led` and the shadow report - resolves under the same mutex and returns a value struct, so the web task never observes a torn slot.

### D5: 25 ms render tick, single wait per iteration

The task loop waits exactly once per iteration, on `ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25))`, and renders every wake.

*Why:* the current loop waits `LED_TASK_DELAY_MS` twice (queue receive, then notify) for a ~100 ms effective period against a 250 ms `BLINK_FAST` half-cycle and 100 ms `DOUBLE_BLINK` segments. 25 ms gives ≥4 samples per shortest segment and 80 steps per `PULSE` fade. Cost is 40 wakeups/second of a priority-1 task doing three `ledcWrite()` calls.

*Trade-off:* with the queue gone there is no wake-on-write, so a `setPattern()` call is reflected within one tick (≤25 ms) rather than immediately. The stop notification is unchanged and still wakes the task at once. 25 ms is below the perceptual threshold for a status LED; adding a second notification channel to shave it is not worth the complexity.

### D6: Brightness applied once, with a critical-only floor

`render()` scales the colour by `brightnessPercent` exactly once. `PULSE` folds its fade factor into that same multiply instead of applying `_calculateBrightness()` a second time on top of `_setHardwareColor()`'s.

When the winning layer is `CRITICAL`, the effective brightness is `max(configured, LED_CRITICAL_MIN_BRIGHTNESS_PERCENT)` (proposed: 10). Nothing is written to NVS.

*Why:* the three `Led::setBrightness(max(Led::getBrightness(), 1))` calls exist only to make critical indications visible at brightness 0, but `setBrightness()` persists, so a button press permanently rewrites a user's stored 0 to 1. A render-time floor is what those call sites actually meant. Those three calls are deleted.

### D7: Compatibility shim over the existing priority constants

`PRIO_NORMAL`/`PRIO_MEDIUM`/`PRIO_URGENT`/`PRIO_CRITICAL` and all the `setRed`/`blinkGreenSlow`/… helpers stay, mapped through `layerForPriority(LedPriority)`: `≤1 → STATUS`, `≤5 → NETWORK`, `≤10 → ALERT`, else `CRITICAL`. `PRIO_USER = 0 → USER`.

*Why:* the whole point is that call sites should not need to change for the semantics to become correct. It also splits the work: the engine lands and is verified first; the call-site cleanup (dropping the `setGreen` re-assert hack and the brightness-floor workarounds) is a separate, reviewable commit.

The constants are kept, not deprecated - a numeric priority is a reasonable public spelling and the layer enum is the internal one. `setPattern()` gains a `Layer` overload for new code, including the API handlers.

### D8: `PUT` semantics for the user layer

`PUT /api/v1/led/color` sets the `USER` slot; `DELETE` releases it. Validation rejects a missing or non-integer channel, a channel outside 0-255, an unknown pattern name, or a negative `duration_ms`, with HTTP 400 and no mutation. Both routes register under the existing LED endpoint group so they inherit the current auth and rate-limit middleware unchanged.

`GET /api/v1/led` returns the *resolved* indication, not the user slot - the caller needs to know what the LED is actually doing, and a Home Assistant integration needs to see that a system indication has taken over. When the layer table cannot be read it returns 503 rather than reporting `off`: a caller cannot distinguish a wrong answer from a real one.

### D9: Where `USER` sits, and what the shadow carries — both settled during review

Two decisions in the first draft of this design were wrong, and the branch review caught them. Recording them because both are easy to re-introduce.

**`USER` is above `STATUS`, not at the bottom.** The draft put `USER` last on the reasoning that system status must always win. But `STATUS` is not an event - the firmware occupies it during boot (`main.cpp` ends setup with `setGreen(PRIO_NORMAL)`) and nothing ever releases it. `clearPattern(PRIO_NORMAL)` appears at zero call sites. A user colour underneath it would therefore have been invisible for the entire uptime of a healthy device: `PUT` would return 200, `GET` would report `layer: "status"`, and the LED would not change. The whole point of issue #105 would have shipped inert. `USER` sits directly above the ambient baseline and below everything that carries an actual event, which gives the intended guarantee ("a user colour can never hide a fault") without the one that made it useless.

**The shadow reports no LED state.** The draft had `_reportSystem()` publish the rendered pattern, colour and layer. The reported `system` object is republished whenever it drifts, on a 3 s check, and the rendered indication changes on every WiFi flap, button press and update - so a device on a marginal link would publish its configuration shadow twice per flap, forever. It is also poor telemetry: a 2 s blink falls between two checks. Dropped entirely; `led_brightness` stays as it was.

### D10: `/api/v1/led` is registered with an exact matcher

A matcher built from a plain string is `BackwardCompatible` in the pinned ESPAsyncWebServer, which matches the path *and everything under it* (`WebServer.cpp:339`), and `_attachHandler()` takes the first match in registration order (`WebServer.cpp:145`). Registered before its siblings, `GET /api/v1/led` silently swallows `GET /api/v1/led/brightness` and returns the wrong document.

`server.on(AsyncURIMatcher::exact("/api/v1/led"), HTTP_GET, ...)` removes the hazard at the declaration instead of relying on registration order, so nothing added to this group later can break it.

Worth a follow-up beyond this change: `/api/v1/led` is not the only route that prefixes a sibling. `/api/v1/logs` vs `/logs/clear` and `/logs/level`, `/api/v1/ade7953/config` vs `/config/reset`, and the same shape for `ade7953/channel`, `custom-mqtt/config`, `influxdb/config` and `system/safe-mode` are all currently safe only because their HTTP methods happen to differ — which no one has stated as an invariant. Sweeping those onto `exact()` is out of scope here.

## Risks / Trade-offs

- **Behaviour change at ~40 call sites that nobody exercises deliberately** → the shim (D7) keeps the mapping mechanical, and the layer semantics are strictly more permissive than today (nothing is dropped). Bench-verify the sequences that currently misbehave: boot colour walk, WiFi disconnect/reconnect, button press-and-hold ladder, OTA, safe mode.
- **A layer left occupied indefinitely masks everything below it forever** → this is already true today and worse (it also blocks the queue). It is now visible: `GET /api/v1/led` reports the owning layer, so a stuck layer is diagnosable rather than inferred from a wrong colour. The review found one indefinite `CRITICAL` that is *not* on a path ending in a restart — `performNvsRestore()` continues booting — and it is released explicitly. The remaining indefinite `CRITICAL` sets all end in a restart.
- **Removing `customwifi.cpp:587`'s `setGreen` re-assert could leave the LED dark** if nothing ever occupied `STATUS` → keep an explicit `setGreen(STATUS)` at the point the device becomes healthy (that is a genuine status assertion, not a workaround) and delete only the duplicate at `customwifi.cpp:1404`. Verify on the bench before the cleanup commit is merged.
- **40 Hz wakeups on the LED task** → measured against the existing ~10-20 Hz; three `ledcWrite()` calls and a struct copy. Confirm via `getTaskInfo()` stack high-water and the task profiler that nothing regresses.
- **Mutex on a teardown path** → the mutex is never deleted, critical sections contain no I/O, no logging and no allocation, and every entry point no-ops when the mutex is absent. Worst case a caller waits microseconds.
- **A user colour could hide a fault** → `USER` is the lowest layer and never persists, so a reboot clears it and any system indication wins while active.

## Migration Plan

No persisted state changes: `PREFERENCES_NAMESPACE_LED/brightness` keeps its key, range and meaning, and no new NVS key is added. An older firmware flashed over this one reads the same brightness value.

Deploy is a normal OTA. Rollback is reverting the commits; nothing on the device needs undoing.

Sequencing (one concern per commit): pure `led_state` module + host tests → `Led` adapter rewritten onto it, shim in place, existing call sites untouched → REST endpoints + swagger → call-site cleanup (the `setGreen` hack, the three `setBrightness(max(...,1))` workarounds) → review, fixes and simplify pass → bench verification.
