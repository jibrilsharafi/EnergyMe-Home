## Purpose

Scheduled acquisition of raw data from equipment on the customer LAN: the cloud defines polling jobs in the `collector` shadow, the device persists and runs them through the gateway executor without needing the cloud, and publishes the raw replies for cloud-side decoding (infra ADR-034). The device never decodes vendor formats.

## ADDED Requirements

### Requirement: Collector switch
The collector SHALL be disabled on a fresh device. `collector_enabled` SHALL be persisted in NVS, exposed as the writable `enabled` boolean of the `collector` shadow and as `GET/PUT /api/v1/collector` on the authenticated local REST API (not in the web UI), both through the same setter. Jobs SHALL run only while both `collector_enabled` and `gateway_enabled` are true; `gateway_enabled` stays the master switch for all LAN traffic. Turning either switch off SHALL stop the active run before its next step (the step in flight finishes within its own timeout), publish nothing more for that run and leave the stored job set untouched. A non-boolean value SHALL be ignored with a warning and leave the stored and reported state unchanged.

#### Scenario: Fresh device runs nothing
- **WHEN** a job set is applied on a device that never enabled the collector
- **THEN** the job set is persisted and reported, no job runs, and each job reports state `off`

#### Scenario: Gateway off, collector on
- **WHEN** `collector_enabled` is true and `gateway_enabled` is false
- **THEN** no job runs and each job reports state `off` with code `GATEWAY_DISABLED`

#### Scenario: Local kill during a run
- **WHEN** `PUT /api/v1/collector` sets `enabled: false` while a three-step run is in its first step
- **THEN** the first step finishes or times out, steps two and three do not start, and the shadow reports `enabled: false` through drift detection

#### Scenario: Invalid switch value
- **WHEN** a `collector` delta sets `enabled: "yes"`
- **THEN** the device logs a warning, nulls `desired.enabled`, and the stored and reported value stay unchanged

