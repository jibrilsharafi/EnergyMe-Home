# Design: Home Pro Support

## Context

See proposal.md for motivation. Relevant current state:

- `hardware_profile.cpp::PCB_PROFILES[]` is a flat array keyed by packed version alone; selection reads `factory_ns::pcb_revision` ("vMAJOR.MINOR"), falls back to `PCB_PROFILES[0]` + community mode. `PCB_VERSION_FALLBACK` pins a profile for unprovisioned bench boards.
- `customwifi.cpp` owns the whole network lifecycle: STA connect/retry, SoftAP provisioning state machine (`lib/wifi_provisioning`), static IP with boot-fail backstop (`wifi_ns`), and the predicates (`isFullyConnected`, `isNetworkServiceable`, `isApServing`) that gate MQTT, time sync, Modbus TCP, telemetry, custom log, and the health-check restart.
- MQTT/TLS clients use `WiFiClientSecure`/`WiFiClient`; on Arduino core 3.x these route through the generic lwIP socket layer, so they follow whatever interface holds the default route.
- Platform pinned to pioarduino 55.03.32 (core 3.3.2 / IDF 5.5.1) - `ETH.h` with W5500 SPI support is part of this core; no platform bump, no new lib_deps.
- Ethernet SPI is a dedicated bus, separate from the ADE7953 bus (per board design).

## Goals / Non-Goals

**Goals:**
- One firmware codebase serves Home and Home Pro (two build outputs, see D3); product/profile resolved at boot from factory NVS.
- Zero behavior change for Home devices (deployed fleet and community), byte-identical network semantics.
- Pro: ETH-primary with automatic WiFi STA fallback and SoftAP as last-resort recovery, in v1.
- Interface-arbitration logic host-testable in `lib/` (Arduino-free), consistent with the `wifi_provisioning` pattern.

**Non-Goals:**
- Per-device mDNS hostnames (stays `energyme.local` for both products).
- A new `eth` cloud shadow: the iot-device-shadows spec enumerates shadows exhaustively; Ethernet state reporting to the cloud is deferred to its own change.
- VLAN tagging, 802.1X, or any managed-network features beyond static IP/DNS.
- Industry product line (separate codebase, not derived from this repo).
- Any provisioning-firmware or cloud-infra implementation (coordinated changes tracked in their own repos).

## Decisions

### D1: Product discriminator is a dedicated NVS key, absent → home
`factory_ns::product_line` with values `home` / `home_pro`, written at manufacturing; mandatory in new provisioning payloads. Firmware maps a missing key to `home` permanently - this is the correct semantics ("Home predates the field"), not a migration shim, so no fleet backfill and no remote write path into `factory_ns` (which holds the device certs; keeping it write-once at manufacturing is a security property).
*Alternatives rejected*: letter-prefixed `pcb_revision` ("p1.0") - smuggles product into a version string, misleading in logs; version-range convention - implicit and error-prone.

### D2: Profile lookup keyed by (product, version); Pro restarts at v1.0
`HardwareProfile` gains a `product` enum field; `findProfileByVersion` becomes find-by-(product, version). The community/latest fallback filters to the *build's* fallback product: a new `PRODUCT_FALLBACK` define (default home) accompanies `PCB_VERSION_FALLBACK`, and every Pro env - prod included - pins `PRODUCT_FALLBACK=home_pro`, because a Pro binary falling back to a Home pinout on damaged factory NVS would drive the wrong mux order and skip Ethernet on hardware whose product is known at compile time. Home builds keep falling back to latest Home, which is also the least-bad answer for a truly unknown `product_line` value (Pro v1.0 happens to share the ADE7953/LED/button pins, but that is luck, not a guarantee - hence per-binary fallback, not runtime guessing). Env naming follows the existing variant-suffix convention (`esp32s3-dev-v5`): `esp32s3-dev-pro`, `esp32s3-prod-pro`.

