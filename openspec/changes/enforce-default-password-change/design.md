## Context

See `proposal.md` - Why. The constraints that shape the approach, all of them pre-existing:

- **The middleware chain is server-global and synchronous.** `_setupMiddleware()` (`customserver.cpp:206-253`) registers `customMiddleware` -> `digestAuth` -> `rateLimit`. `AsyncMiddlewareChain::_runChain` (`Middleware.cpp:56-71`) captures `next` and the list iterator *by reference* into a stack lambda, so a middleware short-circuits by simply not calling `next()`, and must never store `next` for later.
- **`skipServerMiddlewares()` replaces the whole server chain, not part of it** (`WebRequest.cpp:877-891`). The 22 provisioning twins and the health endpoint already use it, so anything added to the server chain is structurally absent from them. That is a feature here, not a gap to close.
- **The request path is the AsyncTCP task.** `_isProvisioningOrigin` (`customserver.cpp:276-287`) carries a long comment establishing the rule for this position: state read plus compare, no mutex, no NVS, no logging, no allocation. Anything evaluated per-request must obey it.
- **`usingDefaultPassword` is computed today by reading NVS and running `strcmp`** (`customserver.cpp:1010-1015`). Acceptable once per status call; not acceptable once per request.
- **`/api/v1/health` is load-bearing.** `_performHealthCheck()` self-probes it from `127.0.0.1` (`customserver.cpp:597`); five failures call `setRestartSystem()`, and the comment at `:566-573` documents where that ends - restarts at ~150 s uptime never arm safe mode, never clear the reset counter, and eventually hit `MAX_RESET_COUNT`, rolling back firmware and wiping user NVS.
- **`source/lib/wifi_provisioning/isAuthBypassAllowed()` is dead code.** It states the carve-out rule, is unit-tested, and is called by nothing; `customserver.cpp` reimplements the rule inline and the two have already diverged on `GRACE` (the library says no bypass, the firmware allows it). This is the failure mode to avoid, not the pattern to copy.

## Goals / Non-Goals

**Goals:**

- Enforcement that holds against a client that ignores everything the device renders.
- Zero added cost on the request path of a device that is not locked down.
- The allowlist expressed once, in host-testable code, and called by the firmware rather than restated in it.
- No new failure mode that can restart or roll back a device.

**Non-Goals:**

- Hashing the stored password. It stays plaintext in `auth_ns`; that is a separate change with its own migration.
- Lockout or backoff on repeated failed digest attempts.
- Per-user accounts, sessions, or anything replacing HTTP digest.
- Gating the non-HTTP local integrations (MQTT, Modbus TCP, InfluxDB). They have no per-user authentication to gate, and `wifi-provisioning` already constrains their exposure.
- Resolving Bench-4 from the provisioning design (whether a LAN host static-routed to the AP address defeats `client()->localIP()`). Pre-existing and tracked there; this change neither helps nor worsens it.

## Decisions

### D1. A server-level middleware, not twin routes

Twin routes are the right tool when two handlers serve different bodies for the same path, which is why `/` uses them. They are the wrong tool for a blanket deny across ~75 API routes: it would mean 75 extra registrations, and every future route would silently opt out of enforcement by being written the ordinary way.

A fourth `AsyncMiddleware` covers every route registered through the server chain, including routes that do not exist yet and the library's catch-all 404 handler. Default-deny with an allowlist, rather than default-allow with a blocklist.

*Alternative considered:* the library's built-in `AsyncAuthorizationMiddleware` (`ESPAsyncWebServer.h:1143-1155`), which is exactly "short-circuit with 403 if a predicate fails". Rejected because it can only send a bare status code - `request->send(_code)` with no body and no headers - and this change needs a JSON body carrying a machine-readable reason, plus a `302` for page routes. The rate limiter (`Middleware.cpp:291-300`) is the pattern to follow instead.

### D2. Chain position: last, after `digestAuth`

Order becomes `customMiddleware` -> `digestAuth` -> `rateLimit` -> `defaultPasswordGuard`.

