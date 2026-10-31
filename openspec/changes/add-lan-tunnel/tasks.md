## 1. Pure logic (`source/lib/tunnel_logic`)

- [x] 1.1 Frame codec (header + fixed payloads, all bounds checked) and golden frames header shared with the tool tests
- [x] 1.2 ARQ receiver (reorder slots, dedupe, cumulative ack, SACK, serial arithmetic, wrap)
- [x] 1.3 ARQ sender (window, RFC 6298 RTO with Karn and backoff, SACK hole resend, consecutive-timeout count)
- [x] 1.4 Credit accounting, token bucket, sid and port-list parsing, argument clamps, close-reason names and statuses
- [x] 1.5 Unity tests in `source/test/test_tunnel_logic`, green via `pio test -e native` from WSL

## 2. Firmware

- [x] 2.1 `GatewayLogic::Op::TUNNEL_OPEN/TUNNEL_CLOSE` and public `Gateway::validateTarget()`
- [x] 2.2 `tunnel` module: task, PSRAM pools, sockets with `select()`, channels, ARQ, timers, terminal status
- [x] 2.3 MQTT integration: binary RX branch, budgeted uplink drain, loop spin and 10 ms interval while active, op routing, reconnect hooks, OTA close, terminal status publish
- [x] 2.4 `tunnel_enabled`, `tunnel_ports`, `tunnel_active` in the `system` shadow
- [x] 2.5 `/api/v1/gateway` `tunnel` field and `tunnel_active`, swagger
- [x] 2.6 Boot and restart-path hooks; `pio run -e esp32s3-dev` and `-e esp32s3-dev-pro` build

## 3. Operator tool and host tests (`source/utils/lan_tunnel`)

- [x] 3.1 Python protocol module (codec, ARQ, credit) checked against the golden frames
- [x] 3.2 Session engine (operator role) and Python reference device
- [x] 3.3 CLI: dispatch `tunnel_open`/`tunnel_close` with boto3, MQTT5 over WSS with SigV4, local listener, prod guard
- [x] 3.4 Chaos tests: loss 5% and 20%, reordering, duplication, reconnect, abrupt close of either end, server-speaks-first

## 4. Dev end-to-end (Home .174, Home Pro .80), final build, 2026-10-31

- [x] 4.1 Reject paths on both: TARGET_NOT_PRIVATE, TARGET_NOT_ALLOWED (self, broadcast), PORT_NOT_ALLOWED, TUNNEL_DISABLED (each switch), TUNNEL_BUSY, NO_SUCH_SESSION
- [x] 4.2 HTTP: web UI of the other meter, 21 resources (821 KB) byte-identical over 8 parallel connections on both; 10/10 sequential channel reuses; 10% loss 3/3, 20% loss Pro 1/1 and Home 2/3 (the miss is the target server dropping a late request; bulk 5/5 identical)
- [x] 4.3 SSH key exchange to auth stage at 5% loss on both, Modbus TCP reads on both; 6 sequential SSH connections through 4 ids (keep-alive linger frees the 5th after ~5 s)
- [x] 4.4 Robustness: tool killed (OPERATOR_LOST in ~35 s), MQTT drop of ~15 s mid-transfer resumes byte-identical, drop over 30 s ends CLOUD_DISCONNECTED (published after reconnect), REST and cloud-shadow kill switches mid-transfer, tunnel_close, device restart mid-session (TIMED_OUT), OTA job during a session (OTA_STARTED, job SUCCEEDED)
- [x] 4.5 Soak on the Home: 35 min, 170 MB, 1019 checksummed downloads identical, 1 response truncated (cause not identified), internal heap min 73 KB flat, stacks flat (tunnel 3.3 KB free, MQTT 2.1 KB free), no MQTT reconnect, meter points not dropped, ADE readings steady
- [x] 4.6 `command_events` (dev Iceberg) holds created, in_progress and the terminal row for every session and reject above
- [x] 4.7 66-minute session (test build with the clamp at 4200 s): terminal SUCCEEDED MAX_DURATION accepted after the 60-minute line

## 5. Later (not in this change)

- [ ] 5.1 Infra: operator IAM permission set (Connect on `client/tunnel-*`, Publish `.../tunnel/*/d`, Subscribe/Receive `.../tunnel/*/u`, StartCommandExecution on the gateway commands)
- [ ] 5.2 Infra: dedicated `energyme-<product>-<env>-tunnel` command template, so IAM can separate tunnel from gateway rights
- [ ] 5.3 Infra: CDK test asserting no IoT rule SQL matches `.../tunnel/...`
- [ ] 5.4 Infra: requester audit (CloudTrail or an ADR-010 requester column); never log tunnel content, only session metadata
- [ ] 5.5 Infra: move the operator tool next to `dispatch_command.py`; check prod device policies and gateway command templates before the pilot
- [ ] 5.6 Raise the 3300 s duration clamp (66 min verified in dev), end-to-end encryption, web UI banner
- [ ] 5.7 Several targets per session (`CH_OPEN` carries `ip:port`, validated against a list given at open): `--forward` port mappings and SOCKS mode in the tool
- [ ] 5.8 Timestamp echo in ACK for unambiguous RTT samples (today a risen RTT more than 2x the estimate keeps resending)
- [ ] 5.9 Deadline-driven tunnel task wait and a split MQTT loop cadence (today 100 passes/s while a session is open)
- [ ] 5.10 Move channel lifecycle and ack scheduling into the pure lib (unit-testable), per-slot due times in ArqSender
- [ ] 5.11 UDP channel type (BACnet/IP, discovery)
