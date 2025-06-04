import os
import shutil
import sys
import hashlib
import requests
from datetime import datetime, timedelta
import time
import getpass

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
    """Authenticate with the device and return the access token"""
    print_info(f"Connecting to device at {ip_address}...")
    url = f'http://{ip_address}/rest/auth/login'
    try:
        response = requests.post(
            url, 
            json={'password': password},
            headers={'Accept': 'application/json', 'Content-Type': 'application/json'}
        )
        response.raise_for_status()
        data = response.json()
        token = data.get('token')
        if token:
            print_success("Authentication successful! 🔐")
        return token
    except requests.exceptions.RequestException as e:
        print_error(f"Authentication failed: {e}")
        if hasattr(e, 'response') and e.response is not None:
            print_error(f"Response: {e.response.text}")
        return None

def calculate_md5(file_path):
    md5_hash = hashlib.md5()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            md5_hash.update(chunk)
    return md5_hash.hexdigest()

def set_md5(ip_address, md5, token):
    url = f'http://{ip_address}/set-md5'
    try:
        headers = {
            'Accept': 'application/json',
            'Authorization': f'Bearer {token}'
        }
        response = requests.get(url, params={'md5': md5}, headers=headers)
        response.raise_for_status()
        print_success(f"MD5 checksum set: {md5[:8]}...{md5[-8:]} 🔑")
        return True
    except requests.exceptions.RequestException as e:
        print_error(f"Error setting MD5: {e}")
        if hasattr(e, 'response') and e.response is not None:
            print_error(f"Response: {e.response.text}")
        return False

def upload_firmware(ip_address, md5, firmware_path, token):
    url = f'http://{ip_address}/do-update'
    try:
        with open(firmware_path, 'rb') as f:
            files = {'file': f}
            headers = {
                'Accept': 'application/json',
                'Authorization': f'Bearer {token}'
            }
            response = requests.post(
                url,
                params={'md5': md5},
                files=files,
                headers=headers
            )
            response.raise_for_status()
        print_success("Firmware uploaded successfully! 📤")
        return True
    except requests.exceptions.RequestException as e:
        print_error(f"Error uploading firmware: {e}")
        if hasattr(e, 'response') and e.response is not None:
            print_error(f"Response: {e.response.text}")
        return False

def get_user_confirmation(prompt):
    while True:
        response = input(f"{Colors.WARNING}❓ {prompt} (y/n): {Colors.ENDC}").lower()
        if response in ['y', 'yes']:
            return True
        elif response in ['n', 'no']:
            return False
        print_warning("Please enter 'y' or 'n'")

def main():
    print_header("EnergyMe Firmware Release Tool")
    
    if len(sys.argv) not in [4, 5]:
        print_error("Usage: python set_release.py <major> <minor> <patch> [ip_address]")
        sys.exit(1)

    try:
        major = int(sys.argv[1])
        minor = int(sys.argv[2])
        patch = int(sys.argv[3])
    except ValueError:
        print_error("Invalid version numbers. Please provide integers for major, minor, and patch.")
        sys.exit(1)

    ip_address = sys.argv[4] if len(sys.argv) == 5 else None

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
        if not get_user_confirmation("Do you want to continue with this old firmware?"):
            print_info("Operation cancelled by user.")
            sys.exit(0)

    firmware_dst = os.path.join('releases', next_version)

    print_step(2, f"Preparing release v{major}.{minor}.{patch}")
    # Check if release already exists
    if os.path.exists(firmware_dst):
        if not get_user_confirmation(f"Release {next_version} already exists. Do you want to replace it?"):
            print_info("Operation cancelled by user.")
            sys.exit(0)    
            
    os.makedirs('releases', exist_ok=True)
    shutil.copy(firmware_src, firmware_dst)
    print_success(f"Firmware copied to releases/{next_version} 📦")

    if ip_address:
        print_step(3, f"Uploading to device at {ip_address}")
        
        # Get password for authentication
        password = getpass.getpass(f"{Colors.OKCYAN}🔐 Enter device password: {Colors.ENDC}")
        
        # Login and get token
        token = login(ip_address, password)
        if not token:
            print_error("Failed to authenticate with device")
            sys.exit(1)
        
        md5 = calculate_md5(firmware_dst)
        print_info(f"Firmware MD5: {md5}")
        
        if set_md5(ip_address, md5, token):
            start_time = time.time()
            file_size = os.path.getsize(firmware_dst) / 1024  # In KB
            print_info(f"Starting upload... ({file_size:.1f} KB)")
            
            if upload_firmware(ip_address, md5, firmware_dst, token):
                duration = time.time() - start_time
                speed = file_size / duration if duration > 0 else 0
                print_success(f"Update completed in {duration:.2f}s ({speed:.1f} KB/s) 🎉")
            else:
                print_error("Failed to upload firmware")
        else:
            print_error("Failed to set MD5")
    else:
        print_success("Firmware release created successfully! 🎊")
        print_info("To upload to device, run with: python set_release.py <major> <minor> <patch> <ip_address>")

if __name__ == "__main__":
    main()