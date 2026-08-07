## 1. The lockout state machine as pure, tested logic

- [x] 1.1 Create `source/lib/auth_lockout/auth_lockout.{h,cpp}`: a fixed-size `Table` of entries keyed by `uint32_t` address, with `init()`, `isLocked(table, ip, nowMs, &retryAfterSeconds)`, `recordFailure(table, ip, nowMs)`, `recordSuccess(table, ip, nowMs)`. No Arduino headers, no networking types, must compile for `native` (design D6).
- [x] 1.2 Implement escalation: `AUTH_LOCKOUT_MAX_FAILURES` consecutive failures lock for `AUTH_LOCKOUT_BASE_SECONDS`, doubling per subsequent lockout, capped at `AUTH_LOCKOUT_MAX_SECONDS` (design D4). Success clears the entry outright.
- [x] 1.3 Exempt loopback in the pure logic, not only structurally (design D7).
- [x] 1.4 Oldest-activity eviction when the table is full, so a many-source flood cannot grow memory (design D5).
- [x] 1.5 Write `source/test/test_auth_lockout/test_auth_lockout.cpp`: threshold boundary (N-1 failures does not lock, N does); success clears at every point below the threshold; lockout expires exactly at its deadline; escalation doubles and then stops at the ceiling; loopback never locks; eviction under more sources than slots; `millis64()` wraparound is not assumed to matter but is covered; retryAfter is never zero or negative while locked.
- [x] 1.6 `pio test -e native` green before touching any firmware file.

## 2. Fix the ordering bug

- [x] 2.1 Move `server.addMiddleware(&rateLimit)` ahead of `server.addMiddleware(&digestAuth)` in `_setupMiddleware()` so a failed login consumes the generic budget (design D1). This alone is the fix for issue #197's first point.
- [x] 2.2 Confirm `defaultPasswordGuard` stays last, so it still only ever sees authenticated callers.

## 3. The lockout middleware

- [x] 3.1 Add `AuthLockoutMiddleware` to `source/include/customserver.h` with the tuning constants. `run()` consults the table, sends `429` + `Retry-After` without calling `next()` when locked, otherwise calls `next()` and inspects `request->getResponse()` (design D2).
- [x] 3.2 Count a failure **only** when the response is `401` *and* the request carried an `Authorization` header (design D3). A credential-less `401` is the ordinary first leg of every digest handshake and must never count - getting this wrong locks out every normal user.
- [x] 3.3 Treat any non-401 outcome as a success that clears the source, including the `403` from the default-password guard, which means authentication succeeded. Treat a null response as "no outcome observed" and record nothing.
- [x] 3.4 Register it first among the throttles, before `rateLimit` and `digestAuth`, so a locked-out source costs the least work.
- [x] 3.5 Verify by inspection that no threshold or duration logic is restated in `customserver.cpp` - it must call the library.

## 4. Build and unit verification

- [x] 4.1 `pio test -e native` - green including the new suite.
- [x] 4.2 `pio run -e esp32s3-dev` - compiles clean, no new warnings.
- [x] 4.3 cppcheck clean on `lib/auth_lockout/`.

## 5. Hardware verification (bench device)

- [x] 5.1 OTA the build and confirm normal operation first - a legitimate user must be provably unaffected before anything adversarial is run.
- [x] 5.2 Reproduce the original bug is gone: repeated failed logins now produce `429`, where the pre-fix measurement was 400 failed logins with zero 429.
- [x] 5.3 **The false-positive test, which matters more than the attack tests**: sign in correctly many times in a row, in fresh sessions so each one starts with a credential-less 401, and confirm no lockout ever occurs.
- [x] 5.4 Confirm a lockout expires on its own, and that a correct password immediately after expiry works.
- [x] 5.5 Confirm escalation: lock out, wait, fail again, and observe a longer second lockout.
- [x] 5.6 Confirm the device stays healthy throughout - uptime continuous, no restart, `/api/v1/health` answering, since the previous change's failure mode was a restart loop ending in firmware rollback.

## 6. Adversarial testing by agents

Run agents whose brief is to *break* this, not to confirm it works. Each gets the device address and credentials and is told to report a bypass, not a pass.

- [x] 6.1 Agent A - **bypass**: could not defeat the single-IP throttle. Header/Host spoofing, credential-less interleave, cross-path accumulation, health-reset all correctly handled - the key is the unspoofable socket IP. **Found one real gap**: the upload routes authenticated before the lockout gate (they run during body parse, ahead of the middleware), so guessing could move there unthrottled and a correct guess would reach Update.begin(). Fixed in d5b4860 and re-verified. The only residual bypass is 8-slot table eviction, which needs >=8 genuine source IPs and is documented (D5).
- [x] 6.2 Agent B - **DoS**: could not produce one against the local-first deployment. Keyed on the unspoofable socket IP (header spoofing collapses onto one entry), escalation capped at 15 min, state RAM-only, health probe exempt and never degraded. The reverse-proxy / CGNAT shared-IP caveat is real and now documented in design D-Risks.
- [x] 6.3 Agent C - **disclosure**: the headline 401-vs-403 default-password oracle is REFUTED - the digestAuth-before-guard ordering means 403 only reaches an already-authenticated caller, so an unauthenticated guesser always gets 401 (design D2 holds). No username oracle (wrong-user and wrong-pass are byte-identical), no usable timing oracle. Retry-After magnitude leaks a source's own escalation tier - per-source, minor, accepted. Separately surfaced the pre-existing digest-replay weakness (nonce issued but never validated), filed as #222 - out of scope for #197.
- [x] 6.4 Triaged: the upload-route gap was reproduced and fixed (d5b4860); the 401-vs-403 oracle was traced in code and refuted; the digest-replay finding was verified real but pre-existing and filed as #222; the shared-IP and table-eviction limits are documented, not defects.
- [x] 6.5 Survived: across three concurrent attacker agents plus follow-up testing, crashCount 0, lastResetWasCrash false (the resets are OTA reboots), health answered 200 throughout, and a correct password works. No NVS damage.

## 7. Land it

- [x] 7.1 Committed in chunks: the pure lib + tests; the ordering fix + middleware; the upload-route fix from adversarial testing.
- [x] 7.2 PR #223 to `development`, `Closes #197`, labels bug + robustness, no milestone. Records the 400x401/zero-429 pre-fix measurement, the post-fix 429 behaviour, and what all three attacker agents did and did not manage.

## 8. Refinements (from review discussion)

- [x] 8.1 Scope the generic ceiling to unauthenticated requests only (`UnauthenticatedRateLimitMiddleware`), so authenticated owner traffic is never throttled (design D1a). Verified: 60 rapid authenticated GETs all 200, no 429.
- [x] 8.2 Log a lockout (WARNING with source IP) and count it once via `recordFailure`'s new lockout-edge return (design D1b). Verified on hardware.
- [x] 8.3 Raise `Code::AuthBruteForce` in the issue registry on any new lockout in the window; pulse-and-linger like PanicReboot (design D1b). Verified: issue appears as `auth_brute_force` / `cleared_unacked` with a human message after a lockout.
- [x] 8.4 `pio test -e native` green (434 cases incl. the new lockout-edge and issue_logic coverage); `pio run -e esp32s3-dev` clean.
