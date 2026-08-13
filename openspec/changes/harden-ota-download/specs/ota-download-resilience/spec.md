## Purpose

Defines how the firmware behaves when an OTA download fails: how many times and on what schedule the download is retried, when retrying is abandoned as pointless, and what diagnostic detail a failed job execution reports back to AWS IoT.

## ADDED Requirements

### Requirement: Failed OTA downloads are retried on an exponential backoff

The system SHALL attempt the OTA download up to 5 times before reporting the job execution as `FAILED`. Between attempts it SHALL wait an exponentially increasing delay, starting at 2 minutes, doubling each attempt, and capped at 15 minutes, giving delays of 2, 4, 8 and 15 minutes and a cumulative wait of 29 minutes. The system SHALL NOT abandon the schedule on an elapsed-time budget. A retry SHALL reuse the presigned URL from the job document without requesting a new one. On the first attempt that succeeds, the system SHALL stop retrying and proceed with the existing post-download flow unchanged.

The system SHALL stop retrying early, and report the failure immediately, when the server refused the request with a 4xx HTTP status, since that outcome cannot change on a later attempt with the same URL. It SHALL also stop retrying when the MQTT module is shutting down. A restart of the MQTT task on its own, such as a cloud-services configuration change, SHALL NOT abandon the schedule.

#### Scenario: First attempt succeeds

- **WHEN** the first download attempt completes successfully
- **THEN** no retry is scheduled and the system proceeds to reboot for validation as before

#### Scenario: Transient failure recovers on a later attempt

- **WHEN** an early download attempt fails and a later attempt within the 5-attempt schedule succeeds
- **THEN** the job execution is not reported as `FAILED`, and the system proceeds to reboot for validation

#### Scenario: All attempts fail

- **WHEN** all 5 download attempts fail
- **THEN** the job execution is reported as `FAILED` once, after the final attempt

#### Scenario: Presigned URL expires partway through the schedule

- **WHEN** an attempt is refused with a 4xx status because the presigned URL has expired
- **THEN** the remaining attempts are abandoned and the failure is reported immediately, carrying the HTTP status that identifies the expiry

#### Scenario: MQTT task restarts during a backoff wait

- **WHEN** the MQTT task is stopped and started again, without the module being shut down, while a backoff wait is in progress
- **THEN** the retry schedule continues rather than being abandoned

### Requirement: A failed OTA download reports device-side diagnostics

When reporting an OTA job execution as `FAILED` after a download failure, the system SHALL include device-side diagnostics that cannot be derived server-side, as discrete name-value pairs in the job execution status details: the platform error name from the failing download call, the HTTP status of the response, download progress in bytes received against total content length, the internal-heap figures (free, minimum free, and largest contiguous allocation), the number of attempts made, device uptime, and WiFi signal strength. Values SHALL be captured at the moment the final attempt fails, not after the retry loop unwinds. The existing `reason` detail SHALL retain its current value so existing consumers are unaffected. The number of pairs SHALL stay within the job service's limit with headroom. The system SHALL NOT include data the job already carries or that is derivable server-side, such as the target firmware version, its checksum, the job identifier, or the device identifier.

Because the platform's download call reports every 4xx and 5xx response as the same generic error, the HTTP status SHALL be the field that distinguishes a server refusal from a transport or memory failure. Byte progress SHALL be reported only for a response that actually carried firmware; for any other response the system SHALL report progress as unavailable rather than describing the error body's length as a completed download.

#### Scenario: Download fails from internal heap exhaustion

- **WHEN** the final download attempt fails because a TLS or AES buffer could not be allocated
- **THEN** the `FAILED` status details carry the platform error name, the heap figures at failure, the attempt count, and the byte progress reached

#### Scenario: Download fails from an unreachable host

- **WHEN** the final download attempt fails because the host could not be resolved or connected
- **THEN** the `FAILED` status details carry a platform error name distinguishing this from a heap failure

#### Scenario: Download refused with an error status

- **WHEN** the download is refused with a 4xx or 5xx response
- **THEN** the `FAILED` status details carry that HTTP status, and progress is reported as unavailable rather than as a completed transfer

#### Scenario: Existing reason value preserved

- **WHEN** any download failure is reported
- **THEN** the `reason` detail still holds the value it held before this change

#### Scenario: Failures other than download are unchanged

- **WHEN** a job execution fails after a successful download, such as a partition, checksum, or preferences error
- **THEN** its status details are reported exactly as before this change

### Requirement: The retry backoff schedule is computed by a host-testable module

The backoff delay for a given attempt number SHALL be produced by a pure computation that depends on no hardware, network, or framework facility, so that the schedule can be verified by host-run unit tests. Given an attempt number, an initial delay, a maximum delay, and a multiplier, it SHALL return the initial delay scaled by the multiplier raised to one less than the attempt number, clamped to the maximum, and SHALL return zero for attempt zero. It SHALL NOT overflow or wrap for any attempt number.

#### Scenario: Schedule matches the specified delays

- **WHEN** the delay is computed for attempts 1 through 5 with a 2 minute initial delay, a multiplier of 2, and a 15 minute cap
- **THEN** the delays are 2, 4, 8, 15 and 15 minutes

#### Scenario: Attempt zero has no delay

- **WHEN** the delay is computed for attempt 0
- **THEN** the result is zero

#### Scenario: Large attempt numbers clamp rather than overflow

- **WHEN** the delay is computed for an attempt number large enough to overflow the scaled value
- **THEN** the result is the maximum delay
