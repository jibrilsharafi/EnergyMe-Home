## Purpose

Checks the phase and reverse configuration of three-phase lines on the device itself, from the measured angles, during setup and afterwards. It proposes a fix only when the evidence admits exactly one, and otherwise offers the installer the candidates to choose from. The installer's labels are the hypothesis under test; CT orientation and switching loads on are never required.

## ADDED Requirements

### Requirement: Three-phase line
The device SHALL detect three-phase lines automatically, with no installer action: a line is a set of exactly three active, non-split-phase channels that share a group label, have the same role, and are configured on three different phases. Once detected, a line SHALL keep its identity, keyed on its group label and its three channels, while its channels' phases or reverse flags change, so that a phase swap applied one channel at a time never dissolves it. It SHALL stop being a line when a channel leaves the group, is deactivated, is set to split-phase or is given another role.

#### Scenario: Detected without any marking
- **WHEN** channels 3, 4 and 5 are active, share the group label "Plant room", have role load and are on phases 1, 2 and 3
- **THEN** they are checked as one three-phase line

#### Scenario: Line survives a phase swap
- **WHEN** the phases of two channels of a line are exchanged one channel write at a time
- **THEN** the line keeps its identity and its evidence throughout

#### Scenario: Single-phase loads sharing a label
- **WHEN** three unrelated single-phase loads on L1, L2 and L3 share a group label
- **THEN** they are detected as a line, their angles disagree, the line becomes `inconsistent`, and nothing is proposed

### Requirement: Evidence is kept in the raw frame
The device SHALL accumulate the evidence of each channel of a line from the voltage-to-current energy pair as the meter measures it against V1, taken after the reading passed the meter's validity checks but before reverse, phase rotation and the load/PV clamp, so that the evidence does not depend on the channel's phase or reverse setting. Readings SHALL count only when the channel's RMS current is at least 3% of its CT rating. Each reading SHALL be weighted by its elapsed time, not by how often the multiplexer read the channel, and the per-channel evidence SHALL be energy-weighted. For each pair of channels in a line the device SHALL accumulate the per-minute angle difference over minutes in which both conducted, weighted by the smaller of the two. All evidence SHALL decay with a half-life of 3 days. The device SHALL reset a channel's evidence, and that of its pairs, when its recent angle departs from the accumulated one by more than 60 degrees for an hour (a moved CT), and on an explicit reset request. Phase and reverse writes SHALL NOT reset evidence. Evidence SHALL be kept in RAM only.

#### Scenario: Clamped readings still count
- **WHEN** a load channel's configured reading is negative and stored as 0 W while its CT carries current above the gate
- **THEN** that channel's evidence still grows from the unclamped raw pair

#### Scenario: Read-rate does not bias the evidence
- **WHEN** channel 0 is read every window and a multiplexed channel of the same line every fourth window, both carrying the same power
- **THEN** both channels accumulate the same evidence per hour

#### Scenario: Applying a fix keeps the evidence
- **WHEN** a proposal is applied to a line
- **THEN** the next evaluation reuses the existing evidence and reports the line `verified` without a new collection period

### Requirement: Line states
Each three-phase line SHALL be in one of these states, re-evaluated once a minute:
- `collecting`: a channel has under 50 Wh of evidence, or fewer than two pairs of the line have 60 co-conducting minutes each;
- `verified`: under the configured phases and reverse flags, every evaluable pair's mean angle difference is within 40 degrees and every channel's angle is inside its role band;
- `mismatch`: the configuration is not verified and exactly one hypothesis remains consistent after the tie-breaks;
- `ambiguous`: the configuration is not verified and more than one hypothesis remains after the tie-breaks; or the pairs agree under the configuration, every channel reads positive power, but an angle falls outside its role band (the configuration itself is then offered with the alternatives);
- `inconsistent`: no hypothesis makes every evaluable pair agree within 40 degrees (genuinely unbalanced feeders, and measurement faults such as multiplexer crosstalk).
No state SHALL block configuration, setup completion or measurement.