### D3: One codebase, per-product binaries (forced by PSRAM hardware)
The Pro board uses ESP32-S3-WROOM-1U-**N16R8** (8 MB octal PSRAM); Home uses N16R2 (2 MB quad). PSRAM mode is compile-time - `board_build.arduino.memory_type` (`qio_qspi` vs `qio_opi`) selects different prebuilt core libraries - so one binary cannot serve both products: a quad-built image on octal PSRAM (or vice versa) fails PSRAM init, and this firmware depends on PSRAM (mbedtls, JSON allocators, buffers). Therefore: one codebase, two build outputs. New `esp32s3-pro-dev` / `esp32s3-pro-prod` envs with `qio_opi` memory type (bench-verify the combo; platform pin unchanged). Ethernet code still compiles into both binaries gated by the profile - the env split exists only for memory type, not features.
Consequences:
- OTA artifacts are product-specific: `energyme_home` (unchanged) and `energyme_home_pro`. Because the Home token is a substring of the Pro token, every product check matches the Pro token FIRST (`home_pro` present → Pro artifact; else `energyme_home` present → Home artifact); naive substring matching on `energyme_home` alone would accept Pro images on Home devices.
- Cross-product flash protection is device-side on every path, not just cloud-side targeting: (a) manual web upload gate in `_initializeOtaUpload` (note: today that function checks only `.bin` - the product gate is new, not an extension of an existing one; the only current name check is the GitHub asset picker at customserver.cpp:1798); (b) the GitHub release asset picker becomes product-aware (releases will carry two `.bin` assets and today's `strstr(energyme_home)` would return whichever comes first); (c) the cloud OTA job document gains a `product` field verified on-device before download (absent = home for compatibility with existing jobs) - job targeting by thing attribute remains, but is defense in depth, not the only defense, since the shared signing key proves authenticity, not product.
- CI/release builds both binaries at the same version with the same signing flow.
*Alternative rejected*: single binary - impossible with different PSRAM silicon; changing the Pro module to N16R2 - forfeits 4x PSRAM on the product that needs headroom most (dual netif + TLS).

### D4: Commissioning model - DHCP-first, SoftAP as recovery only
Ethernet has no credentials, so a cabled Pro with a DHCP lease is network-commissioned at first boot with zero touch; the first web visit still goes through the mandatory default-password change (web-authentication spec), then the device is fully operational. The web UI (found via `energyme.local` or the router's client list; ETH MAC printed on the label) is where static IP is configured afterwards. The SoftAP rises only when no interface is serviceable: no ETH address (no link, or link without lease/static) AND no reachable STA. Button long-press is a full network reset - WiFi credentials, WiFi static config, and ETH static config are all cleared (matching today's `resetWifi()`, which already clears credentials and static config) - so the device comes up on DHCP on whatever is plugged in, or the AP if nothing. The existing `wifi_provisioning` state machine is unchanged; only its raise-conditions gain ETH-aware inputs on Pro.

### D5: Interface arbitration - ETH primary, STA automatic fallback (v1)
Both netifs may be up; `Network.setDefaultInterface()` follows a small pure state machine (new `lib/` module, Unity-tested): ETH serviceable (link up + valid address, DHCP lease or applied static) → ETH default; ETH not serviceable and STA connected → STA default; ETH returns and holds past the hold-down → back to ETH. `ARDUINO_EVENT_ETH_*` events (delivered in the core's Network event task) feed it alongside the existing WiFi events - handlers copy state under a mutex and act outside, per the FreeRTOS rules; the W5500 itself is serviced by the core's ETH/lwIP glue (INT-driven, GPIO5). Network-readiness predicates generalize to "any interface serviceable"; all existing gates (MQTT, NTP, Modbus, telemetry, log, health check) call the generalized predicates and remain interface-agnostic. The health check tolerates a readiness gap up to hold-down + reconnect window during a transition, and its restart deadline re-arms when both interfaces are lost.
A route change does not migrate established sockets: on every default-interface transition the MQTT/custom-MQTT/InfluxDB clients are actively disconnected so they reconnect on the new interface immediately, rather than waiting out TCP keepalive. DNS servers (lwIP's list is global, not per-netif) and the gateway-derived NTP server are reapplied for the active interface on the same transition.
Same-subnet dual-homing note: when ETH and STA sit on the same LAN subnet, lwIP picks the egress netif for on-link traffic by netif order regardless of default route - harmless for correctness (both paths reach the LAN) but worth knowing when reading captures.
*Alternative considered*: defer STA fallback to a follow-up (smaller v1) - overruled; resilience is part of the Pro proposition.

### D6: Ethernet IP config in its own namespace
New `eth_ns` + `EthConfiguration` struct (DHCP/static, ip/gw/subnet/dns1/dns2), cloning the proven `WifiConfiguration` pattern including the boot-fail backstop counter - with one predicate change: the ETH counter increments only on boots where the link is up and the static config fails to become serviceable; a no-link (cable unplugged) boot is neutral, otherwise N cable-out boots would silently abandon a perfectly valid static config. The ETH and WiFi backstops are independent and never touch each other's counters. Validation rejects a static ETH address inside the SoftAP subnet (same lwIP routing hazard the wifi spec guards against). `eth_ns` is created lazily on first write - never on Home devices (first-boot namespace creation and factory reset handle it product-agnostically: clear-if-present) - and is included in config backup/restore. `wifi_ns` and its code path are untouched.
*Alternative rejected*: generalizing `WifiConfiguration` per-interface - touches Home's tested static-IP path and NVS keys for no user-visible gain.

### D7: mDNS - same name, interface-aware
`energyme.local` for both products; discovery relies on mDNS plus the clearly-presented IP during commissioning. The known two-devices-on-one-LAN ambiguity is accepted. What does change: mDNS advertises the active interface's address and re-announces on failover (the current implementation is keyed to a single cached IP from the WiFi lifecycle), and the modbus TXT record's `channels` value comes from the profile instead of the hard-coded "16".

## Home Pro v1.0 pinout (extracted from PCB netlist, 2026-09-01)

From `energyme-home-pro-pcb` Main-board EasyEDA netlist (U2 = ESP32-S3-WROOM-1U-N16R8, U3 = 74HC4067, U6 = W5500):

- ADE7953 SPI: CS=10, MOSI=11, MISO=12, SCLK=13, RST=9, IRQ(QRI#)=14 - identical to v6.x
- RGB LED: R=40, G=41, B=39 - identical to v6.x; Button (FLASH): GPIO0
- Mux selects: S0=21, S1=47, S2=48, S3=38 (same pin set as v6.x, different order)
- W5500 (dedicated SPI bus): CS=16, SCLK=15, MISO=7, MOSI=6, IRQ=5, RST=4; 25 MHz crystal, INT and RST wired
- Mux map (CT1-CT11 → physical Y): CT1=Y15, CT2=Y14, CT3=Y13, CT4=Y12, CT5=Y11, CT6=Y2, CT7=Y8, CT8=Y1, CT9=Y9, CT10=Y0, CT11=Y10; Y3-Y7 grounded/unused. Logical→physical map: [15, 14, 13, 12, 11, 2, 8, 1, 9, 0, 10]
- Channel 0 = CT0 direct into ADE7953 channel A (IAP/IAN); mux common feeds channel B (IBP) - same architecture as Home
- Voltage sensing: ZMPT107-1 with 3x51 kΩ series + 180 Ω burden → R1=153000.0, R2=180.0 - identical ratios to v6.x

## Risks / Trade-offs

- [Dual netif + TLS heap pressure: STA fallback in v1 keeps both stacks live; WiFi/LWIP buffer placement is exactly the class of issue that caused the 2026-08-12 OTA failures] → Mandatory hardware bring-up gate: heap soak (free/minFree/maxAlloc) during MQTT TLS publish and a full cloud OTA with both interfaces up, before any Pro release. The N16R8's 8 MB PSRAM helps only if the pro sdkconfig keeps WiFi/LWIP buffers in PSRAM - verify with diff_platform_sdkconfig.py against the qio_opi core variant.
- [Wrong-product OTA image bricks or degrades a device (quad image on octal PSRAM or vice versa)] → Device-side product checks on all three paths (manual upload gate, GitHub asset picker, cloud job `product` field), with Pro-token-first matching to defeat the substring ambiguity; job targeting by thing attribute as defense in depth.
- [harden-ota-download (in-progress change) also edits mqtt.cpp and customserver.cpp] → No requirement conflict (it governs retry/diagnostics post-download-start); coordinate merge order, land whichever completes first and rebase the other.
- [W5500 SPI ETH support in core 3.3.2 is bench-unverified; exact `ETH.begin()` SPI-overload signature not yet confirmed against the pinned core source] → Verify against the 3.3.2 sources before writing the driver module; spike on a W5500 breakout + devkit before the board arrives. No platform bump under any circumstances.
- [Interface flapping (marginal cable/PoE splitter brownout) could thrash the default route and drop MQTT sessions repeatedly] → Debounce in the arbitration state machine (hold-down timer before switching back to ETH); host tests cover flap sequences.
- [SoftAP raise-condition change is on the shared code path; a regression could strand Home devices (provisioning carve-out history)] → Raise-condition changes are product-gated so Home evaluates the identical predicate as today; host tests assert Home inputs → unchanged outputs.
- [`WiFiClientSecure` assumed to follow the default route over ETH on core 3.3.2] → Verify on the W5500 spike (TLS MQTT connect with WiFi radio off); fallback is switching those clients to `NetworkClientSecure` aliases, still core-provided.
- [Provisioning payload gains a mandatory field - old provisioning host versions would fail for Pro units] → Coordinated manufacturing-repo change; firmware side is tolerant (absent → home) so ordering is safe.

## Migration Plan

No fleet migration. Deployed Home devices see only inert additions (missing `product_line` → home; ETH code dormant). Rollout order: firmware lands first (safe for all existing devices), manufacturing payload change second, first Pro units provisioned last. Rollback = ordinary firmware rollback; no NVS schema to unwind.

## Open Questions

- Whether the W5500 INT line is used (event-driven) or the driver polls - decided during the spike; contained in `custometh`. (INT is wired to GPIO5, so event-driven is available.)
- Label/EOL-test details (MAC printing, RF check thresholds) - manufacturing repo scope.
- `qio_opi` sdkconfig differences vs `qio_qspi` (WiFi/LWIP buffer placement, heap floor) - measured during the spike/bring-up.
