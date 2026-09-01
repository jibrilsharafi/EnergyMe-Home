# Tasks: Home Pro Support

## 1. Product line foundation

- [x] 1.1 Add `FACTORY_KEY_PRODUCT_LINE` to `factory_keys.h` and a `ProductLine` enum (`home`, `home_pro`)
- [x] 1.2 Read `product_line` in `initHardwareProfile()`: absent → home; unknown value → community mode on the build's fallback product profile, warning logged
- [x] 1.3 Add `product` field to `HardwareProfile`; key lookup by (product, version); fallback search filters to the build's fallback product (default home)
- [x] 1.4 Add `PRODUCT_FALLBACK` build define alongside `PCB_VERSION_FALLBACK`; every Pro env pins `PRODUCT_FALLBACK=home_pro` (prod included - a Pro binary must never fall back to a Home pinout)
- [x] 1.5 Add `esp32s3-dev-pro` and `esp32s3-prod-pro` envs to `platformio.ini` (naming matches `esp32s3-dev-v5` convention): `board_build.arduino.memory_type = qio_opi`, `board_build.psram_type = opi` for N16R8; same partition table; Home envs and `[common]` untouched
- [x] 1.6 Expose product line in device info (REST + info page), per the product-line-selection spec scenario

## 2. Home Pro hardware profile

- [x] 2.1 Extend `HardwareProfile` with Ethernet fields (hasEthernet, CS/INT/RST pins, dedicated SPI bus pins) - Home entries all zero/false
- [x] 2.2 Add Home Pro v1.0 profile entry with netlist-derived pinout (design.md "Home Pro v1.0 pinout"): ADE7953 SPI identical to v6.x, mux S0=21/S1=47/S2=48/S3=38, LED 40/41/39, button 0, W5500 CS=16/SCLK=15/MISO=7/MOSI=6/IRQ=5/RST=4, mux map [15,14,13,12,11,2,8,1,9,0,10], dividers 153000/180
- [x] 2.3 Verify all channel-count consumers iterate the profile value with a Pro profile active (grep audit + build); drive the mDNS modbus TXT `channels` value from the profile (currently hard-coded "16" in customwifi.cpp)

## 3. Interface arbitration logic (host-testable)

- [x] 3.1 Create `lib/interface_arbitration`: pure state machine - inputs (eth link up + valid address [DHCP lease or applied static], sta associated, hold-down timer), outputs (preferred interface, ap-raise-allowed, transition events); includes flap debounce
- [x] 3.2 Unity tests: cable pull → STA, cable return after hold-down → ETH, flap sequences don't thrash, static-address ETH preferred, link-up-without-address is not serviceable, Home inputs → outputs identical to current behavior, no-interface → AP allowed
- [x] 3.3 Run `pio test -e native` from WSL - green before any firmware wiring

## 4. Ethernet driver verification

- [x] 4.1 Confirm `ETH.begin()` W5500 SPI overload against pinned core 3.3.2: `begin(type, phy_addr, cs, irq, rst, SPIClass&, spi_freq_mhz)` exists; `CONFIG_ETH_SPI_ETHERNET_W5500=1` and `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=1` in the qio_opi variant (verified 2026-09-01)
- [ ] 4.2 Spike sketch on devkit + W5500 breakout: link events, DHCP lease, INT-driven confirmed (IRQ wired to GPIO5)
- [ ] 4.3 Verify `WiFiClientSecure` TLS MQTT connect works over ETH with WiFi radio off; note heap figures

## 5. Ethernet module

- [x] 5.1 Create `custometh.{h,cpp}`: init from profile (only when hasEthernet), `ARDUINO_EVENT_ETH_*` handlers (run in the Network event task - copy state under mutex, act outside) feeding the arbitration lib, `Network.setDefaultInterface()` on arbitration output; arbitration state and config guarded by `_configMutex` per FreeRTOS rules
- [x] 5.2 `EthConfiguration` struct + `eth_ns` persistence: DHCP/static, ip/gw/subnet/dns, config module pattern (get/set/toJson/fromJson/validate) with mutex; validation rejects a static IP inside the SoftAP subnet; `eth_ns` is created lazily on first write (never on Home), cleared by factory reset, included in config backup/restore on Pro
- [x] 5.3 Boot-fail backstop for static ETH config: counter increments only when the link is up and the static config fails to become serviceable; no-link boots are neutral; independent of (and non-interacting with) the WiFi backstop
- [x] 5.4 Wire into `main.cpp`: ETH init after profile selection, product-gated; boot network wait accepts ETH and stays bounded by the existing `SETUP_NETWORK_WAIT_TIMEOUT_MS`
- [x] 5.5 On default-route change, actively drop MQTT/custom-MQTT/InfluxDB connections so they reconnect on the new interface (no waiting on TCP keepalive timeouts); reapply DNS servers and restart NTP sync for the active interface

