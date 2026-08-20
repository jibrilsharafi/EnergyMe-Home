# Tasks: Home Pro Support

## 1. Product line foundation

- [ ] 1.1 Add `FACTORY_KEY_PRODUCT_LINE` to `factory_keys.h` and a `ProductLine` enum (`home`, `home_pro`)
- [ ] 1.2 Read `product_line` in `initHardwareProfile()`: absent → home; unknown value → community mode on latest Home, warning logged
- [ ] 1.3 Add `product` field to `HardwareProfile`; key lookup by (product, version); fallback search filters to Home
- [ ] 1.4 Add `PRODUCT_FALLBACK` build define alongside `PCB_VERSION_FALLBACK`; honour both in community fallback
- [ ] 1.5 Add `esp32s3-dev-pro` env to `platformio.ini` (extends dev, pins product home_pro + version 10)
- [ ] 1.6 Expose product line in device info (REST + info page), so support dumps identify the product

## 2. Home Pro hardware profile

- [ ] 2.1 Extend `HardwareProfile` with Ethernet fields (hasEthernet, CS/INT/RST pins, dedicated SPI bus pins) - Home entries all zero/false
- [ ] 2.2 Add Home Pro v1.0 profile entry: 74HC4067 with 11 wired channels + direct = 12 channels, placeholder pins clearly marked (BLOCKED on pinout: fill real GPIOs, mux map, voltage divider values when board data arrives)
- [ ] 2.3 Verify all channel-count consumers iterate the profile value with a Pro profile active (grep audit + build)

## 3. Interface arbitration logic (host-testable)

- [ ] 3.1 Create `lib/interface_arbitration`: pure state machine - inputs (eth link/lease, sta associated, hold-down timer), output (preferred interface, ap-raise-allowed); includes flap debounce
- [ ] 3.2 Unity tests: cable pull → STA, cable return after hold-down → ETH, flap sequences don't thrash, Home inputs → outputs identical to current behavior, no-interface → AP allowed
- [ ] 3.3 Run `pio test -e native` from WSL - green before any firmware wiring

## 4. Ethernet driver spike (devkit + W5500 breakout, before board arrives)

- [ ] 4.1 Confirm `ETH.begin()` W5500 SPI overload signature against pinned core 3.3.2 sources
- [ ] 4.2 Spike sketch on devkit + breakout: link events, DHCP lease, decide INT-driven vs polling
- [ ] 4.3 Verify `WiFiClientSecure` TLS MQTT connect works over ETH with WiFi radio off; note heap figures

## 5. Ethernet module

- [ ] 5.1 Create `custometh.{h,cpp}`: init from profile (only when hasEthernet), ETH events feeding the arbitration lib, `Network.setDefaultInterface()` on arbitration output
- [ ] 5.2 `EthConfiguration` struct + `eth_ns` persistence: DHCP/static, ip/gw/subnet/dns, config module pattern (get/set/toJson/fromJson/validate) with mutex
- [ ] 5.3 Boot-fail backstop for static ETH config (counter pattern from wifi static IP)
- [ ] 5.4 Wire into `main.cpp`: ETH init after profile selection, product-gated; boot network wait accepts ETH

## 6. Predicate generalization

- [ ] 6.1 Generalize network-readiness predicates ("any serviceable interface"): `isFullyConnected`/`isNetworkServiceable` or a thin network-level wrapper both modules feed
- [ ] 6.2 Audit and convert every gate: mqtt, custommqtt, influxdb, customtime, customlog, telemetry, health check, maintenance
- [ ] 6.3 Modbus TCP: ETH counts as trusted interface (accept), SoftAP still blocked
- [ ] 6.4 Build both dev envs + run full native test suite

## 7. SoftAP raise conditions (product-gated)

- [ ] 7.1 Feed ETH state into the AP raise/teardown predicates via the arbitration lib; Home path evaluates identically to today
- [ ] 7.2 Unity tests on the provisioning logic: cabled Pro never raises AP, ETH recovery tears AP down, no-interface Pro raises AP, all existing Home scenarios unchanged

## 8. Configuration surface

- [ ] 8.1 REST endpoints for ETH config (get/set/reset) + status (link, mode, IP, gateway, DNS, MAC) mirroring the wifi endpoint pattern; 4xx on products without Ethernet, no NVS side effects
- [ ] 8.2 Web UI: ETH status on info page, ETH settings on configuration page, active-interface indicator; hidden on Home
- [ ] 8.3 Button SINGLE_LONG becomes network reset: clear WiFi credentials + WiFi static + eth_ns static (unconditional; no-op clears on Home), leaving calibration/web password/cloud credentials intact
- [ ] 8.4 swagger.yaml for new endpoints (mind CRLF staging)

## 9. Hardware bring-up (BLOCKED on Pro board)

- [ ] 9.1 Serial-first bring-up of the Pro board (never OTA-test new pre-network boot code)
- [ ] 9.2 Fill real pinout/mux map/divider values in the v1.0 profile; verify all 12 channels measure
- [ ] 9.3 Zero-touch commissioning test: factory-fresh + cable + DHCP → operational, no AP
- [ ] 9.4 Failover tests on hardware: cable pull → STA, cable return → ETH, flap, static-IP backstop, button reset
- [ ] 9.5 RELEASE GATE - dual-netif heap soak: free/minFree/maxAlloc during MQTT TLS publishes and a full cloud OTA with both interfaces up
