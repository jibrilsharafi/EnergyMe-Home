## 1. Pure logic (lib/gateway_logic)

- [x] 1.1 `isPrivateIpv4`: strict dotted-quad parser and the 10/8, 172.16/12, 192.168/16 check
- [x] 1.2 Argument clamps: timeout, UDP wait, ping count, Modbus counts per function
- [x] 1.3 Modbus TCP framing: build read (FC1-4) and write (FC5/6/15/16) requests, parse responses and exceptions, validate transaction id/unit/function
- [x] 1.4 Scan range: the /24 host list around the device address, excluding network, broadcast and self
- [x] 1.5 Unity tests for all of the above (`pio test -e native` from WSL)

## 2. Gateway module (firmware)

- [x] 2.1 `gateway.h/.cpp`: NVS-backed `gateway_enabled` with get/set + mutex, task, depth-1 request queue, mutex-guarded result slot
- [x] 2.2 Primitives: `http_request`, `ping`, `udp_send`, `modbus_read`, `modbus_write`, `net_scan` (ARP sweep + optional mDNS)
- [x] 2.3 Start/stop wiring in `main.cpp` and the restart path

## 3. Command dispatch (mqtt.cpp)

- [x] 3.1 `gateway` operation: envelope validation, base64/object args, enabled/busy/target checks, `IN_PROGRESS`, hand-off
- [x] 3.2 Drain the result slot in the MQTT task and publish the terminal status with the `result` map

## 4. Enable switch surfaces

- [x] 4.1 `system` shadow field `gateway_enabled` (report + apply)
- [x] 4.2 REST `GET/PUT /api/v1/gateway` + `swagger.yaml`

## 5. Validation

- [x] 5.1 Build `esp32s3-dev` and `esp32s3-dev-pro`, `pio check` clean for new files
- [x] 5.2 On-device via `/api/v1/shadow/inject-command`, on Home (192.168.2.174) and Home Pro (192.168.2.80): every primitive against the bench Shelly, Broadlink and Sonoff, rejection paths (disabled, busy, public IP, loopback, bad args), meter publishing during long requests
- [ ] 5.3 Dev cloud end to end: shadow toggle (done on both devices, incl. local edit drift and a non-boolean value), then a real command through the MQTT broker once the `gateway` command template is deployed to dev (energyme-infra PR #787)
- [x] 5.4 Code review agents (one adversarial) + simplification agent, findings triaged
