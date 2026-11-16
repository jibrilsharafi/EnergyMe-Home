## Why

On v6.1 the 74HC4067 runs from 3V3/GND only and the CT signals are GND-referenced, so an unselected input below about -0.3 V injects current into the shared Z node and pollutes the channel being read (issue #255). Home v6.2 and Home Pro v1.1 put a 1k/1k divider (1:2, 500 ohm Thevenin) on every multiplexed CT input and need a new hardware profile plus a doubled ADE7953 gain on current channel B.

## What Changes

- New hardware profile Home v6.2 (`pcb_revision = "v6.2"`, 16 channels). Mux lines Y8 and Y9 carry CT10 and CT9 (swapped against v6.1), Y1 is tied to GND, software numbering keeps the v6.1 convention (no CT2, CT3 is software channel 2).
- New hardware profile Home Pro v1.1 (`pcb_revision = "v1.1"`, 12 channels) with its own mux map. Pro v1.0 is unchanged.
- New profile field `pgaIb`: the PGA_IB register value written at ADE7953 init (default 0 = x1). v6.2 and v1.1 set 1 (x2). Channel A (CT0, direct, no divider) stays at x1.
- No change to the CT scale factors: the 1:2 divider and the x2 PGA cancel, so the ADC full scale is still reached at 0.5 V peak at the jack, about 106% of the CT rated current. Calibration (bIGain, bWGain, phCalB, bIRmsOs) is done per board at PGA_IB = x2 and absorbs the fixed 1.1-1.6% loading error of the divider.
- An invalid `pgaIb` is logged and replaced by x1. A profile whose mux map has a duplicate or out-of-range entry is logged at boot.

## Capabilities

### Modified Capabilities
- `product-line-selection`: ADDED "Divider-equipped PCB revisions".

## Impact

- Firmware: `source/src/hardware_profile.cpp`, `source/include/hardware_profile.h`, `source/src/ade7953.cpp`, new `source/lib/hardware_logic`.
- No REST, MQTT, Modbus or shadow change. The OTA image descriptor PCB range is unbounded, so both revisions are accepted and older firmware behaviour is unchanged (an older binary on a v6.2 board falls back to community mode, as for any unknown revision).
- Manufacturing jig (`energyme-home-manufacturing`) needs matching `home v6.2` and `homepro v1.1` profiles with `pgaIb`.