After `digestAuth` is required, not stylistic. A caller who does not authenticate must receive the ordinary `401` challenge and learn nothing; only a caller who *has* authenticated with the default password is told, via `403`, why the device will not serve them. Putting the guard first would advertise the device's password state to any unauthenticated scanner on the LAN.

After `rateLimit` keeps refused requests counting against the limiter, so the lockdown cannot be used as a cheap way to bypass rate limiting. `customMiddleware` still books a `403` into `statistics.webServerRequestsError`, which is correct - these are refusals worth seeing in the statistics.

### D3. A cached `volatile bool`, refreshed only when the password changes

`static volatile bool _usingDefaultPassword = true;` refreshed by a single `_refreshDefaultPasswordFlag()` that reads NVS and compares. Called from exactly two places: `_setupMiddleware()` at boot, and `updateAuthPasswordWithOneFromPreferences()` (`customserver.cpp:181-194`), which is already the one funnel through which a password change reaches the live `digestAuth` object - called by the change-password handler, the reset-password handler, and the physical button (`buttonhandler.cpp:260`).

Reading NVS per request would be a flash read on the AsyncTCP task for every request to every path, which is precisely what `_isProvisioningOrigin`'s comment forbids. `volatile bool` matches how `getProvisioningState()` and `isApAddress()` already publish state across tasks; no mutex, because a single aligned bool written by one task and read by another needs none, and a one-request-stale read is harmless in both directions (the guard re-evaluates on the next request).

`GET /api/v1/auth/status` reads the same cached flag rather than recomputing, so the endpoint and the enforcement can never disagree - today they would be two separate `strcmp`s.

*Fail-closed:* if NVS cannot be read the flag stays `true`, matching what `/api/v1/auth/status` already does (`bool isDefault = true;` before the read at `customserver.cpp:1010`). A device that cannot prove its password was changed is treated as not having changed it. This does not strand anyone: `_setupMiddleware()` already self-heals a missing password by writing the default back (`:231`), so the next boot reads cleanly and the state is truthful rather than merely conservative.

### D4. The allowlist lives in `source/lib/web_auth_gate/`, and the firmware calls it

```cpp
namespace WebAuthGate {
    enum class Action : uint8_t { ALLOW, REDIRECT_TO_ROOT, DENY };
    Action evaluate(bool usingDefaultPassword, const char *url);
}
```

Pure, no Arduino headers, host-compilable, unit-tested under `source/test/test_web_auth_gate/`. The middleware is then a five-line adapter: read the flag, call `evaluate`, and either `next()`, `send(302)`, or `send(403)`.

The point is that the middleware **calls** this rather than restating it. The `isAuthBypassAllowed()` precedent shows what happens otherwise: a tested library rule and an untested inline rule that quietly drift apart. A task below explicitly verifies there is no second copy of the allowlist in `customserver.cpp`.

Path-only, no method. `/api/v1/auth/change-password` is an `AsyncCallbackJsonWebHandler`, which matches `GET|POST|PUT|PATCH` at the routing layer and enforces `POST` inside the callback via `_validateRequest`; duplicating method logic in the gate would add a second place to get it wrong for no gain.

The rule, in order:

| URL | Action | Why |
|---|---|---|
| `/api/v1/health` | ALLOW | Defence in depth; it already skips the chain, so the guard never actually sees it |
| `/api/v1/auth/status` | ALLOW | The gate page needs to know when to let go |
| `/api/v1/auth/change-password` | ALLOW | The only way out |
| `/` | ALLOW | Serves the gate page (D5) |
| `/css/…`, `/js/…`, `/favicon.svg` | ALLOW | A gate that cannot render cannot be passed |
| anything else under `/api/` | DENY (403) | Default-deny, including routes not yet written |
| anything else | REDIRECT_TO_ROOT (302) | Page routes and unknown paths |

Note what is deliberately *not* allowlisted: `/api/v1/auth/reset-password` (D8), `/api/v1/system/factory-reset`, `/api/v1/ota/upload`, and the `ENV_DEV` NVS debug surface. Factory reset over the API is refused; the physical button (`buttonhandler.cpp:250-273`) remains the recovery path and is unaffected by anything here.

### D5. The gate is a page served by routing, not an overlay injected by script

`/` gets a third registration, ordered between the two that exist:

```cpp
server.on("/", HTTP_GET, serve wifi_setup_html)      // provisioning twin, skips chain
server.on("/", HTTP_GET, serve password_setup_html)  // NEW, .setFilter(_isDefaultPasswordActive)
server.on("/", HTTP_GET, serve index_html)           // dashboard
```

First matching filter wins by insertion order (`WebServer.cpp:145-154`). The gate twin deliberately does **not** call `skipServerMiddlewares()` - the user must still authenticate to see the gate, and the guard allows `/` anyway.

Every other page route is redirected by the middleware rather than by touching its registration, so `_serveStaticContent()` is left alone and a page added later is gated automatically.

`source/html/password-setup.html` is self-contained - inline `<style>`, inline script, no shared JS - following `wifi-setup.html`. Not because of an offline constraint (this page always runs on an authenticated LAN session) but because a gate with fewer moving parts is a gate with fewer ways to fail to render.

*Alternative considered:* a self-installing `js/auth-gate.js` modelled on `issues.js`, which all 11 authenticated pages already load. Cheaper - no new embedded file, no `platformio.ini`/`binaries.h`/route churn. Rejected as the primary mechanism: it is defeatable from devtools, and every page behind it would still be fetching data that returns 403, so the pages would render broken underneath the overlay. The API block would still hold, but the UI would be visibly wrong.

### D6. The gate page asks for the new password only

Two fields, new and confirm. It submits `currentPassword` as the default constant.

This is sound rather than a shortcut: the flag is *defined* as "stored password equals the default", so whenever the gate is being served, the stored password is the default. The user also necessarily supplied it at the digest prompt to get this far. The one divergence - NVS unreadable, flag conservatively `true` while the real password is something else - makes `change-password` return `500` from its own NVS read (`customserver.cpp:1043-1048`) before the comparison is ever reached, so a "current password" field would not have helped.

Hard-coding the default in the page leaks nothing: it is a `#define` in a public repository, printed in the manual, and identical on every shipped device.

### D7. Rejecting the default as a new password belongs in the handler, not in `_validatePasswordStrength()`

`resetWebPassword()` (`customserver.cpp:199`) sets the password *to* the default through `_setWebPassword()`, and must keep working - it backs the physical button. So `_validatePasswordStrength()` keeps its length-only contract, `MIN_PASSWORD_LENGTH` moves 4 -> 8 (`customserver.h:59`), and the "not the default" check goes in the change-password callback alongside the existing validations.

Length is validated on set, never on authenticate, so a device already holding a 4-character password keeps working and only a *new* password must clear 8.

### D8. `reset-password` is refused while the default is already active

It is the one route whose only effect from inside the locked state is to preserve it. Leaving it allowlisted would be an API that exists solely to be a no-op; leaving it deniable by the generic rule is enough, so it simply is not on the allowlist. From a *non*-default state it keeps working unchanged and now drops the device straight into lockdown, which is the honest outcome.

### D9. Provisioning wins - structurally in `UNPROVISIONED`, explicitly everywhere else

**Corrected after hardware review; the first version of this decision was wrong and shipped a regression.**

The original claim was that the provisioning twins call `skipServerMiddlewares()`, so the guard never runs on them, and precedence therefore falls out of `WebRequest.cpp:877-891` for free. That holds only in `UNPROVISIONED`. `_isProvisioningOrigin` returns false in every other state, so in `GRACE` and `AP_ASSIST` the provisioning routes fall through to their *authenticated* twins, which run the full server chain - and the guard saw them:

| State | `/` on the AP | `/api/v1/network/wifi/credentials` |
|---|---|---|
| `UNPROVISIONED` | wifi-setup, chain skipped | allowed, chain skipped |
| `GRACE` | wifi-setup, chain skipped | **403** |
| `AP_ASSIST` | **the password gate** | **403** |

The consequence is a stranded device: a meter on the default password whose router changed raises its assist AP, serves the setup page, and then refuses the one endpoint that AP exists to offer. The physical button cannot recover it - `resetWebPassword()` sets the password *to* the default, deepening the lockdown rather than lifting it.

