import os
import shutil
import sys
import hashlib
import requests
import requests.auth
from datetime import datetime, timedelta
import time
import getpass
import re
from dotenv import load_dotenv

# JUST USE curl -X POST -F "data=@.pio/build/esp32dev/firmware.bin" http://energyme.local/api/v1/ota/upload --digest -u admin:energyme -H "X-MD5: $(md5sum .pio/build/esp32dev/firmware.bin | cut -d' ' -f1)"

"""
EnergyMe Firmware Release Tool

Environment Variables:
- ENERGYME_IP_ADDRESS: IP address of the device
- ENERGYME_USERNAME: Username for authentication (default: admin)
- ENERGYME_PASSWORD: Password for authentication
- ENERGYME_FORCE_UPDATE: Set to '1' to skip confirmations

Usage:
    python set_release.py                          # Use version from constants.h
    python set_release.py [major] [minor] [patch]  # Override version
    
Environment Variables (from .env file):
    ENERGYME_IP_ADDRESS: IP address of the device
    ENERGYME_USERNAME: Username for authentication (default: admin)  
    ENERGYME_PASSWORD: Password for authentication
    ENERGYME_FORCE_UPDATE: Set to '1' to skip confirmations
    
Examples:
    python set_release.py                          # Create release using constants.h version
    python set_release.py 1 0 5                   # Create release v1.0.5
"""

# ANSI color codes for colorized output
class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'

def print_header(text):
    """Print a header with formatting"""
    print(f"\n{Colors.HEADER}{Colors.BOLD}{'='*60}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}🚀 {text}{Colors.ENDC}")
    print(f"{Colors.HEADER}{Colors.BOLD}{'='*60}{Colors.ENDC}")

def print_step(step_num, text):
    """Print a step with formatting"""
    print(f"\n{Colors.OKBLUE}{Colors.BOLD}📋 Step {step_num}: {text}{Colors.ENDC}")

def print_success(text):
    """Print success message"""
    print(f"{Colors.OKGREEN}✅ {text}{Colors.ENDC}")

def print_warning(text):
    """Print warning message"""
    print(f"{Colors.WARNING}⚠️  {text}{Colors.ENDC}")

def print_error(text):
    """Print error message"""
    print(f"{Colors.FAIL}❌ {text}{Colors.ENDC}")

def print_info(text):
    """Print info message"""
    print(f"{Colors.OKCYAN}ℹ️  {text}{Colors.ENDC}")

def login(ip_address, password):
    """Authenticate with the device using digest auth (no longer needed for token-based auth)"""
    print_info(f"Using digest authentication for {ip_address}...")
    # With the new implementation, we use digest auth directly, no token needed
    return True

def check_ota_status(ip_address, username, password):
    """Check OTA status"""
    url = f'http://{ip_address}/api/v1/ota/status'
    try:
        response = requests.get(
            url,
            auth=requests.auth.HTTPDigestAuth(username, password),
            headers={'Accept': 'application/json'},
            timeout=10
        )
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        print_error(f"Error checking OTA status: {e}")
        return None

def upload_firmware_new_api(ip_address, firmware_path, md5, username, password):
    """Upload firmware using the new API with integrated MD5"""
    url = f'http://{ip_address}/api/v1/ota/upload'
    try:
        with open(firmware_path, 'rb') as f:
            files = {'data': f}
            headers = {
                'Accept': 'application/json',
                'X-MD5': md5
            }
            
            print_info("Starting firmware upload...")
            response = requests.post(
                url,
                files=files,
                headers=headers,
                auth=requests.auth.HTTPDigestAuth(username, password),
                timeout=300  # 5 minutes timeout for upload
            )
            response.raise_for_status()
            result = response.json()
            
            if result.get('success'):
                print_success("Firmware uploaded successfully! �")
                print_info(f"Device MD5: {result.get('md5', 'N/A')}")
                return True
            else:
                print_error(f"Upload failed: {result.get('message', 'Unknown error')}")
                return False
                
    except requests.exceptions.RequestException as e:
        print_error(f"Error uploading firmware: {e}")
        if hasattr(e, 'response') and e.response is not None:
            try:
                error_data = e.response.json()
                print_error(f"Server response: {error_data.get('message', e.response.text)}")
            except:
                print_error(f"Response: {e.response.text}")
        return False

