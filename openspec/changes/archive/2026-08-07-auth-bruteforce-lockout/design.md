## Context

See `proposal.md` - Why, and issue #197. Constraints that shape the approach:

- **The chain is synchronous and order-sensitive.** `AsyncMiddlewareChain::_runChain` (`Middleware.cpp:56-71`) walks the list front to back; a middleware short-circuits by not calling `next()`. `AsyncAuthenticationMiddleware::run` (`:143`) does exactly that on failure, which is why anything registered after it never sees a 401.
- **A middleware can observe the outcome.** `CustomMiddleware` (`customserver.h`) already calls `next()` and then inspects `request->getResponse()->code()`. That is the hook for "did this request fail authentication", and it means one middleware can both gate and record.
- **The request path is the AsyncTCP task.** Same rule the provisioning filters and the default-password guard established: no allocation, no NVS, no logging on the hot path.
- **Digest nonces are not validated.** `AsyncWebServerRequest::authenticate` passes `nonce = NULL` (`WebRequest.cpp:1138`), and `checkDigestAuthentication` only compares the nonce when it is non-null (`WebAuthentication.cpp:202`). So a stale nonce cannot produce a spurious 401 - which removes the main source of false-positive lockouts.
- **Every digest handshake begins with a 401.** The client's first request carries no `Authorization` header and is challenged. A browser does this on every fresh session. Counting those as failed logins would lock out the legitimate owner within a few page loads.

## Goals / Non-Goals

**Goals:**

- Make password guessing rate-limited rather than unbounded, at fixed memory cost.
- Never lock out someone who is browsing normally, and never require physical access or a reboot to recover from a lockout.
- Keep the failed-login decision in host-testable pure logic.

**Non-Goals:**

- Defeating a distributed attacker. A per-IP table cannot, and the generic budget is what remains against that.
- Protecting the credential in transit. Digest over plain HTTP is unchanged, as is the plaintext password in `auth_ns`.
- Any persistence. Lockout state is deliberately RAM-only (see D5).
- CAPTCHA, account lockout notifications, or anything needing a second channel.

## Decisions

### D1. Reorder so the throttles run before `digestAuth`

The reported bug is that `digestAuth` short-circuits with `requestAuthentication()` instead of calling `next()` (`Middleware.cpp:143`), so anything after it never runs on a 401. Moving the throttles ahead of it is the fix, and it is the conventional order anyway - cheap checks before the expensive credential check.

Final chain: `customMiddleware` -> `authLockout` -> `unauthRateLimit` -> `digestAuth` -> `defaultPasswordGuard`.

`defaultPasswordGuard` stays last so it still sees only authenticated callers (its own D2 from the previous change). `authLockout` goes first among the throttles because a locked-out source should cost the least possible work.

### D1a. The generic ceiling throttles unauthenticated requests only

The pre-existing `AsyncRateLimitMiddleware` is a *global* sliding-window counter (one `std::list` for the whole server, confirmed in `Middleware.cpp`), 6000 requests / 600 s. Applying that to authenticated traffic is wrong for a local-first meter: the authenticated caller is the owner, and a global budget shared across the dashboard, Home Assistant, the Modbus poller and automation would let one busy legitimate client `429` another. Rate-limiting *failed authentication* is the right thing; rate-limiting *authenticated usage of one's own device* is not.

So a thin `UnauthenticatedRateLimitMiddleware` wraps the library limiter and delegates to it only when the request carries no `Authorization` header. That is exactly the DoS surface worth bounding - credential-less probes and floods - and it deliberately excludes brute-force guesses, which carry credentials and are the lockout's job. A generous global ceiling is then harmless: it only ever sees handshake first-legs and unauthenticated traffic, never the owner's real work.

*Alternative considered:* keying the ceiling per-IP instead of scoping it by auth state. Rejected as more machinery for less benefit - the lockout already provides the per-IP guarantee where it matters (failed logins), and the remaining job of the global ceiling is purely volumetric DoS protection, which does not need per-IP accounting.