#### Scenario: No load at install time
- **WHEN** a line is configured while its load draws no current
- **THEN** the line is `collecting`, setup can be completed, and the line is evaluated as soon as enough evidence accumulates, with no user action

#### Scenario: Inductive load not moved to a higher power factor
- **WHEN** a correctly configured compressor reads 45 degrees lagging on all three phases
- **THEN** the line is `verified`, although the configuration 60 degrees away would read -15 degrees with a higher power factor

#### Scenario: Capacitive feeder left alone
- **WHEN** a feeder reads 30 to 34 degrees leading on all three phases under its configuration
- **THEN** the line is `verified`

#### Scenario: Low power factor load
- **WHEN** a lighting feeder reads 80 degrees lagging on all three phases under its configuration
- **THEN** the line is `ambiguous`, with the configuration as one of the offered candidates

#### Scenario: Crosstalk
- **WHEN** one channel of a line carries an extra signal that its siblings do not see, so that no hypothesis brings the pairs within 40 degrees
- **THEN** the line is `inconsistent` and nothing is proposed

### Requirement: Hypotheses and tie-breaks
For a line with a clamped role (load, PV) the device SHALL consider every assignment of its three channels to phases 1, 2, 3, combined with every reverse setting of each channel. Angles SHALL be expressed in the channel frame: raw angle plus `angle(phase) - angle(voltage phase)` plus 180 degrees when reversed. A hypothesis SHALL be consistent when every evaluable pair agrees within 40 degrees and every channel's angle is inside its role band (load and grid: -50 to +75 degrees; PV: -65 to +35 degrees).

Among consistent hypotheses for a configuration that is not verified, the device SHALL apply, in order:
1. trust the labelled phases over CT orientation: keep the hypotheses that change the fewest labelled phases, provided every other one changes at least 2 more; reverse flags SHALL never count, because orientation errors are common and existing flags may have been set by the detector;
2. for PV, keep the hypothesis whose mean angle is closest to -10 degrees, provided it is at least 15 degrees closer than every other.

Trusting the labels has one accepted limit: a line whose labels are cyclically rotated (written L1-L2-L3, wired L2-L3-L1) reads like a reverse pattern 60 degrees away and can receive a wrong reverse-only fix.

For signed roles (grid, battery, inverter, bidirectional):
- the angles SHALL be evaluated modulo 180 degrees;
- only the phase assignment SHALL be checked;
- reverse flags SHALL be neither checked nor proposed.

#### Scenario: Swapped labels with detector-made reverse flags
- **WHEN** the CTs labelled L1 and L3 of a PV line sit on each other's line, the configured reverse flags make L1 read 60 degrees off and L3 read negative, and all three agree at -15 degrees under the swapped phases with every reverse false
- **THEN** the line is `mismatch` with that single proposal

#### Scenario: One reversed CT on a PV line
- **WHEN** a PV line whose labels are right has one CT reversed and reads -10 degrees once that CT is flipped
- **THEN** the line is `mismatch` with a proposal that flips that channel's reverse flag only

#### Scenario: One reversed CT on a load line
- **WHEN** a load line at 25 degrees whose labels are right has one CT reversed
- **THEN** the line is `mismatch` with a proposal that flips that channel's reverse flag only, preferred over the cyclic relabel 60 degrees away because it keeps every labelled phase

#### Scenario: All CTs reversed
- **WHEN** a load line at 40 degrees whose labels are right has all three CTs reversed
- **THEN** the line is `mismatch` with a proposal that flips all three reverse flags

### Requirement: Labels are tested, not overwritten
The configured phases SHALL be the installer's labels. A proposal SHALL be described relative to them: which labelled phases are exchanged or rotated, and which CTs are reversed.

The device SHALL change the phase of a line's channel only on an explicit request through local REST, the web UI or the `channels` shadow. It SHALL apply a `mismatch` proposal on its own only when that proposal changes reverse flags only and keeps every phase; in that case it SHALL log the change and raise the `ct_polarity_flipped` event, as the detector does today. An `ambiguous` line SHALL never be changed by the device.

