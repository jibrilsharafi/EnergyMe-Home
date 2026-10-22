## 1. PSRAM identity from the eFuse

- [x] 1.1 Add `ImageDescriptor::psramMbFromEfuseCap()` with native tests
- [x] 1.2 `deviceIdentity()` reads `ESP_EFUSE_PSRAM_CAP`; legacy allowance follows it

## 2. Boot-time rollback off a wrong image

- [x] 2.1 `AppImageDescriptor::rollBackIfWrongImage()` called right after `initHardwareProfile()`
- [x] 2.2 `pio test -e native`, `pio run` for Home and Pro dev

## 3. Bench verification (dev devices, needs serial on hand)

- [ ] 3.1 Home dev: log shows eFuse PSRAM 2 MB, normal boot unchanged, OTA of a Home build still accepted
- [ ] 3.2 Home dev with a Home build in the passive slot: upload a Pro build of this branch, the device rolls back by itself to the Home build
- [ ] 3.3 Home dev with an incompatible passive slot: the Pro build stays up long enough to accept a Home upload from the web UI
- [ ] 3.4 Pro bench: eFuse reads 8 MB, normal boot unchanged, Home upload rejected with `psram_mismatch`
