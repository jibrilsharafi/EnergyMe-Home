## MODIFIED Requirements

### Requirement: Secrets and high-rate telemetry stay out of shadows
Shadows SHALL NOT carry secrets (WiFi credentials, CustomMQTT credentials, InfluxDB token, web UI password) or high-rate telemetry (energy counters, instantaneous power). The `wifi` shadow SHALL report only non-secret network state (connected/ssid/ip/gateway/subnet/dns/mac, static_ip, fallback_to_dhcp). On hardware with Ethernet it SHALL additionally report `active_interface` and an `ethernet` object (enabled, and when enabled link_up/static_ip/ip/gateway/subnet/dns1/dns2/mac) instead of a separate named shadow.

#### Scenario: wifi shadow exposes no credentials
- **WHEN** the `wifi` shadow publishes reported state
- **THEN** it contains non-secret network fields only and no WiFi password

#### Scenario: Ethernet state reported on a Home Pro
- **WHEN** the `wifi` shadow publishes on hardware with Ethernet
- **THEN** it includes `active_interface` and the `ethernet` object, and a Home device publishes neither
