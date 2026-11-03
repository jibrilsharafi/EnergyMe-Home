## ADDED Requirements

### Requirement: Discovery ops
The `gateway` command envelope SHALL accept `op` `wifi_scan`, `discover`, `udp_listen` and `mdns_query` in addition to the ops listed in "Gateway command envelope", with the same envelope, enable switch, single-slot `GATEWAY_BUSY` rule and audit logging as the other primitives. They SHALL run in the gateway task, SHALL NOT go through the executor's single-target path, and SHALL NOT take a target address from the cloud: every broadcast or multicast address SHALL be derived by the device from its active interface. Every string taken from the network (SSID, mDNS instance, host and TXT, SSDP header values, datagram payloads) SHALL be put into a result only when it is valid UTF-8 text by the rule of "Primitive results"; an SSID that is not SHALL be returned as `ssid_hex`, a payload that is not SHALL be returned base64, and any other such string SHALL be left out. Result lists SHALL use `s` with compact JSON and SHALL be cut to fit the MQTT buffer, which sets `truncated: true`.

#### Scenario: Discovery op while another command runs
- **WHEN** `udp_listen` arrives while an `http_request` is running
- **THEN** the device publishes `REJECTED` with `GATEWAY_BUSY`

#### Scenario: Gateway disabled
- **WHEN** `mdns_query` arrives while `gateway_enabled` is false
- **THEN** the device publishes `REJECTED` with `GATEWAY_DISABLED`

### Requirement: WiFi scan op
`wifi_scan` `{channel (optional, 1-13), show_hidden (optional, default false)}` SHALL scan for access points on the WiFi station and return `networks` (`[{"ssid" or "ssid_hex","bssid","rssi","channel","auth"}]`, strongest first, at most 32 entries), `count` (access points the scan found) and `truncated`. It SHALL be rejected with `WIFI_UNAVAILABLE` when the WiFi station is not running or the provisioning access point is up, and with `WIFI_BUSY` while an association attempt or another scan is in progress. The device SHALL NOT switch the radio on for a scan. A failed scan SHALL publish `FAILED` with `SCAN_FAILED`. While connected over WiFi a scan leaves the home channel for about 2 s; MQTT and metering publishes SHALL resume afterwards without a reconnect.

#### Scenario: Scan on a WiFi-connected Home
- **WHEN** `wifi_scan` runs on a Home connected over WiFi
- **THEN** the result lists the nearby access points with their RSSI, channel and auth mode, and the MQTT connection stays up

#### Scenario: Scan with the radio off
- **WHEN** `wifi_scan` arrives on a Home Pro running on Ethernet with the WiFi station stopped
- **THEN** the device publishes `REJECTED` with `WIFI_UNAVAILABLE` and the radio stays off

#### Scenario: Non-text SSID
- **WHEN** an access point advertises an SSID that is not valid UTF-8 text
- **THEN** its entry carries `ssid_hex` with the SSID bytes in lowercase hex and no `ssid`

### Requirement: Discover op
`discover` SHALL take `mode` `ssdp`, `broadcast` or `wol`, and an unknown or missing mode SHALL be `BAD_ARGS`.
- `ssdp` `{st (optional, default "ssdp:all", 1-128 printable ASCII characters without spaces), wait_ms (default 2000, 200-5000)}` SHALL send one M-SEARCH built on the device to 239.255.255.250:1900 with `MX` from `wait_ms`, collect the unicast replies until `wait_ms` elapses, and return `responses` (`[{"ip","port","location","st","usn","server"}]`, fields omitted when absent or not printable ASCII), `count` and `truncated`. It is classed read.
- `broadcast` `{port (required), payload_b64 (1-1460 bytes), wait_ms (default 2000, 0-5000)}` SHALL send the payload once to the directed broadcast address of the active interface's subnet and return `sent: true`, `replies` (`[{"ip","port","s" or "bin"}]`, at most 16 replies, each cut at 512 bytes), `count` and `truncated`. It is classed write.
- `wol` `{mac (required, lowercase aa:bb:cc:dd:ee:ff), port (optional, 7 or 9, default 9)}` SHALL send one Wake-on-LAN magic packet (six 0xFF bytes and sixteen copies of the MAC) to the directed broadcast address on that port and return `sent: true`. It is classed write.

A `broadcast` or `wol` SHALL be rejected with `WRITE_NOT_ALLOWED` before any datagram is sent unless a `udp` write grant exists for the directed broadcast address and port. Replies SHALL be accepted only from private IPv4 sources other than the device. Without an active interface the op SHALL publish `FAILED` with `NETWORK_DOWN`.

#### Scenario: SSDP finds a UPnP device
- **WHEN** `discover` runs with `{mode:"ssdp"}` on a LAN with a UPnP media server
- **THEN** `responses` lists its IP with the `LOCATION` and `USN` it answered with

#### Scenario: Broadcast without a grant
- **WHEN** `discover` runs with `{mode:"broadcast", port:80, payload_b64:"..."}` and no grant names the subnet broadcast address and port 80
- **THEN** the device publishes `REJECTED` with `WRITE_NOT_ALLOWED` and sends nothing

#### Scenario: Wake-on-LAN with a grant
- **WHEN** a `udp` grant `{ip:"192.168.2.255", port:9, module:"udp", allow:["*"]}` is stored on a device in 192.168.2.0/24 and `discover` runs with `{mode:"wol", mac:"4c:a9:54:f6:42:4e"}`
- **THEN** a 102-byte magic packet for that MAC is broadcast to 192.168.2.255:9 and the result carries `sent: true`

### Requirement: UDP listen op
`udp_listen` `{port (1024-65535, not 5353), wait_ms (default 3000, 100-10000)}` SHALL bind the port on all interfaces for `wait_ms` and return `datagrams` (`[{"ip","port","s" or "bin"}]` from private IPv4 sources, at most 16, each cut at 512 bytes), `count` (datagrams received from private sources, kept or not) and `truncated`. A port below 1024 or 5353 SHALL be `BAD_ARGS`; a port already bound on the device SHALL publish `FAILED` with `PORT_IN_USE`. The device SHALL send nothing on the port. No datagram SHALL still be a success with `count: "0"`.

#### Scenario: Tuya announcements
- **WHEN** `udp_listen` runs with `{port:6667, wait_ms:6000}` on a LAN with a Tuya plug
- **THEN** `datagrams` carries the plug's broadcast announcement as `bin`, from its IP

#### Scenario: Port the device uses
- **WHEN** `udp_listen` asks for port 5353
- **THEN** the device publishes `REJECTED` with `BAD_ARGS`

### Requirement: mDNS query op
`mdns_query` `{service (required, e.g. "_http._tcp"), timeout_ms (default 1500, 200-3000)}` SHALL browse that service for `timeout_ms` and return `services` (`[{"instance","host","ip","port","txt"}]`, at most 16, `txt` an object of the text entries, fields omitted when absent or not text), `count` and `truncated`. A malformed service SHALL be `BAD_ARGS`, and mDNS not running SHALL publish `FAILED` with `MDNS_UNAVAILABLE`. No answer SHALL still be a success with `count: "0"`. `net_scan` and its `mdns` list SHALL be unchanged.

#### Scenario: Shelly browse
- **WHEN** `mdns_query` runs with `{service:"_shelly._tcp"}` on a LAN with two Shelly devices
- **THEN** within the timeout the result lists both, each with instance, host, IP, port and TXT entries such as `gen`
