# Appendices

## Appendix A: Channel map

Fill in during installation (where known), take a photo before closing the panel.

| Channel # | Breaker label | Description (room/load) | CT rating | Role | Phase | Grouping |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Main breaker | Whole-home incoming line | 30 A | Grid | 1 | Group 0 |
| 1 | | | 30 A | Load | 1 | Group 1 |
| 2 | | | 30 A | Load | 1 | Group 2 |
| 3 | | | 30 A | Load | 1 | Group 3 |
| 4 | | | | | | |
| ... | | | | | | |
| 15 | | | | | | |

---

## Appendix B: Multi-phase configurations

*EnergyMe Home* is fundamentally a single-phase device, but it can monitor a **three-phase main supply**, **specific three-phase loads**, and **North American 120/240 V split-phase circuits** by setting the `Phase` field on each channel and combining readings on the dashboard via the `Grouping` field.

### B.1 Three-phase main supply

#### Hardware

1. Take **3 CTs** from the kit (or expansion kit).
2. Clamp each CT around **one of the three phase conductors** (L1, L2, L3), never around the neutral.
3. Plug them into the device:
   - **Phase L1** → Channel `0` (the main reference channel)
   - **Phase L2** → any free branch channel (e.g., Channel `1`)
   - **Phase L3** → any free branch channel (e.g., Channel `2`)
4. **Write down which physical phase is on which channel;** you'll need it for the software step.

#### Software

