## Why

Issue #26 ("Ensure on first login a password change is requested") was closed via #35, but the implementation is a client-side nudge and nothing more: `index.html:1078-1087` calls `GET /api/v1/auth/status` and raises a dismissible `alert()`. The server does not care. Anyone on the LAN who knows `energyme` - the default is published in this repository, in the manual, and on every device that ships - authenticates successfully and gets the entire API, including OTA firmware upload, factory reset, and the NVS-backed configuration surface. The nudge fires on `/` only, so navigating straight to `/configuration` never shows it at all, and `curl` never sees it.

The provisioning work merged in #219 built exactly the machinery this needs - a filter-and-twin route pattern, a documented `skipServerMiddlewares()` carve-out discipline, and a forced-flow setup page (`wifi-setup.html`) that the user cannot navigate around. Enforcing the password change is now a matter of reusing that pattern rather than inventing one.

## What Changes

- Add a fourth server-level middleware that refuses to serve a device still holding the default web password. While the default is active, every request that is not on a short allowlist is rejected: `/api/v1/*` with `403`, and every HTML page route with a `302` to `/`.
- Cache the "default password is active" state as a lock-free flag refreshed only when the password changes. The existing `usingDefaultPassword` computation reads NVS and does a `strcmp`; running that on the AsyncTCP task for every request to every path is not acceptable, and the provisioning filters (`_isProvisioningOrigin`) already establish the no-NVS/no-lock/no-allocation rule for this position in the request path.
- Put the allowlist decision in `source/lib/` as a pure function with native Unity tests, and have `customserver.cpp` call it. The provisioning change left `isAuthBypassAllowed()` in `source/lib/wifi_provisioning/` unused while `customserver.cpp` reimplemented the rule inline; the two have already diverged on `GRACE`. This change must not repeat that.
- Add `source/html/password-setup.html`, a self-contained forced-flow page modelled on `wifi-setup.html`, and twin-register `/` so it is served instead of the dashboard while the default password is active. The gate is enforced by routing, not by a DOM overlay.
- **BREAKING**: `POST /api/v1/auth/change-password` rejects a `newPassword` equal to the default. Nothing stops that today, so a user can "change" the password to `energyme` and remain in the default state.
- **BREAKING**: `MIN_PASSWORD_LENGTH` goes from 4 to 8, making the firmware the authority and matching what `configuration.html` has always promised. Existing shorter passwords keep authenticating - only setting a password is validated - but a new one must be at least 8 characters.
- **BREAKING**: `POST /api/v1/auth/reset-password` is refused while the default password is already active. It is the one route that *creates* the locked-down state, and calling it from inside that state is a no-op that only exists to confuse.
- Remove the `alert()` nudge from `index.html`; `/` no longer serves the dashboard when it would fire.
- Teach `js/api-client.js` to recognise the lockdown `403` and send the page to `/`, so a tab left open when the password is reset out-of-band (the physical button) lands on the gate instead of filling with failed fetches. Fix the blanket 401 handler in the same file, which currently swallows the server's real error body and reports "Authentication required - please refresh the page" for a wrong current password.

## Capabilities

### New Capabilities
- `web-authentication`: how the local web interface authenticates, where the web password lives, what the device refuses to do while the shipped default is still in place, and how the user is forced out of that state. Covers the digest-auth contract, the default-password lockdown and its allowlist, the password change/reset API, and the precedence between this lockdown and the provisioning carve-out.

### Modified Capabilities
- None. `wifi-provisioning` already states that provisioning routes are reachable from the AP without the web password while `UNPROVISIONED`, and that OTA always requires authentication. Both remain true: the lockdown is an additional constraint layered on authenticated requests, and the provisioning carve-out routes bypass the server middleware chain entirely, so they are unaffected. The precedence rule belongs in the new capability, not as an edit to the old one.

## Impact

**Code**
- `source/lib/web_auth_gate/` + `source/test/test_web_auth_gate/`: new pure allowlist function and its Unity tests. Host-compilable, no Arduino dependencies, following the `wifi_provisioning` precedent.
- `source/src/customserver.cpp`: new `AsyncMiddleware` subclass appended to the chain in `_setupMiddleware()` (`:206-253`); a cached flag refreshed from `updateAuthPasswordWithOneFromPreferences()` (`:181-194`) and at boot; twin registration of `/` (`:922-930`) for the gate page; the default-password rejection and the reset-password refusal in `_serveAuthEndpoints()` (`:994-1085`).
- `source/include/customserver.h`: `MIN_PASSWORD_LENGTH` 4 to 8 (`:59`); the new middleware class alongside `CustomMiddleware` (`:91-124`).
- `source/html/password-setup.html`: new. Self-contained, no shared JS - the same discipline as `wifi-setup.html`.
- `source/html/index.html`: remove `checkDefaultPassword()` and its call site (`:1078-1087`, `:1096`).
- `source/js/api-client.js`: 403 lockdown handling and the 401 error-body fix (`:44-71`).
- `source/platformio.ini` and `source/include/binaries.h`: embed declarations for the new page. Adding an embedded file needs all three of the embed list, the extern pair, and the route.
- `source/resources/swagger.yaml`: document the 403, the new `newPassword` constraint, and the length change.

**APIs**
- Every `/api/v1/*` route gains a `403` failure mode while the default password is active. The allowlist is `/api/v1/health`, `GET /api/v1/auth/status`, and `POST /api/v1/auth/change-password`.
- `/api/v1/health` is already registered with `skipServerMiddlewares()` and no filter (`customserver.cpp:979-991`), so it bypasses the new middleware structurally. This is load-bearing, not incidental: `_performHealthCheck()` self-probes it from `127.0.0.1`, and five consecutive failures call `setRestartSystem()` into a restart loop that never arms safe mode, never clears the reset counter, and eventually rolls the firmware back and wipes user NVS (`customserver.cpp:565-644`).

**Dependencies**
- None added or removed.

**Memory**
- One `volatile bool`, one middleware object, and one embedded HTML page in flash. No heap, no task, no PSRAM. The middleware allocates nothing on the request path.

**Security**
- Closes the actual hole: knowledge of a published constant no longer yields a working device. OTA upload, factory reset, filesystem restore, and the NVS debug surface all move behind "the password has been changed at least once".
- Deliberately unchanged and out of scope: the web password is still stored in plaintext in the `auth_ns` NVS namespace (`customserver.cpp:648-693`), and there is still no lockout or backoff on repeated failed digest attempts.
- Recovery from a forgotten password remains the physical button (`buttonhandler.cpp:250-273`), which is unaffected by the lockdown. `POST /api/v1/system/factory-reset` is *not* allowlisted, so it is refused while locked down.

**Related**
- Closes GitHub issue #214. Supersedes the client-side-only implementation delivered for #26 via #35.
