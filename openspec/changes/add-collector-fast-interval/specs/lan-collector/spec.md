## MODIFIED Requirements

### Requirement: Job schema and quotas
The job document SHALL be `{v: 1, budget_bytes_h?, jobs: [...]}` with `budget_bytes_h` 65536 to 8388608 (default 2097152). Each job SHALL hold: `id` (`[a-z0-9_.-]{1,32}`, unique), `rev` (integer >= 1), `enabled` (boolean), `source_id` (`src_` plus 4 to 8 lowercase hex, ADR-034), `target` `{ip, port, mac?}` (`mac` lowercase `aa:bb:cc:dd:ee:ff`, sent with every step, see "Jobs are read-only"), `module` (`http`, `https`, `modbus_tcp`, `tcp`, `udp`), `interval_s` (10 to 86400), `jitter_s` (0 to min(interval_s/2, 300), default 0), `step_timeout_ms` (100 to 10000, default 5000), `deadline_s` (5 to min(300, interval_s/2), default min(60, interval_s/2)), `max_bytes` (1 to 131072, default 16384), `vars` (optional), `session` (optional), `steps` (1 to 8) and `publish` (`per_step` or `batch`, default `per_step`). For `interval_s` below 60 the effective timeouts of all login and data steps together SHALL NOT exceed `deadline_s` seconds (`BAD_JOB`, field `step_timeout_ms`), so a step that times out never makes the next one miss the deadline; from 60 s up this rule does not apply. A step SHALL hold `id` (same grammar, unique across login and data steps of the job), `args` (the module's request fields from `add-gateway-modules` without `module`, `target`, `access`, `writes`, `timeout_ms` and `max_bytes`), and optional `timeout_ms`, `max_bytes` and `captures`. Limits per job: 4 login steps, 8 captures (each at most 2048 bytes, together at most 4096 bytes held at once, see "Captures"), 8 vars (names `[A-Za-z0-9_.-]{1,32}`, values integers or strings of at most 2048 bytes), and the sum of the data steps' effective `max_bytes` at most 131072, so one run always fits the out-queue. Jobs that share a `source_id` SHALL share `target` and `module`. At most 8 jobs SHALL be configured. Unknown keys at any level SHALL be refused with `BAD_JOB`; quota violations with `QUOTA_EXCEEDED`. A job with `enabled: false` SHALL be stored and reported with state `off` but not run.

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

#### Scenario: Ten-second job
- **WHEN** a job has `interval_s: 10` and no `deadline_s` or `jitter_s`
- **THEN** it is accepted with `deadline_s` 5 and `jitter_s` 0, and a job with `interval_s: 9` is refused with `BAD_JOB`, field `interval_s`

#### Scenario: Ten-second job with two slow steps
- **WHEN** a job has `interval_s: 10`, two Modbus steps and the default `step_timeout_ms` 5000
- **THEN** the set is refused with `BAD_JOB`, field `step_timeout_ms`, and is accepted with `step_timeout_ms: 2500`

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

#### Scenario: Ten-second slots
- **WHEN** a job has `interval_s: 10`
- **THEN** its runs start every 10 s (plus jitter) on fixed slots, and a slot missed by a long run is skipped

### Requirement: Per-job backoff
After `n` consecutive failed runs the next run SHALL be scheduled `min(interval_s * 2^n, max(interval_s, min(3600, 60 * interval_s)))` seconds after the failed run, plus jitter. A successful run SHALL reset `n` and return to slot scheduling. Backoff state SHALL be per job and kept across job set applies only when the job's `id` and `rev` are both unchanged.

#### Scenario: Backoff growth and cap
- **WHEN** a job with `interval_s: 300` fails four times in a row
- **THEN** the delays are 600, 1200, 2400 and 3600 s

#### Scenario: Fast job cap
- **WHEN** a job with `interval_s: 10` keeps failing
- **THEN** the delays are 20, 40, 80, 160, 320 and then 600 s, never more

#### Scenario: Recovery
- **WHEN** the next run succeeds
- **THEN** the following run is at the next regular slot

### Requirement: Job health in the shadow
`reported` SHALL carry `enabled`, `rev`, `sha256`, `apply_error` and `jobs`, an array with one entry `{id, rev, st, since, code, detail}` per job of the applied set, in job list order, with `st` one of `ok`, `err`, `wait`, `off`, `since` the Unix second of the last change of `st` or `code`, `code` the last error or wait reason (`null` when there is none), and `detail` the executor reason_detail that came with `code` when it is a code token `[A-Z_]{1,24}` (`null` otherwise). Every entry SHALL carry all six keys. Every report that carries `jobs` SHALL carry the whole array: AWS merges nested objects key by key but replaces an array whole, so a job that left the set and a cleared `code` or `detail` disappear from the stored shadow with no id list to keep across reboots. The empty set reports `jobs: []`. `st` SHALL become `err` after any failed run and `ok` after one successful run, so a failing job never reports `ok`. An enabled job adopted without carried state (new, or a changed job `rev`) SHALL report `st: "wait"`, `code: "PENDING"` until its first run ends, never a clean `ok` before a run succeeded. Apply results, switch changes and a job entering `err` SHALL be reported at once, an `err` onset at most once per job per 300 s (a later onset within 300 s of the last one reported at once is coalesced like other health changes); other health-only changes (recovery, a new code while already failing, wait reasons) SHALL be reflected in the reported state at most once per 300 s. Counters and timestamps that change on every run SHALL NOT be in the shadow.

#### Scenario: Flapping target
- **WHEN** a 10 s job alternates between failing and succeeding
- **THEN** the first failure is reported at once as `st: "err"`, and later failures and the recoveries in between are coalesced to at most one report per 300 s

#### Scenario: First failure surfaces at once
- **WHEN** a job that reported `st: "ok"` fails one run with `LOGIN_FAILED`
- **THEN** the shadow reports `st: "err"`, `code: "LOGIN_FAILED"` right after that run, not after the 300 s coalescing window

#### Scenario: Re-applied job does not look healthy
- **WHEN** a job in `st: "err"` with `code: "LOGIN_FAILED"` is re-applied with a new job `rev`
- **THEN** the shadow reports `st: "wait"`, `code: "PENDING"` at once, and `st: "err"` again at once if the next run fails

#### Scenario: Job removed from the set
- **WHEN** job `mb_old` reported `st: "err"`, `code: "CONNECT_FAILED"`, and a set without `mb_old` is applied (or the device reboots and loads that set)
- **THEN** the next report carries a `jobs` array without `mb_old`, and the stored AWS shadow no longer holds `mb_old`

#### Scenario: Job recovers from an error
- **WHEN** a job in `st: "err"` with `code: "CONNECT_FAILED"` completes a successful run with every data step `OK`
- **THEN** its entry reports `st: "ok"` with `code: null` and `detail: null`, and the stored AWS shadow shows no `code` next to `st: "ok"`

#### Scenario: Later apply error without a job
- **WHEN** an apply failed with `{code: "BAD_JOB", job: "xweb_hist", field: "interval_s"}` and the next delta fails with `BAD_HASH`
- **THEN** `apply_error` is reported as `{rev, code: "BAD_HASH", job: null, field: null}` and the stored shadow keeps neither the old job nor the old field
