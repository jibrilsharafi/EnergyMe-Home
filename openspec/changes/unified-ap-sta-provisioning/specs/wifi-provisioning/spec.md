## ADDED Requirements

### Requirement: Provisioning never blocks the WiFi task

The WiFi task SHALL remain able to service its notification loop at all times during provisioning. No provisioning operation SHALL block the task for longer than one notification-loop iteration.

#### Scenario: Credential submission with a wrong password

- **WHEN** the user submits credentials that fail to associate
- **THEN** the WiFi task continues processing AP client events, the DNS lifecycle and the grace timer throughout the failure and retry

#### Scenario: AP raised while STA is down

- **WHEN** the device is in `UNPROVISIONED` with the SoftAP up
- **THEN** the task does not issue forced STA reconnects, and the AP-raise trigger count does not increase as a side effect of STA retry logic

### Requirement: The device serves its web interface without an upstream network

The device SHALL start the web server and local integrations when the SoftAP is raised, even when no STA connection exists.

#### Scenario: Unprovisioned first boot

- **WHEN** the device boots with no stored credentials
- **THEN** the SoftAP is raised and the web server accepts requests on the AP netif without waiting for an STA connection

#### Scenario: Health check during AP-only operation

- **WHEN** the device serves on the AP netif with STA down for an extended period
- **THEN** the health check reports healthy, the device does not restart, and the consecutive-reset counter does not increase

### Requirement: The SoftAP is raised while the device is unreachable, and only then

The SoftAP SHALL be raised whenever the device cannot be reached over its own network, and SHALL be torn down once it can. Association, not elapsed time, is the teardown condition.

#### Scenario: Successful provisioning

- **WHEN** STA association succeeds from a session initiated on the AP
- **THEN** the AP is torn down after the grace period, or immediately once no AP clients remain

#### Scenario: Nobody ever connects

- **WHEN** the AP is raised and no client associates
- **THEN** the AP stays raised, because a device nobody can reach is not made more useful by going quiet

#### Scenario: Device loses its network permanently

- **WHEN** the device enters AP-assist because its network is gone and the user never intervenes
- **THEN** the AP stays raised indefinitely, with full authentication still required on it, so the device remains fixable in place rather than needing a power cycle
- **AND** STA association attempts continue throughout, so the device rejoins by itself if the network returns

#### Scenario: Unprovisioned device left alone

- **WHEN** a device with no stored credentials is left running
- **THEN** the AP remains raised, because it has no other way to be reached

#### Scenario: The AP is lowered while the device is still unreachable

- **WHEN** the AP comes down for any reason while the device still cannot associate
- **THEN** it is raised again on the next lifecycle tick, with no cooldown to wait out

### Requirement: Local integrations are not exposed on the SoftAP

Unauthenticated local integration services SHALL NOT accept connections arriving on the SoftAP netif.

#### Scenario: Modbus TCP during provisioning

- **WHEN** a client on the SoftAP connects to the Modbus TCP port
- **THEN** the connection is refused, because Modbus TCP carries no authentication and the SoftAP admits anyone within radio range

### Requirement: Credential changes do not restart the device

Changing the WiFi SSID or password SHALL apply without a restart. Changing the static IP configuration SHALL continue to require a restart.

#### Scenario: SSID change from the UI

- **WHEN** the user submits new WiFi credentials
- **THEN** the device associates to the new network without restarting, and reports the outcome through a status endpoint

### Requirement: New credentials can always be stored, including on a device that cannot associate

Submitting credentials SHALL store them regardless of the current association state. A device that cannot reach its network is precisely the device whose credentials need correcting, so the retry activity of that state SHALL NOT block the write.

#### Scenario: Correcting credentials on a device in AP-assist

- **WHEN** the user submits new credentials to a device that is repeatedly failing to associate
- **THEN** any association in flight is stopped first so the driver accepts the write, and the new credentials are stored and attempted

#### Scenario: The driver rejects the write

- **WHEN** the credential write is refused by the WiFi driver
- **THEN** the failure is retried once, and if it still fails the device SHALL report it through the status endpoint rather than leaving the caller with the success response the request already received

#### Scenario: Static IP change

- **WHEN** the user changes the static IP configuration
- **THEN** the device persists it and restarts to apply it, because reconfiguring a live netif races lwIP

### Requirement: Provisioning routes are reachable from the AP without the web password when unprovisioned

While the device is `UNPROVISIONED`, requests arriving on the SoftAP netif SHALL reach the web interface without digest authentication. In every other state, and on every other netif, digest authentication SHALL apply.

#### Scenario: Captive portal on first boot

- **WHEN** a phone joins the SoftAP of an unprovisioned device and the OS issues a captive-detection probe
- **THEN** the probe is answered without an authentication challenge and the user is directed to the WiFi setup page

#### Scenario: In-service device that lost its network

- **WHEN** a client joins the SoftAP of a device in AP-assist and requests any route
- **THEN** digest authentication is required

#### Scenario: Request arriving on the STA netif