#### Scenario: Reverse-only proposal applied by the device
- **WHEN** a line reaches `mismatch` with a reverse-only proposal
- **THEN** the device sets those reverse flags, raises `ct_polarity_flipped` for each, and the line becomes `verified` at the next evaluation

#### Scenario: Phase proposal waits for a person
- **WHEN** a line reaches `mismatch` with a proposal that changes a phase
- **THEN** the configuration stays unchanged until someone applies the proposal

### Requirement: Phase issues
The device SHALL raise, scoped to the line's lowest channel:
- `channel_phase_suspect` (Warning) while a line is `mismatch` with a proposal that waits for a person;
- `channel_phase_ambiguous` (Warning) while a line is `ambiguous`;
- `channel_readings_inconsistent` (Info) while a line is `inconsistent`.

Each SHALL be raised only after its state has held for 3 consecutive evaluations, and cleared only after 3 consecutive evaluations without it. The message SHALL fit the issue buffer and name the lowest channel and the proposal in short form; the full candidates SHALL be available through REST. For channels of a line, `channel_polarity_mismatch` SHALL be suppressed while the line is `verified`, `mismatch` or `ambiguous`, and kept while it is `collecting`.

#### Scenario: Mismatch reaches the cloud
- **WHEN** a line stays `mismatch` with a phase proposal for 3 evaluations on a cloud-connected device
- **THEN** the `issues` shadow reports `channel_phase_suspect`, and applying the proposal through the `channels` shadow clears it 3 evaluations after the line becomes `verified`

### Requirement: Phase check REST interface
The device SHALL expose `GET /api/v1/ade7953/phase-check`, returning for each line:
- the state;
- the configured phase and reverse of each channel;
- each candidate hypothesis, with the angle and power factor every channel would read under it;
- each channel's angle under the configuration and its accumulated evidence;
- a generation number.

It SHALL expose `POST /api/v1/ade7953/phase-check/apply`, taking a line (by its lowest channel), a candidate's phases and reverse flags, and the generation number:
- if the line's configuration already equals the candidate, the device SHALL return success with no change;
- if the generation still matches and the candidate is one the line currently offers, the device SHALL write all three channels in one step under the configuration lock, so that no intermediate state is applied or persisted, and SHALL roll back the channels already written if any write fails;
- otherwise it SHALL refuse with an error envelope and change nothing.

It SHALL also expose `POST /api/v1/ade7953/phase-check/reset` to drop a line's evidence. All three endpoints SHALL require the web password.

#### Scenario: Stale apply refused
- **WHEN** a client applies a candidate read before the line's generation changed
- **THEN** the device refuses the request and changes nothing

#### Scenario: Retry after success
- **WHEN** a client repeats an apply that already succeeded
- **THEN** the device returns success and changes nothing

### Requirement: Setup page
After Wi-Fi provisioning and the password change, the device SHALL serve a setup page at `/setup` in its local web UI. The page SHALL work without internet access and guide through, in order:
1. cloud services, on by default, with an explicit local-only option;
2. the voltage phase, as the labelled line of the device's own L wire;
3. the channels: label, role, labelled phase and group label (three channels with the same group label on three phases form a line);
4. a live check of each detected line's state from the phase check interface, with one action per candidate to apply it.

The installer SHALL be able to finish while lines are `collecting`. On a device that has never completed setup, `/` SHALL lead to this page once the password is no longer the default; afterwards the page SHALL stay reachable from the configuration pages. A device upgraded with a non-default password SHALL count as already set up.

#### Scenario: Installer finishes with nothing running
- **WHEN** the installer completes the setup page while two lines are `collecting`
- **THEN** setup completes, the lines keep collecting, and a later `channel_phase_suspect` reaches the cloud

#### Scenario: Upgraded device not redirected
- **WHEN** a device with a changed password is updated to this firmware
- **THEN** `/` serves the dashboard, not the setup page
