## Why

Some buildings have a three-phase supply without neutral, 230 V between the lines (old Italian buildings, IT grids in Norway and Belgium). The meter is then wired with its L and N on two lines, so V1 is a line-to-line voltage. Firmware 2.9.0 treats it as line-to-neutral: a CT on one line of the supply reads √3 too much power and 30 degrees off. Field case (an office, November 2026): the three grid CTs read 1588 W against 754 W on the building's panel meter, with power factors near 0.95 instead of 0.62 to 0.86 capacitive.

A single-phase load in such a building sits between two lines, the same kind of voltage V1 measures, so it already reads right. Only CTs on one line of a three-wire circuit (the supply, a three-phase load) need the correction.

## What Changes

- **Device setting `noNeutral`** (default off), next to the voltage phase: the meter's L is on the voltage phase and its N on the next line (L1-L2, L2-L3 or L3-L1).
- **Channel flag `lineCurrent`** (default off): the CT sits on one line of a three-wire circuit. With `noNeutral` on it is measured against its line-to-neutral voltage (1/√3 and -30 degrees); ignored otherwise, so existing installs read exactly as before.
- **Refused combinations**: `noNeutral` with any active split-phase 240 V channel, and split-phase 240 V on a channel while `noNeutral` is on.
- **REST** `GET/PUT /api/v1/ade7953/voltage-phase` gain the optional `noNeutral` field; the `meter` shadow gains `no_neutral`; channel JSON gains `lineCurrent`.
- **Setup page**: a "No neutral (3×230 V)" switch inside the three-phase supply choice. When on, the voltage phase and single-phase circuits show line pairs, and three-phase circuits are saved with `lineCurrent`. No new step or card.
- **Channel page**: the same switch next to the voltage phase; each channel's phase select offers lines and line pairs while it is on.

## Capabilities

### Modified Capabilities
- `voltage-phase`: the no-neutral setting, the per-channel line flag, their storage, REST, shadow and web UI.

## Impact

- `source/lib/phase_utils/phase_utils.h` (pure helper, host-tested), `source/src/ade7953.cpp`, `source/include/ade7953.h`, `source/src/customserver.cpp`, `source/src/shadow.cpp`, `source/html/setup.html`, `source/html/channel.html`, `source/js/api-client.js`, `source/resources/swagger.yaml`, `source/test/test_phase_utils`.
- Device voltage (MQTT, Modbus, cloud) stays the measured V1, the line-to-line voltage.
- **Non-goals**: 3×400 V without neutral (the meter takes 230 V only); two-CT (Aron) metering; unbalanced line voltages (one voltage input assumes balance, as today).
- Downgrading to an older firmware ignores both keys and returns to the inflated readings.
