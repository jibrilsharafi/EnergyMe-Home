## 1. The allowlist as pure, tested logic

- [x] 1.1 Create `source/lib/web_auth_gate/web_auth_gate.h` and `.cpp` with `WebAuthGate::Action { ALLOW, REDIRECT_TO_ROOT, DENY }` and `Action evaluate(bool usingDefaultPassword, const char *url)`. No Arduino headers, no `String`, `char[]` and `strncmp`/`strcmp` only - it must compile for `native` (design D4).
- [x] 1.2 Implement the rule in the order given in design D4, with `usingDefaultPassword == false` returning `ALLOW` unconditionally as the first line.
- [x] 1.3 Write `source/test/test_web_auth_gate/test_web_auth_gate.cpp` covering: the not-locked-down short circuit for a route that would otherwise be denied; each of the five allowlisted paths; `/api/v1/auth/reset-password` and `/api/v1/system/factory-reset` and `/api/v1/ota/upload` denied; an unknown `/api/…` path denied (default-deny for routes not yet written); page routes and unknown non-API paths redirected; a null and an empty URL; a path that merely *starts* like an allowlisted one (`/api/v1/auth/status-extra`, `/cssx/a`) not being allowlisted by a sloppy prefix compare.
- [x] 1.4 Run `pio test -e native` from WSL and get it green before touching any firmware file.

## 2. Cached password state

- [x] 2.1 Add `static volatile bool _usingDefaultPassword = true;` and `_refreshDefaultPasswordFlag()` to `source/src/customserver.cpp` - reads NVS via `_getWebPasswordFromPreferences`, compares against `WEBSERVER_DEFAULT_PASSWORD`, leaves the flag `true` if the read fails (design D3, fail-closed).
- [x] 2.2 Call it from `_setupMiddleware()` after the boot-time password load, and from `updateAuthPasswordWithOneFromPreferences()` (`customserver.cpp:181-194`) so the change-password handler, the reset-password handler, and the physical button all refresh it through the one funnel that already exists.
- [x] 2.3 Rewrite `GET /api/v1/auth/status` (`customserver.cpp:1001-1020`) to report the cached flag instead of doing its own NVS read and `strcmp`, so the endpoint and the enforcement cannot disagree.

## 3. The guard middleware

- [x] 3.1 Add a `DefaultPasswordGuardMiddleware : public AsyncMiddleware` to `source/include/customserver.h` beside `CustomMiddleware`, whose `run()` reads the cached flag, calls `WebAuthGate::evaluate()`, and does exactly one of `next()`, a `302` to `/`, or a `403`. It must not call `next()` on the refusing paths, and must not retain `next` (design: `_runChain` captures by reference).
- [x] 3.2 Give the `403` a JSON body `{"success":false,"error":"...","reason":"default_password"}` following the rate limiter's short-circuit pattern (`Middleware.cpp:291-300`), not the bare-code `AsyncAuthorizationMiddleware`.
- [x] 3.3 Register it last in `_setupMiddleware()`, after `rateLimit` - after `digestAuth` so an unauthenticated caller gets the ordinary `401` and learns nothing about the device's password state (design D2).
- [x] 3.4 Verify by inspection that the allowlist appears in `customserver.cpp` exactly zero times - the middleware must call `evaluate()`, never restate it. This is the `isAuthBypassAllowed()` failure mode the design calls out.

## 4. Password rules

- [x] 4.1 Change `MIN_PASSWORD_LENGTH` from 4 to 8 in `source/include/customserver.h:59`.
- [x] 4.2 Reject `newPassword == WEBSERVER_DEFAULT_PASSWORD` in the change-password callback (`customserver.cpp:1022-1071`) with a `400` and a message that names the reason. Put it in the handler, not in `_validatePasswordStrength()`, so `resetWebPassword()` can still set the default (design D7).
- [x] 4.3 Confirm `resetWebPassword()` (`customserver.cpp:196-200`) still succeeds after 4.1 and 4.2 - the default is 8 characters, so the length change does not break it, but this is the assertion that would catch it if the default ever changed.

## 5. The gate page

- [x] 5.1 Write `source/html/password-setup.html`: self-contained, inline `<style>` and inline script, no shared JS (design D5). Two fields (new, confirm), client-side check for length >= 8 / match / not equal to the default, submitting `currentPassword` as the default constant (design D6).
- [x] 5.2 On load, fetch `/api/v1/auth/status` and `location.href = '/'` if `usingDefaultPassword` is false, so the page cannot be left stale after the change - mirroring `wifi-setup.html`'s entry-state probe.
- [x] 5.3 On success, tell the user plainly that the browser will ask them to sign in again with the new password, then navigate to `/`. The digest credentials the browser cached are stale the moment `updateAuthPasswordWithOneFromPreferences()` runs, and the re-prompt is otherwise alarming.
- [x] 5.4 Surface the server's error body on failure (400 for too short / reused default, 500 for an unreadable NVS), using the `.status-box` pattern from `wifi-setup.html` rather than a toast.
- [x] 5.5 Add the file to `board_build.embed_txtfiles` in `source/platformio.ini`, add the `extern` pair to `source/include/binaries.h`, and register the twin route on `/` between the provisioning twin and the dashboard twin, filtered on the cached flag and *without* `skipServerMiddlewares()` (design D5).

## 6. Client-side recovery

