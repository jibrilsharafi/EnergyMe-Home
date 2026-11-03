## 1. Pure logic in `lib/gateway_logic` with unit tests

- [x] 1.1 Op names `wifi_scan`, `discover`, `udp_listen`, `mdns_query`
- [x] 1.2 Clamps: discover wait, listen wait, mDNS timeout; listen port rule (1024-65535, not 5353); WiFi channel 1-13
- [x] 1.3 SSDP: `ST` validation, M-SEARCH builder (MX from wait), reply parser (status line, case-insensitive LOCATION/ST/USN/SERVER, printable ASCII only, truncation)
- [x] 1.4 Wake-on-LAN magic packet, directed broadcast address

## 2. Firmware

- [x] 2.1 `GatewayExec::udpWriteGranted` over the grant matcher
- [x] 2.2 `discover` (ssdp, broadcast, wol) and `udp_listen` over raw lwIP UDP sockets
- [x] 2.3 `mdns_query` over `mdns_query_ptr`
- [x] 2.4 `CustomWifi::scanForGateway` and `wifi_scan`
- [x] 2.5 Gateway status struct, `info` shadow `gateway` object, debounced report from the MQTT task
- [x] 2.6 UNKNOWN_OP description, header comment

## 3. Verification (dev, Home .174)

- [x] 3.1 Native tests green (WSL), `esp32s3-dev` and `esp32s3-dev-pro` build
  - 2026-11-03: native 1011/1011 (11 new discovery cases in `test_gateway_logic`), both dev builds green.
- [x] 3.2 Golden replay of the existing ops unchanged
  - Baseline captured on .174 with development 2.8.0 before the first flash, replayed on the final build: 42/43 identical, the one difference is the intended `UNKNOWN_OP` description listing the new ops. (The replay caught a review-pass regression that dropped every `bin` payload; fixed before this run.)
- [x] 3.3 Each new op end to end through dev IoT Commands, including rejections (no grant, bad port, port in use, bad service, bad mode)
  - Home .174 over dev IoT Commands: `wifi_scan` (all channels, channel 6, show_hidden, bad channel), `mdns_query` (`_http._tcp` found 4 hosts with TXT, empty `_shelly._tcp`, bad service), `udp_listen` (20 PC datagrams text/binary/1400 B cut at 512, 6667 for 6 s silent, 5353/1023/non-integer refused), `discover` ssdp (`ssdp:all` and `upnp:rootdevice`, FRITZ!Box and WD NAS answered, bad `st`), `broadcast` (refused without grant, echo reply as `bin` with a `192.168.2.255:19000` grant), `wol` (refused without grant, 102-byte magic packet verified on the PC, port 10 refused), bad mode. `PORT_IN_USE` not reproduced on the bench (no device-held UDP port >= 1024); `WIFI_UNAVAILABLE` not testable on a Home (STA always up).
- [x] 3.4 Internal heap free / floor / largest block and gateway stack free before and after each op; MQTT stays connected through `wifi_scan`
  - Internal heap after the golden replay and all new ops: free ~67.7 KB, floor 53.1 KB, largest block 31.7 KB, unchanged by the new ops. `gateway_task` stack floor 2592 B free, set by the golden replay, not lowered by the new ops (frames 96-320 B each, kept out of the task frame with `noinline`). 8 scans: MQTT connections 1, errors 0, WiFi reconnects 0.
- [x] 3.5 `info` shadow shows the gateway status after a command and `busy` during a long one
  - A 10 s `udp_listen` showed `busy: true` about 2 s after it started; it and the following failed `http_request` coalesced into one publish with `last_op: http_request`, `last_status: FAILED`, `last_code: TIMEOUT`, `last_ms: 1508`. A success clears `last_code`.