So precedence is now stated rather than assumed. `evaluate()` takes an `isApOrigin` flag, supplied by `_isApOrigin()` - the same volatile-load-plus-address-compare as the provisioning filters, but with no state condition, because the widening authorises nothing on its own. When set, the allowlist widens to `/wifi-setup.html` and the four `/api/v1/network/wifi/*` routes.

The exposure that buys is bounded: an AP-origin request already implies physical proximity and the AP's own PSK, which is a strictly higher bar than the published web password this lockdown exists because of. OTA is deliberately *not* widened, so `wifi-provisioning`'s "Firmware update always requires authentication" continues to hold on the SoftAP in every state.

`/` still serves the gate in `AP_ASSIST` - the twin ordering is unchanged - so the gate page carries a link to `/wifi-setup.html`. Without it, a user who came in over the AP to fix their network lands on a password form that does not address their problem.

The structural exemption remains real and load-bearing for `/api/v1/health` and for the `UNPROVISIONED` twins, and still needs a test that would fail if someone later "tidied" either back onto the server chain.

### D10. Client changes are recovery, not enforcement

`js/api-client.js` gains a `403`-with-`reason: "default_password"` branch that navigates to `/`. Its only job is the tab that was already open when the physical button reset the password; without it that tab fills with failed fetches. Enforcement does not depend on it.

The same edit fixes the blanket `401` handler at `api-client.js:44-71`, which discards the server's response body and reports "Authentication required - please refresh the page". Change-password returns `401` for a wrong current password with a real message, so today that message is unreachable. This is in scope because the gate page's sibling form in `configuration.html` depends on it.

## Risks / Trade-offs

- **A bug in the guard bricks the web UI on a device that is not on the default password.** -> The flag is the single input; it is `false` for every device in the field that has ever changed its password, and `evaluate()` returns `ALLOW` unconditionally when it is `false`. That path is the first unit test. Recovery if it happens anyway is OTA - which is refused only when the flag is `true`.
- **Getting the health endpoint wrong restarts the device in a loop and can roll back firmware and wipe NVS.** -> It is structurally exempt (`skipServerMiddlewares()`, no filter), allowlisted anyway as defence in depth, and the e2e plan holds a device locked down for longer than 5 x 30 s while watching `uptimeSeconds` and `resetCount` for continuity.
- **Allowlisting `/css/` and `/js/` by prefix opens every asset, not just the gate's.** -> They are compile-time-embedded static text served from flash, identical on every device, and already served without authentication during provisioning. There is nothing behind that prefix worth protecting.
- **Redirecting unknown paths to `/` instead of 404ing changes an observable behaviour** while locked down. -> Only while locked down, and a locked-down device has one thing to say. Accepted rather than enumerating the page routes, which would need editing every time a page is added.
- **`volatile bool` is not `std::atomic`.** -> A single aligned byte, written by the API/button task and read by AsyncTCP, on a platform where that write is atomic. A stale read costs one request in either direction and self-corrects. Matches the existing `getProvisioningState()` / `isApAddress()` precedent rather than introducing a second convention.
- **A user who forgets a password they just set is now more stuck**, because `factory-reset` over the API is refused and the UI is gated. -> The physical button resets the password without touching anything else, and is documented in `manual/`. This is the same recovery path as before.
- **The allowlist could be restated inline during a later refactor and drift**, exactly as `isAuthBypassAllowed()` did. -> A task verifies the single call site, and the comment at the call site says why.

## Migration Plan

No data migration; nothing persisted changes shape. `MIN_PASSWORD_LENGTH` is validated on set, so no stored password is invalidated.

Every device already in the field that has changed its password sees no behavioural change at all. A device still on `energyme` is locked out of the API on first boot after the update and must be taken through the gate page - which is the entire point, and is the breaking change called out in the proposal.

Rollback is an ordinary OTA to the previous firmware, and is available to any device that is not locked down. A device that *is* locked down cannot OTA out, by design; its exit is the gate page or the physical button.

## Open Questions

None. The two decisions that would have changed the specs or the task breakdown - whether HTML routes are gated as well as the API, and which side of the 4-vs-8 minimum-length mismatch wins - were settled before this document was written and are recorded as D4/D5 and D7.
