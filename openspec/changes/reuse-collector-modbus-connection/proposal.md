## Why

The Roncade pool site polls a Sungrow SG50CX-P2 through its WiNet-S dongle (Modbus TCP 502). Every run fails from the fourth step on: the collector opens and closes one TCP connection per step, about 10 ms apart, and the WiNet-S refuses a fourth fresh connection opened right after three short-lived ones (the steps then fail within about 15 ms as `TIMEOUT`). Many small embedded Modbus servers limit or rate-limit connections the same way.

## What Changes

- The `modbus_tcp` steps of one collector run share one TCP connection: opened at the first step that needs it, kept open while steps succeed, closed at the end of the run (normal end, stop, `DEADLINE`, pause, abandon, job table change).
- Any failed step closes it (`TIMEOUT`, `BAD_RESPONSE`, `MODBUS_EXCEPTION`, a closed connection, a MAC mismatch), so a late reply to a timed-out request can never be read as the next step's reply; the next step opens a fresh connection.
- A step on a kept connection that fails before any reply byte (the send fails, or the peer had closed it) is retried once on a fresh connection within the same step. A failed reconnect stops the run like any connect failure.
- The target MAC compare still runs before the first byte of every step.
- One-shot gateway requests (`modbus_read` and `modbus_write` IoT commands) keep one connection per request.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
- `lan-collector` (introduced by add-lan-collector): ADDED "Modbus connection reuse within a run".

## Impact

- Firmware: `source/lib/gateway_logic` (reuse and retry decisions), `source/include/gateway_executor.h` (`ModbusSession` on the request), `source/src/gateway_modules.cpp`, `source/src/collector.cpp`.
- No shadow, REST, topic or part format change. Infra unchanged; the per-job step limit stays 8.

## Dependencies

- Requires add-lan-collector (implemented, not archived).