- [x] 6.1 In `source/js/api-client.js`, branch on `403` with `reason === "default_password"` and navigate to `/`. This is recovery for a tab left open across an out-of-band password reset, not enforcement (design D10).
- [x] 6.2 Fix the blanket `401` handler (`api-client.js:44-71`) so it surfaces the server's response body instead of replacing every 401 with "Authentication required - please refresh the page", which currently makes change-password's real "Current password is incorrect" unreachable.
- [x] 6.3 Remove `checkDefaultPassword()` and its call site from `source/html/index.html` (`:1078-1087`, `:1096`). The dashboard is no longer served in the state that function existed to warn about.

## 7. Documentation

- [x] 7.1 Update `source/resources/swagger.yaml`: the `403` failure mode and its `reason` field on the API surface, the `newPassword` constraints (min 8, not the default) on change-password, and the new refusal on reset-password.
- [x] 7.2 ~~Point `source/utils/_device_auth.py:17` at the firmware default.~~ Premise was wrong: its docstring says `energyme00` is deliberately the *bench provisioning* password, not the firmware default. Named `FIRMWARE_DEFAULT_PASSWORD` beside it and documented that a script reaching a device on `energyme` now gets 403.

## 8. Build and unit verification

- [x] 8.1 `pio test -e native` from WSL - green, including the new suite.
- [x] 8.2 `pio run -e esp32s3-dev` - compiles clean. Note any warning that is not pre-existing in untouched code.
- [x] 8.3 cppcheck clean on `lib/web_auth_gate/` (zero defects). Full `pio check -e esp32s3-dev` is not usable here: it drowns in pre-existing `preprocessorErrorDirective` errors from ArduinoJson's macros, project-wide and unrelated to this change.

## 9. Hardware end-to-end (bench device `em-home-bench-588c81c479f8`)

The device moved from 192.168.1.27 to **192.168.1.82** partway through, after a power cycle and a WiFi change. It is deliberately left **on the default password** so the gate can be exercised by hand.

One mid-test outage was investigated and cleared: the device restarted at 13:20 with reason "Software", but the log shows `Power reset detected - extended WiFi timeout (300 s)` followed by five `Health check failed` entries. `_performHealthCheck()` returns false when `isNetworkServiceable()` is false, so an unplugged device fails its own probe. Pre-existing behaviour, not the lockdown - `crashCount: 0`, `lastResetWasCrash: false`, and the firmware MD5 was unchanged (no rollback).

- [x] 9.1 OTA the dev build and confirm the device comes back with the API working normally - the not-locked-down path must be provably unaffected before anything else is tested.
- [x] 9.2 `POST /api/v1/auth/reset-password` with the current credentials. Expect `200`, and the device to enter lockdown immediately without a restart.
- [x] 9.3 `GET /api/v1/system/info` authenticated with `admin:energyme` -> `403` with `reason: "default_password"`. This is the hole from issue #214; it must now be closed.
- [x] 9.4 `GET /api/v1/health` with no credentials -> `200`.
- [x] 9.5 `GET /api/v1/auth/status` with `admin:energyme` -> `200`, `usingDefaultPassword: true`.
- [x] 9.6 `GET /api/v1/system/info` with a *wrong* password -> `401`, not `403`. The lockdown must not disclose the password state to an unauthenticated caller (design D2).
- [x] 9.7 `GET /` -> the gate page. `GET /configuration` -> `302` to `/`. `GET /css/styles.css` -> `200`.
- [x] 9.8 `POST /api/v1/ota/upload` and `POST /api/v1/system/factory-reset` with `admin:energyme` -> `403` both.
- [x] 9.9 `POST /api/v1/auth/reset-password` with `admin:energyme` -> `403` (design D8). Then `POST /api/v1/auth/change-password` with `newPassword: "energyme"` -> `400`, and with a 7-character password -> `400`.
- [x] 9.10 Leave the device locked down for at least 5 minutes - past `HEALTH_CHECK_MAX_FAILURES` x 30 s - then check `uptimeSeconds` is continuous and `resetCount` / `crashCount` are unchanged. A locked-down device must not restart itself, and the consequence of getting this wrong is a firmware rollback and an NVS wipe (design, Risks).
- [x] 9.11 Change-password round trip exercised exactly as the gate page posts it: locked -> `200` -> `usingDefaultPassword: false` -> previously-blocked `/api/v1/system/info` returns `200` -> old default returns `401` -> `/` serves the dashboard again. No restart. The gate page itself was verified served byte-identical to source, but **not walked in a browser**: Chrome's native digest-auth modal blocks the automation extension, so the visual pass is left to a human.
- [x] 9.12 **Found a regression here, fixed it.** The guard did reach provisioning in `GRACE` and `AP_ASSIST`, because `_isProvisioningOrigin` is `UNPROVISIONED`-only - see the corrected design D9. Fixed by widening the allowlist on AP-origin requests. Verified on hardware that the widening does not leak onto the LAN (wifi routes still `403` from the STA netif while locked down) and that `/wifi-setup.html` redirects to the gate from the LAN.
- [ ] 9.13 **Still outstanding**: exercise the real AP path on hardware - put the device into `AP_ASSIST` (or `UNPROVISIONED`), join its SoftAP, and confirm the setup page loads and credentials are accepted while the default password is active, and that OTA is still refused there. Needs a physical WiFi change, so it was not run.

## 10. Land it

- [x] 10.1 Committed in five chunks: the pure lib + tests; the enforcement (flag, middleware, gate page, route); the client-side recovery; the docs; the AP-origin fix. The enforcement chunk spans `customserver.{h,cpp}` + the embedded page because the route registration cannot compile without the page - splitting it further would have left a non-building tree.
- [ ] 10.2 Open the PR against `development` with `Closes #214`, a label, and no milestone. Record which e2e steps were run on hardware and which were not.