### Requirement: Job set delivered through the collector shadow
The cloud SHALL deliver the whole job set as `desired.jobset` = `{rev, sha256, doc}`: `rev` an integer 1 to 2^31-1, `sha256` the 64-lowercase-hex SHA-256 of the UTF-8 bytes of `doc`, and `doc` the job document as one JSON string. On a delta carrying `jobset` the device SHALL, in the MQTT task, check in this order and stop at the first failure: `doc` length <= 5120 bytes (`TOO_LARGE`), hash matches (`BAD_HASH`), then, when `sha256` equals the applied hash (there is none after `BAD_STORED_SET`, see "Job set persisted on the device"), ack as a no-op without restarting jobs (when the delta's `rev` is higher, the device rewrites `/collector/jobset.json` with the new `rev` through a temporary file and a rename before the ack, and `reported.rev` and the part header `set` take it; if that rewrite fails, `apply_error` is `PERSIST_FAILED` and the old `rev` stays; a lower or equal `rev` leaves everything unchanged), else `rev` greater than the applied rev (`STALE_REV`), `doc` parses as a JSON object (`BAD_JSON`), schema and quotas (`BAD_JOB`, `QUOTA_EXCEEDED`), read-only rule (`WRITE_NOT_ALLOWED`), target rules (`TARGET_NOT_PRIVATE`, `TARGET_NOT_ALLOWED`), template check (`BAD_TEMPLATE`), module availability (`MODULE_UNAVAILABLE`), then persistence (`PERSIST_FAILED`). The set SHALL be applied whole or not at all: on any failure the previously applied set keeps running unchanged and `reported.apply_error` = `{rev, code, job, field}` names the first failure, with all four keys always present and `job` and `field` set to `null` when they do not apply, so the AWS shadow merge never keeps a key from an earlier error. On success `reported.rev` and `reported.sha256` SHALL take the new values and `apply_error` SHALL be `null`. In every case the device SHALL ack with `desired:{jobset:null}` in the same publish, following the existing desired-null contract; the cloud keeps the authoritative copy and compares it with the reported `rev` and `sha256`. An empty `jobs` list SHALL be a valid set that stops all jobs; it still carries `v`, so the empty set is the exact `doc` `{"v":1,"jobs":[]}` with `sha256` over those bytes, and a `doc` without `v` is `BAD_JOB`.

#### Scenario: Valid set applied
- **WHEN** a delta carries `jobset` with `rev: 7`, a matching hash and two valid jobs, and the applied rev is 6
- **THEN** the device persists the set, starts the two jobs and publishes `reported.rev=7`, the new `sha256`, `apply_error: null` and `desired:{jobset:null}` in one message

#### Scenario: Hash mismatch
- **WHEN** `doc` arrives with one byte changed relative to `sha256`
- **THEN** the device reports `apply_error {rev, code: "BAD_HASH", job: null, field: null}`, keeps the old set running and nulls `desired.jobset`

#### Scenario: Stale revision
- **WHEN** a `jobset` with `rev: 5` and a new hash arrives while rev 6 is applied
- **THEN** the device reports `STALE_REV` and keeps rev 6

#### Scenario: One invalid job rejects the whole set
- **WHEN** a set of three jobs has `interval_s: 30` in the second job
- **THEN** none of the three is applied and `apply_error` is `{code: "BAD_JOB", job: "<id>", field: "interval_s"}`

#### Scenario: Empty set stops every job
- **WHEN** the cloud writes `enabled: false` and a `jobset` whose `doc` is `{"v":1,"jobs":[]}` under the next `rev`
- **THEN** the device stores the empty set, stops all jobs and reports the new `rev` and `sha256` with `apply_error: null`; the same write with `doc` `{"jobs":[]}` is refused with `BAD_JOB`, field `v`, and the old set stays stored

#### Scenario: Same content re-sent
- **WHEN** the cloud re-sends the applied document with a higher `rev`, and the device later reboots
- **THEN** the device rewrites the stored file with the new `rev` before the ack, reports the new `rev`, running jobs keep their cursor, session and backoff, parts queued after the ack carry the new `set`, and after the reboot the device still reports the new `rev`

### Requirement: Job set size budget
The serialized `desired` section of the `collector` shadow written by the cloud SHALL be at most 5632 bytes and the device's serialized `reported` section at most 2048 bytes, so the document stays under the 8 KB AWS named-shadow limit and the delta message (state, one metadata entry per leaf, version, timestamp, clientToken and topic) stays under the 9 KB inbound MQTT buffer. Writes above 8 KB are refused by AWS; a delta above 9 KB is dropped by the device's MQTT client without trace, which the cloud SHALL detect as `reported.rev` not reaching the written `rev` within 10 minutes of the device being connected. The device SHALL enforce `doc` <= 5120 bytes as a backstop (`TOO_LARGE`). A job set that does not fit SHALL be reduced by the cloud (fewer or shorter steps, vars, or jobs split across sources), never split across several shadow writes.

#### Scenario: Oversized document backstop
- **WHEN** a delta arrives whose `doc` is 6000 bytes
- **THEN** the device reports `apply_error {code: "TOO_LARGE"}` and keeps the old set

#### Scenario: Delta dropped by the inbound buffer
- **WHEN** the cloud writes a desired state whose delta exceeds 9 KB
- **THEN** the device applies nothing and reports nothing new, and the cloud flags the device when `reported.rev` has not advanced after 10 connected minutes

#### Scenario: Reported state stays within budget
- **WHEN** 8 jobs are configured, each with a 32-character id, a 10-digit `rev`, a 22-character error code and a 24-character `detail`, and `apply_error` names a 32-character job and field
- **THEN** the serialized `reported` section, with the whole `jobs` array and every key present, is at most 2048 bytes

### Requirement: Job set persisted on the device
An applied job set SHALL be written to LittleFS as `/collector/jobset.json` (holding `rev`, `sha256` and `doc`) through a temporary file and a rename, before the shadow ack. A same-hash re-send with a higher `rev` SHALL rewrite the file the same way with only `rev` changed, so the reported `rev` never goes back after a reboot. At boot the device SHALL load it, recompute the hash and validate it with the same rules as a delta except the apply-time read-only classing, which run-time classing replaces (see "Jobs are read-only"); a missing file SHALL mean no jobs, and an unreadable, corrupt or invalid file SHALL mean no jobs, an applied rev of 0 and no applied hash, in RAM and in the reported state (`reported.rev: 0`, `reported.sha256: null`), plus `apply_error {rev: 0, code: "BAD_STORED_SET"}` until the next valid delta. The cloud jobs check then reports the set as lagging and an operator re-sends it (energyme-home-iot-pipeline-lan-points), and that delta, even one carrying the stored `doc` and hash, takes the full validation path and is never acked as a same-hash no-op. Jobs SHALL run from the stored set without any cloud connection. Factory reset SHALL delete the file and reset `collector_enabled` to false. The directory SHALL be outside the paths that the log-clear and energy-file maintenance delete or migrate. Captured session values and cursors SHALL NOT be persisted.

#### Scenario: Runs without the cloud
- **WHEN** a device with an applied job set reboots with no internet access
- **THEN** jobs without time placeholders start running from the stored set and queue their results

#### Scenario: Power cut during the write
- **WHEN** power is lost while a new set is being written
- **THEN** at boot the device loads either the complete old set or the complete new set, never a mix

#### Scenario: Corrupt file
- **WHEN** the stored file fails its hash check at boot
- **THEN** no job runs and the shadow reports `rev: 0`, `sha256: null` and `apply_error.code: "BAD_STORED_SET"`

#### Scenario: Stored set valid by hash fails boot validation
- **WHEN** after a firmware update the stored set passes its hash check but fails validation at boot (for example an `https` job on a product where the module is now `MODULE_UNAVAILABLE`), and the cloud then re-sends the same `doc` and `sha256` with a higher `rev`
- **THEN** the shadow reports `rev: 0`, `sha256: null` and `BAD_STORED_SET` until that delta, and the delta is validated in full and refused or applied, never acked as a same-hash no-op

#### Scenario: Factory reset
- **WHEN** a factory reset runs
- **THEN** `/collector/jobset.json` is gone and `collector_enabled` is false after reboot

### Requirement: Job schema and quotas
The job document SHALL be `{v: 1, budget_bytes_h?, jobs: [...]}` with `budget_bytes_h` 65536 to 8388608 (default 2097152). Each job SHALL hold: `id` (`[a-z0-9_.-]{1,32}`, unique), `rev` (integer >= 1), `enabled` (boolean), `source_id` (`src_` plus 4 to 8 lowercase hex, ADR-034), `target` `{ip, port, mac?}` (`mac` lowercase `aa:bb:cc:dd:ee:ff`, sent with every step, see "Jobs are read-only"), `module` (`http`, `https`, `modbus_tcp`, `tcp`, `udp`), `interval_s` (60 to 86400), `jitter_s` (0 to min(interval_s/2, 300), default 0), `step_timeout_ms` (100 to 10000, default 5000), `deadline_s` (5 to min(300, interval_s/2), default min(60, interval_s/2)), `max_bytes` (1 to 131072, default 16384), `vars` (optional), `session` (optional), `steps` (1 to 8) and `publish` (`per_step` or `batch`, default `per_step`). A step SHALL hold `id` (same grammar, unique across login and data steps of the job), `args` (the module's request fields from `add-gateway-modules` without `module`, `target`, `access`, `writes`, `timeout_ms` and `max_bytes`), and optional `timeout_ms`, `max_bytes` and `captures`. Limits per job: 4 login steps, 8 captures (each at most 2048 bytes, together at most 4096 bytes held at once, see "Captures"), 8 vars (names `[A-Za-z0-9_.-]{1,32}`, values integers or strings of at most 2048 bytes), and the sum of the data steps' effective `max_bytes` at most 131072, so one run always fits the out-queue. Jobs that share a `source_id` SHALL share `target` and `module`. At most 8 jobs SHALL be configured. Unknown keys at any level SHALL be refused with `BAD_JOB`; quota violations with `QUOTA_EXCEEDED`. A job with `enabled: false` SHALL be stored and reported with state `off` but not run.

#### Scenario: Ninth job
- **WHEN** a set holds 9 jobs
- **THEN** it is refused with `QUOTA_EXCEEDED` and field `jobs`

#### Scenario: Unknown key
- **WHEN** a job carries `intervall_s`
- **THEN** the set is refused with `BAD_JOB`, field `intervall_s`

#### Scenario: Shared source with another target
- **WHEN** two jobs use `src_7f3a` with different `target.ip`
- **THEN** the set is refused with `BAD_JOB`, field `source_id`

#### Scenario: Run larger than the queue allows
- **WHEN** a job has three data steps with `max_bytes: 65536` each
- **THEN** the set is refused with `QUOTA_EXCEEDED`, field `max_bytes`

#### Scenario: Deadline longer than half the interval
- **WHEN** a job has `interval_s: 60` and `deadline_s: 45`
- **THEN** the set is refused with `BAD_JOB`, field `deadline_s`

### Requirement: Jobs are read-only
Every login and data step SHALL run as a caller-declared read of the job's own target: the collector SHALL set `declared_read` and `writes: "deny"` (the caller fields of `add-gateway-modules` "Read-only callers") on every step, so a literal POST, PUT, PATCH or DELETE or a `tcp`/`udp` payload is classed read without a grant list entry, and anything the executor still classes write is `REJECTED` `WRITE_NOT_ALLOWED` with reason_detail `WRITES_DENIED` before any socket opens whatever write grants exist. A job step carries no `access` field (`BAD_JOB`) and the job schema has no `writes` or `declared_read` key, so the cloud cannot change either. Write grants SHALL never apply to collector jobs: they authorize one-shot commands only. At apply time (and at boot load) a step that cannot be a literal read SHALL refuse the whole set with `WRITE_NOT_ALLOWED`, field `steps[i]` (or `session.login[i]`): a `modbus_tcp` step with `values`, an `http`/`https` GET with a non-empty `body` or `body_b64`, a POST, PUT, PATCH or DELETE whose path contains a placeholder, and a `tcp` step whose text payload contains a placeholder. Placeholders in a non-GET body stay allowed (the XWEB login body carries `${secret:...}`). The job `target` `mac`, when present, SHALL be sent with every step; the executor requires it for an on-link non-GET or payload step and for a step that expands a secret (`MAC_REQUIRED`), compares it with the ARP entry before the first byte (`TARGET_MAC_CHANGED`) and refuses it for an off-link target. A run-time `WRITE_NOT_ALLOWED` or `SECRET_TARGET_MISMATCH` (reason_detail `WRITES_DENIED`, `MAC_REQUIRED` or `TARGET_MAC_CHANGED`) SHALL stop the run like a target rule failure.

An `http`/`https` GET step without a body SHALL be classed read by method, and the device SHALL NOT try to tell a state-changing GET (for example Shelly `GET /rpc/Switch.Set?on=false`) from a read: a job step cannot raise its class (`access` is `BAD_JOB`) and `writes: "deny"` never fires on a request classed read. Likewise a declared-read POST is read whatever its body. "Jobs are read-only" therefore holds only as far as the job document is a read list; the control is the cloud jobs tool (infra `energyme-home-iot-pipeline-lan-points`), which writes a job set only when every step matches the source driver's `READ_REQUESTS`.

#### Scenario: Modbus write in a job
- **WHEN** a `modbus_tcp` step carries `values`
- **THEN** the set is refused with `WRITE_NOT_ALLOWED`

#### Scenario: GET step with a body
- **WHEN** a set holds a step `GET /rpc` to a Shelly target with the body `{"method":"Switch.Set","params":{"id":0,"on":false}}`
- **THEN** the set is refused with `WRITE_NOT_ALLOWED`, field `steps[i]`

#### Scenario: XWEB POSTs
- **WHEN** a set holds the XWEB job of design worked example 1 with `target` `{ip:"192.168.1.50", port:80, mac:"<xweb mac>"}` and no grant list entry covers the XWEB
- **THEN** the set is accepted, and each run sends the login and data POSTs after the executor matches the XWEB MAC

#### Scenario: Step claims read
- **WHEN** an `http` POST step carries `access: "read"`
- **THEN** the set is refused with `BAD_JOB`, field `access`

#### Scenario: Templated non-GET path
- **WHEN** a job step sends `POST /cgi-bin/datachart.cgi?t=${now}`
- **THEN** the set is refused with `WRITE_NOT_ALLOWED`, since the path is not literal

#### Scenario: State-changing GET accepted by the device
- **WHEN** a set holds an `http` step `GET /rpc/Switch.Set?on=false` to a Shelly target
- **THEN** the device classes it read, accepts the set and sends it every run; nothing on the device detects it, and the only control is the cloud jobs tool refusing GET steps outside the driver's `READ_REQUESTS` before the set is written

#### Scenario: On-link XWEB job without a MAC
- **WHEN** the XWEB job `target` has no `mac` and the XWEB is on-link
- **THEN** the set is applied and each run fails at the login step before any socket opens, its part carrying `st: "REJECTED"`, `rc: "WRITE_NOT_ALLOWED"` and `rdt: "MAC_REQUIRED"`

#### Scenario: Swapped device
- **WHEN** another device takes the XWEB IP after DHCP renumbering
- **THEN** each run fails at the login step before any byte is sent, its part carries `st: "REJECTED"`, `rc: "WRITE_NOT_ALLOWED"` and `rdt: "TARGET_MAC_CHANGED"`, and the job's health shows `detail: "TARGET_MAC_CHANGED"`

#### Scenario: Literal UDP and TCP queries
- **WHEN** a `udp` step carries a fixed 17-byte query frame, or a `tcp` step carries `payload: "STATUS\r\n"`, and the job target has its `mac`
- **THEN** the set is accepted and each run publishes the reply, with no grant list entry

#### Scenario: Templated TCP payload
- **WHEN** a `tcp` step payload contains `${now}`
- **THEN** the set is refused with `WRITE_NOT_ALLOWED`, since the payload is not literal

#### Scenario: TCP banner read
- **WHEN** a `tcp` step carries no payload
- **THEN** the set is accepted and each run publishes the bytes the peer sends until it closes, `idle_ms` passes or `max_bytes` is reached

### Requirement: Placeholders and encodings
The collector SHALL provide these resolvers to the expansion stage of `add-gateway-modules` through the per-call `Resolver*` of its `expand()` (gateway-modules design D7), never through a global registration: `${now}` and `${now:ms}` (the window end in device UTC, decimal seconds or milliseconds; the window end is the run start unless the window is capped), `${cursor}` and `${cursor:ms}` (window start in device UTC), `${var:<name>}` (the job var as text) and `${cap:<name>}` (a captured value, marked sensitive). `${secret:<name>}` resolves through `add-gateway-secrets` and only for the secret's bound target. The `now`, `cursor`, `var` and `cap` resolvers SHALL resolve only inside a job run, against that job's own window, vars and captures: the collector passes its chain (collector resolvers plus the secret resolver) when it expands a job step, and the one-shot command adapter passes the secret resolver alone, so in a cloud `gateway` command `${now}`, `${cursor}`, `${var:x}` and `${cap:x}` are `REJECTED` `BAD_TEMPLATE` before any socket is opened. A job step SHALL never resolve another job's captures or vars. Encodings are those of the expansion stage (`|url`, `|json`, `|b64`). At apply time every placeholder SHALL be well formed, `var` names SHALL exist in `vars`, and `cap` names SHALL be captured by an earlier login or data step of the same job, else `BAD_TEMPLATE` with the step as field. A missing secret or a target mismatch SHALL be a run failure with the reason code of `add-gateway-secrets`, not an apply failure. A request that exceeds a module limit after expansion SHALL fail the run with `BAD_ARGS`. Expanded requests and resolved values SHALL never be logged, published or returned by `collector_status`.

#### Scenario: Time placeholders in device UTC
- **WHEN** device UTC is 1730380000 at the start of a run whose window is not capped
- **THEN** `${now}` expands to `1730380000`, with no source clock offset applied

#### Scenario: Capture used before it is set
- **WHEN** step 1 uses `${cap:sses}` and only step 2 captures `sses`
- **THEN** the set is refused with `BAD_TEMPLATE`, field `steps[0]`

#### Scenario: Secret set later
- **WHEN** a job references `${secret:xweb_pw}` and no such secret exists
- **THEN** the set is applied, each run fails with the secret store's reason code and backs off, and runs succeed once the secret is set

#### Scenario: On-link secret without a MAC
- **WHEN** a GET step of a job uses `${secret:meter_token}` in a header, its target is on-link and the job `target` has no `mac`
- **THEN** the set is applied, each run fails at that step before any socket opens with a part carrying `st: "REJECTED"`, `rc: "SECRET_TARGET_MISMATCH"` and `rdt: "MAC_REQUIRED"`, and runs succeed once the job target carries the MAC

#### Scenario: Capture placeholder in a one-shot command
- **WHEN** a cloud `http_request` command to 192.168.1.99 carries body `${cap:sses}` while the XWEB job holds a live `sses`
- **THEN** the command is `REJECTED` with `BAD_TEMPLATE`, no socket is opened and the captured value is not expanded anywhere

#### Scenario: Time placeholder in a one-shot command
- **WHEN** a cloud `http_request` command carries `startTS=${cursor}`
- **THEN** the command is `REJECTED` with `BAD_TEMPLATE`

### Requirement: Captures
A capture SHALL be `{name, from: "body"|"header", key?, header?, after?, before?}` with `name` in the var grammar. `from: "body"` with `key` SHALL take the value of that top-level key of a JSON object body (string as is, number as its decimal text); `from: "body"` without `key` SHALL take the text between the first occurrence of `after` and the next occurrence of `before` (each 1 to 32 bytes); `from: "header"` with `after` SHALL scan every response header line named `header` (case-insensitive) in reply order and use the first whose value contains `after`, narrowed by `after`/`before`; `from: "header"` without `after` SHALL use the first such line, optionally cut at `before`. No regex, path expressions or scripting. A value SHALL be 1 to 2048 bytes, and the captures held by one job SHALL total at most 4096 bytes; anything else SHALL fail the run with `CAPTURE_MISSING`. Use is bounded by the post-expansion module limit (5760 bytes for an HTTP request), checked as `BAD_ARGS`. Captured values SHALL be held in RAM only, per job, zeroized when replaced or discarded, and discarded on session expiry, every job set adoption, switch-off and reboot. They SHALL never be logged, persisted, published or returned by `collector_status`.

#### Scenario: Session id from a JSON body
- **WHEN** a login reply body is `{"sses":"4f2a9c","user":"op"}` and the capture is `{name:"sses", from:"body", key:"sses"}`
- **THEN** `${cap:sses}` expands to `4f2a9c` in later steps

#### Scenario: Cookie from a header
- **WHEN** the reply has `Set-Cookie: SID=ab12; Path=/` and the capture is `{name:"sid", from:"header", header:"set-cookie", after:"SID=", before:";"}`
- **THEN** the captured value is `ab12`

#### Scenario: Session cookie in the second Set-Cookie line
- **WHEN** the login reply has `Set-Cookie: csrftoken=x9; Path=/` followed by `Set-Cookie: sessionid=q7w; HttpOnly` and the capture is `{name:"sid", from:"header", header:"set-cookie", after:"sessionid=", before:";"}`
- **THEN** the captured value is `q7w`

#### Scenario: Bearer JWT from a login body
- **WHEN** a login reply body is `{"access_token":"<1200-byte JWT>"}` and the capture is `{name:"tok", from:"body", key:"access_token"}`
- **THEN** the 1200-byte value is captured and `Authorization: Bearer ${cap:tok}` expands within the 5760-byte request limit

#### Scenario: Capture over the limit
- **WHEN** a captured value would be 2100 bytes, or a job's captures would total 4200 bytes
- **THEN** the run fails with `CAPTURE_MISSING` and no partial value is kept

#### Scenario: Marker not found
- **WHEN** the `after` delimiter does not occur in the reply
- **THEN** the run fails with `CAPTURE_MISSING`, later steps do not run and no captured value is logged

### Requirement: Session handling with one re-login
A job with `session` = `{login: [steps], ttl_s, expiry: {status?, marker?}}` (`ttl_s` 60 to 86400; `status` up to 4 HTTP status codes; `marker` 1 to 32 bytes) SHALL run its login steps before the first data step whenever no session is held: first run, after `ttl_s` since the last successful login, after expiry, after a job change or reboot. A data step reply whose `proto_status` is in `expiry.status` or whose body contains `marker` SHALL be treated as expiry: the reply is not published, captures are discarded, login runs once and the same step is retried once. A second expiry in the same run SHALL fail the run with `SESSION_EXPIRED`: the retried reply body is not published, and the data step SHALL publish a header-only part (`ph: "step"`, `st: "FAILED"`, `rc: "SESSION_EXPIRED"`, `ps` the retried reply's status, `len: 0`). A login step whose executor result is not `OK` SHALL fail the run with the executor's own `st` and reason code (for example `REJECTED` `WRITE_NOT_ALLOWED`, `SECRET_TARGET_MISMATCH` or `TARGET_NOT_ALLOWED`, or `FAILED` `CONNECT_FAILED` or `TIMEOUT`), carried by its login part as `st` and `rc` with the executor's reason_detail in `rdt`, and by the job health as `code` and `detail`. A login step whose reply arrived (`OK` from the executor) but did not yield every capture of that step SHALL fail the run with `LOGIN_FAILED`, and its login part SHALL carry `st: "FAILED"` and `rc: "LOGIN_FAILED"`. `LOGIN_FAILED` therefore always means the source answered but the login did not produce a session, never a device-side refusal or a transport failure. Login step replies SHALL be published as header-only parts (status and length, never body or headers).

#### Scenario: Session reused across runs
- **WHEN** a job with `ttl_s: 1800` and `interval_s: 300` succeeds twice in a row
- **THEN** the second run sends no login request

#### Scenario: Expired session recovered
- **WHEN** a data step returns the login page containing `marker`
- **THEN** the device logs in once, retries the step, publishes only the retried reply and the run succeeds

#### Scenario: Expiry twice
- **WHEN** the retried step also matches the expiry rule
- **THEN** the run fails with `SESSION_EXPIRED`, the data step publishes a header-only part with `st: "FAILED"`, `rc: "SESSION_EXPIRED"`, `ps` and `len: 0` and no body bytes, and the job backs off

#### Scenario: Wrong password
- **WHEN** the login reply carries no session id
- **THEN** the run fails with `LOGIN_FAILED`, the published login part carries `st: "FAILED"`, `rc: "LOGIN_FAILED"`, `ps`, `total` and `len: 0` only, and the data step does not run

#### Scenario: Login target offline
- **WHEN** the login step of the XWEB job gets `CONNECT_FAILED`
- **THEN** the login part carries `st: "FAILED"` and `rc: "CONNECT_FAILED"`, not `LOGIN_FAILED`, the data step does not run and the job backs off

### Requirement: Scheduling and step outcomes
Runs SHALL be scheduled per job at fixed slots `t0 + k*interval_s` plus a fresh uniform jitter in `[0, jitter_s]`, with `t0` set at boot or job change to now plus a random delay in `[0, min(interval_s, 60)]` s. Missed slots (pause, backoff, a long run) SHALL be skipped, never caught up in a burst. One run SHALL execute at a time, in job list order when several are due. No step SHALL start after `deadline_s` from run start (`DEADLINE`), excluding time spent executing interleaved cloud commands. These step failures SHALL stop the run: `CONNECT_FAILED`, `NETWORK_DOWN`, `NO_MEMORY`, target rule failures, `WRITE_NOT_ALLOWED`, secret store failures, `CAPTURE_MISSING`, `BAD_ARGS`, and for `http`, `https` and `tcp` also `TIMEOUT` and `NO_REPLY`. The collector SHALL NOT infer from the reason code whether the TCP connection was established, since the executor codes a connect that runs out of time as `TIMEOUT`, not `CONNECT_FAILED`; it SHALL use the executor result field `connected` (`add-gateway-modules`) and SHALL NOT use `bytes_tx` for this decision. For `modbus_tcp`, a `TIMEOUT`, `NO_REPLY` or `BAD_RESPONSE` SHALL be a per-step result like `MODBUS_EXCEPTION` only when `connected` is true, which means the connection was established and one unit behind a TCP-to-RTU gateway did not answer properly. Then a header-only part with the code is published and the run continues with the next step. The same codes with `connected` false mean the connect never completed and SHALL stop the run like `CONNECT_FAILED`. A reply that is not a transport failure (any HTTP status outside the expiry rule, `MODBUS_EXCEPTION`, `BAD_RESPONSE`) SHALL be published and the run SHALL continue. Every step that ran SHALL produce a part, a failed step a header-only part with its reason code, so the cloud can tell no response from a value. A run SHALL count as failed, for backoff and health, when it was stopped (by the codes above, `LOGIN_FAILED`, `SESSION_EXPIRED`, `TRUNCATED` or `DEADLINE`) or when none of its data steps ended `st: "OK"`. A run that completed with at least one `OK` data step and one or more failed data steps SHALL count as successful and SHALL set the job's health `code` to the first failed step's `rc` with `st` unchanged by it.

#### Scenario: Modbus exception on one unit
- **WHEN** a three-unit Modbus job gets exception code 2 from the second unit
- **THEN** the second part carries `rc: "MODBUS_EXCEPTION"` and `ps: 2` with no bytes, and the third unit is still read

#### Scenario: Silent unit behind a Modbus gateway
- **WHEN** a five-unit Modbus job on one TCP-to-RTU gateway gets `TIMEOUT` from unit 4 (powered off) while units 1-3 and 5 answer
- **THEN** unit 4 gives a header-only part with `st: "FAILED"`, `rc: "TIMEOUT"`, unit 5 is still read, the run counts as successful, the job keeps its regular slots and its health shows `code: "TIMEOUT"`

#### Scenario: All Modbus units silent
- **WHEN** every step of a Modbus job gets `TIMEOUT` with `connected` true
- **THEN** every step publishes a header-only part, the run counts as failed and the job backs off

#### Scenario: Target offline
- **WHEN** the first step of a job gets `CONNECT_FAILED`
- **THEN** a header-only part is published, the remaining steps do not run and the job enters backoff

#### Scenario: Modbus gateway powered off
- **WHEN** the TCP-to-RTU gateway of a five-unit Modbus job is powered off, so the connect of the first step never completes and the executor returns `FAILED` `TIMEOUT` with `connected` false
- **THEN** the first step publishes a header-only part with `rc: "TIMEOUT"`, the remaining four steps do not run, the run counts as failed and the job enters backoff

#### Scenario: No burst after a pause
- **WHEN** a 60 s job resumes after a 10-minute pause
- **THEN** it runs once at its next slot, not ten times

### Requirement: Per-job backoff
After `n` consecutive failed runs the next run SHALL be scheduled `min(interval_s * 2^n, max(interval_s, 3600))` seconds after the failed run, plus jitter. A successful run SHALL reset `n` and return to slot scheduling. Backoff state SHALL be per job and kept across job set applies only when the job's `id` and `rev` are both unchanged.

#### Scenario: Backoff growth and cap
- **WHEN** a job with `interval_s: 300` fails four times in a row
- **THEN** the delays are 600, 1200, 2400 and 3600 s

#### Scenario: Recovery
- **WHEN** the next run succeeds
- **THEN** the following run is at the next regular slot

### Requirement: Cursor and overlapping window
Each job using `${cursor}` SHALL keep a cursor in device UTC seconds, in RAM, initialised to `now - backfill_s` at its first run after NTP synchronisation and again after a job change, and reset the same way when the clock is found earlier than the cursor. There is no source clock offset: a source whose clock is wrong (stepped, or a local-time clock across a DST change) is fixed at the source, preferably with NTP. The device cannot see such a clock; the cloud `date` drift check flags it, and after the fix the cloud bumps the job `rev`, which resets the cursor and re-requests the last `backfill_s`. A run's window SHALL be `[start, min(start + w, now)]` with `start = max(cursor - overlap_s, now - backfill_s)`, `now` fixed at run start, `overlap_s` 0 to 3600 (default 0), `backfill_s` `overlap_s` to 86400 (default 3600) and `w` the effective window length, at most the reserved var `window_max_s` (60 to 86400, default 3600). A backlog is therefore caught up one window per run. The cursor SHALL advance to the window end only after every part of the run has been written to the MQTT connection and only when every data step of the run ended `st: "OK"`; a failed run, a failed data step, a dropped part or a lost connection SHALL leave it unchanged, so the next window re-covers the gap up to `backfill_s`. A data part with `tr: true` in a cursor job SHALL fail the run with `TRUNCATED` (the part is still published) and halve `w` for the following runs, down to 60 s; each successful run SHALL double `w` again up to `window_max_s`. Duplicates from overlap are expected and removed by the cloud.

#### Scenario: Normal advance with overlap
- **WHEN** a job with `overlap_s: 600` publishes a run with window end 1730380000
- **THEN** the next run's window starts at 1730379400

#### Scenario: Outage longer than backfill
- **WHEN** the cloud connection was down for 10 hours, `backfill_s` is 21600 and `window_max_s` is 3600
- **THEN** the first run after reconnect requests the hour starting 6 hours ago, and the following runs request one hour each until the window reaches the present

#### Scenario: Truncated slice
- **WHEN** a one-hour XWEB window returns more than `max_bytes`
- **THEN** the truncated part is published with `tr: true`, the run fails with `TRUNCATED`, the cursor stays, and the next run requests a 30-minute window from the same start

#### Scenario: Clock steps backwards
- **WHEN** NTP corrects the clock to a time earlier than the cursor
- **THEN** the cursor is reset to `now - backfill_s` and no window with a negative length is requested

#### Scenario: Part dropped from the queue
- **WHEN** one part of a run is dropped by the out-queue
- **THEN** the cursor does not advance and the next run requests the same window start again

### Requirement: Clock rules
Jobs whose templates use `${now}` or `${cursor}` SHALL NOT run until the device clock is NTP-synchronised; they report state `wait` with code `WAIT_CLOCK`. Other jobs SHALL run before synchronisation; their part headers carry `ts: null` and `up` (uptime in ms at reply), and the MQTT task SHALL fill `ts` from `up` at publish time once the clock is synchronised. Source timestamps SHALL be taken as sent: the device applies no source clock offset and the part header carries none; the decoder uses the `date` header only for drift detection.

#### Scenario: Boot without NTP
- **WHEN** a device boots without internet and has an XWEB job using `${cursor}` and a Modbus job without time placeholders
- **THEN** the Modbus job runs and queues parts with `ts: null`, and the XWEB job waits with `WAIT_CLOCK`

#### Scenario: Timestamp filled after sync
- **WHEN** a part queued with `ts: null` is published after NTP synchronisation
- **THEN** its header `ts` is the publish time minus the elapsed uptime since the reply

### Requirement: Priority and pausing
The collector SHALL yield to other device work: in each MQTT loop pass, live telemetry, shadow, command, gateway result and tunnel publishes SHALL go before collector publishes, and collector publishes before the offline energy replay; at most one collector message SHALL be published per pass and at most one per 1000 ms, whatever the loop cadence (the loop runs every 10 ms while a tunnel is open), the same pace as the offline energy replay. The drain SHALL publish nothing while a tunnel session is open, while an OTA download or OTA reboot is pending, or while internal heap is under the `LOW_HEAP` thresholds below; parts stay queued (subject to the out-queue drop rule) and the drain resumes at the first pass after the condition clears. No new run or step SHALL start while a tunnel session is open (`PAUSED_TUNNEL`), while an OTA download or OTA reboot is pending (`PAUSED_OTA`), or while free internal heap is below 40 KB or the largest free internal block below 16 KB (`LOW_HEAP`, thresholds confirmed by the bench task); a run interrupted this way is abandoned without advancing its cursor and the job reports state `wait` with the code. Jobs using `${cursor}` SHALL NOT run while the cloud connection is down (`OFFLINE`); other jobs keep running into the out-queue.

#### Scenario: Tunnel opened mid-run
- **WHEN** a tunnel session opens while an XWEB run is between steps
- **THEN** no further step starts, the job reports `wait` / `PAUSED_TUNNEL`, and it runs at its first slot after the session closes

#### Scenario: OTA starts
- **WHEN** an OTA job starts
- **THEN** no collector step starts until the device has rebooted or the OTA ended

#### Scenario: Full queue during an OTA
- **WHEN** the out-queue holds 256 KB of parts and an OTA download starts
- **THEN** no collector message is published until the OTA ends or the device reboots, and a message being streamed when the OTA started is the last one

#### Scenario: Full queue during a tunnel session
- **WHEN** a tunnel session opens while the out-queue holds 256 KB of parts
- **THEN** no collector message is published while the session is open, and after it closes the drain publishes at most one message per 1000 ms

#### Scenario: Full queue drain pace
- **WHEN** the cloud reconnects with 256 KB of parts queued and no pause applies
- **THEN** the queue drains at most one message (at most 49152 bytes of payload) per 1000 ms

#### Scenario: Offline energy backlog
- **WHEN** the offline energy buffer is replaying after a reconnect and collector parts are queued
- **THEN** at most one collector message is published per 1000 ms, each before the replay chunk of that pass, and the replay keeps progressing

### Requirement: Target rules at apply and run time
Every job target SHALL pass the `lan-gateway` target rules (private IPv4 literal, not the device itself, not a network or broadcast address of its subnets) at apply time and again before each run, because the device address can change. A target that fails at run time SHALL fail the run with `TARGET_NOT_PRIVATE` or `TARGET_NOT_ALLOWED` without opening a socket.

#### Scenario: Device moved onto the target's address
- **WHEN** after a DHCP change the device's own address equals a job target
- **THEN** each run fails with `TARGET_NOT_ALLOWED` and no connection is opened

### Requirement: Cloud commands wait at most one step
The collector SHALL run in the gateway task. Between two steps, and before starting a run, the task SHALL execute a queued cloud `gateway` command first. A command SHALL therefore wait at most one step (bounded by that step's timeout, at most 10 s). Session captures SHALL survive an interleaved command.

#### Scenario: Command between login and data step
- **WHEN** an `http_request` command arrives while an XWEB job is in its login step
- **THEN** the command runs right after the login step, then the data step runs with the captured session, and the run succeeds

### Requirement: Job table handover between tasks
The gateway task SHALL own the active job table and all run state (captures, window, cursor, session, retry flag, deadline, backoff); no other task SHALL read or free them. After validating and persisting a set, the MQTT task SHALL post the new PSRAM table to a one-slot mailbox and return without waiting; a newer set posted before adoption SHALL replace the pending one, and the replaced pending table SHALL be freed by the poster. The gateway task SHALL adopt a pending table only between steps (never while a step or an expansion is in progress): it abandons the active run without publishing more of it and without advancing its cursor, carries `seq` over by job `id`, carries slot, backoff, cursor and window length over only to jobs whose `id` and `rev` are both unchanged, zeroizes the captures and drops the held session of every job, so each session job's next run logs in again before any data step, and frees the old table itself. A captured session value is therefore never sent to a target from an earlier set, whether or not the set bumped `rev`. Health, counters and the data for `collector_status` and the shadow's reported state SHALL come from a fixed-size snapshot struct that the gateway task updates and the MQTT task copies, each under a mutex held only for the copy (no I/O, no allocation, no JSON while it is held), so the MQTT task never waits on a step.

#### Scenario: Apply between steps
- **WHEN** a new set arrives while an XWEB run has finished its login step and not started its data step, and the XWEB job's `id` and `rev` are unchanged
- **THEN** the gateway task adopts the table before the next step, abandons that run without advancing the cursor, zeroizes the captures, keeps the job's slot, backoff, cursor and window length, and the job's next run logs in again before its data step

#### Scenario: Apply during a step
- **WHEN** a new set is posted while a 10 s step is in progress
- **THEN** the MQTT task acks the shadow without waiting, the step finishes or times out (with `publish: per_step` its part is queued as usual), the run is abandoned without advancing its cursor, and the gateway task adopts the new table before any further step

#### Scenario: Changed job
- **WHEN** an adopted set changes a job's `rev`
- **THEN** that job's captures are zeroized, its backoff and cursor reset, and its session is re-established at its next run

#### Scenario: Status during a long step
- **WHEN** the drift check, `collector_status` and a shadow apply happen during a 10 s step
- **THEN** each reads the snapshot or posts to the mailbox within microseconds and MQTT keepalive is not delayed

### Requirement: Raw publish topic and part format
Collector results SHALL be published QoS0, not retained, on `$aws/rules/<rule>/energyme/<product>/v1/<device_id>/collector/<source_id>` with `<rule>` the product's `AWS_IOT_CORE_RULE_COLLECTOR_HOME` or `AWS_IOT_CORE_RULE_COLLECTOR_HOMEPRO`, only from the MQTT task. A message SHALL be one or more parts; a part SHALL be a single-line UTF-8 JSON header (at most 768 bytes, no raw newline) followed by `\n` and exactly `len` raw bytes. Header fields: `v` (1), `job`, `rev` (job rev), `set` (job set rev), `src`, `boot`, `seq`, `step`, `ph` (`login` or `step`), `mod`, `i` and `n` (chunk index and count), `off` (byte offset of the chunk), `len`, `total` (full body bytes), `sha` (SHA-256 hex of the full body, omitted when empty or not published), `tr` (truncated by `max_bytes`), `st` (`OK`, `FAILED`, `REJECTED`), `rc` (reason code, omitted on `OK`), `rdt` (the executor's reason_detail when it is a code token `[A-Z_]{1,24}`, only on parts that are not `OK`, omitted otherwise), `ps` (protocol status), `ts` (Unix ms at reply, or `null`), `up`, `dur` (ms), `win` (`[from, to]` in source-clock seconds, only for jobs with time placeholders), `dq` (parts dropped by the out-queue since boot), `rd` (`true` when the executor reported `redacted` for the step or the collector capture scrub replaced anything, omitted otherwise), and per module `ct` (HTTP Content-Type, at most 64 bytes) and `date` (the raw `Date` response header value, http/https data parts only, never login parts, at most 40 printable ASCII bytes, omitted when absent or longer or not printable ASCII) or `addr` and `cnt` (Modbus canonical address and count). The header with every field at its maximum SHALL fit 768 bytes (an `OK` https data part with every field at its maximum is about 610 bytes); the cloud decoder accepts headers up to 1024 bytes. Raw bytes SHALL be the HTTP body (never the header block, so cookies never leave the device; `date` carries no session material), the Modbus registers as big-endian 16-bit words or coils/inputs packed as in the Modbus PDU, or the TCP reply bytes. Request bytes SHALL never be published.

Secrets and captures SHALL each be scrubbed by one layer. Secrets: the executor scrubs every result whose request expanded a `${secret:x}`, for every caller (`add-gateway-secrets` "Expanded secrets never leave the request"), and reports `redacted`; the collector SHALL NOT scrub secrets again. Captures: for any data step whose request expanded a `${cap:x}` or whose own reply yielded a capture, the collector SHALL run the same pure scrub of `add-gateway-secrets` over the body the executor returned, in its PSRAM buffer, after capture extraction and before chunking and queueing. The scrubbed set SHALL be every captured value the step expanded, in raw form and in the encoded form used, plus every value the step captured from its own reply (body or header), in raw form and in each encoded form in which any step of the job uses that capture (known from the job's templates at apply time). Each occurrence of a value in the set of at least 4 bytes is replaced with `${cap:<name>}`, and when the body was cut by `max_bytes` a trailing prefix of either form of at least 4 bytes at the cut is removed. The scrubbed body SHALL stay within `max_bytes`: when replacements would make it longer, it is cut at `max_bytes` with `tr: true` and no partial value or partial placeholder at the cut. `len`, `total` and `sha` SHALL be computed over the final bytes, after both scrubs, and `rd: true` SHALL be set when the executor reported `redacted` or the capture scrub replaced anything. Login parts publish no body and need no scrub.

#### Scenario: Binary body
- **WHEN** an HTTP step returns a 900-byte body containing NUL bytes
- **THEN** one part carries a header with `len: 900`, `total: 900`, `i: 1`, `n: 1` and the 900 bytes unchanged after the newline

#### Scenario: Login part
- **WHEN** a login step returns a 300-byte reply with a session id
- **THEN** its part has `ph: "login"`, `total: 300`, `len: 0`, no `sha` and no bytes

#### Scenario: Modbus registers
- **WHEN** a step reads `u3/hr3000` with count 2 and gets values 0x0102 and 0x0304
- **THEN** the part has `addr: "u3/hr3000"`, `cnt: 2`, `len: 4` and bytes `01 02 03 04`

#### Scenario: Error page echoes the credential
- **WHEN** a step sent `Authorization: Basic ${secret:meter_basic|b64}` and the 401 page echoes the request headers
- **THEN** the executor's scrub has replaced the encoded credential with `${secret:meter_basic}` and reported `redacted`, the collector finds nothing more to replace, `total` and `sha` match the scrubbed bytes, the header has `rd: true`, and neither the raw nor the base64 value reaches S3

#### Scenario: Reply echoes the session id
- **WHEN** a data step sent `SSES=${cap:sses|url}` and the reply body contains the session id
- **THEN** every occurrence is replaced with `${cap:sses}` before queueing and the header has `rd: true`

#### Scenario: Data step captures from its own body
- **WHEN** a data step that expanded no `${cap:x}` captures `tok` with `from: "body"`, `key: "next"` from its JSON reply, and later steps use `${cap:tok|url}`
- **THEN** `tok` is extracted first, then every raw and URL-encoded occurrence of its value in that step's body is replaced with `${cap:tok}` before queueing, the body stays within `max_bytes`, the header has `rd: true`, and the token value never reaches S3

#### Scenario: Capture scrub grows a body at its cap
- **WHEN** a step with `max_bytes: 4096` expanded a 4-byte capture and the 4096-byte reply is filled with copies of it
- **THEN** the published body is at most 4096 bytes, ends on a whole `${cap:<name>}` placeholder or on non-sensitive bytes, has `tr: true` and `rd: true`, and holds no partial value or placeholder at the cut

#### Scenario: Date header carried
- **WHEN** an XWEB data reply carries `Date: Thu, 31 Oct 2026 12:00:00 GMT`
- **THEN** the data part header has `date: "Thu, 31 Oct 2026 12:00:00 GMT"` and the login part of the same run has no `date`

#### Scenario: Oversized Date header
- **WHEN** a reply carries a 60-byte `Date` value
- **THEN** the part header omits `date`

### Requirement: Chunking and coalescing
A body larger than 49152 bytes SHALL be split into chunks of at most 49152 bytes, each published as its own message with the same `boot`, `job`, `seq` and `step`, `i` from 1 to `n`, and `off`. Queued parts of the same `source_id` SHALL be coalesced into one message while the message stays at most 5120 bytes; a part larger than that SHALL travel alone. Coalescing SHALL use only what is already queued and SHALL NOT delay a publish. With `publish: batch` a run's parts SHALL be queued together at run end; with `per_step` each part is queued as soon as its step ends.

#### Scenario: 100 KB body
- **WHEN** a step returns 102400 bytes with `max_bytes: 131072`
- **THEN** three messages carry chunks of 49152, 49152 and 4096 bytes with `n: 3` and the same `sha`

#### Scenario: Small Modbus batch
- **WHEN** a `batch` job reads three units of 60 registers each
- **THEN** one message of three parts (each 120 raw bytes) is published

### Requirement: Out-queue and hourly budget
Parts SHALL be queued in a PSRAM ring of 256 KB, allocated at boot when `collector_enabled` is persisted true, otherwise when the collector is enabled, and kept until reboot. When the contiguous allocation fails, the switch SHALL stay on and be reported as set, no job SHALL run, each enabled job SHALL report state `wait` with code `NO_MEMORY`, and the allocation SHALL be retried every 60 s until it succeeds. When a new part does not fit, the oldest parts SHALL be dropped, whole bodies at a time (every chunk of a body), and `dq` and the dropped byte counter incremented. Parts being streamed by the MQTT task are pinned and never evicted; if only pinned parts remain the new part is dropped and counted instead. On any `beginPublish`, `write` or `endPublish` failure, a disconnect or a task restart the MQTT task SHALL unpin those parts under the mutex, leave them unpublished and retry them on the next connected pass. A body with at least one chunk already published SHALL be finished, or counted dropped whole in `dq` if it must go; its remaining chunks SHALL never be evicted alone. Published payload bytes SHALL draw from a token bucket of `budget_bytes_h` refilled at `budget_bytes_h/3600` bytes per second; a run SHALL NOT start while the bucket is empty, and the job reports `wait` / `BUDGET`. The queue SHALL NOT survive a reboot.

#### Scenario: Long cloud outage
- **WHEN** a Modbus job keeps polling for hours while the cloud is unreachable
- **THEN** the queue holds the newest 256 KB of parts, `dq` counts the dropped ones, and memory use stays flat

#### Scenario: Budget exhausted
- **WHEN** a job set publishes more than `budget_bytes_h` in an hour
- **THEN** further runs wait with `BUDGET` until the bucket refills

#### Scenario: Ring allocation fails at enable
- **WHEN** the collector is enabled days after boot and PSRAM has no free block of 256 KB
- **THEN** the shadow reports `enabled: true`, no job runs, every enabled job reports `wait` / `NO_MEMORY`, and the device retries the allocation every 60 s and starts the jobs at their next slot once it succeeds

#### Scenario: Connection drops during a 48 KB chunk
- **WHEN** the TLS connection drops while chunk 2 of a 3-chunk body is being streamed and chunk 1 was already published
- **THEN** chunk 2 is unpinned and stays queued, chunks 2 and 3 are published after reconnect before newer parts of that body's job, and the cursor advances only then

### Requirement: Dedup identity
Each part SHALL be identified by `(boot, job, seq, step, i)`: `boot` a random 16-lowercase-hex id generated at every boot, `seq` the job's run counter starting at 1 per boot. `sha` SHALL let the cloud drop identical bodies from overlapping windows. A retried step after session expiry SHALL keep the run's `seq`.

#### Scenario: Power cycle
- **WHEN** the device loses power and boots again
- **THEN** new parts carry a new `boot` id, so `seq` restarting at 1 never collides with parts from the previous boot

### Requirement: Job health in the shadow
`reported` SHALL carry `enabled`, `rev`, `sha256`, `apply_error` and `jobs`, an array with one entry `{id, rev, st, since, code, detail}` per job of the applied set, in job list order, with `st` one of `ok`, `err`, `wait`, `off`, `since` the Unix second of the last change of `st` or `code`, `code` the last error or wait reason (`null` when there is none), and `detail` the executor reason_detail that came with `code` when it is a code token `[A-Z_]{1,24}` (`null` otherwise). Every entry SHALL carry all six keys. Every report that carries `jobs` SHALL carry the whole array: AWS merges nested objects key by key but replaces an array whole, so a job that left the set and a cleared `code` or `detail` disappear from the stored shadow with no id list to keep across reboots. The empty set reports `jobs: []`. `st` SHALL become `err` after 3 consecutive failed runs and `ok` after one successful run. Health-only changes SHALL be reflected in the reported state at most once per 300 s; apply results and switch changes SHALL be reported at once. Counters and timestamps that change on every run SHALL NOT be in the shadow.

#### Scenario: Flapping target
- **WHEN** a job alternates between failing and succeeding every minute
- **THEN** the `collector` shadow is published at most once per 300 s for health changes

#### Scenario: Persistent failure surfaces
- **WHEN** a job fails 3 runs in a row with `CONNECT_FAILED`
- **THEN** within 300 s the shadow reports `st: "err"`, `code: "CONNECT_FAILED"`

#### Scenario: Job removed from the set
- **WHEN** job `mb_old` reported `st: "err"`, `code: "CONNECT_FAILED"`, and a set without `mb_old` is applied (or the device reboots and loads that set)
- **THEN** the next report carries a `jobs` array without `mb_old`, and the stored AWS shadow no longer holds `mb_old`

#### Scenario: Job recovers from an error
- **WHEN** a job in `st: "err"` with `code: "CONNECT_FAILED"` completes a successful run with every data step `OK`
- **THEN** its entry reports `st: "ok"` with `code: null` and `detail: null`, and the stored AWS shadow shows no `code` next to `st: "ok"`

#### Scenario: Later apply error without a job
- **WHEN** an apply failed with `{code: "BAD_JOB", job: "xweb_hist", field: "interval_s"}` and the next delta fails with `BAD_HASH`
- **THEN** `apply_error` is reported as `{rev, code: "BAD_HASH", job: null, field: null}` and the stored shadow keeps neither the old job nor the old field

### Requirement: collector_status op
A `gateway` command with `op: "collector_status"` and args `{}` or `{job}` SHALL be answered by the MQTT task from the health snapshot (see "Job table handover between tasks") without using the gateway execution slot and without touching the job table, subject to `GATEWAY_DISABLED` like every gateway op. It SHALL end `SUCCEEDED` with result keys `enabled` (b), `active` (b), `rev` and `boot` (s), `queue` (s, compact JSON `{bytes, parts, dropped_parts, dropped_bytes}`), `budget` (s, `{limit_h, tokens}`) and `jobs` (s, compact JSON list of `{id, rev, st, code, last_ok, last_err, last_err_detail, last_err_ts, runs, fails, consec_fails, next_run, last_dur_ms, last_bytes, cursor}`), cut to 3500 bytes with `truncated: true` when it does not fit. An unknown `job` SHALL be `REJECTED` with `NO_SUCH_JOB`. No captured value, secret, var value or expanded request SHALL appear.

#### Scenario: Status of one job
- **WHEN** `collector_status` arrives with `{job: "xweb_hist"}`
- **THEN** the result lists only that job's counters, next run and cursor

#### Scenario: Status while a run is active
- **WHEN** `collector_status` arrives during a 10 s step
- **THEN** it is answered at once without waiting for the step

### Requirement: Collector audit log
Each applied job set SHALL be logged at INFO with rev and job count, each refused set at WARNING with its code. Run starts and successes SHALL be logged at DEBUG with job id, seq and byte count; run failures at WARNING with job id and reason code when the job's health changes, else at DEBUG, so a job that keeps failing does not log every interval. Logs SHALL never contain expanded requests, captured values, secret values or reply bodies.

#### Scenario: Failure logged without secrets
- **WHEN** an XWEB run fails with `LOGIN_FAILED`
- **THEN** a WARNING line names the job and `LOGIN_FAILED` and contains no password, session id or body text
