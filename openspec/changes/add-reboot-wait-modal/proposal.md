## Why

`configuration.html` and `update.html` each guess how long a restart takes and hardcode a `setTimeout` before reloading or redirecting (5s, 10s, 12s, 15s, 30s, all different, all wrong some of the time) - or, for `configuration.html`'s WiFi credential switch, don't even redirect, just tell the user to wait and leave the button disabled forever. Each page also has its own inline copy for the wait, so the experience drifts between pages for the same underlying event: the device dropped off the network and hasn't come back yet. We already expose an unauthenticated `/api/v1/health` endpoint that can be polled to know for real when the device is back, so there's no reason to keep guessing.

## What Changes

- New shared dimmed-modal component (spinner, one status line, an elapsed timer, and a rotating "did you know" fact strip with manual prev/next) that any page can invoke after triggering a reboot.
- Detection polls `/api/v1/health` instead of a fixed delay. Only three states are ever really observable (command sent, unreachable, reachable again), so the copy doesn't claim more than that. A poll success is only treated as "back online" after at least one failed poll first, to avoid a false-positive redirect from a health check that lands before the device actually drops.
- Total wait is capped with a manual fallback ("still not back, refresh" style) instead of polling forever.
- `update.html`'s OTA upload screen keeps its own real byte-progress ring (it has genuine data `/api/v1/ota/status` reports); once the upload finishes and the restart fires, it hands off to this same modal for the reboot half instead of its own `setTimeout`-based redirect.
- The "did you know" facts move out of `update.html`'s local array into one shared list both surfaces pull from.
- Every reboot-triggering action in the web UI switches to this component in place of its own `setTimeout`/`showStatus` combo: plain restart, network config apply, WiFi credential switch, OTA update, firmware rollback, configuration restore, filesystem restore.
- **Excluded on purpose**: factory reset. It erases the stored WiFi credentials, so the device leaves the current network entirely and comes back only on its own SoftAP at an address the browser has no way to guess. Polling the old address would never succeed, so `configuration.html`'s existing "here's how to reconnect" instructions stay as they are.
- **Also out of scope**: `wifi-setup.html`'s own unauthenticated provisioning flow (the initial SoftAP setup a factory-fresh or freshly-reset device goes through). It already has bespoke polling suited to that specific transition (the device leaves AP mode entirely, so there's no "session" to keep alive in the way this component assumes) and isn't a hardcoded-delay problem. The WiFi credential switch this change targets is the *authenticated* one in `configuration.html`, for a device that's already provisioned and simply moving to a different network.

## Capabilities

### New Capabilities
- `reboot-wait-ui`: the shared "device is restarting" wait experience used by every web UI action that triggers a reboot: how it detects the device is back (poll-based, not timer-based), what it shows while waiting, and which actions are required to use it instead of a hardcoded delay.

### Modified Capabilities

(none - this changes web UI behavior only; `firmware-rollback` and other backend capabilities are unaffected, no new or changed REST endpoints)

## Impact

- New: `source/js/reboot-wait.js` (poll/detect/redirect logic + trivia carousel), plus supporting CSS.
- `source/html/configuration.html`: plain restart, network-config-apply, and WiFi-credential-switch flows (the first two need a configurable redirect target since a static-IP or network change can move the device to a different address; WiFi switch has no known target, so it best-effort polls `energyme.local` the same way network-config-apply's DHCP branch does).
- `source/html/update.html`: OTA update, firmware rollback, configuration restore, filesystem restore flows; existing upload-progress ring stays, hands off to the new modal for the reboot half.
- No firmware or REST API changes; reuses the existing unauthenticated `/api/v1/health` endpoint as-is.
