## Why

The web password is the only thing standing between a LAN host and the whole device, and nothing slows down guessing it. `_setupMiddleware()` registers `digestAuth` before `rateLimit`, and `AsyncAuthenticationMiddleware::run()` short-circuits with `requestAuthentication()` instead of calling `next()` (`Middleware.cpp:143`) - so the limiter is not merely badly tuned on the failed-login path, it is unreachable. Measured on the bench device: **400 consecutive failed logins, 400 × 401, zero 429**. Issue #197 reports the same at ~19 req/s sustained over 9.4 minutes.

The generic budget would be inadequate even if it ran. `WEBSERVER_MAX_REQUESTS`/`WEBSERVER_WINDOW_SIZE_SECONDS` is 6000 requests per 600 s shared across every endpoint - about 10 req/s with no escalation, which barely inconveniences a dictionary attack.

This matters more now than it did last week. #221 made the device refuse to operate on the shipped default password, so the expected end state is a user-chosen password - and a user-chosen password is exactly the thing worth guessing.

## What Changes

- Add a per-source-IP lockout that counts **only genuine credential failures**. A 401 answering a request that carried no `Authorization` header is the ordinary first leg of every digest handshake - counting those would lock out the legitimate owner after a few page loads. Only a 401 for a request that *did* carry credentials is a failed login.
- Lock an IP out for an escalating window after `AUTH_LOCKOUT_MAX_FAILURES` consecutive failures, answering `429` with `Retry-After` before the digest MD5 is computed. A successful login clears that IP immediately.
- Put the lockout state machine in `source/lib/auth_lockout/` as pure logic with native tests, and have the firmware call it - the same discipline `web_auth_gate` follows, for the same reason.
- Register `authLockout` first, then the generic ceiling, then `digestAuth`. This is also the root fix for #197's headline bug: `digestAuth` short-circuits on failure without calling `next()`, so the limiter that sat after it was structurally unreachable on a 401.
- Scope the generic request ceiling to **unauthenticated requests only**. On a local-first meter the authenticated caller is the owner; a global budget shared across the dashboard, Home Assistant, Modbus poller and automation is a misfeature, and would let one busy legitimate client `429` another. Only credential-less traffic (probes and floods, the real DoS surface) counts toward it. Brute-force guessing carries credentials, so it skips the ceiling and is handled by the lockout - which is the correct division: throttle failed *authentication*, never authenticated *usage*.
- **Make the defence observable.** A lockout is logged (WARNING with the source IP) and raises a device issue (`auth_brute_force`) through the existing registry (#145), so a sustained attack is visible in the REST API and the cloud shadow rather than silent. Logged and raised exactly once per lockout, not per failed request. This is the honest answer to "an attacker can still grind slowly": online guessing cannot be made impossible without a permanent lockout (itself a DoS on the owner), so the layered defence is slow it, force a non-default password (the prior change), and *surface* it so the owner can act.

## Capabilities

### New Capabilities
- `auth-brute-force-protection`: how the device resists password guessing on the local web interface - what counts as a failed login, when a source is locked out, how long for, how it recovers, and what must never be locked out.

### Modified Capabilities
- `web-authentication`: the middleware ordering it documents changes, and the authentication contract gains a new failure mode (`429` before the credential check). The existing requirement that an unauthenticated caller learns nothing about the device's password state must continue to hold - a lockout response must not become a new oracle.
- `crash-reporting` / issue registry (#145): a new device issue code (`auth_brute_force`) is added. No existing issue behaviour changes; this is an additive catalog entry, so it does not need its own spec delta beyond being recorded here.

## Impact

**Code**
- `source/lib/auth_lockout/` + `source/test/test_auth_lockout/`: new pure state machine and its Unity tests.
- `source/src/customserver.cpp`: middleware reorder in `_setupMiddleware()`; a new middleware that consults the table before `digestAuth` and records the outcome after it.
- `source/include/customserver.h`: the new middleware class and its tuning constants.

**Behaviour**
- A device under attack answers `429` instead of `401` once an IP crosses the threshold. Legitimate users are unaffected unless they get the password wrong `AUTH_LOCKOUT_MAX_FAILURES` times in a row, and a single success clears the count.
- The reorder means a client that exhausts the 6000/600 s budget now gets `429` on requests that previously returned `401`. That is the intended fix, not a regression.

**Memory**
- One fixed-size table in `.bss`, `AUTH_LOCKOUT_TABLE_SIZE` entries of ~24 bytes. No heap, no task, nothing on the request path but a linear scan of a handful of entries.

**Security**
- Does not change what a correct password grants, and does not add a way to learn whether a username or password was closer to correct.
- Deliberately out of scope: the password is still stored in plaintext in `auth_ns`; digest auth is still unencrypted over HTTP; and a distributed attacker with many source addresses is only limited by the generic budget, not by the per-IP lockout. NAT means one lockout can affect several users behind one address - accepted, because the alternative is no per-source limit at all.

**Related**
- Closes #197. Builds directly on the middleware chain established by #221.
