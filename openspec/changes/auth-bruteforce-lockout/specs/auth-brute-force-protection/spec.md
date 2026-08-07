## Purpose

Defines how the device resists password guessing against its local web interface: what the device treats as a failed login, when it stops answering a source at all, how long that lasts, how a legitimate user recovers, and which callers must never be locked out.

## ADDED Requirements

### Requirement: Repeated failed logins are throttled

The device SHALL limit how fast a single source address can test passwords. After a bounded number of consecutive failed logins from one source, it SHALL refuse further requests from that source for a period, without evaluating the supplied credentials.

The refusal SHALL tell the client how long to wait, and SHALL cost the device less work than checking the credentials would have.

#### Scenario: Sustained password guessing

- **WHEN** a source sends failed login after failed login
- **THEN** the device stops answering that source with a credential check and starts refusing it outright, so guessing rate is bounded by the lockout rather than by how fast requests can be sent

#### Scenario: Refusal names the wait

- **WHEN** a source is refused because it is locked out
- **THEN** the response states how long to wait before retrying

#### Scenario: Locked-out request is cheap

- **WHEN** a locked-out source sends another request
- **THEN** the device refuses it without performing the credential computation

### Requirement: Only a genuine credential failure counts as a failed login

A request that supplies no credentials at all is the ordinary opening move of an authentication handshake, not a failed login, and SHALL NOT count toward the lockout. Only a request that supplied credentials and was rejected SHALL count.

#### Scenario: Ordinary browser sign-in

- **WHEN** a user opens the web interface repeatedly, each time causing the browser's credential-less first request to be challenged
- **THEN** none of those challenges count toward a lockout, and the user is never locked out for browsing normally

#### Scenario: Unauthenticated scanning

- **WHEN** a host on the network requests protected pages without ever supplying credentials
- **THEN** it is challenged each time and is not locked out by this requirement, because it is not guessing passwords - only the device's general request budget limits it

#### Scenario: Wrong password supplied

- **WHEN** a request carries credentials that do not match
- **THEN** it counts toward the lockout for that source

### Requirement: A successful login clears the source immediately

A correct password SHALL reset the failure count for that source, so a user who mistypes and then succeeds is not penalised afterwards.

#### Scenario: Mistyped then corrected

- **WHEN** a user fails several times, below the threshold, and then signs in successfully
- **THEN** their failure count is cleared and the next mistake starts from zero

### Requirement: Lockout expires on its own and lengthens under repeated abuse

A lockout SHALL end without any action from the user or the device's owner. A source that resumes failing after its lockout expires SHALL be locked out for longer than before, up to a bounded maximum, so that persistent guessing becomes progressively less productive while an honest mistake costs only a short wait.

#### Scenario: Honest mistake

- **WHEN** a user locks themselves out by mistyping
- **THEN** the lock expires after a short wait and they can try again, with no reset, no reboot, and no physical access

#### Scenario: Persistent attacker

- **WHEN** a source waits out its lockout and immediately resumes failing
- **THEN** each subsequent lockout is longer than the last, up to a fixed ceiling

#### Scenario: Lockout never becomes permanent

- **WHEN** a source has been locked out many times
- **THEN** the lockout duration stops growing at a bounded maximum, so no source is ever permanently barred

### Requirement: Throttling never disables the device or its own probes

The device's own liveness probe SHALL never be locked out. Tracking SHALL use a fixed amount of memory regardless of how many distinct sources attack it, and SHALL NOT allocate on the request path.

Being locked out SHALL NOT prevent the device from operating, restarting cleanly, or being recovered by physical means.

#### Scenario: Attack from many source addresses

- **WHEN** requests arrive from more distinct sources than the device can track
- **THEN** it keeps working within its fixed memory budget rather than growing without bound, and continues to throttle what it can still see

#### Scenario: Device self-check during an attack

- **WHEN** the device is under sustained password guessing
- **THEN** its own health probe continues to succeed and it does not restart itself

#### Scenario: Owner locked out and impatient

- **WHEN** the owner is locked out and power-cycles the device
- **THEN** it comes back ready to accept a correct password

### Requirement: A lockout discloses nothing about the password

The response to a locked-out source SHALL NOT reveal whether any attempted password was closer to correct, whether the username exists, or what state the device's password is in.

#### Scenario: Attacker reads the refusal

- **WHEN** an attacker inspects the locked-out response
- **THEN** it conveys only that they are being throttled and when to retry

#### Scenario: Guessing the shipped default

- **WHEN** an attacker guesses passwords against a device
- **THEN** the throttling response is the same whether or not any guess was correct-but-refused for another reason
