# led-indicator Specification

## Purpose
Defines what the device's RGB status LED shows at any moment: which subsystem owns the indication, how simultaneous indications from different subsystems resolve, and the REST surface that lets a user read the LED back and drive it for home-automation use.
## Requirements
### Requirement: Layered indication ownership

The LED SHALL be driven by a fixed set of independent indication layers, ordered by priority. Each layer holds at most one indication, consisting of a pattern, a colour, and an optional duration.

The layers, from lowest to highest priority, SHALL be:

| Layer | Priority | Owner |
|---|---|---|
| `status` | 0 | Ambient operating status (boot stage, healthy) |
| `user` | 1 | REST API / automations |
| `network` | 2 | WiFi and connectivity state |
| `alert` | 3 | Button feedback, updates, recoverable faults |
| `critical` | 4 | Safe mode, factory reset, unrecoverable faults |

`status` is the ambient baseline: the device occupies it during boot and does not release it in normal operation. `user` therefore SHALL sit above `status`, so that a user-set indication replaces the steady operating indication, while every layer that carries an event still overrides it.

Setting an indication SHALL affect only the addressed layer. It SHALL NOT evict, delay, reorder, or discard the indication held by any other layer.

#### Scenario: Highest occupied layer is displayed

- **WHEN** the `status` layer holds solid green and the `network` layer holds pulsing blue
- **THEN** the LED shows pulsing blue

#### Scenario: A lower layer keeps its indication while overridden

- **WHEN** the `status` layer holds solid green, the `critical` layer is then set to fast-blinking red, and the `critical` layer is later released
- **THEN** the LED shows fast-blinking red while `critical` is occupied, and returns to solid green on release, without the owner of `status` re-asserting it

#### Scenario: A lower-priority indication is never dropped or deferred

- **WHEN** the `critical` layer holds an indefinite indication and the `status` layer is set 100 times in succession
- **THEN** every set updates the `status` layer, the last value written is the one held, and the LED continues to show the `critical` indication

#### Scenario: Setting the same layer twice replaces, not queues

- **WHEN** the `alert` layer is set to solid white and then to solid orange
- **THEN** the LED shows solid orange, and solid white is not shown again at any later time

### Requirement: Layer release and expiry

Releasing a layer SHALL clear that layer only and reveal the highest-priority layer still occupied. An indication set with a non-zero duration SHALL be released automatically once that duration has elapsed since it was set.

Releasing an unoccupied layer SHALL be a no-op. When no layer is occupied, the LED SHALL be off.

#### Scenario: Release reveals the layer below

- **WHEN** `status` holds solid green, `network` holds pulsing blue, and `network` is released
- **THEN** the LED shows solid green

#### Scenario: Duration expiry releases the layer

- **WHEN** the `alert` layer is set to fast-blinking green for 2000 ms while `status` holds solid green
- **THEN** the LED shows fast-blinking green for 2000 ms and then solid green

#### Scenario: Duration is measured from when the indication was set

- **WHEN** an indication with a 2000 ms duration is set at time T while a higher-priority layer is occupied, and that higher layer is released at T+5000 ms
- **THEN** the shorter indication has already expired and is not shown

#### Scenario: All layers empty

- **WHEN** every layer is released
- **THEN** the LED is off

### Requirement: Patterns

The LED SHALL support the following patterns, each rendered from the layer's colour:

| Pattern | Behaviour |
|---|---|
| `off` | LED dark |
| `solid` | Constant colour |
| `blink_slow` | 1000 ms on, 1000 ms off |
| `blink_fast` | 250 ms on, 250 ms off |
| `pulse` | Smooth fade up over 1000 ms, fade down over 1000 ms |
| `double_blink` | 100 ms on, 100 ms off, 100 ms on, then 900 ms off |

A pattern's phase SHALL start when the indication is set on its layer, and SHALL be sampled often enough that no on or off segment of any supported pattern is skipped or visibly mistimed.

#### Scenario: Pattern phase restarts on set

- **WHEN** a slow-blinking indication is set
- **THEN** the LED turns on immediately rather than resuming mid-cycle

#### Scenario: Pulse reaches full configured brightness

- **WHEN** a pulsing indication is displayed at a configured brightness of 75%
- **THEN** the peak of the fade equals the same colour rendered solid at 75% brightness

#### Scenario: Fast blink is not distorted

- **WHEN** a fast-blinking indication is displayed
- **THEN** each on and off segment lasts 250 ms within the sampling tolerance of the renderer, and no segment is skipped

### Requirement: Brightness

Configured brightness SHALL be a persisted percentage from 0 to 100 that scales every rendered colour. It SHALL be applied exactly once per rendered colour.

An indication on the `critical` or `alert` layer SHALL be rendered at no less than a fixed per-layer visibility floor, so that an indication the user must see is not silenced by a configured brightness of 0. The `critical` floor SHALL be higher than the `alert` floor. A floor SHALL only raise the rendered brightness, never lower it, SHALL apply only while that layer is the one being rendered, and SHALL NOT modify the persisted brightness value.

#### Scenario: Brightness scales output

- **WHEN** brightness is set to 50% and a solid full-red indication is displayed
- **THEN** the red channel is driven at approximately half of full scale

#### Scenario: Critical stays visible at zero brightness

