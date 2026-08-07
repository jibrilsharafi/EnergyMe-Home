## Purpose

Defines how the local web interface authenticates its user, and what the device refuses to do while the web password is still the value it shipped with. The default password is a published constant, so a device holding it is functionally unauthenticated; this capability makes the device say so and refuse to operate until the password is changed.

## ADDED Requirements

### Requirement: The device refuses to operate while the shipped default password is active

While the stored web password equals the shipped default, the device SHALL refuse every authenticated request except those on a fixed allowlist. Refusal SHALL NOT depend on the client rendering, executing, or honouring anything the device sends; a request made with a command-line HTTP client SHALL be refused exactly as a browser request is.

The allowlist SHALL be limited to what a user needs in order to leave the state: the health endpoint, reading the authentication status, changing the password, the gate page itself, and the static assets that gate page needs to render.

#### Scenario: API request with correct default credentials

- **WHEN** a client authenticates with the default password and requests any API route outside the allowlist
- **THEN** the device responds `403` with a machine-readable body identifying the default password as the reason, and the request's side effect does not occur

#### Scenario: Firmware upload with correct default credentials

- **WHEN** a client authenticates with the default password and posts firmware to the OTA endpoint
- **THEN** the upload is refused and no partition is written

#### Scenario: Command-line client

- **WHEN** the refused request is made by a client that never fetches the web interface
- **THEN** it is refused identically, because enforcement does not depend on the client

#### Scenario: Wrong password while the default is active

- **WHEN** a client supplies neither the default nor any other correct password
- **THEN** the device responds with the normal authentication challenge, not the lockdown refusal, so the lockdown does not disclose the device's password state to an unauthenticated caller

#### Scenario: Password has been changed

- **WHEN** the stored password differs from the shipped default
- **THEN** no request is refused on account of this requirement and the device behaves exactly as it did before this capability existed

### Requirement: The web interface forces the password change and cannot be navigated around

While the default password is active, the device SHALL serve a password-change page in place of the web interface, and SHALL redirect every other page route to it. The forcing SHALL be performed by the device's routing, not by client-side script, so that clearing an element or disabling JavaScript does not reveal the interface behind it.

The page SHALL be reachable and renderable under the lockdown, which requires its stylesheets and scripts to remain served.

#### Scenario: Opening the device's address

- **WHEN** a user opens the device root while the default password is active
- **THEN** the password-change page is served instead of the dashboard

#### Scenario: Navigating directly to an inner page

- **WHEN** a user requests any other page route
- **THEN** the device redirects to the root, which serves the password-change page

#### Scenario: Assets needed to render the gate

- **WHEN** the password-change page requests the stylesheets, scripts, or icon it needs
- **THEN** they are served, because a gate that cannot render is a gate that cannot be passed

#### Scenario: Disabling client-side script

- **WHEN** a user disables JavaScript, or deletes the gate from the document, and requests an inner page
- **THEN** the interface is still not served, because the gate is enforced by routing

#### Scenario: Page open when the password is reset out of band

- **WHEN** the physical button resets the password to the default while a browser tab is showing the interface
- **THEN** that tab's next data request is refused and the tab navigates to the password-change page rather than filling with errors

### Requirement: Leaving the default state requires a genuinely different password

The password-change endpoint SHALL reject a new password equal to the shipped default, and SHALL enforce a minimum length of 8 characters. Length validation SHALL apply when a password is set, not when one is used to authenticate, so a device already holding a shorter password continues to authenticate with it.

#### Scenario: Changing the password to the default

- **WHEN** a user submits the shipped default as the new password
- **THEN** the change is rejected with an error explaining that the default cannot be reused, and the device remains locked down

#### Scenario: New password shorter than the minimum

- **WHEN** a user submits a new password shorter than 8 characters
- **THEN** the change is rejected and the stored password is unchanged

#### Scenario: Successful change

- **WHEN** a user submits a new password that is neither the default nor too short
- **THEN** it is stored, the lockdown lifts without a restart, and the user is told they will be asked to sign in again

