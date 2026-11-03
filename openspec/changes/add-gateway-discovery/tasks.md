## 1. Pure logic in `lib/gateway_logic` with unit tests

- [ ] 1.1 Op names `wifi_scan`, `discover`, `udp_listen`, `mdns_query`
- [ ] 1.2 Clamps: discover wait, listen wait, mDNS timeout; listen port rule (1024-65535, not 5353); WiFi channel 1-13
- [ ] 1.3 SSDP: `ST` validation, M-SEARCH builder (MX from wait), reply parser (status line, case-insensitive LOCATION/ST/USN/SERVER, printable ASCII only, truncation)
- [ ] 1.4 Wake-on-LAN magic packet, directed broadcast address

## 2. Firmware

- [ ] 2.1 `GatewayExec::udpWriteGranted` over the grant matcher
- [ ] 2.2 `discover` (ssdp, broadcast, wol) and `udp_listen` over raw lwIP UDP sockets
- [ ] 2.3 `mdns_query` over `mdns_query_ptr`
- [ ] 2.4 `CustomWifi::scanForGateway` and `wifi_scan`
- [ ] 2.5 Gateway status struct, `info` shadow `gateway` object, debounced report from the MQTT task
- [ ] 2.6 UNKNOWN_OP description, header comment

## 3. Verification (dev, Home .174)

- [ ] 3.1 Native tests green (WSL), `esp32s3-dev` and `esp32s3-dev-pro` build
- [ ] 3.2 Golden replay of the existing ops unchanged
- [ ] 3.3 Each new op end to end through dev IoT Commands, including rejections (no grant, bad port, port in use, bad service, bad mode)
- [ ] 3.4 Internal heap free / floor / largest block and gateway stack free before and after each op; MQTT stays connected through `wifi_scan`
- [ ] 3.5 `info` shadow shows the gateway status after a command and `busy` during a long one
