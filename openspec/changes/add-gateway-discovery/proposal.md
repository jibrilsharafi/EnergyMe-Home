## Why

The cloud can reach a LAN device only once it knows its address, and the gateway's only discovery tool is `net_scan` (ARP sweep plus an optional 3 s mDNS query, about 10 s in all). Several integrations announce themselves in other ways: Tuya devices broadcast on UDP 6666/6667, UPnP devices answer SSDP, Broadlink answers a broadcast probe, and a PC asleep on the LAN needs a Wake-on-LAN packet. The gateway rejects every broadcast target today, so none of these work. When a gateway command misbehaves, the cloud also has no view of the gateway's state without device logs (issue #275).

## What Changes

- New `gateway` ops, run in the gateway task one at a time like the existing primitives:
  - `wifi_scan`: SSID, BSSID, RSSI, channel and auth mode of the nearby access points. Rejected while the WiFi station is not running (Home Pro on Ethernet with the radio off), while an association is in flight and while a provisioning scan runs. The radio is never switched on for a scan. A scan while connected leaves the home channel for about 2 s, so MQTT and metering traffic stall for that long.
  - `discover`: fixed-protocol discovery on the device's own subnet. `ssdp` sends one M-SEARCH built on the device to 239.255.255.250:1900 and collects the replies. `broadcast` sends a cloud payload to the subnet's directed broadcast address on one port and collects the replies. `wol` sends a Wake-on-LAN magic packet built on the device. `broadcast` and `wol` are writes and need a `udp` write grant for `<subnet broadcast>:<port>`. The device derives every broadcast or multicast address itself, so the cloud still cannot name a broadcast target.
  - `udp_listen`: listen on one UDP port for a bounded time and return the datagrams from private sources, with a bounded count and bytes per datagram.
  - `mdns_query`: a standalone mDNS browse for one service type, with a clamped timeout (default 1.5 s), returning instance, host, address, port and TXT.
- Gateway status in the `info` shadow: `gateway.busy`, plus the op, status, reason code, Unix time and duration of the last command the gateway task ran. Reported with a short trailing debounce after each change, so a burst of commands publishes once.
- Every network-provided string (SSID, mDNS names and TXT, SSDP headers, datagrams) is checked as UTF-8 text before it goes into a result, because one invalid byte makes AWS refuse the whole command response.

Not in this change (still open on #275): BLE scan, a batch of primitives in one command, publishing to a LAN MQTT broker.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
- `lan-gateway`: four new ops (`wifi_scan`, `discover`, `udp_listen`, `mdns_query`), added as requirements beside the envelope, the way `collector_status` was added.
- `iot-device-shadows`: the `info` shadow gains the `gateway` status object.

## Dependencies

- Builds on add-gateway-modules (write grants, `udp` module classification) and add-lan-collector (the gateway task loop). Archive after both. No MODIFIED requirement, so the deltas do not conflict with the open changes on `lan-gateway`.

## Impact

- Firmware: `source/src/gateway.cpp` (new ops, status), `source/include/gateway.h`, `source/lib/gateway_logic` (SSDP request and reply, Wake-on-LAN packet, clamps, listen port rule), `source/src/gateway_executor.cpp` (grant check for a broadcast target), `source/src/customwifi.cpp` (raw IDF scan owned by the WiFi module), `source/src/shadow.cpp` (info `gateway` object), `source/src/mqtt.cpp` (debounced info report, UNKNOWN_OP text).
- Unit tests in `source/test/test_gateway_logic` (native, WSL); dev e2e on Home .174.
- Cloud: no infra change; the existing `gateway` command template takes a free-string `op`.
- Memory: every receive and record buffer is PSRAM; sockets and the mDNS results are short-lived. The WiFi driver holds the scan list in internal RAM until it is read, which happens immediately.
