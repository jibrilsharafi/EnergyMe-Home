## 1. Profiles and PGA

- [x] 1.1 Home v6.2 and Home Pro v1.1 entries in `PCB_PROFILES[]`, mux maps verified against the PCB netlists
- [x] 1.2 `HardwareProfile::pgaIb`, applied to PGA_IB in `_setDefaultParameters`; pure helpers in `lib/hardware_logic`
- [x] 1.3 Unity tests in `test_hardware_logic` (PGA register, net gain, mux maps, profile selection)

## 2. Docs

- [x] 2.1 `hardware/README.md` profile list

## 3. Hardware verification (needs a v6.2 board)

- [ ] 3.1 Repeat the issue #255 sweep on a v6.2 prototype: victims stay at 0 W, ch6/ch0 ratio flat up to full scale
- [ ] 3.2 Calibrate bIGain, bWGain, phCalB, bIRmsOs at PGA_IB x2
