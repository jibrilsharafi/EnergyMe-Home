## ADDED Requirements

### Requirement: Info shadow reports the gateway status
The `info` shadow SHALL report a `gateway` object with `busy` (a gateway command is queued, running or not yet answered) and, for the last command the gateway task ran, `last_op`, `last_status` (`SUCCEEDED`, `FAILED` or `REJECTED`), `last_code` (its reasonCode, `null` on success), `last_at` (Unix seconds when it ended, `null` while the clock was not synchronised) and `last_ms` (run time). Before the first command since boot the `last_*` fields SHALL be `null`, so a reboot clears the previous values. Commands answered by the MQTT task without running (`GATEWAY_DISABLED`, `GATEWAY_BUSY`, `UNKNOWN_OP`, `collector_status`, tunnel ops) and collector job steps SHALL NOT change the `last_*` fields. Targets, arguments and payloads SHALL NOT be reported. The device SHALL republish `info` once the gateway status has been unchanged for 2 s after a change, so a burst of commands yields one publish, and a command still running after 2 s is visible as `busy: true`.

#### Scenario: Failed command is visible
- **WHEN** an `http_request` to an unused private address ends with `FAILED` / `CONNECT_FAILED`
- **THEN** about 2 s later the `info` shadow reports `gateway: {busy:false, last_op:"http_request", last_status:"FAILED", last_code:"CONNECT_FAILED", ...}`

#### Scenario: Long command shows busy
- **WHEN** a `udp_listen` with `wait_ms: 10000` is running
- **THEN** the `info` shadow reports `gateway.busy: true` while it runs, and the final status once it ends

#### Scenario: Rejection in the MQTT task
- **WHEN** a command is rejected with `GATEWAY_BUSY`
- **THEN** the `last_*` fields keep describing the command that is running or last ran
