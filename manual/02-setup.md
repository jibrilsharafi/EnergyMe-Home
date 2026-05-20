# Setup

From now on, the work can be done by the end user. The electrical panel can stay closed.

---

## 4. Software configuration

### 4.1 Connect to the device's Wi-Fi (captive portal)

On first power-on, or after a Wi-Fi reset, the device broadcasts its own Wi-Fi network so you can configure it.

1. On your phone or laptop, open Wi-Fi settings.
2. Look for a network named **`EnergyMe-<DEVICE_ID>`**, where `<DEVICE_ID>` is the 12-character code printed on the label inside the device case.
3. Connect to it. The password is:
   - **EnergyMe devices** (purchased from us): printed on the stickers on the case, both as a scannable QR code and as plain text.
   - **Community devices** (self-built): the `DEVICE_ID` itself (same 12-character code as the network name).
4. A configuration page opens automatically. If it does not, open a browser and navigate to **`http://192.168.4.1`**.

> **ⓘ NOTE: If the network doesn't appear**  
> Wait 60 seconds after power-on. If it still isn't visible, hold the button for 10-15 seconds until the LED turns orange (Wi-Fi reset), then release. The device will restart and open the portal again. See **[Appendix D](appendices.md#appendix-d-user-button-reference)** for the full button reference.

### 4.2 Configure your home Wi-Fi

In the captive portal page:

1. Go to **Configure Wi-Fi**.
2. Select your home Wi-Fi network from the list.
3. Enter your Wi-Fi password.
4. Tap **Save**.
5. The device restarts and connects to your home network, which takes about 30 seconds. The LED will stop blinking blue and settle to **solid green** once connected.

> **✅ TIP: 2.4 GHz only**  
> *EnergyMe Home* uses **2.4 GHz Wi-Fi**. If your router shows separate 2.4 GHz and 5 GHz networks, pick the 2.4 GHz one. If your router uses a single combined name ("band steering"), it will usually work fine, but if you have connection problems, ask your router's admin page to expose the 2.4 GHz band as a separate SSID.

### 4.3 Access the web interface

1. On a phone or laptop connected to **the same home Wi-Fi**, open a browser.
2. Navigate to **`http://energyme.local`**.
3. Log in with the default credentials:
   - **Username:** `admin`
   - **Password:** `energyme`

> **ⓘ NOTE: Note down the IP address**  
> Once logged in, go to **Info** in the top menu. Under **Network Status**, you will see the device's local IP address (e.g. `192.168.1.42`). **Write it down or bookmark it.** If `energyme.local` stops resolving in the future (some routers, PCs, and older Android versions don't handle `.local` names reliably), you can always reach the interface directly by typing that IP address into your browser.
>
> If you haven't noted the IP yet and `energyme.local` no longer works, open your router's admin page and look for a device with hostname `energyme-home-<DEVICE_ID>`.

### 4.4 Change the default password

> **⚠ Do this before anything else.** The device ships with a known default password. Until you change it, anyone on your home network can access the interface.

1. In the top menu, go to **Configuration**.
2. Scroll to **Change Password**.
3. Enter the current password (`energyme`), then your new password twice. The new password must be at least 8 characters.
4. Click **Save**.

The device will remind you with a pop-up on the dashboard every time you open it, until the password has been changed.

> **ⓘ NOTE: Forgot your password?**  
> Hold the button for 5-10 seconds until the LED turns yellow (password reset), then release. The password is reset to `energyme`. Log back in and set a new one straight away. See **[Appendix D](appendices.md#appendix-d-user-button-reference)** for the full button reference.

### 4.5 Configure each channel

Now you map each physical CT to a meaningful name and role in the UI. Go to **Channels** in the top menu and configure each channel that has a CT connected.

Each channel has the following fields:

| Field | What it means |
| --- | --- |
| **Label** | A free-text name (e.g., "Grid", "Kitchen", "EV charger"). It's the name that will appear on the dashboard |
| **Phase** | The phase the CT is clamped on. Use `1` for single-phase systems. For three-phase, see [Appendix B](appendices.md#appendix-b-three-phase-configuration) |
| **Reverse** | Flips the sign of a channel. Normally you won't need this: EnergyMe auto-detects reversed CTs on first activation and corrects them automatically. Manual toggle is available if needed (e.g., a channel role changes). Flips the sign instantly, no need to reopen the panel |
| **Active** | Enables the channel. Untick for unused channels |
| **Grouping** | Channels sharing the same group label are combined into one card on the home dashboard. Use this for multi-phase loads: set all three phase CTs of your oven to "Oven" and the dashboard will show total power for the whole appliance |
| **Role** | What the channel represents (see table below) |

#### Role values

| Role | When to use |
| --- | --- |
| `Load` | A branch circuit consuming energy (kitchen, lights, appliances, EV charger, heat pump...) |
| `Grid (+ import, - export)` | The main supply line. Positive when importing from the grid, negative when exporting (e.g. PV selling back) |
| `PV / Solar (+ generation)` | A solar panel string measured directly (AC side). Positive when generating |
| `Battery (+ discharge, - charge)` | A battery system. Positive when discharging to the home, negative when charging |
| `Inverter (PV + Battery DC-coupled)` | A hybrid inverter where PV and battery share a single AC output |

#### Configuration for a typical single-phase home

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

> **✅ TIP: The "kettle test" (instant verification)**  
> Turn on a single high-power load on a known circuit (e.g., a kettle in the kitchen). You should see the instantaneous power increase **on that one branch channel** and on Channel 0 by approximately the same amount.
>
> - If it appears on **multiple branches**, one of the CTs is on a shared input wire. Go back to [§3.4](01-installation.md#34-install-the-cts-on-the-branch-channels-1-to-15-critical-step-).
> - If it appears on the **wrong branch**, the channel-to-breaker mapping in the UI is wrong. Edit the labels in [§4.5](#45-configure-each-channel).
> - If it appears on **no monitored branch** but on Channel 0, that circuit isn't monitored yet, which is expected if you didn't put a CT on it.

### 4.8 Firmware updates

The device checks for available firmware updates automatically. When a new version is available, a **bell icon (🔔)** appears next to the Firmware Update button in the top navigation bar.

To update:

1. Go to **Update** in the top menu.
2. Follow the on-screen instructions.

> **ⓘ NOTE: Community devices**  
> Automatic update notifications require cloud services to be enabled. For community (self-built) devices, updates are always manual: download the latest firmware binary from [GitHub Releases](https://github.com/jibrilsharafi/EnergyMe-Home/releases) and upload it via the Update page.

### 4.9 Integrations

*EnergyMe Home* supports several integration options: Custom MQTT, InfluxDB, REST API, Modbus TCP, and mDNS service discovery. All configuration and documentation for these is available directly in the web interface under **Integrations** in the top menu; the page is self-contained and includes everything you need to connect to third-party platforms.

---

**Previous:** [← Installation](01-installation.md)  
**Next:** [Troubleshooting →](03-troubleshooting.md)
