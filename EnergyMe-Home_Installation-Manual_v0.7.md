# EnergyMe Home — Installation Manual

**Document version:** 0.7 (draft)  
**Product:** EnergyMe Home  
**Audience:** End user with a qualified electrician for the panel work  
**Language:** English

---

## ⚠ Before you start — Read this page

> *"You're about to discover things you never knew were happening behind your walls."*

Welcome. In the next 15 minutes, your home is going to stop being a black box. You're going to see — for the first time — exactly where your energy goes: which circuits eat it during the night, which appliances are quietly draining your wallet, which lights you forgot on three years ago.

Some users discover their fridge consumes more than their TV. Some find a phantom load that's been running for years. A few discover their solar panels are producing less than they should. **Whatever your home is hiding, EnergyMe is about to reveal it.**

Hold tight. This is going to be interesting.

---

EnergyMe Home connects directly to the AC mains inside your electrical panel. Working inside an electrical panel is **dangerous** if you don't know what you are doing.

| You can do yourself | You need a qualified electrician |
|---|---|
| Open the box and identify the parts (§1) | Anything inside the electrical panel (§3) |
| Configure Wi-Fi and the web interface (§4) | Switching off the main breaker |
| Map circuits to channels in the UI (§4.4) | Connecting L / N to the device |
| | Clamping the CTs around the wires |

> **⚠ WARNING — Electrical hazard**  
> Do not open your electrical panel with the main breaker ON. Mains voltage (230 V AC) can kill. If you have any doubt, **stop and call a qualified electrician**. The installation must comply with **CEI 64-8** (Italy) or the equivalent national wiring regulations. EnergyMe S.r.l. is not liable for installations performed by unqualified personnel or in violation of local codes.

---

## 1. What's in the box

### 1.1 Starter Kit (always included)

| # | Item | Qty | Notes |
|---|---|---|---|
| 1 | EnergyMe Home device | 1 | Pre-assembled, 3-module DIN-rail mount, with L/N wires already connected. **Channels are numbered 0 to 15 on the front sticker** (16 channels total) |
| 2 | CT clamps (30 A, 1 m cable, 3.5 mm jack) | 4 | All identical — the rating "30 A" is printed on each clamp body. 1 will be used for **Channel 0** (main line) + 3 for branch circuits |

You'll also find a **"Let's get started" sticker with a QR code** placed inside the lid of the box.

> *And if you're reading these instructions: nice work — you already found the QR code. 🎯*

### 1.2 Expansion Kit (optional, if ordered)

If you ordered additional CTs to monitor more than 3 branch circuits, you'll find them in a separate bag inside the box. The device supports up to **16 channels in total** (Channel 0 for the main + Channels 1 to 15 for branches).

| Item | Qty |
|---|---|
| Additional CT clamps (30 A or higher rating) | as ordered |

> **ⓘ NOTE — About the CTs**  
> The standard CTs are **30 A rated** (printed on each clamp). They work without issues for **single-phase systems up to 6 kW**.  
>  
> Before clamping any CT, check the current rating printed on the breaker the CT will monitor — it must not exceed the CT rating. If you have circuits drawing higher current (induction hob, EV charger, heat pump), or a three-phase supply, you may need higher-rated CTs (75 A or 150 A). Contact us at `[⚙ TO VERIFY: support@energyme.io]` to order them.

> **ⓘ NOTE — If anything is missing or damaged**  
> Do not install the device. Contact support at `[⚙ TO VERIFY: support@energyme.io]` with a photo of the box content.

> **⚠ Three-phase loads — read this before continuing**  
> If your main supply is **three-phase**, or if you have **specific three-phase loads** to monitor (e.g., a three-phase EV charger), the wiring is slightly different. **Read Appendix B before starting the installation.**

---

## 2. What you need (not included)

**The electrician will need:**

- Insulated screwdriver
- **3 free contiguous DIN modules** in your electrical panel

**You will need:**

- A smartphone, tablet or laptop with Wi-Fi (2.4 GHz)
- Your home Wi-Fi network name (SSID) and password

---

## 3. Electrical installation

> **Before you open the panel:**  
>  
> 1. Open the panel cover and visually identify **3 free contiguous DIN modules** for the device.  
> 2. **Switch OFF the main breaker.**  
>  
> From this point on, the work is genuinely simple — most installers complete it in **10 to 15 minutes**.

> **⚠ WARNING — From this section onwards, the work must be done by a qualified electrician.**  
> Verify the absence of voltage with a tester on every conductor you will touch. Lock-out / tag-out the breaker if other people have access to the panel.

