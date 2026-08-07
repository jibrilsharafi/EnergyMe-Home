## MODIFIED Requirements

### Requirement: The device refuses to operate while the shipped default password is active

While the stored web password equals the shipped default, the device SHALL refuse every authenticated request except those on a fixed allowlist. Refusal SHALL NOT depend on the client rendering, executing, or honouring anything the device sends; a request made with a command-line HTTP client SHALL be refused exactly as a browser request is.

The allowlist SHALL be limited to what a user needs in order to leave the state: the health endpoint, reading the authentication status, changing the password, the gate page itself, and the static assets that gate page needs to render.

A caller that is being throttled for repeated failed logins SHALL be refused for that reason before its credentials are examined, and therefore before this requirement is evaluated. Throttling takes precedence over the lockdown, and neither refusal SHALL disclose the device's password state to a caller that has not authenticated.

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

#### Scenario: Guessing the default password repeatedly

- **WHEN** a client guesses passwords against a device that is on the shipped default, and crosses the failed-login threshold
- **THEN** it is throttled rather than challenged, and the throttling response says nothing about whether the device is on its default password

#### Scenario: Password has been changed

- **WHEN** the stored password differs from the shipped default
- **THEN** no request is refused on account of this requirement and the device behaves exactly as it did before this capability existed
