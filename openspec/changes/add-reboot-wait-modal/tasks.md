## 1. Shared reboot-wait module

- [x] 1.1 Add `source/js/reboot-wait.js`: exposes a function that shows the dimmed modal, polls `/api/v1/health` at a fixed interval, requires one observed failure before treating a success as "back online", caps the wait with a manual fallback, and redirects to a caller-supplied target (default `/`) on success.
- [x] 1.2 Add the shared trivia fact list (moth bug, planets/Moon, China+USA electricity share, Egyptians/Romans, microwave standby, Watt/electron, octopus hearts, "read the code you're waiting on") and the carousel logic (auto-advance interval, manual prev/next that resets the timer, `prefers-reduced-motion` handling per the earlier mockup).
- [x] 1.3 Add supporting CSS for the dimmed scrim, modal box, spinner, status line, elapsed timer, and trivia strip (reuse the existing `button-spinner` keyframes from `forms.css` rather than duplicating them).
- [ ] 1.4 Manually verify the module in isolation against a real device: trigger `POST /api/v1/system/restart` via the browser console, confirm the modal appears, confirm it does not redirect on a health poll that succeeds before the device actually drops (the false-positive race described in design.md), confirm it redirects once the device is truly back.

## 2. Wire into configuration.html

- [x] 2.1 Replace the plain `restart()` function's `setTimeout`/`showStatus` pair with a call into the shared module (default redirect target).
- [x] 2.2 Replace the network-config-apply flow's `setTimeout` with a call into the shared module, passing the newly configured address as the redirect target.
- [x] 2.3 Confirm `factoryReset()` is left untouched (no polling, no redirect, existing reconnection instructions stay).

## 3. Wire into configuration.html's WiFi credential switch

- [x] 3.1 Replace `switchWifi()`'s "wait ~15 seconds, button stays disabled forever" ending with a call into the shared module, polling `energyme.local` as a best-effort target (corrected from the original task text, which named `wifi-setup.html`: the authenticated WiFi switch actually lives in `configuration.html`'s `switchWifi()`; `wifi-setup.html` only has the unauthenticated initial-provisioning flow, which is out of scope, see proposal.md).

## 4. Wire into update.html (non-OTA actions)

- [x] 4.1 Replace `doRollback()`'s post-success `setTimeout(() => window.location.reload(), 5000)` with a call into the shared module.
- [x] 4.2 Replace `restoreConfigSubmitted()`'s nested `setTimeout` pair with a call into the shared module.
- [x] 4.3 Filesystem restore (`restoreFilesystemSubmitted()`) does not currently redirect or restart the device. Confirmed against `customserver.cpp`: `/api/v1/restore/filesystem` extracts straight into the live LittleFS and responds success, unlike `/api/v1/restore/configuration` it never calls `setRestartSystem`. Left as-is, not wired in.

## 5. Wire into update.html (OTA update) and consolidate trivia

- [x] 5.1 Move `getRandomFact()`'s local `facts` array into the shared trivia list from task 1.2; have `getLoadingMessage()` read from the shared list instead of its own copy.
- [x] 5.2 After the OTA upload's `fetch('/api/v1/ota/upload', ...)` succeeds and the byte-progress ring completes, hand off to the shared reboot-wait module instead of the current `setTimeout(() => window.location.href = '/', 5000)`.
- [x] 5.3 Confirm the upload failure path (non-success response) is unaffected and still shows its existing error messaging without invoking the wait screen.

## 6. Final pass

- [ ] 6.1 Run `pio check -e esp32s3-dev` to confirm no regressions (this change is web-UI-only, but confirm no firmware files were accidentally touched).
- [ ] 6.2 Manually exercise every wired-in action against a real device (restart, static IP apply, WiFi switch, rollback, config restore, OTA update) and confirm consistent modal behavior and copy across all of them.
- [ ] 6.3 Code review + simplification pass per this repo's PR checklist before merging to `development`.
