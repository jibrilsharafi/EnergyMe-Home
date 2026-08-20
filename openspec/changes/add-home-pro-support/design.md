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
- One firmware binary serves Home and Home Pro; product/profile resolved at boot from factory NVS.
- Zero behavior change for Home devices (deployed fleet and community), byte-identical network semantics.
- Pro: ETH-primary with automatic WiFi STA fallback and SoftAP as last-resort recovery, in v1.
- Interface-arbitration logic host-testable in `lib/` (Arduino-free), consistent with the `wifi_provisioning` pattern.

**Non-Goals:**
- Per-device mDNS hostnames (stays `energyme.local` for both products).
- VLAN tagging, 802.1X, or any managed-network features beyond static IP/DNS.
- Industry product line (separate codebase, not derived from this repo).
- Any provisioning-firmware or cloud-infra implementation (coordinated changes tracked in their own repos).

## Decisions

### D1: Product discriminator is a dedicated NVS key, absent → home
`factory_ns::product_line` with values `home` / `home_pro`, written at manufacturing; mandatory in new provisioning payloads. Firmware maps a missing key to `home` permanently - this is the correct semantics ("Home predates the field"), not a migration shim, so no fleet backfill and no remote write path into `factory_ns` (which holds the device certs; keeping it write-once at manufacturing is a security property).
*Alternatives rejected*: letter-prefixed `pcb_revision` ("p1.0") - smuggles product into a version string, misleading in logs; version-range convention - implicit and error-prone.

### D2: Profile lookup keyed by (product, version); Pro restarts at v1.0
`HardwareProfile` gains a `product` enum field; `findProfileByVersion` becomes find-by-(product, version). Community/latest fallback filters to Home. New `PRODUCT_FALLBACK` build define accompanies `PCB_VERSION_FALLBACK` for unprovisioned Pro bench boards (new `esp32s3-dev-pro` env).

### D3: Single binary, Ethernet support compiled in for all
W5500 driver instantiates only when the active profile declares Ethernet (profile fields: has-ethernet flag + CS/INT/RST + dedicated SPI bus pins). Home units carry a few KB of dead code on a 16 MB flash; in exchange there is exactly one OTA stream, one release, one signing flow. Fleet targeting (staged Pro rollouts) uses a `product` thing attribute set at provisioning - no firmware involvement.
*Alternative rejected*: per-product build env + binary - doubles the release/OTA/test surface for zero runtime gain.

### D4: Commissioning model - DHCP-first, SoftAP as recovery only
Ethernet has no credentials, so a cabled Pro with a DHCP lease is fully commissioned at first boot with zero touch; the web UI (found via `energyme.local` or the router's client list; ETH MAC printed on the label) is where static IP is configured afterwards. The SoftAP rises only when no interface can come up: no ETH link/lease AND no stored STA credentials. Button long-press resets network config to DHCP (recovers a bad static IP with no UI). The existing `wifi_provisioning` state machine is unchanged; only its raise-conditions gain ETH-aware inputs on Pro.

### D5: Interface arbitration - ETH primary, STA automatic fallback (v1)
Both netifs may be up; `Network.setDefaultInterface()` follows a small pure state machine (new `lib/` module, Unity-tested): ETH link+lease → ETH default; ETH down and STA connected → STA default; ETH link returns → back to ETH. `ARDUINO_EVENT_ETH_*` events feed it alongside the existing WiFi events. Network-readiness predicates generalize to "any interface serviceable"; all existing gates (MQTT, NTP, Modbus, telemetry, log, health check) call the generalized predicates and remain interface-agnostic.
*Alternative considered*: defer STA fallback to a follow-up (smaller v1) - overruled; resilience is part of the Pro proposition.

### D6: Ethernet IP config in its own namespace
New `eth_ns` + `EthConfiguration` struct (DHCP/static, ip/gw/subnet/dns1/dns2), cloning the proven `WifiConfiguration` pattern including the boot-fail backstop counter (static IP that crashes/fails N boots → ignored, DHCP). `wifi_ns` and its code path are untouched.
*Alternative rejected*: generalizing `WifiConfiguration` per-interface - touches Home's tested static-IP path and NVS keys for no user-visible gain.

### D7: mDNS unchanged
`energyme.local` for both products; discovery relies on mDNS plus the clearly-presented IP during commissioning. The known two-devices-on-one-LAN ambiguity is accepted (existing memory/practice: confirm chipId before writing).

## Risks / Trade-offs

- [Dual netif + TLS heap pressure: STA fallback in v1 keeps both stacks live; WiFi/LWIP buffer placement is exactly the class of issue that caused the 2026-08-12 OTA failures] → Mandatory hardware bring-up gate: heap soak (free/minFree/maxAlloc) during MQTT TLS publish and a full cloud OTA with both interfaces up, before any Pro release.
- [W5500 SPI ETH support in core 3.3.2 is bench-unverified; exact `ETH.begin()` SPI-overload signature not yet confirmed against the pinned core source] → Verify against the 3.3.2 sources before writing the driver module; spike on a W5500 breakout + devkit before the board arrives. No platform bump under any circumstances.
- [Interface flapping (marginal cable/PoE splitter brownout) could thrash the default route and drop MQTT sessions repeatedly] → Debounce in the arbitration state machine (hold-down timer before switching back to ETH); host tests cover flap sequences.
- [SoftAP raise-condition change is on the shared code path; a regression could strand Home devices (provisioning carve-out history)] → Raise-condition changes are product-gated so Home evaluates the identical predicate as today; host tests assert Home inputs → unchanged outputs.
- [`WiFiClientSecure` assumed to follow the default route over ETH on core 3.3.2] → Verify on the W5500 spike (TLS MQTT connect with WiFi radio off); fallback is switching those clients to `NetworkClientSecure` aliases, still core-provided.
- [Provisioning payload gains a mandatory field - old provisioning host versions would fail for Pro units] → Coordinated manufacturing-repo change; firmware side is tolerant (absent → home) so ordering is safe.

## Migration Plan

No fleet migration. Deployed Home devices see only inert additions (missing `product_line` → home; ETH code dormant). Rollout order: firmware lands first (safe for all existing devices), manufacturing payload change second, first Pro units provisioned last. Rollback = ordinary firmware rollback; no NVS schema to unwind.

## Open Questions

- Exact Pro v1.0 pinout (all pins incl. W5500 CS/INT/RST + SPI bus, mux map for the 11 wired channels, voltage divider values) - profile entry fills in when the board/pinout arrives; struct shape is not affected.
- Whether the W5500 INT line is used (event-driven) or the driver polls - decided during the spike; contained in `custometh`.
- Label/EOL-test details (MAC printing, RF check thresholds) - manufacturing repo scope.