### D1b. A lockout is logged and raised as a device issue

A silent security control is backwards - the owner should be able to see that someone hammered `admin`. `recordFailure` returns true only on the failure that crosses the threshold, so the middleware logs (WARNING, with the source IP) and bumps `statistics.webServerAuthLockouts` exactly once per lockout, never per failed request.

The device issue goes through the existing registry (#145), which is derive-from-facts: the lockout only increments a counter, and the registry tick pulses `Code::AuthBruteForce` on any new lockout in the window. Like `PanicReboot` it lingers in `ClearedUnacked` until acked, so an attack that has since stopped is still visible - in `GET /api/v1/system/issues` and, via the shadow, the cloud. This is deliberate architecture, not a shortcut: modules never call into the registry, so the lockout exposes a fact and the registry decides the issue.

This also answers the honest limitation that the lockout only *slows* guessing (about 5 attempts per 15-minute ceiling per IP, so a determined attacker with days can still grind a weak password). Online guessing cannot be made impossible without a permanent lockout, which is itself a DoS on the owner. The layered answer is: slow it (this change), force a non-default password (the prior change), and make it visible so the owner can react (this decision).

### D2. One middleware, gating before and recording after

`AuthLockoutMiddleware::run` consults the table; if the source is locked it sends `429` with `Retry-After` and does not call `next()`. Otherwise it calls `next()` and then inspects the response code, recording a failure or a success.

Placing it *before* `digestAuth` is what saves the MD5 computation for a locked-out source, and inspecting the response *after* `next()` is what lets a single middleware see the auth outcome. Two middlewares (one before, one after) would work identically but would double the per-request `std::function` cost the chain already imposes.

*Alternative considered:* wrapping `AsyncAuthenticationMiddleware` in a subclass. Rejected - `allowed()` is not virtual in a useful place, and subclassing a library type couples this to its internals more tightly than reading a status code does.

### D3. Only a 401 with an `Authorization` header counts

This is the decision the whole feature turns on. `request->hasHeader("Authorization")` distinguishes:

| Request | Response | Counts? |
|---|---|---|
| No credentials (handshake leg 1, or a scanner) | 401 challenge | **No** |
| Credentials, wrong | 401 challenge | **Yes** |
| Credentials, right | 2xx/3xx/403 | Clears the source |

Counting credential-less 401s would lock out every normal user, because that is how digest works. Not counting them costs nothing defensively: to *test* a password an attacker must send one, so the guessing path is exactly the path that counts. Unauthenticated flooding is still bounded by `rateLimit`, which now runs.

A `403` from the default-password guard means authentication succeeded, so it clears the source rather than counting against it.

### D4. Escalating, self-expiring, bounded

`AUTH_LOCKOUT_MAX_FAILURES` consecutive failures lock the source for `AUTH_LOCKOUT_BASE_SECONDS`, doubling on each subsequent lockout up to `AUTH_LOCKOUT_MAX_SECONDS`. Any success clears the entry entirely.

Escalation is what makes sustained guessing unproductive without making a typo expensive: the first lockout is short enough to be an annoyance, the tenth is long enough to be useless. The ceiling exists so that no source is ever permanently barred - a locked-out owner must always be able to wait it out, because the alternatives (reboot, physical button) are exactly what someone locked out of their own meter cannot conveniently reach.

### D5. Fixed table, RAM only, no persistence

`AUTH_LOCKOUT_TABLE_SIZE` entries in `.bss`, linear scan, oldest-activity eviction when full. No heap and no growth under a many-source attack.

Deliberately not persisted across reboot. Persisting would mean an NVS write on the failed-login path - the exact hot path an attacker controls - which turns a brute-force attempt into a flash-wear attack. A reboot clearing the table is an accepted weakness: an attacker who can reboot the device at will has better options than guessing passwords.

Eviction under a many-source flood means an attacker can push a real lockout out of the table. That is a real limitation, mitigated only by `rateLimit` running now; the table is a defence against guessing, not against flooding.

### D6. The state machine is pure and tested; the firmware calls it

`source/lib/auth_lockout/` holds the table and its transitions with no Arduino dependency, tested natively. `customserver.cpp` supplies the clock and the source address. Same reasoning as `web_auth_gate`: the rule that decides whether someone is locked out is exactly the rule worth testing exhaustively, and it must not exist in two places.

Addresses are compared as `uint32_t`, so the library needs no networking types.

### D7a. Upload routes must gate on the lockout before authenticating

Upload and body callbacks run *during* body parsing (`WebRequest.cpp:612/783`), before `_runMiddlewareChain` (`:233`). The OTA and restore routes therefore do their own authentication inside the handler (`_rejectUploadIfNotPermitted`) - they have to, because a middleware auth check arrives after `Update.begin()` has already erased the partition. But that in-handler check runs *ahead of* this lockout middleware.

Adversarial testing found the consequence: without an explicit gate, an attacker moves password guessing to the upload routes, where every attempt runs a full digest check regardless of lock state, and a correct guess reaches `Update.begin()` before the middleware's `429` can mask it. The lockout would be masking the response code while doing nothing to actually throttle the guessing.

So `_rejectUploadIfNotPermitted` calls `authLockout.isSourceLocked()` first, before reading the password or authenticating, and refuses a locked source with `429` there. The middleware still records the failure afterwards from the `401` the handler leaves set, so recording is not duplicated. `isSourceLocked` is a pure query with no side effect - unit-tested as such, because a defence that mutated state merely by being checked would be a footgun for exactly this kind of early call.

### D7. Loopback is never locked out

`_performHealthCheck` probes `/api/v1/health` from `127.0.0.1`. That route skips the whole chain, so the lockout cannot reach it - but the exemption is stated in the pure logic anyway, because the consequence of getting it wrong is the restart-loop-into-rollback failure documented in the previous change, and one structural guarantee is not enough for that.

## Risks / Trade-offs

- **Locking out the legitimate owner.** -> Only credentialed failures count, a success clears instantly, lockouts expire on their own, and the first one is short. The failure mode is a wait, never a lockout requiring physical recovery.
- **Shared source IP: one address, many users.** -> Does not arise for the threat this defends against. A brute-force attacker against a local-first meter is a host *on the LAN*, and every LAN host has its own distinct address - there is no NAT between it and the device. CGNAT and reverse proxies live on the internet-egress path, not on the local segment, so they do not collapse a local attacker's identity. The lockout keys on `client->remoteIP()` - the socket peer as the device sees it - and deliberately ignores `X-Forwarded-For`/`X-Real-IP` (an attacker sets those freely; adversarial testing confirmed spoofing them collapses onto the one real socket IP and cannot forge table entries). The only way distinct users end up sharing one lockout entry is a deliberately non-default deployment: the device placed *behind* a reverse proxy on the LAN, or exposed to the internet through one. In that case a trusted proxy would need to inject a forwarded-for header the device consults *only* from that proxy, never from a raw client. For the intended deployment this is a non-issue; it is noted only so the assumption is explicit.
- **Table exhaustion under a distributed flood.** -> Documented in D5. `rateLimit` is the backstop; the table is not a flood defence.
- **`getResponse()` returning null after `next()`.** -> Treated as "no outcome observed", recording neither failure nor success, so an unusual path cannot accidentally lock anyone out.
- **Clock source.** -> `millis64()` as used elsewhere in this file; monotonic and unaffected by NTP steps, which matters because a wall-clock jump could otherwise extend or void a lockout.
- **The reorder changes existing behaviour**: a client over budget now sees `429` where it previously saw `401`. Intended, and the substance of #197's first point.

## Migration Plan

None. No persisted state, no API shape change, no configuration. Devices gain the behaviour on update and lose it on rollback; nothing is written that an older firmware would have to understand.

## Open Questions

None. The threshold, base duration and ceiling are tuning values recorded as constants in the header, chosen to make a typo cheap and sustained guessing useless; they can be changed without touching the specs or the task breakdown.
