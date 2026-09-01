# Add Home Pro Support

## Why

EnergyMe Home Pro is a new hardware variant: same ESP32-S3 platform but with a WROOM-1U-N16R8 module (external antenna, 8 MB octal PSRAM), a W5500 SPI Ethernet controller on its own SPI bus, and 12 measurement channels instead of 16. The board is designed and about to be built; the firmware must support it from the same codebase, one release process, and one open-source repo. The octal-vs-quad PSRAM difference forces per-product binaries (PSRAM mode is compile-time), so releases build two artifacts at the same version and OTA delivery is product-targeted.

## What Changes

- Factory NVS gains a `product_line` key (`home` / `home_pro`), written at manufacturing. Firmware treats a missing key as `home` so the entire deployed fleet and community devices keep working with zero action. New provisioning payloads must always set it (mandatory in the pydantic model going forward).
- Hardware profile selection becomes keyed by `(product, pcb version)` instead of version alone; Pro PCB numbering restarts at v1.0. Community fallback remains "latest Home".
- New Home Pro v1.0 hardware profile: 74HC4067 with 11 wired channels + direct ADE7953 input = 12 channels; W5500 pins on a dedicated SPI bus; pinout extracted from the PCB netlist (see design.md), verified on hardware at bring-up.
- New Ethernet subsystem (W5500 via the core `ETH` API): DHCP-first zero-touch commissioning, static IP configurable via web UI, stored in a new `eth_ns` namespace following the existing WifiConfiguration + boot-fail-backstop pattern.
- Network role model on Pro: Ethernet is the primary interface; WiFi STA is the automatic fallback (cable pull → STA takes over, ETH link return → ETH resumes); the SoftAP rises only when no interface can come up (recovery/first-config channel). Button long-press is a full network reset: WiFi credentials, WiFi static config, and Ethernet static config → DHCP everywhere. All gated on `product_line`; Home behavior is byte-identical.
- Cross-product OTA protection on-device on all three delivery paths (manual upload, community release download, cloud job), with unambiguous product-token matching.
- Network-readiness predicates (`isFullyConnected` / `isNetworkServiceable` and the gates in MQTT, time sync, Modbus, telemetry, health checks) generalize from "WiFi up" to "any interface up".
- Web UI: show active interface and Ethernet link/IP/config; Ethernet settings page for Pro.
- mDNS stays `energyme.local` for both products (no per-device hostnames).

**BREAKING**: none for deployed devices; provisioning payload schema adds a mandatory field for newly manufactured units (manufacturing repo change, coordinated).

## Capabilities

### New Capabilities

- `product-line-selection`: how firmware determines the product (factory NVS `product_line`, absent → home), how the hardware profile is selected by (product, version), and the community/fallback rules.
- `ethernet-connectivity`: W5500 bring-up, DHCP-first commissioning, static IP configuration with boot-fail backstop, link-state events, and ETH-primary/WiFi-fallback interface arbitration on Pro.

### Modified Capabilities

- `wifi-provisioning`: SoftAP raise conditions change on Pro (rises only when no Ethernet link/lease and no STA credentials); network-serviceability predicates include Ethernet.
- `device-provisioning`: factory NVS schema gains mandatory `product_line`; Pro devices carry `pcb_revision` starting at v1.0.

## Impact

- Firmware: `hardware_profile.{h,cpp}`, `factory_keys.h`, new `custometh.{h,cpp}`, `customwifi.{h,cpp}` (predicates + AP raise conditions), `main.cpp` boot sequence, `modbustcp`, `mqtt`/`custommqtt`/`influxdbclient`/`customtime`/`customlog` gates, web UI pages + REST endpoints, `buttonhandler` (network reset).
- Build: one codebase, two binaries. New `esp32s3-pro-dev`/`esp32s3-pro-prod` envs in `platformio.ini` with `qio_opi` memory type for the N16R8 module (Home envs untouched). No new lib_deps (core `ETH.h`). Manual OTA upload filename check and cloud job targeting become product-aware so a wrong-product image cannot be flashed.
- Manufacturing (coordinated, other repo): pydantic payload adds mandatory `product_line`; label gains Ethernet MAC; EOL test adds W5500 link test and external-antenna RF check.
- Cloud (other repo): `product` thing attribute/group at provisioning is now required for OTA job targeting (per-product binaries); release pipeline builds and signs both artifacts at the same version.
- Tests: new host-testable interface-arbitration logic in `lib/` + Unity tests; dual-netif heap soak on hardware during OTA/TLS is a mandatory bring-up gate (STA fallback in v1 keeps both stacks live).
