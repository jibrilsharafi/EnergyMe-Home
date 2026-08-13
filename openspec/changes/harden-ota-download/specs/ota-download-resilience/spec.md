## Purpose

Defines how the firmware behaves once an OTA download has started: what other network traffic the device suppresses to protect the download, how many times and on what schedule a failed download is retried, and what diagnostic detail a failed job execution reports back to AWS IoT.

## ADDED Requirements

### Requirement: Non-essential MQTT publishes are suppressed during the OTA download

While an OTA download is in progress, the system SHALL withhold all non-essential MQTT publishes: meter, grid, energy, system-dynamic, statistics, crash, and the OTA jobs request. The system SHALL keep the AWS IoT MQTT session connected and serviced throughout, and SHALL continue to publish OTA job execution status updates. Suppression SHALL end when the download window ends, whether it succeeded, failed, or was abandoned, and withheld publish requests SHALL be honoured after the window rather than discarded.

#### Scenario: Meter publish falls due during a download

- **WHEN** the meter publish interval elapses while an OTA download is in progress
- **THEN** no meter message is published, and the meter publish remains pending

#### Scenario: Withheld publishes resume after a failed download

- **WHEN** an OTA download ends in failure and publishes were withheld during it
- **THEN** the pending publishes are published on the next publish cycle

#### Scenario: Job status still reported while suppressed

- **WHEN** the system reports OTA job execution status while a download is in progress
- **THEN** the status update is published normally, unaffected by suppression

#### Scenario: Telemetry gap on a successful download

- **WHEN** an OTA download succeeds and the device reboots to validate the new firmware
- **THEN** telemetry withheld during the download window is not published, and this gap is bounded by the duration of the download window

### Requirement: Failed OTA downloads are retried on an exponential backoff

The system SHALL attempt the OTA download up to 5 times before reporting the job execution as `FAILED`. Between attempts it SHALL wait an exponentially increasing delay, starting at 2 minutes, doubling each attempt, and capped at 15 minutes, giving delays of 2, 4, 8 and 15 minutes and a cumulative wait of 29 minutes. The system SHALL NOT abandon the download early on a time budget; if the presigned URL expires mid-schedule, the remaining attempts SHALL run and fail normally, reporting the resulting error. A retry SHALL reuse the presigned URL from the job document without requesting a new one. On the first attempt that succeeds, the system SHALL stop retrying and proceed with the existing post-download flow unchanged.

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

- **WHEN** the presigned URL expires before the retry schedule is exhausted
- **THEN** the remaining attempts still run, fail, and the reported error reflects the expiry rather than a heap condition

#### Scenario: Publishes stay suppressed across the whole retry schedule

- **WHEN** the system is waiting between download attempts
- **THEN** non-essential MQTT publishes remain suppressed until the final attempt resolves

### Requirement: A failed OTA download reports device-side diagnostics

When reporting an OTA job execution as `FAILED` after a download failure, the system SHALL include device-side diagnostics that cannot be derived server-side, as discrete name-value pairs in the job execution status details: the platform error name from the failing download call, download progress in bytes received against total content length, free internal heap, minimum free internal heap, largest contiguous internal allocation, the number of attempts made, device uptime, and WiFi signal strength. Values SHALL be captured at the moment the final attempt fails, not after the retry loop unwinds. The existing `reason` detail SHALL retain its current value so existing consumers are unaffected. The system SHALL NOT include data the job already carries or that is derivable server-side, such as the target firmware version, its checksum, the job identifier, or the device identifier.

#### Scenario: Download fails from internal heap exhaustion

- **WHEN** the final download attempt fails because a TLS or AES buffer could not be allocated
- **THEN** the `FAILED` status details carry the platform error name, the heap figures at failure, the attempt count, and the byte progress reached

#### Scenario: Download fails from an unreachable host

- **WHEN** the final download attempt fails because the host could not be resolved or connected
- **THEN** the `FAILED` status details carry a platform error name distinguishing this from a heap failure

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
