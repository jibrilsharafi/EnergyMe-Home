# Software Configuration

Now that your EnergyMe device is physically installed, let's configure it through the web interface.

## Step 1: Connect to EnergyMe WiFi

First, ensure the LED is quickly blinking blue, indicating it is in WiFi configuration mode. If it is pulsating blue, it means it is trying to connect to a previously configured WiFi network.

### Using Smartphone (Recommended)

1. **Scan the QR code** on your EnergyMe - Home case
2. Your phone will automatically connect to the EnergyMe WiFi network
3. A configuration page should open automatically

### Manual Connection

![WiFi name](../media/software/wifi-name.png)

If scanning doesn't work:

1. Open WiFi settings on your smartphone or computer
2. Look for a network named **EnergyMe-XXXXXX** (XXXXXX = unique ID)
3. Connect to this network
4. Open a web browser and navigate to: `http://192.168.4.1` (if the configuration page doesn't open automatically)

## Step 2: Configure WiFi

Once connected to the EnergyMe network:

1. The WiFi configuration page will appear
2. **Select your home WiFi network** from the list
3. **Enter your WiFi password**
4. Click **Save**

![WiFi connection](../media/software/wifi-connection.png)
![WiFi credentials](../media/software/wifi-add-credentials.png)

The device will restart and connect to your home network. Wait about 30 seconds.

### Status LED Indicator

- **Red/Flashing**: Connecting to WiFi
- **Green**: Successfully connected

## Step 3: Access the Web Interface

Now that EnergyMe is connected to your home network, you can access its web interface.

### Option 1: Using mDNS (Easiest)

Open a web browser and go to: `http://energyme.local`

### Option 2: Using IP Address

If `energyme.local` doesn't work:

1. Find the device's IP address in your router's connected devices list
2. Look for a device named "EnergyMe" or "ESP32"
3. Use that IP address in your browser (e.g., `http://192.168.1.123`)

## Step 4: Login

**Default credentials:**

- **Username**: `admin`
- **Password**: `energyme`

🔒 **Important**: Change the password immediately after first login in the Configuration page!

## Step 5: Configure Channels

1. Go to **Configuration** → **Channel**
2. You'll see a list of all 17 channels (0-16)

![Login screen](../media/software/homepage-navigation-top.png)
![Configuration](../media/software/configuration.png)
![Channel](../media/software/channel.png)

### Enable Active Channels

Based on your hardware installation, enable the channels you're using:

- **Channel 0** (Audio 1): 50A CT - Main circuit - ✅ Check **Active**
- **Channel 1** (Audio 2): 30A CT #1 - ✅ Check **Active**
- **Channel 5** (Audio 6): 30A CT #2 - ✅ Check **Active**
- **Channel 6** (Audio 7): 30A CT #3 - ✅ Check **Active**

Leave Channels 2, 3, 4, and 7 **inactive** (unchecked).

### Customize Channel Labels

Give each channel a meaningful name:

- **Channel 0**: "Main" or "Total"
- **Channel 1**: "Kitchen"
- **Channel 5**: "Living Room"
- **Channel 6**: "HVAC"

Choose labels that match your actual installation.

### Fix Negative Values

If you see negative power values for any channel:

- ✅ Enable the **Reverse** checkbox for that channel
- This reverses the polarity calculation

### Save Configuration

The data is saved automatically when you make changes.

## ✅ Configuration Complete

Congratulations! Your EnergyMe system is now fully operational.

You can now:

- Monitor real-time power consumption
- Track energy usage by circuit
- Access the web interface from any device on your network
- Export data for analysis

---

## Troubleshooting

### Can't connect to energyme.local

- Try using the IP address instead
- Check that your device is on the same network
- Some networks don't support mDNS - use IP address

### Negative power values

- Enable the **Reverse** checkbox for affected channels
- This is normal if the CT was installed "backwards"

### No readings on a channel

- Check that the channel is marked as **Active**
- Verify the CT is fully closed around the cable
- Verify the jack is firmly connected
- Try a different channel to test if the CT is working

---

## Need Help?

- **Documentation**: [github.com/jibrilsharafi/EnergyMe-Home](https://github.com/jibrilsharafi/EnergyMe-Home)
- **Project updates**: [energyme.net](https://energyme.net)
- **Support**: <jibril.sharafi@gmail.com>

**Thank you for choosing EnergyMe!** 🎉