def monitor_ota_progress(ip_address, username, password, max_wait_time=60):
    """Monitor OTA progress in real-time"""
    print_info("Monitoring OTA progress...")
    start_time = time.time()
    last_progress = -1
    ota_started = False
    
    while time.time() - start_time < max_wait_time:
        status_data = check_ota_status(ip_address, username, password)
        if not status_data:
            time.sleep(2)
            continue
            
        status = status_data.get('status', 'unknown')
        
        if status == 'running':
            if not ota_started:
                print_success("OTA process started! 🚀")
                ota_started = True
                
            progress = status_data.get('progressPercent', 0)
            size = status_data.get('size', 0)
            uploaded = status_data.get('progress', 0)
            
            if progress != last_progress:
                print(f"\r{Colors.OKCYAN}📤 OTA progress: {progress:.1f}% ({uploaded}/{size} bytes){Colors.ENDC}", end='', flush=True)
                last_progress = progress
                
        elif status == 'idle':
            if 'lastError' in status_data and status_data['lastError']:
                print(f"\n{Colors.FAIL}❌ OTA failed: {status_data['lastError']}{Colors.ENDC}")
                return False
            elif ota_started:
                print(f"\n{Colors.OKGREEN}✅ OTA completed successfully!{Colors.ENDC}")
                return True
            else:
                # Still waiting for OTA to start
                print(".", end='', flush=True)
        
        time.sleep(1)
    
    if not ota_started:
        print(f"\n{Colors.WARNING}⚠️  Timeout waiting for OTA to start{Colors.ENDC}")
    else:
        print(f"\n{Colors.WARNING}⚠️  Timeout waiting for OTA completion{Colors.ENDC}")
    return False

def calculate_md5(file_path):
    """Calculate MD5 hash of a file"""
    md5_hash = hashlib.md5()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            md5_hash.update(chunk)
    return md5_hash.hexdigest()

def wait_for_device_restart(ip_address, username, password, max_wait_time=120):
    """Wait for device to restart after firmware update"""
    print_info("Waiting for device to restart...")
    start_time = time.time()
    device_down = False
    
    while time.time() - start_time < max_wait_time:
        try:
            response = requests.get(
                f'http://{ip_address}/api/v1/health',
                auth=requests.auth.HTTPDigestAuth(username, password),
                timeout=5
            )
            if response.status_code == 200:
                if device_down:
                    print_success("Device is back online! 🌐")
                    return True
                else:
                    # Device hasn't restarted yet
                    time.sleep(2)
            else:
                device_down = True
                time.sleep(2)
        except requests.exceptions.RequestException:
            device_down = True
            print(".", end='', flush=True)
            time.sleep(2)
    
    print_error("Timeout waiting for device to restart")
    return False

def verify_firmware_update(ip_address, expected_md5, username, password):
    """Verify that the firmware was updated successfully"""
    print_info("Verifying firmware update...")
    try:
        status_data = check_ota_status(ip_address, username, password)
        if status_data:
            current_md5 = status_data.get('currentMD5', '')
            firmware_state = status_data.get('firmwareState', '')
            current_version = status_data.get('currentVersion', '')
            
            print_info(f"Current firmware version: {current_version}")
            print_info(f"Current firmware MD5: {current_md5}")
            print_info(f"Firmware state: {firmware_state}")
            
            if current_md5.lower() == expected_md5.lower():
                print_success("Firmware MD5 verified! ✅")
                if firmware_state == "NEW_TO_TEST":
                    print_info("Firmware is in testing state - waiting for stability verification")
                return True
            else:
                print_warning(f"MD5 mismatch! Expected: {expected_md5}, Got: {current_md5}")
                return False
        else:
            print_error("Could not verify firmware update")
            return False
    except Exception as e:
        print_error(f"Error verifying firmware: {e}")
        return False

def get_user_confirmation(prompt):
    while True:
        response = input(f"{Colors.WARNING}❓ {prompt} (y/n): {Colors.ENDC}").lower()
        if response in ['y', 'yes']:
            return True
        elif response in ['n', 'no']:
            return False
        print_warning("Please enter 'y' or 'n'")

def parse_version_from_constants():
    """Parse version information from constants.h file"""
    constants_path = os.path.join('include', 'constants.h')
    if not os.path.exists(constants_path):
        print_error(f"Constants file {constants_path} not found")
        return None, None, None
    
    try:
        with open(constants_path, 'r') as f:
            content = f.read()
        
        # Extract version numbers using regex
        major_match = re.search(r'#define\s+FIRMWARE_BUILD_VERSION_MAJOR\s+"(\d+)"', content)
        minor_match = re.search(r'#define\s+FIRMWARE_BUILD_VERSION_MINOR\s+"(\d+)"', content)
        patch_match = re.search(r'#define\s+FIRMWARE_BUILD_VERSION_PATCH\s+"(\d+)"', content)
        
        if major_match and minor_match and patch_match:
            major = int(major_match.group(1))
            minor = int(minor_match.group(1))
            patch = int(patch_match.group(1))
            return major, minor, patch
        else:
            print_error("Could not parse version from constants.h")
            return None, None, None
    except Exception as e:
        print_error(f"Error reading constants.h: {e}")
        return None, None, None

