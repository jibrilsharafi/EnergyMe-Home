## ADDED Requirements

### Requirement: Modbus connection reuse within a run
The `modbus_tcp` steps of one run SHALL share one TCP connection to the job target. The first step SHALL open it; each later step SHALL send on it while it is open and on the same `ip:port`, and SHALL still run the target MAC compare (when the step needs one) before its first byte. A step on a kept connection SHALL count as connected (`connected` true), so a `TIMEOUT`, `NO_REPLY` or `BAD_RESPONSE` on it is a per-step result as in "Scheduling and step outcomes". Any step that does not end `OK` SHALL close the connection, and the next step SHALL open a new one. A read on a kept connection whose send fails, or whose peer closed the connection before any reply byte, SHALL be retried once on a new connection within the same step and the same step timeout; a failed reconnect SHALL stop the run like `CONNECT_FAILED`. The connection SHALL be closed whenever a run ends, whatever the reason (completed, stopped, `DEADLINE`, paused, abandoned, job table replaced). Gateway IoT commands SHALL keep one connection per request.

#### Scenario: Six steps on one connection
- **WHEN** a `modbus_tcp` job with six steps runs against a server that answers every request
- **THEN** the server sees one connection carrying six requests, closed by the device after the sixth reply

#### Scenario: Server closes the connection mid-run
- **WHEN** the server closes the connection after the third reply of a six-step run
- **THEN** the fourth step is sent once on a new connection, all six steps end `OK`, and the server sees two connections with three requests each

#### Scenario: A unit times out mid-run
- **WHEN** the third step of a six-step run times out on the kept connection
- **THEN** its header-only part carries `TIMEOUT`, the connection is closed, and the fourth step opens a new connection and the run continues

#### Scenario: One-shot command
- **WHEN** a `modbus_read` IoT command runs while no collector run is active
- **THEN** it opens and closes its own connection
