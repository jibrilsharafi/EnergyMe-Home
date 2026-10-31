# ADR-0001: Solve at the source, no single-case mechanisms

- Status: accepted
- Date: 2026-10-31
- Twin: energyme-infra [ADR-036](https://github.com/EnergyMe-Srl/energyme-infra/blob/development/docs/ADR/ADR-036-solve-at-the-source.md) (same decision for the cloud), which links back here

## Context

The LAN collector (`openspec/changes/add-lan-collector`) first carried a per-job source clock offset (`vars.clock_offset_s`, header field `co`) because the pilot Dixell XWEB clock ran about 1 h 24 min ahead. That one device shaped a job var, a header field, an adoption rule (reset the cursor when the offset changes), decoder logic and an operator workflow (re-measure the offset after drift or a DST step). Every such case-shaped mechanism stays in the firmware, the cloud contract and the docs long after the case that justified it is gone, and each one interacts with the next.

## Decision

Do not add configuration, fields or code paths shaped around one device or one customer case.

1. Fix the problem at the source when the source is wrong: a misconfigured device, a wrong or drifting clock (set it, or enable NTP), a vendor setting.
2. If a fix is needed on our side, prefer a general mechanism that holds for every source of that kind over a knob for this one.
3. When the only option left is case-specific, it is an explicit decision recorded in the change's design, with the condition for removing it.

Applied first to the collector: the clock offset is removed from the job schema, the part header and the decoder. Source timestamps are taken as sent, and the pilot XWEB clock is corrected at the source before its job is enabled.

## Consequences

- Smaller contracts and fewer interacting rules on the device and in the cloud.
- Some onboarding work moves to the site: a source with a wrong clock or a bad setting is fixed before its data is trusted. The cloud `date` drift check flags a source clock that drifts later; the fix is again at the source.
- Reviews ask of every new field or option: does it exist for one case?
