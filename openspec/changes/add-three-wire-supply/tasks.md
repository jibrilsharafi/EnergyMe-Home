## 1. Measurement

- [ ] 1.1 `phase_utils.h`: reference scale and extra rotation for a channel, a shared pair rotation; host tests with the field case as golden vectors
- [ ] 1.2 `ade7953`: `no_neutral` setting with the voltage phase setter, boot load, NVS; `lineCurrent` channel flag in NVS and JSON
- [ ] 1.3 Read path: scale and rotate line-current channels after the register reads; no-load gate and waveform shift follow
- [ ] 1.4 Refuse no neutral with split-phase 240 V channels, and the reverse

## 2. Interfaces

- [ ] 2.1 REST `voltage-phase` GET/PUT `noNeutral`, `meter` shadow `no_neutral`, swagger, api-client
- [ ] 2.2 Setup page: switch in the three-phase choice, pair labels, `lineCurrent` on three-phase circuits
- [ ] 2.3 Channel page: switch next to the voltage phase, lines and pairs in the phase select

## 3. Verification

- [ ] 3.1 `pio test -e native` (WSL), dev builds for Home and Home Pro
- [ ] 3.2 Bench OTA: setting off reads as before, setting on transforms only line-current channels, back off
