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

## Appendix B: Three-phase configuration

*EnergyMe Home* is fundamentally a single-phase device, but it can monitor a **three-phase main supply** (or specific three-phase loads) by using **one CT per phase** and combining the three readings on the dashboard via the `Grouping` field.

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

---

## Appendix C: LED status reference

The LED on the front of the device communicates the system state at a glance.

> **ⓘ Boot colours**  
> During the first ~10 seconds after power-on the LED cycles through yellow, orange, purple, and other colours briefly. **This is normal.** Each colour marks an internal startup stage. Wait for the LED to settle before drawing any conclusion.

### Normal operation

| LED | State | Meaning |
| --- | --- | --- |
| 🟢 Green, solid | Connected | Wi-Fi connected, monitoring normally |
| 🔵 Blue, slow pulse | Reconnecting | Wi-Fi temporarily lost; reconnecting automatically, usually resolves on its own |
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
