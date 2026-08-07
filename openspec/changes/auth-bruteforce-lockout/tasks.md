## 1. The lockout state machine as pure, tested logic

- [ ] 1.1 Create `source/lib/auth_lockout/auth_lockout.{h,cpp}`: a fixed-size `Table` of entries keyed by `uint32_t` address, with `init()`, `isLocked(table, ip, nowMs, &retryAfterSeconds)`, `recordFailure(table, ip, nowMs)`, `recordSuccess(table, ip, nowMs)`. No Arduino headers, no networking types, must compile for `native` (design D6).
- [ ] 1.2 Implement escalation: `AUTH_LOCKOUT_MAX_FAILURES` consecutive failures lock for `AUTH_LOCKOUT_BASE_SECONDS`, doubling per subsequent lockout, capped at `AUTH_LOCKOUT_MAX_SECONDS` (design D4). Success clears the entry outright.
- [ ] 1.3 Exempt loopback in the pure logic, not only structurally (design D7).
- [ ] 1.4 Oldest-activity eviction when the table is full, so a many-source flood cannot grow memory (design D5).
- [ ] 1.5 Write `source/test/test_auth_lockout/test_auth_lockout.cpp`: threshold boundary (N-1 failures does not lock, N does); success clears at every point below the threshold; lockout expires exactly at its deadline; escalation doubles and then stops at the ceiling; loopback never locks; eviction under more sources than slots; `millis64()` wraparound is not assumed to matter but is covered; retryAfter is never zero or negative while locked.
- [ ] 1.6 `pio test -e native` green before touching any firmware file.

## 2. Fix the ordering bug

- [ ] 2.1 Move `server.addMiddleware(&rateLimit)` ahead of `server.addMiddleware(&digestAuth)` in `_setupMiddleware()` so a failed login consumes the generic budget (design D1). This alone is the fix for issue #197's first point.
- [ ] 2.2 Confirm `defaultPasswordGuard` stays last, so it still only ever sees authenticated callers.

## 3. The lockout middleware

- [ ] 3.1 Add `AuthLockoutMiddleware` to `source/include/customserver.h` with the tuning constants. `run()` consults the table, sends `429` + `Retry-After` without calling `next()` when locked, otherwise calls `next()` and inspects `request->getResponse()` (design D2).
- [ ] 3.2 Count a failure **only** when the response is `401` *and* the request carried an `Authorization` header (design D3). A credential-less `401` is the ordinary first leg of every digest handshake and must never count - getting this wrong locks out every normal user.
- [ ] 3.3 Treat any non-401 outcome as a success that clears the source, including the `403` from the default-password guard, which means authentication succeeded. Treat a null response as "no outcome observed" and record nothing.
- [ ] 3.4 Register it first among the throttles, before `rateLimit` and `digestAuth`, so a locked-out source costs the least work.
- [ ] 3.5 Verify by inspection that no threshold or duration logic is restated in `customserver.cpp` - it must call the library.

## 4. Build and unit verification

- [ ] 4.1 `pio test -e native` - green including the new suite.
- [ ] 4.2 `pio run -e esp32s3-dev` - compiles clean, no new warnings.
- [ ] 4.3 cppcheck clean on `lib/auth_lockout/`.

## 5. Hardware verification (bench device)

- [ ] 5.1 OTA the build and confirm normal operation first - a legitimate user must be provably unaffected before anything adversarial is run.
- [ ] 5.2 Reproduce the original bug is gone: repeated failed logins now produce `429`, where the pre-fix measurement was 400 failed logins with zero 429.
- [ ] 5.3 **The false-positive test, which matters more than the attack tests**: sign in correctly many times in a row, in fresh sessions so each one starts with a credential-less 401, and confirm no lockout ever occurs.
- [ ] 5.4 Confirm a lockout expires on its own, and that a correct password immediately after expiry works.
- [ ] 5.5 Confirm escalation: lock out, wait, fail again, and observe a longer second lockout.
- [ ] 5.6 Confirm the device stays healthy throughout - uptime continuous, no restart, `/api/v1/health` answering, since the previous change's failure mode was a restart loop ending in firmware rollback.

## 6. Adversarial testing by agents

Run agents whose brief is to *break* this, not to confirm it works. Each gets the device address and credentials and is told to report a bypass, not a pass.

- [ ] 6.1 Agent A - **bypass the lockout**: vary source appearance (headers like `X-Forwarded-For`, `X-Real-IP`, spoofed `Host`), alternate paths and methods, mix credential-less and credentialed requests to keep the counter from advancing, and try to find any route that checks credentials without going through the lockout middleware (upload/body routes are the known-dangerous class here - they check auth inside the handler, so confirm they consult the lockout too or explain why not).
- [ ] 6.2 Agent B - **lock out the legitimate owner (denial of service)**: try to get a valid user locked out by a third party, exhaust the table with many synthetic sources to evict a real attacker's entry, and probe whether the lockout can be made permanent or long enough to constitute a DoS.
- [ ] 6.3 Agent C - **information disclosure and timing**: determine whether the `429`, the `401`, or response timing reveals whether a password was correct, whether the device is on its default password, or whether a username exists.
- [ ] 6.4 Triage every finding: reproduce it, decide real vs theoretical, fix or document with the reason. Do not mark this done on an agent's say-so alone.
- [ ] 6.5 Confirm the device survived the adversarial run: no crash, no restart, no NVS damage, still reachable, and a correct password still works.

## 7. Land it

- [ ] 7.1 Commit in small chunks: the pure lib + tests; the ordering fix; the middleware; any fixes from adversarial testing.
- [ ] 7.2 PR to `development` with `Closes #197`, labels `bug` + `robustness`, no milestone. Record the pre-fix and post-fix measurements, and what the attacker agents did and did not manage.