def main():
    print_header("EnergyMe Firmware Release Tool")
    
    # Load environment variables from .env file
    load_dotenv()
    
    # Parse version from constants.h as default
    major, minor, patch = parse_version_from_constants()
    
    if major is None or minor is None or patch is None:
        print_error("Could not parse version from constants.h")
        # Fallback to command line arguments if constants.h parsing fails
        if len(sys.argv) >= 4:
            try:
                major = int(sys.argv[1])
                minor = int(sys.argv[2])
                patch = int(sys.argv[3])
                print_info("Using version from command line arguments (fallback)")
            except ValueError:
                print_error("Invalid version numbers. Please provide integers for major, minor, and patch.")
                sys.exit(1)
        else:
            print_error("Could not determine version. Please ensure constants.h is available or provide version as arguments.")
            print_error("Usage: python set_release.py [major] [minor] [patch]")
            sys.exit(1)
    else:
        print_info(f"Using version from constants.h: {major}.{minor}.{patch}")
        # Allow command line override of version if provided
        if len(sys.argv) >= 4:
            try:
                major = int(sys.argv[1])
                minor = int(sys.argv[2])
                patch = int(sys.argv[3])
                print_info("Overriding with version from command line arguments")
            except ValueError:
                print_error("Invalid version numbers in command line. Using constants.h version.")

    # Get configuration from environment variables
    ip_address = os.getenv('ENERGYME_IP_ADDRESS')
    username = os.getenv('ENERGYME_USERNAME', 'admin')
    env_password = os.getenv('ENERGYME_PASSWORD')
    force_update = os.getenv('ENERGYME_FORCE_UPDATE', '0').lower() in ['1', 'true', 'yes']
    
    if ip_address:
        print_info(f"Using device configuration from environment: {username}@{ip_address}")
    else:
        print_info("No IP address in environment variables - will only create release file")

    next_version = f"energyme_home_{major:02}_{minor:02}_{patch:02}.bin"
    firmware_src = '.pio/build/esp32dev/firmware.bin'

    print_step(1, "Checking firmware source")
    if not os.path.exists(firmware_src):
        print_error(f"Source file {firmware_src} does not exist.")
        sys.exit(1)

    last_modified_time = os.path.getmtime(firmware_src)
    modified_datetime = datetime.fromtimestamp(last_modified_time)
    readable_time = modified_datetime.strftime('%Y-%m-%d %H:%M:%S')
    print_info(f"Firmware last modified: {readable_time}")

    # Check if firmware is older than 1 day
    if datetime.now() - modified_datetime > timedelta(days=1):
        print_warning("Firmware was last modified more than a day ago.")
        if not force_update and not get_user_confirmation("Do you want to continue with this old firmware?"):
            print_info("Operation cancelled by user.")
            sys.exit(0)

    firmware_dst = os.path.join('releases', next_version)

    print_step(2, f"Preparing release v{major}.{minor}.{patch}")
    # Check if release already exists
    if os.path.exists(firmware_dst):
        if not force_update and not get_user_confirmation(f"Release {next_version} already exists. Do you want to replace it?"):
            print_info("Operation cancelled by user.")
            sys.exit(0)    
            
    os.makedirs('releases', exist_ok=True)
    shutil.copy(firmware_src, firmware_dst)
    print_success(f"Firmware copied to releases/{next_version} 📦")

    if ip_address:
        print_step(3, f"Uploading to device at {ip_address}")
        
        # Get password
        if env_password:
            password = env_password
            print_info("Using password from environment variable")
        else:
            password = getpass.getpass(f"{Colors.OKCYAN}🔐 Enter device password: {Colors.ENDC}")
        
        # Test connection with device
        if not login(ip_address, password):
            print_error("Failed to connect to device")
            sys.exit(1)
        
        md5 = calculate_md5(firmware_dst)
        print_info(f"Firmware MD5: {md5}")
        
        # Check initial OTA status
        initial_status = check_ota_status(ip_address, username, password)
        if initial_status and initial_status.get('status') == 'running':
            print_warning("OTA update already in progress!")
            if not get_user_confirmation("Do you want to continue anyway?"):
                sys.exit(0)
        
        start_time = time.time()
        file_size = os.path.getsize(firmware_dst) / 1024  # In KB
        print_info(f"Starting upload... ({file_size:.1f} KB)")
        
        # Upload firmware using new API
        if upload_firmware_new_api(ip_address, firmware_dst, md5, username, password):
            duration = time.time() - start_time
            speed = file_size / duration if duration > 0 else 0
            print_success(f"Upload completed in {duration:.2f}s ({speed:.1f} KB/s) 🎉")
            
            # Wait a moment for the device to start processing
            print_info("Waiting for OTA process to begin...")
            time.sleep(3)
            
            # Monitor OTA progress
            if monitor_ota_progress(ip_address, username, password):
                # Wait for device restart
                if wait_for_device_restart(ip_address, username, password):
                    # Verify firmware update
                    time.sleep(5)  # Give device a moment to fully boot
                    if verify_firmware_update(ip_address, md5, username, password):
                        print_success("🎊 Firmware update completed and verified successfully!")
                    else:
                        print_warning("Firmware uploaded but verification failed")
                else:
                    print_warning("Firmware uploaded but device restart verification failed")
            else:
                print_error("OTA progress monitoring failed")
        else:
            print_error("Failed to upload firmware")
    else:
        print_success("Firmware release created successfully! 🎊")
        print_info("To upload to device, run with environment variables:")
        print_info("  ENERGYME_IP_ADDRESS=your.device.ip")
        print_info("  ENERGYME_USERNAME=admin") 
        print_info("  ENERGYME_PASSWORD=your_password")
        print_info("Or create a .env file with these values")

if __name__ == "__main__":
    main()