## MODIFIED Requirements

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
| `disco` | Continuously lit; the colour changes every 120 ms to a pseudo-random choice, ignoring the layer's colour |

A pattern's phase SHALL start when the indication is set on its layer, and SHALL be sampled often enough that no on or off segment of any supported pattern is skipped or visibly mistimed.

The `disco` colour sequence SHALL be a deterministic function of the indication's seed and the elapsed time since it was set, so the same seed always produces the same sequence. Consecutive colour choices SHALL differ from one another, so every step is visible. `disco` SHALL never render dark, and SHALL be reported as lit for its whole duration.

#### Scenario: Pattern phase restarts on set

- **WHEN** a slow-blinking indication is set
- **THEN** the LED turns on immediately rather than resuming mid-cycle

#### Scenario: Pulse reaches full configured brightness

- **WHEN** a pulsing indication is displayed at a configured brightness of 75%
- **THEN** the peak of the fade equals the same colour rendered solid at 75% brightness

#### Scenario: Fast blink is not distorted

- **WHEN** a fast-blinking indication is displayed
- **THEN** each on and off segment lasts 250 ms within the sampling tolerance of the renderer, and no segment is skipped

#### Scenario: Disco is reproducible

- **WHEN** two `disco` indications are set with the same seed and sampled at the same elapsed times
- **THEN** they produce the identical colour sequence

#### Scenario: Different seeds diverge

- **WHEN** two `disco` indications are set with different seeds and sampled at the same elapsed times
- **THEN** their colour sequences differ

#### Scenario: Disco holds a colour for one step

- **WHEN** a `disco` indication is sampled repeatedly within the same 120 ms step and then in the next step
- **THEN** the colour is constant within the step and different in the next one

#### Scenario: Disco ignores the requested colour

- **WHEN** a `disco` indication is set with colour `{0, 0, 0}`
- **THEN** the LED is lit throughout, and never renders dark

#### Scenario: Disco obeys configured brightness

- **WHEN** a `disco` indication is displayed at a configured brightness of 50%
- **THEN** each colour in the sequence is scaled by 50%, exactly as `solid` would be

### Requirement: User-controlled LED via API

The device SHALL expose `PUT /api/v1/led/color`, which sets the `user` layer from a JSON body containing `red`, `green` and `blue` (integers 0-255), an optional `pattern` (defaulting to `solid`), an optional `duration_ms` (0 or absent meaning indefinite), and an optional `seed` (integer 0 to 4294967295).

`red`, `green` and `blue` SHALL be required for every pattern except `disco`, which chooses its own colours and SHALL accept a body without them. When they are supplied alongside `disco` they SHALL be ignored.

For `pattern` of `disco`, `duration_ms` SHALL default to 15000 ms when absent or 0, and a larger value SHALL be clamped to 15000 ms. Disco SHALL NOT run indefinitely. `seed` SHALL select the colour sequence; when absent the device SHALL choose a seed that varies between requests, so repeated calls do not replay the same sequence.

`seed` SHALL be ignored by every other pattern.

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

#### Scenario: Starting disco with no other fields

- **WHEN** `PUT /api/v1/led/color` is called with `{"pattern": "disco"}`
- **THEN** the request succeeds and the LED runs disco on the `user` layer for 15000 ms, after which the layer is released

#### Scenario: Disco duration is capped

- **WHEN** `PUT /api/v1/led/color` is called with `{"pattern": "disco", "duration_ms": 600000}`
- **THEN** the request succeeds and the indication is released after 15000 ms

#### Scenario: Disco is interruptible

- **WHEN** `DELETE /api/v1/led/color` is called while disco is running
- **THEN** the LED immediately returns to whatever layer is occupied beneath `user`

#### Scenario: Out-of-range seed is rejected

- **WHEN** `PUT /api/v1/led/color` is called with a negative or non-integer `seed`
- **THEN** the request is rejected with HTTP 400 and the `user` layer is unchanged

### Requirement: Read current LED state

The device SHALL expose `GET /api/v1/led` returning the currently rendered indication: its pattern, its RGB colour, the name of the owning layer, the remaining duration in milliseconds (or null when indefinite), whether the LED is currently lit, and the configured brightness.

The reported colour SHALL be the colour being shown at that moment, not the colour that was requested. For `disco` this is the colour of the current step.

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

#### Scenario: Reading disco

- **WHEN** `GET /api/v1/led` is called while disco is running
- **THEN** the response reports pattern `disco`, layer `user`, a non-null remaining duration, `is_lit` true, and the colour of the current step

## ADDED Requirements

### Requirement: Disco mode is reachable from the web interface

The web interface SHALL offer a control that starts disco mode on the `user` layer for its default duration, on the same page as the LED brightness control.

While the indication is running the control SHALL indicate that it is running and SHALL NOT be re-triggerable, and it SHALL become available again once the duration has elapsed.

A failure to start SHALL be reported to the user and SHALL leave the control available.

#### Scenario: Starting disco from the browser

- **WHEN** the user activates the disco control
- **THEN** the device runs disco for its default duration and the control is unavailable until it ends

#### Scenario: Disco control recovers from an error

- **WHEN** the request to start disco fails
- **THEN** an error is shown and the control is immediately available again