- **WHEN** a request arrives on the STA netif while the device is `UNPROVISIONED`
- **THEN** digest authentication is required

#### Scenario: Reading the log while provisioning fails

- **WHEN** a user on the SoftAP of an unprovisioned device opens the log view to find out why association keeps failing
- **THEN** it is reachable without the web password, matching what the removed diagnostic page offered

#### Scenario: Grace period after a successful connect

- **WHEN** a client on the SoftAP requests a route during the grace period
- **THEN** digest authentication is required, because the device is now reachable on the LAN and the justification for opening up is gone

#### Scenario: Device's own loopback health probe

- **WHEN** the health check connects to `127.0.0.1`
- **THEN** it is not treated as an AP-origin request for the purpose of the authentication carve-out

#### Scenario: LAN host routing to the AP address

- **WHEN** a host on the LAN sends a request to the SoftAP address via a static route through the device's STA address
- **THEN** the request does not receive the authentication carve-out

### Requirement: Firmware update always requires authentication

The OTA endpoint SHALL require digest authentication in every provisioning state and on every netif, including the SoftAP.

#### Scenario: OTA attempt from the AP while unprovisioned

- **WHEN** a client on the SoftAP of an unprovisioned device requests the OTA endpoint
- **THEN** digest authentication is required, even though other routes are carved out in that state

### Requirement: The DNS responder is confined to AP-only operation

The catch-all DNS responder SHALL run only while the SoftAP is raised and STA is disconnected, and SHALL be stopped once STA connects.

#### Scenario: STA connects while the AP is still up

- **WHEN** STA association succeeds during the grace period
- **THEN** the DNS responder stops, so the device does not answer DNS queries arriving on the LAN

#### Scenario: AP address changed by subnet-collision avoidance

- **WHEN** the SoftAP address is moved to avoid a collision with the STA subnet
- **THEN** the DNS responder answers with the current SoftAP address, not a previously cached one

### Requirement: The SoftAP subnet never overlaps the STA subnet

The SoftAP SHALL be assigned a subnet that does not overlap the STA subnet, because lwIP resolves an ambiguous route to the first matching interface and a later-added interface is prepended.

#### Scenario: Router uses the default AP subnet

- **WHEN** the STA network occupies the SoftAP's default subnet
- **THEN** the SoftAP is assigned a different non-overlapping subnet before it is raised

#### Scenario: Restored backup carries a foreign static IP

- **WHEN** a configuration backup from a different LAN is restored and its static IP overlaps the SoftAP subnet
- **THEN** subnet selection accounts for the configured static IP, not only the live STA lease

#### Scenario: Collision appears after the AP is raised

- **WHEN** the AP is raised while unprovisioned and STA later obtains a lease that overlaps the AP subnet
- **THEN** the condition is logged and the AP is torn down rather than left routing ambiguously

### Requirement: The setup page tells the user where the meter went

A client on the SoftAP loses the ability to read the device's state the moment it submits credentials, because that transition closes the auth carve-out. The page SHALL therefore tell the user where to find the meter at submit time, and SHALL NOT present a pending state it cannot resolve.

#### Scenario: Credentials accepted

- **WHEN** the setup page submits credentials successfully over the SoftAP
- **THEN** it immediately shows the address the meter will be reachable at, and what the device LED will do
- **AND** it does not wait on a status endpoint that will answer 401 from that moment on

#### Scenario: Landing on the setup page

- **WHEN** an unprovisioned device serves its setup page
- **THEN** the user is offered both connecting the meter to WiFi and inspecting the device, rather than only a credentials form

### Requirement: Connection diagnostics survive the removal of the configuration portal

Disconnect diagnostics that are currently available only through the WiFiManager diagnostic page SHALL remain available through the device's own API.

#### Scenario: Diagnosing a failed association

- **WHEN** STA association fails and the user opens the interface from the SoftAP
- **THEN** the last attempted SSID, disconnect reason, disconnect BSSID and RSSI are retrievable

### Requirement: Power-cut recovery does not raise the SoftAP

A power interruption that also restarts the user's router SHALL NOT cause the device to raise a SoftAP before the router has had time to boot.

#### Scenario: Household power cut

- **WHEN** the device restarts from a power-on or brownout reset and the router is still booting
- **THEN** the device applies the extended connection timeout before counting association failures toward raising the SoftAP

## REMOVED Requirements

### Requirement: WiFi configuration through the WiFiManager captive portal

**Reason**: Replaced by provisioning served from the device's own web interface. The WiFiManager portal blocks the WiFi task, forces a restart on credential save, presents a second unrelated UI, and serves an unauthenticated firmware-upload page.

**Migration**: Provisioning moves to the device's own interface on the SoftAP. Stored credentials are unaffected; devices with valid credentials never raise the AP and see no change. The `/diagnostic` page is replaced by a diagnostics API endpoint. Users who relied on the portal's unauthenticated OTA now authenticate with the web password; a forgotten password is still recoverable with the 5 to 10 second button press.