In **Channels** ([§4.5](02-setup.md#45-configure-each-channel)), set up the three channels like this:

| Channel | Label | Phase | Active | Grouping | Role |
| --- | --- | --- | --- | --- | --- |
| 0 | `Grid L1` | `1` | ☑ | `Grid` | `Grid (+ import, - export)` |
| 1 (or chosen branch) | `Grid L2` | `2` | ☑ | `Grid` | `Grid (+ import, - export)` |
| 2 (or chosen branch) | `Grid L3` | `3` | ☑ | `Grid` | `Grid (+ import, - export)` |

The three channels share the same **Grouping** value (`Grid`), so the dashboard will show a single "Grid" card with the **total three-phase power** of the main supply.

> **ⓘ Override the default group.** The Grouping field is pre-filled with `Group 0`, `Group 1`, `Group 2`, etc., by default. For three-phase grouping you must **replace** the default value with the shared group name (e.g., `Grid`) on each of the three channels.

### B.2 Three-phase branch load (e.g., EV charger, heat pump, oven)

#### Hardware

1. Take **3 CTs**.
2. Clamp each CT on one phase of the load.
3. Plug them into 3 free branch channels (e.g., Channels `3`, `4`, `5`).
4. Note down which phase is on which channel.

#### Software

Example for a three-phase EV charger:

| Channel | Label | Phase | Active | Grouping | Role |
| --- | --- | --- | --- | --- | --- |
| 3 | `EV charger L1` | `1` | ☑ | `EV charger` | `Load` |
| 4 | `EV charger L2` | `2` | ☑ | `EV charger` | `Load` |
| 5 | `EV charger L3` | `3` | ☑ | `EV charger` | `Load` |

The dashboard will show a single "EV charger" card with the total three-phase power of the appliance.

> **✅ TIP: Naming the group**  
> Use a clean group name (e.g., `Oven`, `Heat pump`, `EV charger`) without phase suffixes; that's what will appear on the dashboard. Put the phase suffix only in the `Label` of each channel, so you can still inspect individual phases if needed.

### B.3 North American 120/240 V split-phase

Standard residential service in North America is a **split-phase** system: a centre-tapped transformer provides two 120 V "legs" (L1 and L2) plus a neutral. Most circuits run line-to-neutral at 120 V (lighting, outlets); large appliances (electric range, water heater, dryer, central AC, Level 2 EV charger) run line-to-line at 240 V across both legs.

*EnergyMe Home* measures the voltage **line-to-neutral** (≈120 V), so a CT on a 240 V circuit would by default underestimate the power by a factor of two. The firmware handles this for you: select **`240V (Split Phase)`** in the `Phase` field of the channel and a **×2 voltage multiplier** is applied automatically.

#### When to use each phase setting (North America)

| Circuit type | What the CT is clamped on | Phase setting |
| --- | --- | --- |
| 120 V branch (one leg, line-to-neutral) | Either leg of the 120 V circuit | `1` |
| 240 V branch (line-to-line, both legs) | Either leg of the 240 V circuit | `240V (Split Phase)` |
| Main supply, single leg | One of the two main legs | `1` |
| Main supply, both legs separately | One CT on each leg, on separate channels | `1` on each |

#### Hardware

For a 240 V branch (e.g., a dryer or Level 2 EV charger):

1. Take **one** CT clamp.
2. Clamp it around **either** of the two line conductors of the 240 V circuit (L1 or L2). Do not clamp around the neutral, and never around both legs together.
3. Close the clamp until it clicks.
4. Plug the 3.5 mm jack into a free channel on the device.

#### Software

In **Channels** ([§4.5](02-setup.md#45-configure-each-channel)), set:

| Field | Value |
| --- | --- |
| Label | e.g., `Dryer` or `EV charger` |
| Phase | **`240V (Split Phase)`** |
| Active | ☑ |
| Grouping | One group per appliance (default `Group N` is fine) |
| Role | `Load` (or `Battery` / `Inverter` if applicable) |

> **ⓘ NOTE: Why ×2 and not measure the voltage directly?**  
> The voltage transformer inside *EnergyMe Home* is wired between Line and Neutral, which on a North American split-phase service is ≈120 V. A 240 V line-to-line circuit has exactly twice that RMS voltage, so multiplying current by 2× the measured 120 V gives the correct power. This is preferable to running an additional voltage reference and works for any standard split-phase service.

> **⚠ Do not use `240V (Split Phase)` outside North American split-phase systems.** It is a numerical shortcut for the specific 120/240 V split-phase topology and would produce wrong readings on a European 230 V single-phase or any three-phase system.

---

## Appendix C: LED status reference

The LED on the front of the device communicates the system state at a glance.

> **ⓘ Boot colours**  
> During the first ~10 seconds after power-on the LED cycles through yellow, orange, purple, and other colours briefly. **This is normal.** Each colour marks an internal startup stage. Wait for the LED to settle before drawing any conclusion.

### Normal operation

| LED | State | Meaning |
| --- | --- | --- |
| 🟢 Green, solid | Connected | Wi-Fi connected, monitoring normally |
| 🔵 Blue, slow pulse | Connecting | Wi-Fi credentials configured but not currently connected (first boot, router still booting, or temporary connection loss). Reconnects automatically; usually resolves within 60 s |
| 🔵 Blue, fast blink | Portal active | No Wi-Fi configured; captive portal is open ([§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)) |

### Alerts

| LED | State | Meaning | What to do |
| --- | --- | --- | --- |
| 🟣 Purple, solid | Safe mode | Crash protection triggered; device is still monitoring and fully reachable via the web interface | Go to **Logs** in the top menu to investigate; the device will recover automatically |
| 🔴 Red, fast blink | Critical error | Persistent failure after recovery attempts | Restart via button ([Appendix D](#appendix-d-user-button-reference)); if it recurs, contact support |

### Button feedback (while the button is held; see [Appendix D](#appendix-d-user-button-reference))

| LED colour while holding | Action that will trigger on release |
| --- | --- |
| ⚪ White | Press registered but below action threshold. Release for no action |
| 🔵 Cyan | Restart |
| 🟡 Yellow | Password reset to default |
| 🟠 Orange | Wi-Fi reset (re-opens captive portal) |
| 🔴 Red | Factory reset: all data and settings will be erased |
| ⚪ White (again) | Held too long. Release and try again |

---

## Appendix D: User button reference

The button on the front of the device lets you recover from common situations without needing a phone or laptop.

**How it works:** press and hold. The LED changes colour as you hold longer, showing which action will trigger on release. **Release the button as soon as you see the colour for the action you want.**

| Hold time | LED colour | Action on release |
| --- | --- | --- |
| < 2 s | White | No action |
| 2-5 s | **Cyan** | **Restart:** equivalent to a power cycle |
| 5-10 s | **Yellow** | **Password reset:** resets the web password to `energyme` |
| 10-15 s | **Orange** | **Wi-Fi reset:** clears Wi-Fi credentials and reopens the captive portal ([§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)). Energy data and channel configuration are preserved |
| 15-20 s | **Red** | **Factory reset** ⚠: erases all data, configuration, and credentials. Use only as a last resort |
| > 20 s | White | No action. Release and try again |

> **⚠ WARNING: Factory reset is irreversible**  
> A factory reset erases all accumulated energy data, channel names, calibration, Wi-Fi credentials, and your custom password. There is no undo. Only hold to red if you are certain.

> **✅ TIP: Colour as confirmation**  
> You don't need to count seconds. Watch the LED: release the moment it shows the colour you want. If you overshoot to white again, keep holding; just release without triggering any action. Then try again from the start.

---

**Previous:** [← Troubleshooting](03-troubleshooting.md)