### 3.1 Mount the device on the DIN rail

1. In the 3-module space you identified, hook the **top** of the device on the rail first.
2. Push the **bottom** until the spring clip clicks.
3. Pull gently downwards to verify the device is locked in place.

### 3.2 Connect the power supply (L and N)

The device comes with **two wires already connected** to the internal power terminal — a **brown wire (Line)** and a **blue wire (Neutral)**. You only need to connect the free ends of these wires to the **nearest Line and Neutral references** in the panel.

1. Connect the **brown wire (Line)** to the nearest available Line reference — typically the Line side of any breaker in the panel.
2. Connect the **blue wire (Neutral)** to the **Neutral bar** of the panel (the common neutral terminal block).
3. Tighten the screws firmly.

`[⚙ TO VERIFY: insert photo showing the brown/blue wires already connected to the device, and the connection to a nearby breaker + neutral bar]`

### 3.3 Install the CT on **Channel 0** (main line)

The CT on **Channel 0** measures the total energy entering your home and is the reference for all other channels.

1. Take **one** of the CT clamps from the kit (any of them — they're identical).
2. Identify the **main line conductor** downstream of the main breaker (typically the brown/black wire coming from the kWh meter into the panel).
3. Open the CT clamp.
4. Clamp it **around the single Line conductor only** — never around L and N together (the magnetic fields would cancel out and you would read zero).
5. Close the clamp until you hear/feel the click.
6. Plug the 3.5 mm jack into the socket marked **`0`** on the device (the channel numbers are printed on the front sticker).

> **ⓘ NOTE — Clamp direction doesn't matter**  
> The CT clamps are not directional. If you discover later (in §4.5) that the readings on a channel are inverted (e.g., negative when you expect positive), you don't need to reopen the panel — just check the **Reverse** box for that channel in the web interface and the sign is flipped instantly.

> **⚠ Three-phase main supply**  
> If your main supply is three-phase, the CT on Channel 0 goes on **one of the three phases**, and you'll need additional CTs on the other two phases (on free branch channels). See **Appendix B**.

### 3.4 Install the CTs on the **branch channels** (1 to 15) — ⚠ Critical step

This is the step where most installation mistakes happen. **Read this section before clamping anything.**

#### 3.4.1 The "shared input wire" problem

Inside an electrical panel, breakers are usually fed in **daisy-chain** on the **input** side: the supply line enters breaker #1, then jumps to breaker #2, then to breaker #3, and so on. This wiring is normal and code-compliant — but it has a major consequence for measurement:

> **⚠ The wire on the INPUT (top) side of a breaker carries the current of THAT breaker AND of all the breakers downstream of it in the chain.**  
> If you clamp the CT on the input wire, you will read the **sum of multiple circuits** — which is wrong.

The wire on the **OUTPUT (bottom) side** of the breaker, instead, carries **only** the current of the loads connected to that specific breaker. **This is where the CT must go.**

```
        From the main breaker
                │
   ┌────────────┼────────────┬────────────┐
   │            │            │            │
   │ INPUT      │ INPUT      │ INPUT      │   ← shared bus, current sums up
   │            │            │            │      ❌ DO NOT clamp here
  ┌─┴─┐        ┌─┴─┐        ┌─┴─┐        ┌─┴─┐
  │ B1│        │ B2│        │ B3│        │ B4│   ← breakers
  └─┬─┘        └─┬─┘        └─┬─┘        └─┬─┘
   │            │            │            │
   │ OUTPUT     │ OUTPUT     │ OUTPUT     │ OUTPUT
   │            │            │            │      ✅ CLAMP HERE
   ▼            ▼            ▼            ▼
  Load B1     Load B2     Load B3     Load B4
```

`[⚙ TO VERIFY: replace ASCII with a proper SVG diagram in the final docx]`

#### 3.4.2 The rule, in one sentence

> **Always clamp the branch CTs on the OUTPUT (load) side of the breaker — i.e., the wire that leaves the breaker and goes to the loads in the house. Never on the input bus.**

#### 3.4.3 How to identify the output wire

In a typical DIN-rail panel:

| Look at the breaker | Output side is… |
|---|---|
| MCB / magnetothermal breaker (single module) | The **bottom** terminal — the wire goes **down** towards the wiring duct |
| Differential (RCD) | Same — bottom terminal feeds the loads |
| Combined RCBO | Same — bottom terminal |

> **✅ TIP — Trace the wire**  
> If you are unsure which side is input and which is output, trace the wire visually:  
> - The **input** side connects to a **comb bar** (a horizontal copper bar shared with neighboring breakers).  
> - The **output** side connects to a **single wire** that disappears into the wiring duct towards the rest of the house.  
>  
> **The single wire is where the CT goes.**

#### 3.4.4 Step-by-step for each branch CT

For each circuit you want to monitor:

1. Decide which breaker you want to monitor (e.g., "kitchen", "lights", "washing machine").
2. Locate the **output** wire of that breaker — the one leaving the bottom terminal towards the loads. **Not** the input wire on top.
3. Open the CT clamp.
4. Clamp it **around the single Line conductor** of that output wire — not around L+N together, not around the protective earth (yellow/green wire).
5. Close the clamp until it clicks.
6. Plug the 3.5 mm jack into a free socket on the device, **numbered 1 to 15** (the channel numbers are printed on the front sticker).
7. **If you know what circuit it is, write down the channel number and the breaker label** (e.g., "Channel 2 → Garden lights"). You'll use this in §4.4 to give each channel a meaningful name in the UI. Use the table in Appendix A.

> **✅ TIP — Don't know what each breaker controls?**  
> No problem. Plug the CTs into any free channels and proceed with the installation. Once the device is online, **EnergyMe Intelligence** will help you identify which circuit is which from the consumption patterns — you can rename the channels later from the web interface.

> **⚠ WARNING — Common mistakes to avoid**  
> - ❌ Clamping on the **input** wire of a breaker (reads multiple circuits)  
> - ❌ Clamping on the **comb bar** (reads the sum of all breakers on the comb)  
> - ❌ Clamping around **both** L and N (reads zero)  
> - ❌ Clamping around the **protective earth** (reads zero in normal conditions, may read fault currents otherwise)  
> - ❌ Forcing the clamp on a wire that is too thick — the jaws must close fully and click

> **⚠ Three-phase branch loads**  
> If a specific load you want to monitor is three-phase (e.g., a three-phase EV charger or heat pump), see **Appendix B** before clamping.

### 3.5 Final check before closing the panel

Before turning the main breaker back ON:

| Check | OK? |
|---|---|
| All CT jacks are fully inserted in the device (1 on Channel 0, the others on Channels 1–15) | ☐ |
| Brown (L) and Blue (N) wires from the device are tightly connected, no copper visible | ☐ |
| Channel 0 CT is on the main Line conductor | ☐ |
| Each branch CT is on the OUTPUT side of its breaker (not on the comb bar) | ☐ |
| No tools or screws left inside the panel | ☐ |
| Channel map (Appendix A) filled in (where known) and photographed | ☐ |
| Panel cover ready to be re-installed | ☐ |

### 3.6 First power-on

1. Close the panel cover.
2. Turn the main breaker ON.
3. Observe the device LED `[⚙ TO VERIFY: confirm sequence and frequency with firmware team]`:

| LED behavior | Meaning |
|---|---|
| Solid white for ~2 s | Power OK, booting |
| Blinking blue (`[⚙ TO VERIFY: 1 Hz]`) | Wi-Fi captive portal active, ready to configure |
| Solid green | Connected to home Wi-Fi, normal operation |
| Blinking red (`[⚙ TO VERIFY: 2 Hz]`) | Error — see Troubleshooting |

> **⚠ WARNING**  
> If you smell anything burning, hear a buzz, or see smoke: **switch the main breaker OFF immediately** and contact support.

---

## 4. Software configuration

From now on, the work can be done by the end user. The electrical panel can stay closed.

### 4.1 Connect to the device's Wi-Fi (captive portal)

When EnergyMe Home is powered on for the first time, it creates its own Wi-Fi network so you can configure it.

1. On your phone or laptop, open the Wi-Fi settings.
2. Look for a network named **`EnergyMe-XXXXXX`** (where `XXXXXX` is the last 6 digits of the device ID, printed on the front of the device).
3. Connect to it. `[⚙ TO VERIFY: open network, no password]`
4. A configuration page should open automatically (captive portal). If it doesn't, open a browser and go to **`http://192.168.4.1`**.

> **ⓘ NOTE — If the network doesn't appear**  
> Wait 60 seconds after power-on. If it still doesn't show, press the **reset button** on the device for `[⚙ TO VERIFY: 5 seconds]` until the LED blinks blue.

### 4.2 Configure your home Wi-Fi

In the captive portal:

1. Select your home Wi-Fi network from the list.
2. Enter your Wi-Fi password.
3. Tap **Save & Connect**.
4. The device will reboot and connect to your home Wi-Fi (takes ~30 s).

> **✅ TIP — 2.4 GHz only**  
> EnergyMe Home connects to **2.4 GHz Wi-Fi**. If your router uses separate 2.4 / 5 GHz networks, choose the 2.4 GHz one. If your router uses a single combined network ("band steering"), it should work — but if you have problems, ask the router to expose the 2.4 GHz band as a separate SSID.

### 4.3 Access the web interface

1. On a device connected to the **same home Wi-Fi**, open a browser.
2. Go to **`http://energyme.local`**.
3. Log in with the default credentials:
   - Username: `admin`
   - Password: `energyme`
4. **Change the password immediately** — Settings → Security → Change password.

> **ⓘ NOTE — If `energyme.local` doesn't work**  
> Some networks (especially with mesh routers or older Android phones) don't resolve `.local` names. As an alternative, find the device's IP address in your router's admin page (look for a device named `EnergyMe-XXXXXX`) and use it directly (e.g., `http://192.168.1.42`).

### 4.4 Configure each channel

Now you map each physical CT to a meaningful name and role in the UI. Go to **Channels** in the menu and configure each channel that has a CT connected.

Each channel has the following fields:

| Field | What it means |
|---|---|
| **Label** | A free-text name (e.g., "Rete", "Kitchen", "EV charger"). It's the name that will appear on the dashboard |
| **Phase** | The phase the CT is clamped on. Use `1` for single-phase systems. For three-phase, see Appendix B |
| **Reverse** | Tick this if the channel reads with the wrong sign (e.g., shows -8 W when you expect +8 W). Flips the sign instantly — no need to reopen the panel |
| **Active** | Enables the channel. Untick for unused channels |
| **Grouping** | Channels sharing the same group label are combined into one card on the home dashboard. Use this for multi-phase loads — e.g. set all three phase CTs of your oven to "Oven" and the dashboard will show total power for the whole appliance |
| **Role** | What the channel represents — see table below |

#### Role values

| Role | When to use |
|---|---|
| `Load` | A branch circuit consuming energy (kitchen, lights, appliances, EV charger, heat pump…) |
| `Grid (+ import, - export)` | The main supply line. Positive when importing from the grid, negative when exporting (e.g. PV selling back) |
| `PV / Solar (+ generation)` | A solar panel string measured directly (AC side). Positive when generating |
| `Battery (+ discharge, - charge)` | A battery system. Positive when discharging to the home, negative when charging |
| `Inverter (PV + Battery DC-coupled)` | A hybrid inverter where PV and battery share a single AC output |

#### Configuration for a typical single-phase home

**Channel 0 (main line):**

| Field | Value |
|---|---|
| Label | `Rete` (or `Main`, `Grid`, your preference) |
| Phase | `1` |
| Reverse | (leave unchecked, fix later if needed) |
| Active | ☑ |
| Grouping | `Group 0` (default) |
| Role | `Grid (+ import, - export)` |

**Channels 1 to 15 (branch loads):**

| Field | Value |
|---|---|
| Label | The breaker label from Appendix A (e.g., "Kitchen") |
| Phase | `1` |
| Reverse | (leave unchecked, fix later if needed) |
| Active | ☑ for channels with a CT, ☐ for unused channels |
| Grouping | One group per channel by default (e.g., `Group 1`, `Group 2`…). Change only if you want to combine channels on the dashboard (see Appendix B for three-phase loads) |
| Role | `Load` (or `PV / Solar`, `Battery`, `Inverter` if applicable) |

Click **Save** for each channel.

> **⚠ Three-phase configuration**  
> If you have three-phase loads (main supply or specific branches), the **Phase** and **Grouping** fields must be configured carefully. See **Appendix B**.

### 4.5 Verification

| Check | When | Expected | If wrong → |
|---|---|---|---|
| Voltage | Within seconds | ~230 V | Re-check L/N connection |
| Channel 0 power (W) — when importing from grid | Within seconds | **Positive** (importing energy) | If consistently negative when you know you're importing → tick **Reverse** for Channel 0 |
| Channel 0 power (W) — when exporting (PV) | Within seconds | **Negative** | Sign convention is correct: `+ import, - export` |
| Branch channel power (W) on `Load` channels | Within seconds | Positive when consuming | If consistently negative → tick **Reverse** for that channel |
| Sum of `Load` branches ≤ Channel 0 import | Within seconds | Yes | If branches > main → likely a CT on input side / comb bar (§3.4.1) |
| Hourly / daily / monthly **energy** (kWh) | After a few hours | Numbers populating | The device needs time to accumulate energy data — totals fill in progressively over hours |

> **ⓘ NOTE — Don't worry if the dashboard looks empty at first**  
> Instantaneous power (Watts) appears within seconds. **Aggregated energy values (kWh) take a few hours to populate**, because the device needs to accumulate measurements before showing trends. Check back later in the day.

> **ⓘ NOTE — The `Reverse` checkbox is your friend**  
> If a channel reads with the wrong sign, you don't need to reopen the panel and flip the CT. Just tick **Reverse** for that channel in §4.4 — the sign flips instantly.

> **✅ TIP — The "kettle test" (instant verification)**  
> Turn on a single high-power load on a known circuit (e.g., a kettle in the kitchen). You should see the instantaneous power increase **on that one branch channel** **and** on Channel 0 by approximately the same amount.  
>  
> - If it appears on **multiple branches** → one of the CTs is on a shared input wire. Go back to §3.4.  
> - If it appears on the **wrong branch** → the channel-to-breaker mapping in the UI is wrong. Edit the labels in §4.4.  
> - If it appears on **no monitored branch** but on Channel 0 → that circuit isn't monitored yet, which is expected if you didn't put a CT on it.

---

## Appendix A — Channel map

Fill in during installation (where known), take a photo before closing the panel.

| Channel # | Breaker label | Description (room/load) | CT rating | Role | Phase | Grouping |
|---|---|---|---|---|---|---|
| 0 | Main breaker | Whole-home incoming line | 30 A | Grid | 1 | Group 0 |
| 1 | | | 30 A | Load | 1 | Group 1 |
| 2 | | | 30 A | Load | 1 | Group 2 |
| 3 | | | 30 A | Load | 1 | Group 3 |
| 4 | | | | | | |
| ... | | | | | | |
| 15 | | | | | | |

---

## Appendix B — Three-phase configuration

EnergyMe Home is fundamentally a single-phase device, but it can monitor a **three-phase main supply** (or specific three-phase loads) by using **one CT per phase** and combining the three readings on the dashboard via the `Grouping` field.

### B.1 Three-phase main supply

#### Hardware

1. Take **3 CTs** from the kit (or expansion kit).
2. Clamp each CT around **one of the three phase conductors** (L1, L2, L3) — never around the neutral.
3. Plug them into the device:
   - **Phase L1** → Channel `0` (the main reference channel)
   - **Phase L2** → any free branch channel (e.g., Channel `1`)
   - **Phase L3** → any free branch channel (e.g., Channel `2`)
4. **Write down which physical phase is on which channel** — you'll need it for the software step.

#### Software

In **Channels** (§4.4), set up the three channels like this:

| Channel | Label | Phase | Active | Grouping | Role |
|---|---|---|---|---|---|
| 0 | `Rete L1` | `1` | ☑ | `Rete` | `Grid (+ import, - export)` |
| 1 (or chosen branch) | `Rete L2` | `2` | ☑ | `Rete` | `Grid (+ import, - export)` |
| 2 (or chosen branch) | `Rete L3` | `3` | ☑ | `Rete` | `Grid (+ import, - export)` |

The three channels share the same **Grouping** value (`Rete`), so the dashboard will show a single "Rete" card with the **total three-phase power** of the main supply.

### B.2 Three-phase branch load (e.g., EV charger, heat pump, oven)

#### Hardware

1. Take **3 CTs**.
2. Clamp each CT on one phase of the load.
3. Plug them into 3 free branch channels (e.g., Channels `3`, `4`, `5`).
4. Note down which phase is on which channel.

#### Software

Example for a three-phase EV charger:

| Channel | Label | Phase | Active | Grouping | Role |
|---|---|---|---|---|---|
| 3 | `EV charger L1` | `1` | ☑ | `EV charger` | `Load` |
| 4 | `EV charger L2` | `2` | ☑ | `EV charger` | `Load` |
| 5 | `EV charger L3` | `3` | ☑ | `EV charger` | `Load` |

The dashboard will show a single "EV charger" card with the total three-phase power of the appliance.

> **✅ TIP — Naming the group**  
> Use a clean group name (e.g., `Oven`, `Heat pump`, `EV charger`) without phase suffixes — that's what will appear on the dashboard. Put the phase suffix only in the `Label` of each channel, so you can still inspect individual phases if needed.

---

`[⚙ TO VERIFY: Troubleshooting, Warranty, Disposal — next iterations]`