- **WHEN** brightness is 0 and a `critical` indication is displayed
- **THEN** the LED is visibly lit

#### Scenario: Button feedback stays visible at zero brightness

- **WHEN** brightness is 0 and an `alert` indication is displayed
- **THEN** the LED is lit

#### Scenario: A floored indication does not rewrite stored brightness

- **WHEN** brightness is 0, a `critical` indication is displayed and then released
- **THEN** the persisted brightness is still 0 and the LED returns to dark

#### Scenario: The floor does not leak to other layers

- **WHEN** brightness is 0 and a `status` or `user` indication is displayed
- **THEN** the LED is dark

#### Scenario: Brightness out of range is rejected

- **WHEN** a brightness above 100 is submitted
- **THEN** the request is rejected and the stored brightness is unchanged

### Requirement: Read current LED state

The device SHALL expose `GET /api/v1/led` returning the currently rendered indication: its pattern, its RGB colour, the name of the owning layer, the remaining duration in milliseconds (or null when indefinite), whether the LED is currently lit, and the configured brightness.

When no layer is occupied, the response SHALL report pattern `off` and a null owning layer.

If the current indication cannot be determined, the request SHALL fail with HTTP 503. It SHALL NOT report the LED as off, because a caller cannot tell a wrong answer from a real one.

#### Scenario: Reading an active indication

- **WHEN** the `network` layer holds pulsing blue indefinitely and `GET /api/v1/led` is called
- **THEN** the response reports pattern `pulse`, colour `{0, 0, 255}`, layer `network`, and a null remaining duration

#### Scenario: Reading an idle LED

- **WHEN** no layer is occupied and `GET /api/v1/led` is called
- **THEN** the response reports pattern `off` and a null layer

#### Scenario: State unavailable

- **WHEN** the current indication cannot be read
- **THEN** the request fails with HTTP 503 rather than reporting pattern `off`

#### Scenario: The state route does not shadow its siblings

- **WHEN** `GET /api/v1/led/brightness` is called
- **THEN** it returns the brightness document, not the LED state document

### Requirement: User-controlled LED via API

The device SHALL expose `PUT /api/v1/led/color`, which sets the `user` layer from a JSON body containing `red`, `green` and `blue` (integers 0-255, required), an optional `pattern` (defaulting to `solid`), and an optional `duration_ms` (0 or absent meaning indefinite).

The device SHALL expose `DELETE /api/v1/led/color`, which releases the `user` layer.

The `user` layer SHALL sit above `status` and below every other layer, so any event-carrying indication overrides it while active and the user colour reappears when that layer is released.

The `user` layer SHALL NOT be persisted across reboots.

#### Scenario: Setting a user colour on a healthy device

- **WHEN** `PUT /api/v1/led/color` is called with `{"red": 255, "green": 0, "blue": 255}` while the device is healthy and the `status` layer holds solid green
- **THEN** the LED shows solid magenta and `GET /api/v1/led` reports layer `user`

#### Scenario: Suppressing the ambient indication

- **WHEN** `PUT /api/v1/led/color` is called with `pattern` of `off`
- **THEN** the LED is dark despite the `status` layer being occupied, and a subsequent `critical` indication is still shown

#### Scenario: System status overrides the user colour

- **WHEN** the user layer holds solid magenta and the device enters safe mode, which occupies the `critical` layer
- **THEN** the LED shows the safe-mode indication, and it returns to solid magenta once safe mode's indication is released

#### Scenario: Timed user colour

- **WHEN** `PUT /api/v1/led/color` is called with `duration_ms` of 5000
- **THEN** the user colour is shown for 5000 ms and the `user` layer is then released

#### Scenario: Releasing the user colour

- **WHEN** `DELETE /api/v1/led/color` is called while the user layer holds a colour and no other layer is occupied
- **THEN** the LED turns off

#### Scenario: Invalid colour is rejected

- **WHEN** `PUT /api/v1/led/color` is called with a missing or out-of-range channel value, or an unknown `pattern`
- **THEN** the request is rejected with HTTP 400 and the `user` layer is unchanged

#### Scenario: User colour does not survive reboot

- **WHEN** a user colour is set and the device restarts
- **THEN** the `user` layer is unoccupied

### Requirement: LED endpoint authorisation

`GET /api/v1/led`, `PUT /api/v1/led/color` and `DELETE /api/v1/led/color` SHALL be subject to the same authentication and rate limiting as the existing LED brightness endpoints.

#### Scenario: Unauthenticated LED control is refused

- **WHEN** `PUT /api/v1/led/color` is called without valid credentials on a device that requires authentication
- **THEN** the request is refused and the `user` layer is unchanged

### Requirement: LED state stays out of the device shadow

The reported device shadow SHALL continue to carry `led_brightness` and SHALL NOT carry the rendered pattern, colour or owning layer.

The shadow's reported `system` object is republished whenever it drifts from the last report. The rendered indication changes on every WiFi flap, button press and update, so including it would turn a near-static configuration document into a source of recurring MQTT traffic — while still being unable to report a short indication that falls between two drift checks. The REST surface is the supported way to read LED state.

#### Scenario: Shadow does not churn on LED activity

- **WHEN** the LED changes indication repeatedly while the device is otherwise idle
- **THEN** no shadow update is published as a result

