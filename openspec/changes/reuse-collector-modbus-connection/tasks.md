## 1. Pure logic (`source/lib/gateway_logic`, `source/lib/collector_logic`)

- [x] 1.0 Job field `conn` (`"run"` default, `"step"`), `modbus_tcp` only; Unity tests in `test_collector_logic`

- [x] 1.1 `modbusMayReuse` (open and same endpoint) and `modbusRetryOnFresh` (reused read, send failed or closed before any reply byte)
- [x] 1.2 Unity tests in `test_gateway_logic`

## 2. Firmware

- [x] 2.1 `GatewayNet::Connection::isOpen`; `GatewayExec::ModbusSession` and `Request::session`
- [x] 2.2 `runModbus` on a held connection: reuse, MAC compare, retry once, close on any failure; one-shot path unchanged
- [x] 2.3 Collector: one session per run for `modbus_tcp` jobs with `conn: "run"`, closed in `_endRun`, before interleaved commands and at gateway task stop
- [x] 2.4 `pio run` for `esp32s3-dev` and `esp32s3-dev-pro`

## 3. Review and bench (dev only: Home .174 and Home Pro .80 polling a logging Modbus TCP server on the PC)

- [x] 3.1 Code-review agents (one adversarial) and a simplification agent; triage, re-run tests
  - Accepted: ARP miss on a kept connection falls back to a new connect; a retry that runs out of time is the per-step `TIMEOUT`; held connection closed before interleaved commands and at task stop; spec wording of the retry; dead null checks and comments trimmed. Unit tests 1059/1059.
- [x] 3.2 Six-step job: one connection, `reqs=6`, per run on both benches; with `conn: "step"`, six connections of one request
- [x] 3.3 Server closing after three requests: two connections of three requests, all six steps `OK`
  - 2026-11-13, both benches, final build: c1/c2 `reqs=3` each per run, health `ok`, 66 bytes (all 33 registers), 0 fails.
- [x] 3.4 Server restarted between runs: the next run works
  - Server restarted three times during the bench; every following run ended `ok`. 3.2: .174 `conn` run, one connection `reqs=6` per run (4 runs); .80 `conn` step, six connections `reqs=1` per run (3 runs).
