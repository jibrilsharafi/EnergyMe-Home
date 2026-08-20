# ethernet-connectivity Delta

## Purpose

Wired networking for Home Pro: W5500 SPI Ethernet bring-up, zero-touch DHCP commissioning, static IP configuration with safety backstops, and arbitration between Ethernet and WiFi so the device is reachable through the best available interface.

## ADDED Requirements

### Requirement: A cabled Pro device commissions itself with zero touch

On a product whose profile declares Ethernet, the system SHALL bring up the Ethernet interface at boot and acquire a DHCP lease when a link is present. A device with a link and a lease SHALL be fully operational (web UI, cloud, local integrations) without any provisioning step and without raising the SoftAP.

#### Scenario: First boot with cable and DHCP

- **WHEN** a factory-fresh Home Pro boots with an Ethernet cable on a DHCP network
- **THEN** it obtains a lease, all services start on the Ethernet interface, and no SoftAP is raised

#### Scenario: First boot with no cable and no WiFi credentials

- **WHEN** a factory-fresh Home Pro boots with no Ethernet link and no stored STA credentials
- **THEN** the SoftAP recovery channel is raised, per the wifi-provisioning raise conditions

### Requirement: Ethernet IP configuration is persistent, safe, and recoverable without a UI

The system SHALL support static IP configuration for Ethernet (address, gateway, subnet, DNS) set via the web interface, persisted independently of the WiFi configuration, and applied at boot. A static configuration that prevents the device from coming up SHALL be abandoned for DHCP after a bounded number of failed boots. A long press of the device button SHALL reset network configuration to DHCP.

#### Scenario: Static IP applied after restart

- **WHEN** the user saves a static Ethernet configuration
- **THEN** it is persisted and takes effect on the next restart, and the WiFi configuration is unchanged

#### Scenario: Unbootable static configuration

- **WHEN** a stored static Ethernet configuration fails a bounded number of consecutive boots
- **THEN** the device ignores it and boots on DHCP, so a bad static IP can never permanently strand a headless device

#### Scenario: Button network reset

- **WHEN** the user long-presses the button (the existing WiFi-reset tier)
- **THEN** all network configuration is cleared - WiFi credentials, WiFi static config, and Ethernet static config - so the device comes up on DHCP on whatever interface is available, or the SoftAP if none
- **AND** measurement data, calibration, web password, and cloud credentials are untouched
- **AND** the behavior is identical on every product: on a device without Ethernet the Ethernet-config clear is a no-op, so Home semantics are unchanged from today's WiFi reset

### Requirement: Ethernet is primary; WiFi STA is the automatic fallback

When both interfaces are available, Ethernet SHALL carry the default route. On Ethernet loss, an associated WiFi STA SHALL take over automatically. On Ethernet recovery, the default route SHALL return to Ethernet after a hold-down period that filters link flapping. Interface transitions SHALL NOT require a restart.

#### Scenario: Cable pulled with WiFi configured

- **WHEN** the Ethernet link drops on a device with associated STA
- **THEN** traffic (MQTT, NTP, telemetry) continues over WiFi without a restart, and cloud sessions re-establish rather than staying wedged

#### Scenario: Cable restored

- **WHEN** the Ethernet link returns and holds beyond the hold-down period
- **THEN** the default route moves back to Ethernet

#### Scenario: Flapping link

- **WHEN** the Ethernet link bounces repeatedly within the hold-down period
- **THEN** the default route does not thrash: it stays on the stable interface until the link holds

### Requirement: All network consumers are interface-agnostic

Every service gated on network readiness (MQTT cloud and local, NTP, InfluxDB, telemetry, health check, log streaming) SHALL treat "any serviceable interface" as connected. No service SHALL require WiFi specifically.

#### Scenario: Ethernet-only operation

- **WHEN** a Pro device runs with Ethernet up and no WiFi credentials stored
- **THEN** cloud MQTT over TLS, NTP sync, and all local integrations work, and the health check reports healthy indefinitely

#### Scenario: Health check during interface failover

- **WHEN** the default route moves between interfaces
- **THEN** the device does not restart due to a transient readiness gap during the transition

### Requirement: Ethernet is a trusted interface for local integrations

Services blocked on the SoftAP because it admits anyone in radio range (e.g. Modbus TCP) SHALL accept connections arriving on the Ethernet interface, which carries the same trust as the STA interface.

#### Scenario: Modbus TCP over Ethernet

- **WHEN** a client on the wired LAN connects to the Modbus TCP port
- **THEN** the connection is accepted, subject to the same rules as an STA-side client

### Requirement: Products without Ethernet are untouched

On a product whose profile declares no Ethernet, the system SHALL NOT initialize any Ethernet hardware or expose Ethernet configuration or status surfaces. Behavior SHALL be identical to firmware before this change.

#### Scenario: Home device on shared firmware

- **WHEN** a Home device runs a firmware build containing Ethernet support
- **THEN** no SPI bus, task, web page, or API field related to Ethernet is active, and heap/boot behavior is unchanged

### Requirement: Ethernet status is observable

The system SHALL expose the Ethernet state (link, DHCP/static, IP, gateway, DNS, MAC) through the web interface and REST API on products with Ethernet, alongside the existing WiFi status.

#### Scenario: Reading the wired address

- **WHEN** the user opens the device info or configuration page on a Pro device
- **THEN** the active interface is identified, and the Ethernet link state, addressing mode, IP and MAC are shown

### Requirement: Ethernet endpoints mirror the WiFi surface and refuse non-Ethernet products

The REST API SHALL expose Ethernet configuration and status endpoints following the existing WiFi network endpoint pattern (get/set/reset configuration, status). On a product whose profile declares no Ethernet, these endpoints SHALL return a 4xx client error - they do not exist as usable surface there, and the web UI SHALL NOT link to them.

#### Scenario: Configuring Ethernet on a Pro device

- **WHEN** a client sets an Ethernet configuration through the API on a Pro device
- **THEN** it is validated, persisted to the Ethernet namespace, and applied per the restart rules, mirroring the WiFi configuration endpoints' contract

#### Scenario: Ethernet endpoint called on a Home device

- **WHEN** any Ethernet configuration or status endpoint is requested on a device without Ethernet
- **THEN** the API answers with a 4xx error identifying the feature as unavailable on this product, and no NVS namespace is created or written
