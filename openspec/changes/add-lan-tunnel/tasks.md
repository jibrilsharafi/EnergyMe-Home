## 1. Pure logic (`source/lib/tunnel_logic`)

- [ ] 1.1 Frame codec (header + fixed payloads, all bounds checked) and golden frames header shared with the tool tests
- [ ] 1.2 ARQ receiver (reorder slots, dedupe, cumulative ack, SACK, serial arithmetic, wrap)
- [ ] 1.3 ARQ sender (window, RFC 6298 RTO with Karn and backoff, SACK fast retransmit, consecutive-timeout count)
- [ ] 1.4 Credit accounting, token bucket, sid and port-list parsing, argument clamps, close-reason names and statuses
- [ ] 1.5 Unity tests in `source/test/test_tunnel_logic`, green via `pio test -e native` from WSL

## 2. Firmware

- [ ] 2.1 `GatewayLogic::Op::TUNNEL_OPEN/TUNNEL_CLOSE` and public `Gateway::validateTarget()`
- [ ] 2.2 `tunnel` module: task, PSRAM pools, sockets with `select()`, channels, ARQ, timers, terminal status
- [ ] 2.3 MQTT integration: binary RX branch, budgeted uplink drain, loop spin and 10 ms interval while active, op routing, reconnect hooks, OTA close, terminal status publish
- [ ] 2.4 `tunnel_enabled`, `tunnel_ports`, `tunnel_active` in the `system` shadow
- [ ] 2.5 `/api/v1/gateway` `tunnel` field and `tunnel_active`, swagger
- [ ] 2.6 Boot and restart-path hooks; `pio run -e esp32s3-dev` builds

## 3. Operator tool and host tests (`source/utils/lan_tunnel`)

- [ ] 3.1 Python protocol module (codec, ARQ, credit) checked against the golden frames
- [ ] 3.2 Session engine (operator role) and Python reference device
- [ ] 3.3 CLI: dispatch `tunnel_open`/`tunnel_close` with boto3, MQTT5 over WSS with SigV4, local listener, prod guard
- [ ] 3.4 Chaos tests: loss 5% and 20%, reordering, duplication, reconnect, abrupt close of either end

## 4. Dev end-to-end (Home .174, Home Pro .80)

- [ ] 4.1 Reject paths: switches off, busy, port not allowed, self target, non-private target
- [ ] 4.2 HTTP through the tunnel to a LAN web UI, multi-channel page load, checksummed bulk download
- [ ] 4.3 SSH (or record why not tested) and Modbus TCP through the tunnel
- [ ] 4.4 Robustness: tool killed (OPERATOR_LOST), injected loss, MQTT reconnect, REST and shadow kill switches, tunnel_close
- [ ] 4.5 Soak with heap, stack high-water marks, MQTT loop time and telemetry cadence watched
- [ ] 4.6 Both rows in `command_events` for a session

## 5. Later (energyme-infra, not in this change)

- [ ] 5.1 Operator IAM permission set: Connect on `client/tunnel-*`, Publish `.../tunnel/*/d`, Subscribe/Receive `.../tunnel/*/u`, StartCommandExecution on the gateway commands
- [ ] 5.2 Dedicated `energyme-<product>-<env>-tunnel` command template, so IAM can separate tunnel from gateway rights
- [ ] 5.3 CDK test asserting no IoT rule SQL matches `.../tunnel/...`
- [ ] 5.4 Requester audit (CloudTrail or an ADR-010 requester column)
- [ ] 5.5 Move the operator tool next to `dispatch_command.py`
- [ ] 5.6 65-minute session test before raising the 3300 s duration clamp; end-to-end encryption, SOCKS mode, web UI banner
