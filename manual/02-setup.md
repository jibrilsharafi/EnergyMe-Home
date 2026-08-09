# Setup

From now on, the work can be done by the end user. The electrical panel can stay closed.

---

> **⚠ WARNING: Devices on firmware older than 2.3.0 use the older WiFiManager portal**  
> Firmware **2.3.0** replaced the previous WiFiManager-based captive portal with the flow described in this chapter. The core idea is the same either way: connect to the device's own Wi-Fi network, pick your home network from a list, submit, so this chapter still gets you there. Expect it to look less polished (no Welcome/Device screens from [§4.1](#41-connect-to-the-devices-wi-fi-setup-network), and the setup network is AP-only during configuration rather than staying up alongside your home Wi-Fi). The real gap is enforcement: the mandatory password gate and login lockout in [§4.4](#44-change-the-default-password) were both added in 2.3.0. On older firmware, the default password (`energyme`) is only a dismissible dashboard alert; nothing stops anyone on your network from using it as long as it's unchanged. Update to the latest firmware first if you can ([§4.8](#48-firmware-updates)); if you set the device up on an older version, go to **Configuration** and change the default password manually right after connecting.

---

## 4. Software configuration

### 4.1 Connect to the device's Wi-Fi (setup network)

On first power-on, or after a Wi-Fi reset, the device broadcasts its own Wi-Fi network so you can configure it. This setup network and your home Wi-Fi both run at the same time on the device (it does not switch between them), so the device stays reachable on its own network throughout setup.

1. On your phone or laptop, open Wi-Fi settings.
2. Look for a network named **`EnergyMe-<DEVICE_ID>`**, where `<DEVICE_ID>` is the 12-character code printed on the label inside the device case.
3. Connect to it. The password is:
   - **EnergyMe devices** (purchased from us): printed on the stickers on the case, both as a scannable QR code and as plain text.
   - **Community devices** (self-built): the `DEVICE_ID` itself (same 12-character code as the network name).
4. A configuration page opens automatically. If it does not, open a browser and navigate to **`http://172.31.42.1`**.

> **ⓘ NOTE: The address is usually `172.31.42.1`, but not guaranteed**  
> The device picks this address by default, but automatically falls back to a different one if it would clash with your home network's own address range (rare, but possible if your router happens to use `172.31.x.x`). If `172.31.42.1` doesn't load, let the page open automatically instead of typing an address, or check **Device** on the welcome screen (see below) for the exact "Setup network" address once you're connected to it.

> **ⓘ NOTE: If the network doesn't appear**  
> Wait 60 seconds after power-on. If it still isn't visible, hold the button for 10-15 seconds until the LED turns orange (Wi-Fi reset), then release. The device will restart and open its setup network again. See **[Appendix D](appendices.md#appendix-d-user-button-reference)** for the full button reference.

The page opens on a **Welcome** screen with two options: **Connect it to WiFi** (goes to §4.2 below) and **See the device** (shows the device model, ID, firmware version, uptime, and current network status, useful for confirming you're talking to the right device before you touch anything).

### 4.2 Configure your home Wi-Fi

From **Connect it to WiFi**, the device scans for nearby networks and lists them (signal strength and lock icon shown for each):

1. Select your home Wi-Fi network from the list, or type its name in if it is hidden or didn't show up in the scan.
2. Enter your Wi-Fi password (leave it blank for an open network).
3. Tap **Connect**.
4. The device joins your home network without restarting. The page reports the address to use afterwards, and the LED stops blinking blue and settles to **solid green** once connected.

> **ⓘ NOTE: The setup network drops for a few seconds**  
> To join your Wi-Fi, the meter has to move its own network to the same radio channel as your router (the chip has one radio and can't run its own network and your router's network on two different channels at once), so `EnergyMe-<DEVICE_ID>` disappears briefly right after you tap **Connect**. This is expected. Stay on the setup page; most phones rejoin on their own, and the page keeps waiting for the result. If it reports a failure, it shows the reason your router gave, such as a wrong password.

> **ⓘ NOTE: The setup network stays up for a few minutes after connecting**  
> Once the device joins your home Wi-Fi, its own `EnergyMe-<DEVICE_ID>` network stays up for about **5 more minutes** before it shuts down on its own. This is deliberate: it gives you time to read the new address off the confirmation screen (or reconnect to the setup network if something looks wrong) without immediately losing access to the device.

> **✅ TIP: 2.4 GHz only**  
> *EnergyMe Home* uses **2.4 GHz Wi-Fi**. If your router shows separate 2.4 GHz and 5 GHz networks, pick the 2.4 GHz one. If your router uses a single combined name ("band steering"), it will usually work fine, but if you have connection problems, ask your router's admin page to expose the 2.4 GHz band as a separate SSID.

### 4.3 Access the web interface

1. On a phone or laptop connected to **the same home Wi-Fi**, open a browser.
2. Navigate to **`http://energyme.local`**.
3. Log in with the default credentials:
   - **Username:** `admin`
   - **Password:** `energyme`

> **✅ TIP: Live demo**  
> A fully configured device is available online at **[demo-energyme-home.energyme.net](https://demo-energyme-home.energyme.net/)**. Use it to explore the interface, compare against your own dashboard, or familiarise yourself with the layout before installing.

> **ⓘ NOTE: Note down the IP address**  
> Once logged in, go to **Info** in the top menu. Under **Network Status**, you will see the device's local IP address (e.g. `192.168.1.42`). **Write it down or bookmark it.** If `energyme.local` stops resolving in the future (some routers, PCs, and older Android versions don't handle `.local` names reliably), you can always reach the interface directly by typing that IP address into your browser.
>
> If you haven't noted the IP yet and `energyme.local` no longer works, open your router's admin page and look for a device with hostname `energyme-home-<DEVICE_ID>`.

### 4.4 Change the default password

> **⚠ Mandatory, not just a suggestion.** The device ships with a known default password (`energyme`). Until you change it, the device refuses **everything else**: the API, the web UI, integrations, all of it. This isn't a dashboard reminder you can dismiss, it's enforced by the device itself.

1. After logging in during [§4.3](#43-access-the-web-interface), the device shows a **Choose a password** page automatically; you can't reach anything else until this is done.
2. Enter your new password twice (minimum 8 characters, and it can't be `energyme` again).
3. Click **Set password**.
4. Your browser will ask you to sign in again with the new password. The username stays `admin`.

> **ⓘ NOTE: Forgot your password?**  
> Hold the button for 5-10 seconds until the LED turns yellow (password reset), then release. The password is reset to `energyme`, which immediately re-opens the mandatory password-change gate above. Log back in and set a new one straight away. See **[Appendix D](appendices.md#appendix-d-user-button-reference)** for the full button reference.

> **ⓘ NOTE: Repeated wrong passwords temporarily lock you out**  
> After 5 consecutive failed login attempts from the same address, the device refuses further login attempts from it for a short cooldown (starting around 30 seconds, doubling if it happens again). This is a brute-force protection, not a bug: if you mistype your password a few times in a row, wait a moment and try again. It resets as soon as you log in successfully.

### 4.5 Configure each channel

Now you map each physical CT to a meaningful name and role in the UI. Go to **Channels** in the top menu and configure each channel that has a CT connected.

Each channel has the following fields:

| Field | What it means |
| --- | --- |
| **Label** | A free-text name (e.g., "Grid", "Kitchen", "EV charger"). It's the name that will appear on the dashboard |
| **Phase** | The AC phase the CT is clamped on. Use `1` for European single-phase and for standard 120 V North American loads. Use `2` or `3` for the other two phases of a three-phase system (see [Appendix B](appendices.md#appendix-b-three-phase-configuration)). Use **`240V (Split Phase)`** for a North American 240 V circuit that spans both legs L1-L2 (see [Appendix B.3](appendices.md#b3-north-american-120240-v-split-phase)); the firmware automatically applies a 2× voltage multiplier because the voltage reference is line-to-neutral (≈120 V) while the circuit is line-to-line (240 V). |
| **Reverse** | Flips the sign of a channel. Normally you won't need this: EnergyMe auto-detects reversed CTs on first activation and corrects them automatically. Manual toggle is available if needed (e.g., a channel role changes). Flips the sign instantly, no need to reopen the panel |
| **Active** | Enables the channel. Untick for unused channels. **Channel 0 is the exception:** it's always active and this can't be unticked, even if you never clamped a CT on it (see [§3.3](01-installation.md#33-install-the-ct-on-channel-0-main-line)) |
| **Grouping** | Channels sharing the same group label are combined into one card on the home dashboard. Use this for multi-phase loads: set all three phase CTs of your oven to "Oven" and the dashboard will show total power for the whole appliance |
| **Role** | What the channel represents (see table below) |

> **⚠ WARNING: Every channel in a group must have the same Role**  
> The device refuses to save a channel whose Role doesn't match the other channels already sharing its Grouping label (e.g., one channel set to `Load` and another to `Grid (+ import, - export)` under the same group). The save silently fails with no visible reason in the UI; only the device's **Logs** page shows the actual cause ("Group '...' role mismatch"). This most often happens by accident: the default Grouping values are `Group 0`, `Group 1`, etc. per channel, and changing a channel's Role without first giving it its own group can collide with a leftover default on another channel. If a channel's settings won't save, check that every channel sharing its Grouping label also shares its Role.

#### Role values

| Role | When to use |
| --- | --- |
| `Load` | A branch circuit consuming energy (kitchen, lights, appliances, EV charger, heat pump...) |
| `Grid (+ import, - export)` | The main supply line. Positive when importing from the grid, negative when exporting (e.g. PV selling back) |
| `PV / Solar (+ generation)` | A solar panel string measured directly (AC side). Positive when generating |
| `Battery (+ discharge, - charge)` | A battery system. Positive when discharging to the home, negative when charging |
| `Inverter (PV + Battery DC-coupled)` | A hybrid inverter where PV and battery share a single AC output |

#### Configuration for a typical single-phase home

> **ⓘ NOTE: Channel 0's Role doesn't have to be Grid**  
> The table below shows the common case: most single-phase homes want their most accurate, highest-sample-rate channel ([§3.3](01-installation.md#33-install-the-ct-on-channel-0-main-line)) on the main line. But Channel 0 is a normal channel for every purpose except being the voltage reference; give it whatever Role actually matches what's clamped to it (`Load`, `PV / Solar`, etc.) if you'd rather dedicate it to something else, for example if you already get whole-home totals from a utility meter integration and want Channel 0 on a specific branch instead.

**Channel 0 (main line):**

| Field | Value |
| --- | --- |
| Label | `Grid` (or `Main`, or your preference) |
| Phase | `1` |
| Reverse | (leave unchecked, fix later if needed) |
| Active | ☑ |
| Grouping | `Group 0` (default) |
| Role | `Grid (+ import, - export)` |

**Channels 1 to 15 (branch loads):**

| Field | Value |
| --- | --- |
| Label | The breaker label from [Appendix A](appendices.md#appendix-a-channel-map) (e.g., "Kitchen") |
| Phase | `1` |
| Reverse | (leave unchecked, fix later if needed) |
| Active | ☑ for channels with a CT, ☐ for unused channels |
| Grouping | One group per channel by default (e.g., `Group 1`, `Group 2`...). Change only if you want to combine channels on the dashboard (see [Appendix B](appendices.md#appendix-b-three-phase-configuration) for three-phase loads) |
| Role | `Load` (or `PV / Solar`, `Battery`, `Inverter` if applicable) |

Click **Save** for each channel.

> **⚠ Three-phase configuration**  
> If you have three-phase loads (main supply or specific branches), the **Phase** and **Grouping** fields must be configured carefully. See **[Appendix B](appendices.md#appendix-b-three-phase-configuration)**.

### 4.6 CT calibration: only if using non-standard CTs

> **ⓘ For standard 30 A CTs (the ones included in the kit)**  
> EnergyMe devices are calibrated at the factory. If you are using the supplied 30 A CTs, **skip this section entirely;** no calibration action is needed. Minor differences from a reference meter (well under 1%) are expected and normal.

You need to visit the **Calibration** page if:

- You ordered higher-rated CTs (75 A, 150 A) for high-current circuits; **or**
- You are using a community build with self-sourced CTs.

> **ⓘ NOTE: Cheap generic clamps vary more from unit to unit**  
> Common SCT-013-type clamps work fine (any CT with a voltage output and 3.5 mm jack does), but their calibration can differ noticeably between individual units, more so than higher-quality CTs. This matters less for one or two channels, but on a full 16-channel build it means each clamp may need its own small correction. If your readings are consistently off on some channels but not others with identical CTs, per-channel **Scaling Factor** below is exactly the tool for that: compare each channel against a known load and trim the offset per channel rather than assuming one clamp's calibration applies to all of them.

#### How to calibrate

1. In the top menu, go to **Calibration**.
2. Select the channel you want to calibrate from the drop-down.
3. Set **CT Current Rating (A)** to the value printed on the CT body (e.g. `75` for a 75 A CT). This is mandatory whenever the installed CT differs from the default 30 A.
4. Set **CT Voltage Output (V RMS)** to the voltage the CT outputs at its rated current: `0.333` V for the standard CTs in the kit, or check your CT's datasheet.

   > **⚠ Do not exceed 0.5 V output at your maximum expected current;** this is the ADE7953 measurement IC's safe input limit. For example, a 75 A / 1 V CT is fine if the circuit will never exceed ~37 A (half the rated current), but not for a fully loaded 75 A circuit.

5. Leave **Scaling Factor (%)** at `0` unless you are trimming a residual error against a known reference meter. Adjustment range is in 0.1% steps; only touch this if you have a calibrated reference to compare against.
6. Click **Save**.

Repeat for every channel that uses a non-standard CT.

### 4.7 Verification

| Check | When | Expected | If wrong |
| --- | --- | --- | --- |
| Voltage | Within seconds | Your nominal grid voltage (~120 V or ~230 V depending on your region) | Re-check L/N connection |
| Channel 0 power (W) when importing from grid | Within seconds | **Positive** (importing energy) | If consistently negative when you know you're importing, tick **Reverse** for Channel 0 |
| Channel 0 power (W) when exporting (PV) | Within seconds | **Negative** | Sign convention is correct: `+ import, - export` |
| Branch channel power (W) on `Load` channels | Within seconds | Positive when consuming | If consistently negative, tick **Reverse** for that channel |
| Sum of `Load` branches is less than or equal to Channel 0 import | Within seconds | Yes | If branches exceed the main, a CT is likely on the input side or comb bar ([§3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-)) |
| Hourly / daily / monthly energy (kWh) | After a few hours | Numbers populating | The device needs time to accumulate energy data; totals fill in progressively over hours |

> **ⓘ NOTE: Don't worry if the dashboard looks empty at first**  
> Instantaneous power (Watts) appears within seconds. **Aggregated energy values (kWh) take a few hours to populate**, because the device needs to accumulate measurements before showing trends. Check back later in the day.

> **ⓘ NOTE: The Reverse checkbox is your friend**  
> Most reversed CTs are auto-corrected by EnergyMe on first activation. But if a channel ever reads with the wrong sign (e.g., shows -8 W when you expect +8 W), you don't need to reopen the panel. Just tick **Reverse** for that channel in [§4.5](#45-configure-each-channel); the sign flips instantly.

> **ⓘ NOTE: Three-phase installs — a low power factor can mean a wrong Phase setting**  
> See the power-factor check in **[Appendix B.1](appendices.md#b1-three-phase-main-supply)** if a channel's numbers look plausible but off.

> **✅ TIP: The "kettle test" (instant verification)**  
> Turn on a single high-power load on a known circuit (e.g., a kettle in the kitchen). You should see the instantaneous power increase **on that one branch channel** and on Channel 0 by approximately the same amount.
>
> - If it appears on **multiple branches**, one of the CTs is on a shared input wire. Go back to [§3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-).
> - If it appears on the **wrong branch**, the channel-to-breaker mapping in the UI is wrong. Edit the labels in [§4.5](#45-configure-each-channel).
> - If it appears on **no monitored branch** but on Channel 0, that circuit isn't monitored yet, which is expected if you didn't put a CT on it.

### 4.8 Firmware updates

> **✅ Always keep the device on the latest firmware.** Updates aren't only new features: past releases have shipped fixes for measurement accuracy, security (the mandatory password gate and login lockout in [§4.4](#44-change-the-default-password) were both added this way), and stability. Check **[GitHub Releases](https://github.com/jibrilsharafi/EnergyMe-Home/releases/latest)** for what changed in each version.

> **✅ TIP: Let the device update itself**  
> On an EnergyMe device (not a community/self-built one), enable **Cloud Services** in **Configuration**. Once enabled, new firmware is fetched and installed automatically in the background, no bell icon to watch for and no manual step on the Update page.

> **⚠ WARNING: An EnergyMe device with Cloud Services off gets no update notification at all**  
> Updates on these devices are assumed to be handled through the cloud, so the bell icon below only ever appears on **community (self-built)** devices. If you keep Cloud Services disabled, check [GitHub Releases](https://github.com/jibrilsharafi/EnergyMe-Home/releases/latest) yourself now and then, since nothing on the device will tell you a new version exists.

On community devices, the device checks for available firmware updates on its own whenever it has internet access. When a new version is available, a **bell icon (🔔)** appears next to the Firmware Update button in the top navigation bar.

To update manually:

1. Go to **Update** in the top menu.
2. Follow the on-screen instructions.

> **ⓘ NOTE: Community devices**  
> Community (self-built) devices have no factory-provisioned cloud credentials, so **Cloud Services** can't be enabled and updates are always manual: download the latest firmware binary from [GitHub Releases](https://github.com/jibrilsharafi/EnergyMe-Home/releases/latest) and upload it via the Update page. Consider watching the repository (GitHub's "Watch" button, Releases only) so you hear about new versions without having to check manually.

### 4.9 Integrations

*EnergyMe Home* supports several integration options: Custom MQTT, InfluxDB, REST API, Modbus TCP, and mDNS service discovery. All configuration and documentation for these is available directly in the web interface under **Integrations** in the top menu; the page is self-contained and includes everything you need to connect to third-party platforms.

---

**Previous:** [← Installation](01-installation.md)  
**Next:** [Troubleshooting →](03-troubleshooting.md)
