# Electrical Installation

⚠️ **WARNING: This section involves working with electrical power. If you are not comfortable or experienced with electrical work, please consult a qualified electrician.**

## Safety Precautions

🔴 **BEFORE YOU START**:

- **Turn off the main breaker** - Disconnect power from the entire electrical panel
- **Verify power is off** - Use a voltage tester to confirm
- **Do not turn power back on** until ALL installation steps are complete
- **Keep children and pets away** from the work area

### Phase and Neutral Connection

It is **essential** to connect the terminals correctly:

- **PHASE (L)**: Left terminal (outermost) - Brown, Black, or Gray wire
- **NEUTRAL (N)**: Right terminal - Blue wire

⚠️ **An incorrect connection may damage the device or cause electrical hazards.**

## Step 1: Mount in the Electrical Panel

1. Locate a free space on your DIN rail in the electrical panel (3 modules required, or 54mm of width)
2. Hook the EnergyMe device onto the DIN rail
3. Press down until it clicks into place

## Step 2: Install Current Transformers

![CT clamp open](../media/installation/ct-clamp-open.jpg) ![CT clamp closed](../media/installation/ct-clamp-closed.jpg)

### Install the 50A CT (Main Circuit)

1. Open the 50A current transformer by squeezing the release tabs
2. Position it around the **main circuit cable**
3. Close it until you hear a **click** - ensure it's fully closed
4. The jack should already be connected to Audio 1 (Channel 0) from the assembly step

### Install the 30A CTs (Individual Circuits)

1. Open each 30A current transformer
2. Position them around the cables of circuits you want to monitor (e.g., kitchen, living room, HVAC)
3. Close each one until you hear a **click**
4. The jacks should already be connected to Audio 2, Audio 6, and Audio 7

**Important**: Make sure all CTs are fully closed before proceeding.

## Step 3: Connect Power Supply

![Power terminals](../media/installation/power-terminals.jpg)

Now you'll connect the EnergyMe device to mains power:

1. Identify your **phase wire** (L) - usually brown, black, or gray
2. Identify your **neutral wire** (N) - usually blue
3. Connect them to the terminals:
   - **Left terminal (L)**: Phase wire
   - **Right terminal (N)**: Neutral wire
4. Tighten the terminal screws securely

⚠️ **Double-check the connections before proceeding!**

## Step 4: Install Terminal Covers

![Terminal cover top](../media/installation/terminal-cover-top.jpg)

Install the upper terminal cover.

![Terminal cover bottom](../media/installation/terminal-cover-bottom.jpg)

Install the lower terminal cover.

## Step 5: Install Transparent Window

![Front panel](../media/installation/front-panel.jpg)

Place the transparent window to complete the installation.

## Step 6: Restore Power

1. **Verify all connections one final time**
2. **Ensure all CTs are fully closed**
3. **Ensure terminal covers are installed**
4. Turn the main breaker back on

### Status LED

Look through the transparent window at the LED on the device:

- **Various colours**: Device is starting up
- Pulsing **blue**: Device is waiting for WiFi credentials
- Solid **green**: Device is ready (after WiFi configuration)

---

## ✅ Electrical Installation Complete

Your EnergyMe device is now physically installed and powered. Now it is time to configure the WiFi and set up monitoring channels through the web interface.

**Next Step**: [Software Configuration](03-software-configuration.md)

---

## Troubleshooting

**LED not turning on?**

- Check that the main breaker is on
- Verify phase and neutral connections are correct and secure
- Check that terminal screws are tight

**LED does not become blue or green?**

- This is normal during first boot
- Continue to software configuration to set up WiFi
- If that does not work, reboot the device by turning off and on the main breaker or carefully pressing the RST button with a non-conductive tool (e.g. a plastic pen)

---

**Need help?** Contact: <jibril.sharafi@gmail.com>
