## 1. Pure logic (lib/gateway_logic)

- [ ] 1.1 `isPrivateIpv4`: strict dotted-quad parser and the 10/8, 172.16/12, 192.168/16 check
- [ ] 1.2 Argument clamps: timeout, UDP wait, ping count, Modbus counts per function
- [ ] 1.3 Modbus TCP framing: build read (FC1-4) and write (FC5/6/15/16) requests, parse responses and exceptions, validate transaction id/unit/function
- [ ] 1.4 Scan range: the /24 host list around the device address, excluding network, broadcast and self
- [ ] 1.5 Unity tests for all of the above (`pio test -e native` from WSL)

## 2. Gateway module (firmware)

- [ ] 2.1 `gateway.h/.cpp`: NVS-backed `gateway_enabled` with get/set + mutex, task, depth-1 request queue, mutex-guarded result slot
- [ ] 2.2 Primitives: `http_request`, `ping`, `udp_send`, `modbus_read`, `modbus_write`, `net_scan` (ARP sweep + optional mDNS)
- [ ] 2.3 Start/stop wiring in `main.cpp`, task profiler registration

## 3. Command dispatch (mqtt.cpp)

- [ ] 3.1 `gateway` operation: envelope validation, base64/object args, enabled/busy/target checks, `IN_PROGRESS`, hand-off
- [ ] 3.2 Drain the result slot in the MQTT task and publish the terminal status with the `result` map

## 4. Enable switch surfaces

- [ ] 4.1 `system` shadow field `gateway_enabled` (report + apply)
- [ ] 4.2 REST `GET/PUT /api/v1/gateway` + `swagger.yaml`
- [ ] 4.3 Web UI toggle on the configuration page

## 5. Validation

- [ ] 5.1 Build `esp32s3-dev` and `esp32s3-dev-pro`, `pio check` clean for new files
- [ ] 5.2 On-device via `/api/v1/shadow/inject-command`, on Home (192.168.2.174) and Home Pro (192.168.2.80): every primitive against the bench Shelly, Broadlink and Sonoff, rejection paths (disabled, busy, public IP, loopback, bad args), meter publishing during long requests
- [ ] 5.3 Dev cloud end to end: shadow toggle, then a real command through the MQTT broker on both devices
- [ ] 5.4 Code review agents (one adversarial) + simplification agent, findings triaged