## 6. Predicate generalization (one module per task/commit)

- [x] 6.1 Generalize network-readiness predicates ("any serviceable interface"): thin network-level predicate both customwifi and custometh feed; health-check tolerance for failover = hold-down + reconnect window, restart deadline re-arms when both interfaces are lost
- [x] 6.2 Convert mqtt.cpp gates to the generalized predicate
- [x] 6.3 Convert custommqtt.cpp gates
- [x] 6.4 Convert influxdbclient.cpp gates
- [x] 6.5 Convert customtime.cpp gates; gateway-derived NTP server must follow the active interface
- [x] 6.6 Convert customlog.cpp + telemetry gates
- [x] 6.7 Convert maintenance/health-check gates
- [x] 6.8 Modbus TCP: ETH counts as trusted interface (accept), SoftAP still blocked
- [x] 6.9 mDNS: advertise the active interface's IP, re-announce on failover
- [x] 6.10 Build both dev envs + run full native test suite

## 7. SoftAP raise conditions (product-gated)

- [x] 7.1 Feed ETH state into the AP raise/teardown predicates via the arbitration lib; Home path evaluates identically to today; link-up-without-address counts as unreachable (AP may rise after the DHCP wait)
- [x] 7.2 DNS responder confinement and AP-subnet collision checks account for ETH (responder stops when ETH is serviceable; AP subnet never overlaps the ETH subnet)
- [x] 7.3 Unity tests on the provisioning logic: cabled Pro never raises AP, ETH recovery tears AP down, no-interface Pro raises AP, link-but-no-lease raises AP, all existing Home scenarios unchanged

## 8. Configuration surface + OTA product safety

- [x] 8.1 REST endpoints for ETH config (get/set/reset) + status (link, mode, IP, gateway, DNS, MAC) mirroring the wifi endpoint pattern; 4xx on products without Ethernet, no NVS side effects
- [x] 8.2 Web UI: ETH status on info page, ETH settings on configuration page, active-interface indicator; hidden on Home
- [x] 8.3 Button SINGLE_LONG becomes network reset: clear WiFi credentials + WiFi static + eth_ns static (unconditional; no-op clears on Home), leaving calibration/web password/cloud credentials intact
- [x] 8.4 swagger.yaml for new endpoints (mind CRLF staging)
- [x] 8.5 Product-aware manual OTA upload gate in `_initializeOtaUpload`: filename must match the running product's artifact token, checked unambiguously (Pro token checked before the Home token, since `energyme_home` is a substring of `energyme_home_pro`); reject before any flash write
- [x] 8.6 GitHub release asset picker (`_fetchGitHubReleaseInfo`) becomes product-aware with the same unambiguous matching
- [x] 8.7 Cloud OTA job document gains a `product` field verified on-device before download starts (absent field = home for fleet compatibility); coordinate the job-creation side in the infra repo

## 9. Hardware bring-up (BLOCKED on Pro board) - covers all hardware-only spec scenarios

- [ ] 9.1 Serial-first bring-up of the Pro board (never OTA-test new pre-network boot code)
- [ ] 9.2 Verify the netlist-derived pinout/mux map/divider values on hardware; correct any discrepancy in the v1.0 profile; verify all 12 channels measure
- [ ] 9.3 Zero-touch commissioning test: factory-fresh + cable + DHCP → reachable, first-run password setup, then fully operational; no AP raised
- [ ] 9.4 Failover tests on hardware: cable pull → STA, cable return → ETH, flap, static-IP backstop, button reset, DNS/NTP follow the active interface
- [ ] 9.5 Home regression on shared code: v6.1 device boots with no ETH objects created and free-heap delta within noise (< 8 KB) vs previous release
- [ ] 9.6 RELEASE GATE - dual-netif heap soak: free/minFree/maxAlloc during MQTT TLS publishes and a full cloud OTA with both interfaces up
