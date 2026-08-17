## Purpose

Defines the shared "device is restarting" wait experience used by every web UI action that reboots the device: how it detects the device is actually back, what it shows while waiting, and which actions must use it instead of a hardcoded delay.

## ADDED Requirements

### Requirement: Device availability is detected by polling, not a fixed delay
After a reboot-triggering request succeeds, the web UI SHALL detect the device coming back by polling `GET /api/v1/health` rather than waiting a fixed, hardcoded duration before redirecting or reloading.

#### Scenario: Device takes longer than any previously hardcoded delay
- **WHEN** the device takes 25 seconds to come back online after a restart
- **THEN** the wait screen remains visible and polling until health succeeds, and does not redirect early based on an elapsed-time guess

#### Scenario: Device comes back faster than expected
- **WHEN** the device comes back online in 3 seconds
- **THEN** the wait screen detects this via a successful poll and proceeds to redirect within one poll interval, without waiting out any fixed minimum

### Requirement: A poll success only counts after at least one poll has failed
The web UI SHALL NOT treat a successful `/api/v1/health` response as "device back online" unless at least one poll attempt since the reboot was triggered has already failed (timed out or errored). This guards against a health check landing before the device has actually dropped off the network.

#### Scenario: First poll after triggering the action still succeeds
- **WHEN** the restart was just triggered and the device has not yet dropped off the network, so the first health poll still succeeds
- **THEN** the wait screen does not redirect yet, and keeps polling until a poll fails and a subsequent poll succeeds

#### Scenario: Device drops immediately
- **WHEN** the device drops off the network before the first poll completes
- **THEN** the first failed poll satisfies the "seen unreachable" condition, and the next successful poll is treated as back online

### Requirement: Waiting has a capped duration with a manual fallback
The web UI SHALL stop polling automatically after a fixed maximum wait and present a manual option (at minimum, a way to retry or navigate home) instead of polling indefinitely.

#### Scenario: Device never comes back within the cap
- **WHEN** the device does not respond to health polls within 90 seconds of the reboot being triggered
- **THEN** the wait screen stops auto-polling and shows a manual fallback instead of spinning forever

### Requirement: The wait screen only claims states that are actually observable
The wait screen SHALL only display status text for states the polling can actually distinguish: the triggering request was accepted, the device is unreachable/waiting, and the device is back online. It SHALL NOT display an intermediate step (e.g. "applying configuration", "verifying firmware") that the web UI has no way to observe once the device has dropped off the network.

#### Scenario: Generic reboot action
- **WHEN** a plain restart is triggered
- **THEN** the wait screen shows only these state transitions: command accepted, waiting/unreachable, back online

### Requirement: The underlying page is visible but inert while waiting
The wait screen SHALL be presented as a dimmed overlay above the current page: the page content stays visible behind it, but is not interactive until the wait screen closes.

#### Scenario: User tries to interact with the page during the wait
- **WHEN** the wait screen is active and the user clicks an element on the underlying page
- **THEN** the click has no effect on the underlying page

### Requirement: A rotating trivia strip fills the waiting time
While waiting, the wait screen SHALL display one fact at a time from a shared fact list, advancing automatically on a fixed interval, with manual previous/next controls that immediately show a different fact and reset the automatic advance timer.

#### Scenario: Left idle
- **WHEN** the user does not interact with the trivia controls
- **THEN** the displayed fact changes automatically at a regular interval for as long as the wait continues

#### Scenario: Manual navigation
- **WHEN** the user clicks the next or previous control
- **THEN** the displayed fact changes immediately to reflect the chosen direction, and the automatic advance timer restarts from that point

### Requirement: The fact list is shared with the OTA upload progress screen
The trivia fact list used by the wait screen SHALL be the same list used by the firmware update page's upload-progress screen (`update.html`), maintained in one place rather than duplicated.

#### Scenario: A fact is added to the shared list
- **WHEN** a new fact is added to the shared list
- **THEN** both the reboot wait screen and the OTA upload progress screen can display it

### Requirement: Redirect target is configurable per action
The wait screen SHALL redirect to `/` by default once the device is back online, and SHALL accept a caller-specified target URL for actions where the device may become reachable at a different address (e.g. applying a static IP configuration).

#### Scenario: Plain restart redirects home
- **WHEN** a plain restart completes and the device is detected back online
- **THEN** the browser is redirected to `/`

#### Scenario: Static IP change redirects to the new address
- **WHEN** a network configuration change that alters the device's IP address completes
- **THEN** the wait screen polls and redirects to the newly configured address, not the address the page was loaded from

### Requirement: Every reboot-triggering web UI action uses the shared wait screen
The following actions SHALL use this wait screen instead of a page-specific hardcoded delay: plain device restart, network configuration apply, WiFi credential switch, OTA firmware update (after the upload completes), firmware rollback, and configuration restore.

#### Scenario: OTA update hands off after upload completes
- **WHEN** a firmware upload finishes and the device begins its restart
- **THEN** the upload's byte-progress ring is replaced by the shared wait screen for the remainder of the wait

### Requirement: Factory reset does not use the shared wait screen
Because factory reset erases the device's stored WiFi credentials, the device leaves the current network and becomes reachable only via its own SoftAP at an address the browser cannot know in advance. The web UI SHALL NOT poll or redirect after triggering a factory reset; it SHALL continue to show manual reconnection instructions instead.

#### Scenario: Factory reset triggered
- **WHEN** a factory reset is triggered and completes
- **THEN** the web UI shows reconnection instructions and does not attempt to poll `/api/v1/health` or redirect

### Requirement: Filesystem restore does not use the shared wait screen
Restoring a filesystem backup extracts directly into the live LittleFS and responds without restarting the device, unlike configuration restore which does trigger a restart. The web UI SHALL NOT poll or redirect after a filesystem restore, since the device never reboots.

#### Scenario: Filesystem restore triggered
- **WHEN** a filesystem restore completes
- **THEN** the web UI does not invoke the shared wait screen and does not poll `/api/v1/health`