#### Scenario: Authenticating with a pre-existing short password

- **WHEN** a device already holds a password shorter than the new minimum, set before this requirement existed
- **THEN** that password still authenticates, and only the act of setting a new one is validated

### Requirement: The route that restores the default password is refused while the default is active

The endpoint that resets the web password to the shipped default SHALL be refused while the default password is already active. Resetting the password SHALL remain available by physical means so that a forgotten password is always recoverable.

#### Scenario: Resetting an already-default password over the API

- **WHEN** a client calls the reset endpoint while the default password is active
- **THEN** the request is refused, because it can only preserve the state the device is trying to leave

#### Scenario: Resetting a changed password over the API

- **WHEN** a client calls the reset endpoint while a non-default password is active
- **THEN** the password is reset to the default and the device enters the locked-down state immediately

#### Scenario: Forgotten password

- **WHEN** a user who does not know the current password holds the physical reset control
- **THEN** the password returns to the default and the device enters the locked-down state, from which a new password can be set

### Requirement: The lockdown never disables the device's own liveness probe

The endpoint the device uses to verify its own web server SHALL remain reachable, unauthenticated, in the locked-down state. A locked-down device SHALL NOT restart, roll back its firmware, or erase user configuration as a consequence of being locked down.

#### Scenario: Sustained operation while locked down

- **WHEN** a device sits in the locked-down state for longer than its self-check failure threshold would take to trigger a restart
- **THEN** it neither restarts nor rolls back, because its self-check continues to succeed

### Requirement: The lockdown never prevents the device from being put back on a network

A device that has never been given network credentials is necessarily still on the default password, and a device that has lost its network may be too. In neither case SHALL the lockdown prevent network setup: whenever the device offers its own access point, the WiFi setup page and the endpoints that scan for and accept credentials SHALL remain reachable to a client on that access point, in every provisioning state.

That widening SHALL apply only to requests arriving on the device's own access point. On any other interface the lockdown SHALL refuse those same endpoints.

Firmware update SHALL remain refused throughout, on the access point as everywhere else, both for want of authentication and for want of a changed password.

#### Scenario: First boot with no credentials

- **WHEN** a user connects to a factory-fresh device's access point and completes network setup
- **THEN** every step of that flow works, and the lockdown does not intervene

#### Scenario: In-service device that lost its network

- **WHEN** a device still on the default password loses its network, raises its access point, and a user submits new credentials from it
- **THEN** the credentials are accepted, because a device that cannot be put back on a network cannot be rescued at all - the physical reset control only restores the default password, which deepens the lockdown rather than lifting it

#### Scenario: Provisioning endpoints from the LAN

- **WHEN** a client on the ordinary network requests the WiFi setup page or the credential endpoints while the device is locked down
- **THEN** they are refused, because the widening exists for the access point and must not extend past it

#### Scenario: Password change immediately after provisioning

- **WHEN** the device joins a network and the user opens it on the LAN for the first time
- **THEN** the password-change page is served, because the lockdown applies as soon as the provisioning carve-out no longer does

#### Scenario: Firmware upload from the access point

- **WHEN** a client on the access point attempts a firmware upload, in any provisioning state
- **THEN** it is refused

#### Scenario: Reaching the gate over the access point

- **WHEN** a user who came in over the access point to fix the network is served the password-change page
- **THEN** that page offers a way to reach WiFi setup, so they are not left on a form that does not address their problem

### Requirement: The device's password state is readable without cost on the request path

Determining whether the default password is active SHALL NOT require reading persistent storage, taking a lock, or allocating, on the path of an individual request. The state SHALL be observable through the authentication status endpoint and SHALL reflect a password change without a restart.

#### Scenario: Password changed while the device is running

- **WHEN** the password is changed
- **THEN** the authentication status endpoint reports the device is no longer on the default, and the lockdown lifts, without a restart

#### Scenario: Password state cannot be read from storage

- **WHEN** the stored password cannot be read
- **THEN** the device treats itself as holding the default password, refusing rather than opening up
