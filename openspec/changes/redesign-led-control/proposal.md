# Redesign LED control: priority layers, explicit state machine, user API

## Why

The RGB LED is the only feedback channel on a headless meter, and today it lies. `Led::setPattern()` pushes commands into a FIFO queue where a lower-priority command that loses arbitration is *re-queued at the back* and retried every loop. Once any indefinite high-priority pattern is active (`Led::setOrange(PRIO_CRITICAL)` in `utils.cpp:2318`, `Led::blinkOrangeFast(PRIO_CRITICAL)` in `customwifi.cpp:927`) nothing lower ever wins again: the 10-slot queue fills with spinning retries and further commands are silently dropped. The displayed colour stops matching the device's actual state, which is exactly the reported symptom.

The same model has no notion of "release my layer and reveal what is underneath". `clearPattern(prio)` is implemented as *turn the LED off at that priority*, so callers have to guess what should come next and re-assert it by hand. `customwifi.cpp:587` says so in the source: `// Hack: to ensure we get back to green light, we set it here even though a proper LED manager would handle priorities better`.

Two open issues ask for the follow-ups this unblocks: #111 (refactor the LED task to an explicit state machine) and #105 (user-controlled LED colour API for Home Assistant and automations). Neither is safely buildable on the current queue.

## What Changes

**Ownership model — replace the arbitration queue with priority layers**

- Each source owns a named layer (`STATUS`, `USER`, `NETWORK`, `ALERT`, `CRITICAL`, lowest first). A layer holds at most one request (pattern + colour + optional expiry). Setting a layer overwrites that layer only; it never competes with, blocks, or evicts another.
- The rendered output is always the highest-priority *occupied* layer. Releasing a layer, or letting its duration expire, reveals the next layer down automatically, with no re-assertion by the caller.
- Commands are never dropped or reordered: a request either lands in its layer or replaces what that layer held.
- Removes the "re-queue on arbitration loss" path, the queue-overflow drop, and the `setGreen` re-assert hack.

**State machine — issue #111**

- Extract pattern rendering into `lib/led_state/`, a pure, Arduino-free, host-testable module: layer table + `resolve()` + an explicit per-pattern state machine stepped by `tick(nowMs) -> Color`, with defined enter/step semantics per state.
- The firmware `Led` namespace becomes a thin adapter: own the FreeRTOS task, feed it wall time, write the resolved colour to the PWM pins.
- Add Unity tests under `test/test_led_state/` covering layer precedence, fallback on release, expiry, and each pattern's waveform.

**REST API — issue #105**

- `GET /api/v1/led` — current resolved pattern, colour, owning layer, remaining duration, and brightness. Today there is no way to read back what the LED is doing.
- `PUT /api/v1/led/color` — set the `USER` layer: RGB, optional pattern, optional duration. It sits just above the ambient `STATUS` layer, so it replaces the steady healthy colour but is overridden by anything eventful.
- `DELETE /api/v1/led/color` — release the `USER` layer.
- `GET`/`PUT /api/v1/led/brightness` keep their current contract.
- The device shadow is deliberately left alone. Reporting the rendered indication there would republish the otherwise near-static `system` document on every WiFi flap and button press.

**Correctness fixes found while reading the current code**

- `PULSE` applies `_calculateBrightness()` in `_processPattern()` and again in `_setHardwareColor()`, so pulse is dimmed by brightness squared (at 75% it peaks at 56%).
- The task loop waits `LED_TASK_DELAY_MS` twice per iteration (queue receive timeout *and* notify timeout), giving a ~100 ms render period against a 250 ms `BLINK_FAST` half-cycle, and rendering output that is already 50 ms stale.
- A duration-limited command that loses arbitration and is re-queued restarts its full duration whenever it is finally accepted, instead of expiring on schedule.
- `Led::setBrightness(max(Led::getBrightness(), 1))` at `utils.cpp:619`, `utils.cpp:927` and `buttonhandler.cpp:160` exists to make critical alerts visible when the user set brightness to 0 — but `setBrightness()` persists to NVS, so pressing the button silently and permanently overwrites the user's saved brightness. Replaced by a render-time floor applied to the `CRITICAL` layer only, with nothing persisted.
- `min(max(brightness, (uint8_t)0), ...)` is a no-op clamp on an unsigned type.
- `performNvsRestore()` (`utils.cpp`) sets an indefinite `PRIO_CRITICAL` orange and never releases it, on a path that continues booting rather than restarting — so after any configuration restore the LED is stuck orange for the whole uptime and the success/failure indications it sets afterwards are masked.
- `buttonhandler.cpp` clears the button-feedback priority *after* `_processButtonPress()` has set the outcome indication on that same priority, so the confirmation blink is released before it is ever rendered.

**Cleanup**

- Final review-and-simplify pass over `led.cpp`/`led.h` and the touched call sites, per the project's PR gate.

## Capabilities

### New Capabilities
- `led-indicator`: what the status LED shows, who owns it, how conflicting indications resolve, and the REST surface for reading and driving it.

### Modified Capabilities

_None — no existing spec covers LED behaviour._

## Impact

- **Code**: `source/src/led.cpp`, `source/include/led.h` (rewritten around layers); new `source/lib/led_state/`; new `source/test/test_led_state/`; call-site updates in `customwifi.cpp`, `buttonhandler.cpp`, `utils.cpp`, `customserver.cpp` (`_serveLedEndpoints()`).
- **API**: adds `GET /api/v1/led`, `PUT /api/v1/led/color`, `DELETE /api/v1/led/color`; `resources/swagger.yaml` updated. Existing brightness endpoints unchanged.
- **Compatibility**: `PRIO_NORMAL`/`PRIO_MEDIUM`/`PRIO_URGENT`/`PRIO_CRITICAL` and the `setRed`/`blinkBlueFast`/… convenience helpers stay, mapped onto layers, so the ~40 existing call sites keep compiling. No **BREAKING** change to callers.
- **Persistence**: no new NVS keys. `PREFERENCES_NAMESPACE_LED/brightness` keeps its meaning; the `USER` layer is deliberately volatile (lost on reboot).
- **Resources**: layer table replaces the 10-entry command queue, saving RAM; task stack unchanged.
- **Issues**: closes #111 and #105.
