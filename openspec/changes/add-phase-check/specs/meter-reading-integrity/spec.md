## ADDED Requirements

### Requirement: Three-phase line orientation is not flipped by the sign vote
A channel of a three-phase line (`phase-check`) with a clamped role SHALL NOT be armed for, or flipped by, the CT-reversal sign-vote detector while the line is `collecting`, `verified`, `mismatch` or `ambiguous`. Joining a line SHALL clear any pending arm and its vote counters, including the arm that activating a channel or changing its phase sets today. The orientation of such a channel SHALL change only through an explicit configuration write or a reverse-only phase check proposal. While a line is `inconsistent` (not behaving as one three-phase load, such as unrelated single-phase loads sharing a label), its channels SHALL be handled by the detector like any other channel. A channel that leaves a line SHALL be handled like any other channel from then on, with no arm carried over from the line. Channels outside lines keep the detector unchanged.

#### Scenario: Wrong phase does not trigger a flip
- **WHEN** a load channel of a line is configured on the wrong phase and reads steadily negative
- **THEN** its reverse flag is not changed, its readings are clamped as before, and the line's phase check state reports the problem

#### Scenario: Single-phase install unchanged
- **WHEN** a load channel outside any line is activated with a reversed CT
- **THEN** the detector flips its reverse flag as before

#### Scenario: Unrelated loads sharing a label
- **WHEN** a line made of three unrelated single-phase loads becomes `inconsistent` and one of its CTs is reversed
- **THEN** the detector flips that channel's reverse flag as it does for any single channel
