## 1. PSRAM identity from the eFuse

- [x] 1.1 Add `ImageDescriptor::psramMbFromEfuseCap()` with native tests
- [x] 1.2 `deviceIdentity()` reads `ESP_EFUSE_PSRAM_CAP`; legacy allowance follows it

## 2. Boot-time rollback off a wrong image

- [x] 2.1 `AppImageDescriptor::rollBackIfWrongImage()` called right after `initHardwareProfile()`
- [x] 2.2 `pio test -e native`, `pio run` for Home and Pro dev

## 3. Bench verification (dev devices, needs serial on hand)

The web upload of this branch refuses a Pro build on a Home board, so the wrong image goes on over serial (`esptool write-flash` of the Pro .bin to the active app slot, Home build of this branch in the other slot).

- [ ] 3.1 Home dev: serial shows `Board PSRAM: 2 MB (image built for 2 MB)`, normal boot unchanged, OTA of a Home build still accepted, Pro build rejected with `psram_mismatch`
- [ ] 3.2 Home dev, Pro build of this branch running, Home build in the passive slot: the device switches to the Home build by itself on the first boot
- [ ] 3.3 Home dev, Pro build of this branch running, passive slot erased: boot continues, and a Home upload from the web UI is accepted (if the Pro image lives long enough to serve it)
- [ ] 3.4 Pro bench: serial shows `Board PSRAM: 8 MB`, normal boot unchanged, Home upload rejected with `psram_mismatch`
