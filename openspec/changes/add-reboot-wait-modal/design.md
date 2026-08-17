## Context

See proposal.md - Why. Relevant existing pieces:

- `GET /api/v1/health` (`customserver.cpp`) already exists, is unauthenticated (`.skipServerMiddlewares()`), and returns `{status, uptime, timestamp}`. No firmware change needed to use it as a poll target.
- `POST /api/v1/system/restart`, `POST /api/v1/ota/rollback`, `POST /api/v1/restore/configuration`, `POST /api/v1/restore/filesystem`, the WiFi-switch and static-IP-apply endpoints in `wifi-setup.html`/`configuration.html`, and the OTA upload flow in `update.html` are the seven call sites this replaces. Each currently disables its trigger button, shows a `showStatus()` toast, and fires a page-specific `setTimeout` before redirecting or reloading.
- `update.html` already has real progress data during the upload phase via `GET /api/v1/ota/status` (`Update.progress()/size()`), polled every `UPDATE_PROGRESS_INTERVAL` (2000ms). That polling loop and its progress ring are unaffected by this change; only what happens after the upload finishes and the device restarts is replaced.
- The mocked UI (dimmed modal, honest 3-state copy, trivia carousel with manual arrows) was agreed on directly with Jibril before this change was scaffolded; the visual/copy decisions below aren't re-litigated here.

## Goals / Non-Goals

**Goals:**
- One shared client-side module that owns reboot-wait detection and presentation, used by every reboot-triggering action.
- Detection that reflects reality (poll-based) instead of a guessed duration.
- A single shared trivia fact list, not two copies that drift.

**Non-Goals:**
- No firmware/backend changes. `/api/v1/health` is used as-is.
- No change to the OTA upload byte-progress screen itself, only what happens after it hands off.
- No coverage for factory reset (see spec: it leaves the network entirely, so polling the old address is meaningless).
- No i18n/localization of the wait copy or trivia facts; the existing pages are English-only and this doesn't change that.

## Decisions

**Poll `/api/v1/health` on a fixed interval, require one failure before a success counts.**
Alternative considered: trust the POST response alone and just wait a fixed delay (status quo). Rejected: that's the exact problem being fixed. Alternative: no "require a failure first" guard, just redirect on the first successful health poll. Rejected: for actions where the device applies a change without necessarily restarting the TCP/HTTP stack fast (or the poll happens to race the still-alive old connection), the very first poll can succeed before the device has actually gone down, which would redirect the user to a page that's about to become unreachable mid-load. Requiring an observed failure first is a cheap, testable guard against that race.

**Poll interval and total cap are UI constants, not configurable per call site.**
A single interval (short enough to feel responsive, long enough not to hammer the device: low single-digit seconds) and a single cap (90s, generous relative to every currently-hardcoded delay it replaces) apply everywhere. Per-action tuning would reintroduce the same "everyone guesses independently" problem this change removes. If a specific action genuinely needs different timing later, that's a deliberate follow-up, not a default.

**Redirect target is a parameter, not a second component.**
`configuration.html`'s static-IP apply flow is the one case where the device may answer at a different address after restart. Rather than a separate wait component for that one case, the shared module takes an optional target URL (defaults to `/`) and, when a target is given, polls health at that target address instead of the current origin.

**Shared trivia list lives in one place; both surfaces import it.**
`update.html` currently owns `getRandomFact()` with its own array. That array moves to the new shared module (or a small shared data file) and `update.html` calls into it instead of keeping its own copy. This is a one-time consolidation, not a new abstraction layer: one array, two readers.

**Modal is a dimmed overlay (page visible but inert behind it), not a full-page takeover or a non-blocking banner.**
Decided directly with Jibril after comparing four mocked treatments (full takeover, dimmed modal, sticky banner, toast-only). Dimmed modal was picked as the right weight for these actions: serious enough to demand attention, without fully replacing the page.

## Risks / Trade-offs

- [Polling adds request load during the wait, however brief] → Interval is a few seconds and the endpoint is trivial (`{status, uptime, timestamp}`, no auth, no JSON body parsing on input); negligible compared to the ESP32's normal request handling.
- [The "require one failure first" guard could itself misfire if the device is fast enough that the health server socket closes and reopens between two polls without a poll ever timing out] → Use a short per-poll timeout (not just relying on TCP refusal) so a slow/dropped connection counts as a failure even if the port technically stays closed only briefly; acceptable residual risk, bounded by the overall wait cap either way.
- [Static-IP redirect target could be wrong if the user made a typo in the new address] → Out of scope for this change; the wait screen polls whatever target it's given, same as `configuration.html` already assumes for its own redirect today.
- [Consolidating the trivia list touches `update.html`'s existing working upload flow] → Keep the change to that file minimal: swap the local array/function for calls into the shared list, don't restructure the surrounding upload logic.

## Migration Plan

No feature flag or staged rollout needed, this is a client-side-only swap with no backend contract change and no persisted state. Suggested order, each its own commit per the repo's one-concern-per-commit convention:
1. Add the shared `reboot-wait` module (detection + modal + trivia) and its CSS, unused by any page yet.
2. Wire it into `configuration.html`'s plain restart (simplest call site, no custom redirect target).
3. Wire it into `configuration.html`'s network-config-apply (introduces the redirect-target parameter).
4. Wire it into `wifi-setup.html`'s credential switch.
5. Wire it into `update.html`'s rollback, configuration-restore, and filesystem-restore flows.
6. Wire it into `update.html`'s OTA update flow (hand-off from the existing progress ring) and consolidate the trivia list.

Rollback is trivial at any step: each commit only touches one page's JS, reverting it restores that page's previous `setTimeout` behavior without affecting the others.
