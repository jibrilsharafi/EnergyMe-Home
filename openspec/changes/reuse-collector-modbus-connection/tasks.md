## 1. Pure logic (`source/lib/gateway_logic`)

- [x] 1.1 `modbusMayReuse` (open and same endpoint) and `modbusRetryOnFresh` (reused read, send failed or closed before any reply byte)
- [x] 1.2 Unity tests in `test_gateway_logic`

## 2. Firmware

- [x] 2.1 `GatewayNet::Connection::isOpen`; `GatewayExec::ModbusSession` and `Request::session`
- [x] 2.2 `runModbus` on a held connection: reuse, MAC compare, retry once, close on any failure; one-shot path unchanged
- [x] 2.3 Collector: one session per run for `modbus_tcp` jobs, closed in `_endRun`
- [x] 2.4 `pio run` for `esp32s3-dev` and `esp32s3-dev-pro`

## 3. Review and bench (dev only: Home .174 and Home Pro .80 polling a logging Modbus TCP server on the PC)

- [ ] 3.1 Code-review agents (one adversarial) and a simplification agent; triage, re-run tests
- [ ] 3.2 Six-step job: one connection, `reqs=6`, per run on both benches
- [ ] 3.3 Server closing after three requests: two connections of three requests, all six steps `OK`
- [ ] 3.4 Server restarted between runs: the next run works
