# Multiple EnergyMe Dashboard

Simple PHP dashboard to aggregate and display power consumption from multiple EnergyMe devices with auto-refresh every 2 seconds.

## Setup

1. Copy files to your web server
2. Edit `meter_ajax.php`:
   - Replace `<IP of 1st EnergyMe>` and `<IP of 2nd EnergyMe>` with your device IPs
   - Update username/password if changed from defaults
3. Open `index.php` in your browser

## Requirements

- PHP with cURL extension
- Access to EnergyMe devices on local network

## Customization

**Add more devices:** Edit `meter_ajax.php` to add more API calls and update the JSON response

**Change threshold:** Edit the `3000` value in `index.php` (line with `data.totale > 3000`)

**Monitor different channels:** Change `?index=0` to `?index=1` (or other channel) in the API URL
