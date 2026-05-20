# Installation

> Read the [safety preamble](README.md#before-you-start-read-this-page) before starting.

---

## 1. What's in the box

### 1.1 Starter Kit (always included)

| # | Item | Qty | Notes |
| --- | --- | --- | --- |
| 1 | *EnergyMe Home* device | 1 | Pre-assembled, 3-module DIN-rail mount, with L/N wires already connected. **Channels are numbered 0 to 15 on the front stickers** (16 channels total, 4 on the top and 12 on the bottom) |
| 2 | CT clamps (30 A, 1 m cable, 3.5 mm jack) | 4 | All identical; the rating "30 A" is printed on each clamp body. 1 will be used for **Channel 0** (main line) and 3 for branch circuits |

You'll also find a **"Let's get started" sticker with a QR code** placed inside the lid of the box.

> *And if you're reading these instructions: nice work, you already found the QR code. 🎯*

### 1.2 Expansion Kit (optional, if ordered)

If you ordered additional CTs to monitor more than 3 branch circuits, you'll find them in a separate bag inside the box. The device supports up to **16 channels in total** (Channel 0 for the main and Channels 1 to 15 for branches).

> **ⓘ NOTE: About the CTs**  
> The standard CTs are **30 A rated** (printed on each clamp). They work without issues for **single-phase systems up to ~7 kW at 230 V, or ~3.5 kW at 120 V**.
>
> Before clamping any CT, check the current rating printed on the breaker the CT will monitor; it must not exceed the CT rating. If you have circuits drawing higher current (power supply, heat pump), or a three-phase supply, you may need higher-rated CTs (75 A or 150 A). Contact us at `support@energyme.net` to order them.

> **ⓘ NOTE: Grid frequency**  
> *EnergyMe Home* works on both **50 Hz** (Europe, Asia, Africa, Oceania) and **60 Hz** (North America, parts of South America and Japan) grids with no configuration needed.

> **ⓘ NOTE: If anything is missing or damaged**  
> Do not install the device. Contact support at `support@energyme.net` with a photo of the box content.

> **⚠ Three-phase loads: read this before continuing**  
> If your main supply is **three-phase**, or if you have **specific three-phase loads** to monitor (e.g., a three-phase EV charger), the wiring is slightly different. **Read [Appendix B](appendices.md#appendix-b-three-phase-configuration) before starting the installation.**

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
> From this point on, the work is genuinely simple; most installers complete it in **10 to 15 minutes**.

> **⚠ WARNING: From this section onwards, the work must be done by a qualified electrician.**  
> Verify the absence of voltage with a tester on every conductor you will touch.

### 3.1 Mount the device on the DIN rail

1. In the 3-module space you identified, hook the **top** of the device on the rail first.
2. Push the **bottom** until the spring clip clicks.
3. Pull gently downwards to verify the device is locked in place.

### 3.2 Connect the power supply (L and N)

The device comes with **two wires already connected** to the internal power terminal: a **brown wire (Line)** and a **blue wire (Neutral)**. You only need to connect the free ends of these wires to the **nearest Line and Neutral references** in the panel.

1. Connect the **brown wire (Line)** to the nearest available Line reference, typically the Line side of any breaker in the panel.
2. Connect the **blue wire (Neutral)** to the **Neutral bar** of the panel (the common neutral terminal block).
3. Tighten the screws firmly.

> **ⓘ NOTE: Custom wiring**  
> If the provided wiring is not suitable for your application, it is possible to use your own wires.
>
> To do so, lift the plastic cover of the power terminal on the device, unscrew and disconnect the brown and blue wires, and connect your own wires to the same terminals, following the same Line/Neutral convention. Then put the cover back on.
>
> Make sure to use wires of appropriate gauge and insulation for mains voltage.

![Connection of L and N wires](assets/connection_l_n.jpg)

### 3.3 Install the CT on Channel 0 (main line)

The CT on **Channel 0** is the most accurate channel and should be used to measure the total energy entering your home.

1. Take **one** of the CT clamps from the kit (any of them, they're all identical).
2. Identify the **main line conductor** downstream of the main breaker (typically the brown/black wire coming from the kWh meter into the panel).
3. Open the CT clamp.
4. Clamp it **around the single Line conductor only**. Never clamp around L and N together; the magnetic fields cancel out and you would read zero. It is possible to also clamp around the neutral.
5. Close the clamp until you hear/feel the click.
6. Plug the 3.5 mm jack into the socket marked **`0`** on the device (the channel numbers are printed on the top front sticker).

> **ⓘ NOTE: Clamp direction doesn't matter**  
> The CT clamps are not directional. If you discover later (in [§4.7](02-setup.md#47-verification)) that the readings on a channel are inverted (e.g., negative when you expect positive), you don't need to reopen the panel. Just check the **Reverse** box for that channel in the web interface and the sign flips instantly.

> **⚠ Three-phase main supply**  
> If your main supply is three-phase, the CT on Channel 0 goes on **one of the three phases**, and you'll need additional CTs on the other two phases (on free branch channels). See **[Appendix B](appendices.md#appendix-b-three-phase-configuration)**.

### 3.4 Install the CTs on the branch channels (1 to 15): Critical step ⚠

This is the step where most installation mistakes happen. **Read this section before clamping anything.**

#### 3.4.1 The "shared input wire" problem

Inside an electrical panel, breakers can be fed in **daisy-chain** on the **input** side: the supply line enters breaker #1, then jumps to breaker #2, then to breaker #3, and so on. This has a major consequence for measurement:

> **⚠ The wire on the INPUT (top) side of a breaker carries the current of THAT breaker AND of all the breakers downstream of it in the chain.**  
> If you clamp the CT on the input wire, you will read the **sum of multiple circuits**, which is wrong.

The wire on the **OUTPUT (bottom) side** of the breaker carries **only** the current of the loads connected to that specific breaker. **This is where the CT must go.**

![CT Placement](assets/ct_placement_breaker.svg)

#### 3.4.2 The rule

> **Always clamp the branch CTs on the OUTPUT (load) side of the breaker, meaning the wire that leaves the breaker and goes to the loads in the house. Never on the input bus.**

#### 3.4.3 Step-by-step for each branch CT

For each circuit you want to monitor:

1. Decide which breaker you want to monitor (e.g., "kitchen", "lights", "washing machine").
2. Locate the **output** wire of that breaker, the one leaving the bottom terminal towards the loads. **Not** the input wire on top.
3. Open the CT clamp.
4. Clamp it **around the single Line conductor** of that output wire. Not around L+N together, not around the protective earth (yellow/green wire). Neutral also works fine.
5. Close the clamp until it clicks.
6. Plug the 3.5 mm jack into a free socket on the device, **numbered 1 to 15** (the channel numbers are printed on the front stickers).
7. **If you know what circuit it is, write down the channel number and the breaker label** (e.g., "Channel 2 → Garden lights"). You'll use this in [§4.5](02-setup.md#45-configure-each-channel) to give each channel a meaningful name in the UI. Use the table in [Appendix A](appendices.md#appendix-a-channel-map).

> **✅ TIP: Don't know what each breaker controls?**  
> No problem. Plug the CTs into any free channels and proceed with the installation. Once the device is online you can rename the channels at any time from the web interface.

> **✅ TIP: Don't worry about CT orientation**  
> Installed a CT backwards by accident? No problem. EnergyMe automatically detects reversed CTs on the first reading after you activate a channel and flips the polarity for you. No need to reopen the panel or manually adjust anything.

> **⚠ WARNING: Common mistakes to avoid**
>
> - ❌ Clamping on the **input** wire of a breaker (reads multiple circuits)
> - ❌ Clamping on the **comb bar** (reads the sum of all breakers on the comb)
> - ❌ Clamping around **both** L and N (reads zero)
> - ❌ Clamping around the **protective earth** (reads zero in normal conditions, may read fault currents otherwise)
> - ❌ Forcing the clamp on a wire that is too thick; the jaws must close fully and click

> **⚠ Three-phase branch loads**  
> If a specific load you want to monitor is three-phase (e.g., a three-phase EV charger or heat pump), see **[Appendix B](appendices.md#appendix-b-three-phase-configuration)** before clamping.

### 3.5 Final check before closing the panel

Before turning the main breaker back ON:

| Check | OK? |
| --- | --- |
| All CT jacks are fully inserted in the device (1 on Channel 0, the others on Channels 1-15) | ☐ |
| Brown (L) and Blue (N) wires from the device are tightly connected, no copper visible | ☐ |
| Channel 0 CT is on the main Line conductor | ☐ |
| Each branch CT is on the OUTPUT side of its breaker (not on the comb bar) | ☐ |
| No tools or screws left inside the panel | ☐ |
| Channel map ([Appendix A](appendices.md#appendix-a-channel-map)) filled in (where known) and photographed | ☐ |
| Panel cover ready to be re-installed | ☐ |

### 3.6 First power-on

1. Close the panel cover.
2. Turn the main breaker **ON**.
3. Watch the LED on the front of the device.

> **ⓘ NOTE: Boot sequence colours**  
> During the first ~10 seconds the LED cycles through several colours in quick succession (yellow, orange, purple, and others). **This is entirely normal;** each colour marks a stage of the startup sequence. Do not act on any of them; wait until the LED settles.

After boot, three outcomes are possible:

| LED behaviour | Meaning | Next step |
| --- | --- | --- |
| 🔵 Blue, fast blink | No Wi-Fi configured yet; captive portal active | Go to **[§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal)** |
| 🔵 Blue, slow pulse | Known Wi-Fi found but not yet connected (e.g. router still booting) | Wait up to 60 s; LED should settle to solid green |
| 🟢 Solid green | Connected to home Wi-Fi, monitoring | Go to **[§4.3](02-setup.md#43-access-the-web-interface)** (skip [§4.1](02-setup.md#41-connect-to-the-devices-wi-fi-captive-portal) and [§4.2](02-setup.md#42-configure-your-home-wi-fi)) |

> **⚠ WARNING**  
> If you smell anything burning, hear buzzing, or see smoke: **switch the main breaker OFF immediately** and contact support before doing anything else.

---

**Next:** [Setup →](02-setup.md)
