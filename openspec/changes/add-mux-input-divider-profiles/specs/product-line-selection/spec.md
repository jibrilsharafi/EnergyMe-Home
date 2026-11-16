## ADDED Requirements

### Requirement: Divider-equipped PCB revisions

Home v6.2 and Home Pro v1.1 SHALL have their own hardware profiles, selected by `pcb_revision` `v6.2` and `v1.1` for products `home` and `homepro`. Their mux channel maps SHALL follow the board routing. Home v6.2 SHALL expose 16 channels (CT0 direct plus 15 multiplexed, no CT2) with CT9 on Y9 and CT10 on Y8. Home Pro v1.1 SHALL expose 12 channels. Each profile SHALL carry `pgaIb = 1` (PGA_IB x2), and the firmware SHALL write that value to PGA_IB at ADE7953 init. Profiles that do not set `pgaIb` SHALL keep PGA_IB at x1, and PGA_IA SHALL stay at x1 on every profile. An unsupported `pgaIb` value SHALL be logged and replaced by x1.

#### Scenario: Home v6.2 device boots

- **WHEN** a Home device with `pcb_revision = "v6.2"` boots
- **THEN** the Home v6.2 profile is selected, PGA_IB is written as x2, and CT9 is read on mux line Y9

#### Scenario: Home Pro v1.1 device boots

- **WHEN** a Home Pro device with `pcb_revision = "v1.1"` boots
- **THEN** the Home Pro v1.1 profile is selected, PGA_IB is written as x2, and 12 channels carry data

#### Scenario: Older revision unchanged

- **WHEN** a Home device with `pcb_revision = "v6.1"` boots
- **THEN** PGA_IB stays at x1 and the v6.1 mux map is used

#### Scenario: Net gain stays unity

- **WHEN** a multiplexed CT on a divider-equipped revision carries its rated 333 mV rms
- **THEN** the ADC sees 333 mV rms, so the CT current scale factors are the same as on v6.1
