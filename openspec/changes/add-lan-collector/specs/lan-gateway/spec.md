## MODIFIED Requirements

### Requirement: Gateway work never blocks the MQTT task
Primitives SHALL run in a dedicated gateway task, one request at a time. The MQTT task SHALL publish `IN_PROGRESS` and hand the request off. The terminal status SHALL be published from the MQTT task once the gateway task reports a result. The same task SHALL also run the scheduled collector jobs of the `lan-collector` capability, and a cloud command SHALL take precedence over them: a command handed off while a collector run is active SHALL start as soon as the step in flight ends (at most that step's timeout, 10 s at most), before the run's next step, and SHALL NOT be rejected for that reason. A `gateway` command received while another command is queued or running SHALL be rejected with reasonCode `GATEWAY_BUSY`. Ops answered by the MQTT task itself (tunnel ops, `collector_status`) SHALL NOT occupy the command slot.

#### Scenario: Concurrent request is rejected
- **WHEN** a second `gateway` command arrives while a `net_scan` is running
- **THEN** the second command is rejected with `GATEWAY_BUSY` and the scan completes normally

#### Scenario: Meter publishing continues during a long request
- **WHEN** an `http_request` to an unresponsive private host waits for its full timeout
- **THEN** meter and MQTT keepalive traffic continue during the wait

#### Scenario: Command during a collector run
- **WHEN** an `http_request` command arrives while a collector job is in a step with a 5000 ms timeout
- **THEN** the command is not rejected, starts within 5000 ms when that step ends, and the collector run continues with its next step after the command's result is stored

#### Scenario: Second command while the first waits for a step
- **WHEN** a command is waiting for a collector step to end and another `gateway` command arrives
- **THEN** the second command is rejected with `GATEWAY_BUSY`

#### Scenario: Collector idle
- **WHEN** a command arrives while no collector run is active
- **THEN** it starts at once, as before this change

## ADDED Requirements

### Requirement: Collector status op
The `gateway` command envelope SHALL accept `op: "collector_status"`, specified in the `lan-collector` capability, in addition to the ops listed in "Gateway command envelope". It SHALL be answered by the MQTT task, SHALL NOT occupy the command slot, and SHALL be rejected with `GATEWAY_DISABLED` while the gateway is disabled, like the primitives.

#### Scenario: Status during a running command
- **WHEN** `collector_status` arrives while an `http_request` command is running
- **THEN** it is answered at once and the running command is unaffected

#### Scenario: Gateway disabled
- **WHEN** `collector_status` arrives while `gateway_enabled` is false
- **THEN** the device publishes `REJECTED` with `GATEWAY_DISABLED`
