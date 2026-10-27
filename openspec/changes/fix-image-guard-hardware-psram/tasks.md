## 1. PSRAM identity from the eFuse

- [x] 1.1 Add `ImageDescriptor::psramMbFromEfuseCap()` with native tests
- [x] 1.2 `deviceIdentity()` reads `ESP_EFUSE_PSRAM_CAP`; legacy allowance follows it

## 2. Boot-time rollback off a wrong image

- [x] 2.1 `AppImageDescriptor::rollBackIfWrongImage()` called right after `initHardwareProfile()`
- [x] 2.2 `pio test -e native`, `pio run` for Home and Pro dev

## 3. Bench verification (dev devices, needs serial on hand)

The web upload of this branch refuses a Pro build on a Home board, so the wrong image goes on over serial (`esptool write-flash` of the Pro .bin to the active app slot, Home build of this branch in the other slot).

- [x] 3.1 Home dev: serial shows `Board PSRAM: 2 MB (image built for 2 MB)`, normal boot unchanged, OTA of a Home build still accepted, Pro build rejected with `psram_mismatch`
  - 2026-10-27, USB on a blank Home board (2 MB, eFuse PSRAM_CAP=2), development @ 489949f: serial shows `Board PSRAM: 2 MB (image built for 2 MB)` and a normal Home boot.
  - 2026-10-27, over the network on .174 (development @ fc9a242): normal boot unchanged, OTA of a Home build accepted, Pro build rejected on the first chunk with `psram_mismatch` and the running image kept. Serial line still unchecked (no USB attached).
- [x] 3.2 Home dev, Pro build of this branch running, Home build in the passive slot: the device switches to the Home build by itself on the first boot
  - 2026-10-27, USB, same board with factory NVS `pcb_revision=v6.1` (no product_line): Pro image in app0 prints `Board PSRAM: 2 MB (image built for 8 MB)`, `Rolled back to the previous firmware (app1), restarting`, then the Home build boots (`Line: home | PCB: v6.1`). Without factory NVS (community mode) the Home image is refused as `product_mismatch` and the board crash-loops, the known limit.
- [ ] 3.3 Home dev, Pro build of this branch running, passive slot erased: boot continues, and a Home upload from the web UI is accepted (if the Pro image lives long enough to serve it)
  - 2026-10-27, USB, factory NVS, app1 erased: boot continues (log `Rolling back to the previous firmware failed: ESP_ERR_OTA_VALIDATE_FAILED`, since the Home legacy allowance accepts the blank slot and the OTA API refuses it), no panic in 60 s, web server up. Web upload not tested: the unprovisioned board has no WiFi credentials and the Pro image drops its SoftAP right after raising it.
- [ ] 3.4 Pro bench: serial shows `Board PSRAM: 8 MB`, normal boot unchanged, Home upload rejected with `psram_mismatch`
  - 2026-10-27, over the network on .80 (development @ fc9a242): normal boot unchanged, Home upload rejected on the first chunk with `psram_mismatch` and the running image kept. Serial line still unchecked (no USB attached).
