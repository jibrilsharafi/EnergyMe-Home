## Purpose

Checks the phase and reverse configuration of three-phase installs on the device itself, from the measured angles, while the installer is on the setup page. It advises and never decides: a reverse-only fix is proposed alone, any phase change is a choice among the candidates, and nothing is applied without the installer. The installer's labels are the hypothesis under test; CT orientation and switching loads on are never required. Checking installs after setup is left to the cloud, which already receives the readings.

## ADDED Requirements

### Requirement: Check window
The device SHALL run the phase check only inside a check window. The window SHALL be open while the device has never completed setup, and for 2 hours after `POST /api/v1/setup/start` (sent by the configuration page's "Run setup again"). It SHALL close when setup completes or is skipped, when those 2 hours pass, and on reboot for a window opened by `setup/start`. Outside the window the device SHALL keep no evidence, run no evaluation and report no line or channel check. The phase check SHALL never change the configuration on its own and SHALL raise no issue.

#### Scenario: Window closes at finish
- **WHEN** the installer finishes the setup while a line is still `collecting`
- **THEN** the device drops that line's evidence, stops evaluating it, and changes nothing

#### Scenario: Setup run again
- **WHEN** the configuration page's "Run setup again" is used on a device set up months ago
- **THEN** the check runs on its lines for up to 2 hours or until the setup is finished, and the energy counters are kept

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
- **THEN** they are detected as a line, no hypothesis makes their angles agree, the line stays `collecting`, and nothing is proposed

### Requirement: Evidence is kept in the raw frame
The device SHALL accumulate the evidence of each channel of a line, and of each channel under the single-phase check, from the voltage-to-current energy pair as the meter measures it against V1, taken after the reading passed the meter's validity checks but before reverse, phase rotation and the load/PV clamp, so that the evidence does not depend on the channel's phase or reverse setting. Each reading SHALL be weighted by its elapsed time, not by how often the multiplexer read the channel, into a minute average, and a minute SHALL count only when that average apparent power is at least 3% of the CT rating at 230 V. For each pair of channels in a line the device SHALL accumulate the per-minute angle difference over minutes in which both conducted, weighted by the smaller of the two. Evidence SHALL start when the window opens or the line forms, SHALL be kept in RAM only, and SHALL be dropped when the window closes and when a channel's CT rating or role changes. Phase and reverse writes SHALL NOT reset it. For each such channel the device SHALL also keep its latest RMS current taken at the same point, before the clamp, so a reversed CT on a load channel still shows its current.

#### Scenario: Clamped readings still count
- **WHEN** a load channel's configured reading is negative and stored as 0 W and 0 A while its CT carries 8 A
- **THEN** that channel's evidence still grows from the unclamped raw pair, and the phase check reports 8 A for it

#### Scenario: Read-rate does not bias the evidence
- **WHEN** channel 0 is read every window and a multiplexed channel of the same line every fourth window, both carrying the same power
- **THEN** both channels accumulate the same evidence per minute

#### Scenario: Applying a candidate keeps the evidence
- **WHEN** a candidate is applied to a line
- **THEN** the device re-evaluates the line at once on the existing evidence, and reports it `verified` without a new collection period when the evidence supports the applied configuration

### Requirement: Line states
Each three-phase line SHALL be evaluated each time a minute of evidence closes, and at once after any configuration write to one of its channels (an apply included), in one of these states:
- `collecting`: a channel has under 1 Wh of evidence, fewer than two pairs of the line have 2 co-conducting minutes each, or no hypothesis fits;
- `verified`: under the configured phases and reverse flags, every evaluable pair's mean angle difference is within 40 degrees and every channel's angle is inside its role band;
- `fix`: a single proposal that changes reverse flags only (see "Hypotheses and proposals");
- `choose`: two or more candidates, most likely first, for the installer to pick from.

A state other than `collecting` SHALL be reported only once two consecutive minute evaluations agree; an evaluation after a configuration write is reported at once. No state SHALL block configuration, setup completion or measurement. Every evaluation works on the evidence already summed and takes no new measurement.

#### Scenario: Running load at install time
- **WHEN** a line's load runs while the installer is on the phase check step
- **THEN** the line leaves `collecting` within minutes

#### Scenario: No load at install time
- **WHEN** a line is configured while its load draws no current
- **THEN** the line is `collecting`, and setup can be completed

#### Scenario: Inductive load not moved to a higher power factor
- **WHEN** a correctly configured compressor reads 45 degrees lagging on all three phases
- **THEN** the line is `verified`, although the configuration 60 degrees away would read -15 degrees with a higher power factor

#### Scenario: Capacitive feeder left alone
- **WHEN** a feeder reads 30 to 34 degrees leading on all three phases under its configuration
- **THEN** the line is `verified`

#### Scenario: Crosstalk
- **WHEN** one channel of a line carries an extra signal that its siblings do not see, so that no hypothesis brings the pairs within 40 degrees
- **THEN** the line stays `collecting` and nothing is proposed

### Requirement: Hypotheses and proposals
For a line with a clamped role (load, PV) the device SHALL consider every assignment of its three channels to phases 1, 2, 3, combined with every reverse setting of each channel. Angles SHALL be expressed in the channel frame: raw angle plus `angle(phase) - angle(voltage phase)` plus 180 degrees when reversed. A hypothesis SHALL fit when every evaluable pair agrees within 40 degrees and every channel's angle is inside its role band (load: -50 to +75 degrees; PV: -65 to +35 degrees). Fitting hypotheses SHALL be ranked by how close their mean channel angle is to the role's typical angle (20 degrees lagging for load, 10 degrees leading for PV), most likely first.

When the configuration is not verified:
1. if its pairs agree, every channel reads positive power and only the role band fails, the line SHALL be `choose` with the configuration first and the fitting hypotheses after it, because a uniform error and an unusual load (low-PF lighting, a capacitive feeder) look the same;
2. otherwise, if exactly one fitting hypothesis keeps every configured phase and it is also ranked first, the line SHALL be `fix` with that hypothesis alone;
3. otherwise the line SHALL be `choose` with every fitting hypothesis, ranked.

A change of any phase SHALL therefore always be a choice. Rotated labels (written L1-L2-L3, wired L2-L3-L1) can still read like a reverse-only change 60 degrees away; rule 2 offers it alone only when it is also the most likely hypothesis.

For signed roles (grid, battery, inverter, bidirectional):
- the angles SHALL be evaluated modulo 180 degrees;
- only the phase assignment SHALL be checked;
- reverse flags SHALL be neither checked nor proposed, so such a line is never `fix`.

#### Scenario: One reversed CT
- **WHEN** a load line at 25 degrees whose labels are right has one CT reversed
- **THEN** the line is `fix` with a proposal that flips that channel's reverse flag only

#### Scenario: All CTs reversed
- **WHEN** a load line at 40 degrees whose labels are right has all three CTs reversed
- **THEN** the line is `fix` with a proposal that flips all three reverse flags

#### Scenario: Swapped labels
- **WHEN** a load line at 22 degrees has its L1 and L3 labels swapped
- **THEN** the line is `choose`, with the true wiring first and its alias 60 degrees away second

#### Scenario: Rotated labels
- **WHEN** a load line at 10 degrees has labels written L1-L2-L3 but wired L2-L3-L1
- **THEN** the line is `choose` with the true wiring first and "all CTs reversed" as another option, not `fix`

#### Scenario: Low power factor load
- **WHEN** a lighting feeder reads 80 degrees lagging on all three phases under its configuration
- **THEN** the line is `choose` with the configuration first

### Requirement: Labels are tested, not overwritten
The configured phases SHALL be the installer's labels. A proposal SHALL be described relative to them, channel by channel: the phase and reverse flag now and after. The device SHALL change a line's channels only on an explicit apply through local REST or the web UI, never on its own, including a `fix`.

#### Scenario: Fix waits for the installer
- **WHEN** a line is `fix` and the installer finishes the setup without applying it
- **THEN** the reverse flags stay as configured

### Requirement: Single-phase channel check
On a device with three-phase channels, each active load or PV channel outside any line SHALL get an advisory check inside the check window, with the same minimums as a line: the device SHALL report it `suspect` when its angle under the configured phase and reverse flag falls outside its role band, with the phase and reverse options that fit, most likely first, and `ok` otherwise. A wrong phase combined with a reversed CT is 60 degrees off and can pass unnoticed; only a three-phase line can catch it.

#### Scenario: Single-phase load on the wrong phase
- **WHEN** an oven wired on L2 is configured on L1
- **THEN** the channel is `suspect` with L2 offered first

### Requirement: Phase check REST interface
The device SHALL expose `GET /api/v1/ade7953/phase-check`, returning whether the check window is open and, while it is, for each line:
- the state;
- the configured phase and reverse of each channel, and each channel's latest RMS current before the clamp;
- each candidate, with the phase and reverse of each channel and the active power and power factor each channel would read under it;
- each channel's active power and power factor under the configuration;
- the line's generation number, which changes on any configuration write to one of its channels and on every apply to it, and on nothing else.

The same response SHALL list the single-phase channel checks: channel, state, current, options.

It SHALL expose `POST /api/v1/ade7953/phase-check/apply`, taking a line (by its lowest channel), the phase and reverse flag of each of its channels keyed by channel index, and the line's generation number. Applies SHALL be serialized under the configuration lock, so concurrent requests run one after the other:
- if the line's configuration already equals the candidate, the device SHALL return success with no change;
- if the generation still matches and the candidate is one the line currently offers, the device SHALL write all three channels in one step under the configuration lock, so that no intermediate state is applied or persisted, SHALL roll back the channels already written if any write fails, and SHALL then re-evaluate the line at once; a channel label that is the line's group label followed by its old phase (`<group> L1`) SHALL follow the new phase, and any other label SHALL be kept;
- otherwise it SHALL refuse with an error envelope and change nothing.

A single-phase option SHALL be applied as an ordinary channel write. Both endpoints SHALL require the web password.

#### Scenario: Stale apply refused
- **WHEN** a client applies a candidate read before the line's generation changed
- **THEN** the device refuses the request and changes nothing

#### Scenario: Two clients apply at once
- **WHEN** two clients apply the same candidate to a line at the same moment
- **THEN** one request writes the line and the other returns success with no change

#### Scenario: Lines are independent
- **WHEN** a client applies a candidate to one line and then a candidate it read earlier for another line
- **THEN** both applies succeed, because an apply changes only its own line's generation

#### Scenario: Retry after success
- **WHEN** a client repeats an apply that already succeeded
- **THEN** the device returns success and changes nothing

### Requirement: Setup page
After Wi-Fi provisioning and the password change, the device SHALL serve a setup page at `/setup` in its local web UI, built from the web UI's shared styles, API client, toasts and tooltips. The page SHALL work without internet access and guide through, in order:
1. cloud services, on by default, with an explicit local-only option; the choice SHALL be sent when setup completes, not when the step is left (`cloud-services-default`);
2. the supply: single-phase (the default), three-phase or split-phase, each with a one-line description, and a note that the meter's N always goes to neutral; the voltage phase SHALL be asked only for three-phase, and is `PHASE_1` otherwise. The supply SHALL NOT be stored: the page SHALL derive it from the channels (any split-phase channel: split-phase; a voltage phase other than 1 or any channel on phase 2 or 3: three-phase; otherwise single-phase);
3. the circuits, one per breaker, the first one open: name, role (with the channel page's role labels and tooltip), CT rating (30, 75, 150 or 400 A, or a custom value) and the channel each CT is on, with channels used by other circuits not selectable. A three-phase supply SHALL offer single-phase or three-phase circuits; a three-phase circuit has three CTs, one per phase, and SHALL be refused unless its three phases differ. A split-phase supply SHALL offer 120 V or 240 V per circuit, on any channel. A circuit SHALL be saved as channels sharing its name as group label, with labels `<name>` or `<name> L<phase>`;
4. for a three-phase supply only, the phase check of each line and each `suspect` single-phase circuit:
   - while `collecting` and right after an apply, a measuring indicator and each channel's current; when one channel of a line carries no current above the gate while another does, the page SHALL name that channel as the likely unclamped or misplaced CT;
   - otherwise each channel's phase, reverse flag, active power and power factor under the configuration and, for a proposal, under each candidate, with every change marked;
   - a `fix` SHALL be applied with one action; a `choose` SHALL need the installer to select a candidate and then confirm it, with the first one marked as most likely. Apply actions SHALL be disabled while a request is in flight.

Errors SHALL be shown as toasts. Reloading the page, or opening it from another device, before setup completes SHALL resume on the phase check step when the device has lines or checked single-phase channels. The installer SHALL be able to finish at any time; finishing while a line is `collecting`, `fix` or `choose`, or a single-phase channel is `suspect`, SHALL ask for confirmation, naming them. The installer SHALL also be able to skip the setup from its first step, which completes it with the defaults.

The device SHALL expose `GET /api/v1/setup/status`, `POST /api/v1/setup/start` and `POST /api/v1/setup/complete` (taking the cloud services choice), behind the web password. Completing or skipping the first setup SHALL reset the energy counters of every channel, so the install starts from zero; completing a setup run again SHALL NOT. The device decides where `/` leads: on a device that has never completed setup, to this page once the password is no longer the default, and to the dashboard afterwards. The configuration page SHALL offer to run the setup again; it starts from the current configuration on the first step and resets nothing. A device that booted earlier firmware, or that already holds stored energy, SHALL count as already set up; it never sees the setup page, never has its cloud held and never has its energy reset.

#### Scenario: Installer finishes with nothing running
- **WHEN** the installer finishes the setup while two lines are `collecting` and confirms
- **THEN** setup completes, the energy counters restart from zero, the check stops and nothing is changed

#### Scenario: Page reopened from a laptop
- **WHEN** the installer saves the circuits on a phone, closes the page, and opens the device from a laptop before finishing
- **THEN** the page opens on the phase check step with the lines' current states

#### Scenario: Reversed CT before a verdict
- **WHEN** a load line runs with one CT reversed and the line is still `collecting`
- **THEN** the page shows the current of all three channels, the reversed one included

#### Scenario: CT not clamped
- **WHEN** two channels of a line carry 8 A and the third carries no current
- **THEN** the line stays `collecting` and the page names the third channel

#### Scenario: Setup skipped
- **WHEN** the installer skips the setup on its first step
- **THEN** setup completes with cloud services on and the channels unchanged, the energy counters restart from zero, and `/` serves the dashboard

#### Scenario: Three-phase circuit on two equal phases
- **WHEN** the installer leaves the circuits step with a three-phase circuit whose CTs are on L1, L1 and L3
- **THEN** nothing is saved, a toast names the circuit and the circuit opens

#### Scenario: Upgraded device not redirected
- **WHEN** a device in service is updated to this firmware
- **THEN** `/` serves the dashboard, not the setup page
