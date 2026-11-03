## Context

The gateway task runs one primitive at a time with an 8 KB internal stack (about 2.2 KB free at its worst after add-lan-collector). Internal heap is the constraint on both products (Home .174: ~75 KB free, 44 KB floor, 31 KB largest block). The executor (add-gateway-modules) is built around one validated unicast `ip:port`; `validateTarget` rejects the device's own addresses and the network/broadcast addresses of its subnets, which the original add-lan-gateway design chose so the cloud cannot reach the local API or flood a subnet.

## Goals / Non-Goals

Goals: the four discovery ops of #275 at low heap cost; the gateway status visible in the cloud. Non-goals: BLE, batches, LAN MQTT, switching the WiFi radio on, any change to existing op replies.

## Decisions

### D1. Discovery ops bypass the executor
`ping` and `net_scan` already do. `validateTarget` keeps rejecting broadcast for every other op; the new ops never take an address from the cloud: the directed broadcast comes from `netif_default` (address | ~mask, read under `LOCK_TCPIP_CORE`), the SSDP group is the fixed 239.255.255.250:1900.

### D2. Read/write classes
An SSDP M-SEARCH is built on the device from a validated `ST` and only asks devices to describe themselves: read, no grant. `broadcast` carries cloud bytes to every host, and `wol` wakes a machine: both are writes, consistent with "every udp send is a write". They need a `udp` grant for `<directed broadcast>:<port>` (the grant validator already accepts it, it is a private literal), matched with the executor's grant matcher through a small `GatewayExec::udpWriteGranted(ip, port)`. No MAC binding: a broadcast has no single ARP entry. `udp_listen` sends nothing: read.

### D3. Sockets
Raw lwIP BSD sockets (`SOCK_DGRAM`), `SO_BROADCAST` only where needed, no `SO_REUSEADDR` (a port someone else holds must fail with `PORT_IN_USE`, never steal or share its datagrams). Receive with `select()` capped per wait and an absolute deadline, one 1460-byte PSRAM receive buffer, kept entries go straight into a PSRAM `JsonDocument`. Entries are capped (16 replies/datagrams, 512 bytes each, 32 SSDP responses) and every datagram is read even when it is not kept, so lwIP pbufs (internal RAM, 6-deep receive mailbox) are released at once. Sources must be private IPv4 and not the device.

### D4. `udp_listen` ports
1024-65535 except 5353 (mDNS). Below 1024 the device owns or may own the port (DHCP 68, NTP) and nothing in scope needs it. An ephemeral port held by an outgoing socket fails `bind()` and reports `PORT_IN_USE`.

### D5. `wifi_scan` lives in CustomWifi
customwifi.cpp touches Arduino's scan buffer only from the AsyncTCP task. The gateway scan therefore uses raw IDF, owned by a new `CustomWifi::scanForGateway()`: `esp_wifi_scan_start(&config, true)` blocks the gateway task only; Arduino's `_scanDone` ignores a scan it did not start (no `WIFI_SCANNING_BIT`), so it never steals the records. Records go into a PSRAM array (cap 32); `esp_wifi_scan_get_ap_records` frees the driver list, and `esp_wifi_clear_ap_list` covers the error paths. Refusals: STA not started or AP up (`WIFI_UNAVAILABLE`); association in flight or an Arduino scan running (`WIFI_BUSY`). Active scan, 120 ms per channel, home-channel dwell 30 ms (IDF default): about 13 x 150 ms = 2 s off-channel in short slices. The radio is never switched on: on Home Pro the WiFi driver costs tens of KB of internal heap and bringing the STA up could flip interface arbitration and reconnect MQTT.

### D6. `mdns_query` uses IDF directly
`MDNS.queryService` has a fixed 3 s timeout and shared static results. `mdns_query_ptr(service, proto, timeout, 16, &results)` with `timeout_ms` clamped to 200-3000 ms, results freed with `mdns_query_results_free` before the reply is built from the PSRAM document. Results come from internal heap (`CONFIG_MDNS_MEMORY_ALLOC_INTERNAL`), bounded by 16 results and short-lived.

### D7. Untrusted strings
SSIDs are 32 raw bytes, mDNS and SSDP strings and datagrams come from any host. AWS refuses the whole command response on one bad byte, so each value passes `utf8TextLength` (strings) or a printable-ASCII check (SSDP header values) first; an SSID that fails goes out as `ssid_hex`, a payload as base64, anything else is left out.

### D8. Gateway status
A small struct in gateway.cpp (`busy` is the existing `_busy` flag; current op and start time; last op, status, code, end time, run time), written by the gateway task at the start and end of `_run` and read by the `info` report, under the existing result mutex. A sequence number bumps on every change. The MQTT task loop requests an `info` report when the sequence changed and has been stable for 2 s (trailing debounce): a quick command yields one publish with the final state, a long one shows `busy` after 2 s. `info` is not drift-checked (it is published on connect and every 24 h), so this is the only trigger. Only commands the gateway task ran count; MQTT-task rejections and collector steps do not, so `last_*` answers "what did the gateway last do".

## Risks / Trade-offs

- WiFi scan hiccup: about 2 s of off-channel time while connected; MQTT keepalive and publishes survive it (bench check), the cloud decides when to scan.
- `broadcast` reaches every host on the subnet, including meters on the same LAN; the grant makes it an explicit cloud decision, and payload size is capped at one datagram.
- `udp_listen` cannot receive broadcasts dropped by WiFi modem sleep: if a device is not heard, retry with a longer `wait_ms`.
- The `WIFI_UNAVAILABLE` path cannot be bench-tested on a Home (its STA is always up); it is a plain mode check.
