#!/usr/bin/env python3
"""
OTA Updater for EnergyMe-Home
Automates firmware upload with version management and status monitoring
"""

import os
import sys
import time
import hashlib
import argparse
import requests
import json
import re
from datetime import datetime
from pathlib import Path
from requests.auth import HTTPDigestAuth


class OTAUpdater:
    def __init__(self, host, username="admin", password="energyme"):
        self.host = host
        self.base_url = f"http://{host}"
        self.auth = HTTPDigestAuth(username, password)
        self.session = requests.Session()
        self.session.auth = self.auth
        
    def get_firmware_version(self, constants_file="include/constants.h"):
        """Extract firmware version from constants.h"""
        try:
            with open(constants_file, 'r') as f:
                content = f.read()
            
            # Extract version components
            major_match = re.search(r'#define FIRMWARE_BUILD_VERSION_MAJOR "(\d+)"', content)
            minor_match = re.search(r'#define FIRMWARE_BUILD_VERSION_MINOR "(\d+)"', content)
            patch_match = re.search(r'#define FIRMWARE_BUILD_VERSION_PATCH "(\d+)"', content)
            
            if not major_match or not minor_match or not patch_match:
                raise ValueError("Could not parse version from constants.h")
                
            version = f"{major_match.group(1)}.{minor_match.group(1)}.{patch_match.group(1)}"
            return version
            
        except Exception as e:
            print(f"❌ Error reading firmware version: {e}")
            return None
    
    def calculate_md5(self, file_path):
        """Calculate MD5 hash of a file"""
        hash_md5 = hashlib.md5()
        try:
            with open(file_path, "rb") as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    hash_md5.update(chunk)
            return hash_md5.hexdigest()
        except Exception as e:
            print(f"❌ Error calculating MD5: {e}")
            return None
    
    def calculate_sha256(self, file_path):
        """Calculate SHA256 hash of a file"""
        hash_sha256 = hashlib.sha256()
        try:
            with open(file_path, "rb") as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    hash_sha256.update(chunk)
            return hash_sha256.hexdigest()
        except Exception as e:
            print(f"❌ Error calculating SHA256: {e}")
            return None
    
    def get_ota_status(self, silent=False):
        """Get current OTA status"""
        try:
            response = self.session.get(f"{self.base_url}/api/v1/ota/status", timeout=10)
            response.raise_for_status()
            return response.json()
        except Exception as e:
            if not silent:
                print(f"⚠️ Error getting OTA status: {e}")
            return None
    
    def monitor_ota_progress(self, poll_interval=2, max_wait=120):
        """Monitor OTA upload progress with visual progress bar and speed"""
        print("📊 Monitoring OTA progress...")
        
        start_time = time.time()
        last_progress = -1
        last_size = 0
        last_time = start_time
        upload_start_time = None
        bar_length = 25  # Initialize bar_length at the start
        
        while time.time() - start_time < max_wait:
            status = self.get_ota_status(silent=True)
            if not status:
                time.sleep(poll_interval)
                continue
            
            progress = status.get('progressPercent', 0)
            ota_status = status.get('status', 'idle')
            size = status.get('size', 0)
            remaining = status.get('remaining', 0)
            
            # Track when upload actually starts
            if ota_status != 'idle' and upload_start_time is None:
                upload_start_time = time.time()
                last_time = upload_start_time
                last_size = size - remaining if remaining > 0 else 0
            
            # Only print when status is not idle and progress changed
            if ota_status != 'idle' and progress != last_progress:
                current_time = time.time()
                current_uploaded = size - remaining if remaining > 0 else 0
                
                # Calculate speed
                time_diff = current_time - last_time
                size_diff = current_uploaded - last_size
                
                if time_diff > 0 and size_diff > 0:
                    speed_bps = size_diff / time_diff  # bytes per second
                    speed_kbps = speed_bps / 1024  # KB/s
                    
                    if speed_kbps >= 1024:
                        speed_text = f"{speed_kbps/1024:.1f}MB/s"
                    else:
                        speed_text = f"{speed_kbps:.1f}KB/s"
                else:
                    speed_bps = 0
                    speed_text = "0KB/s"
                
                # Estimate time to end
                if speed_bps > 0 and remaining > 0:
                    eta_sec = remaining / speed_bps
                    if eta_sec > 60:
                        eta_text = f"{int(eta_sec // 60)}m {int(eta_sec % 60)}s"
                    else:
                        eta_text = f"{int(eta_sec)}s"
                else:
                    eta_text = "--"
                
                # Create progress bar
                filled_length = int(bar_length * progress // 100)
                bar = '█' * filled_length + '░' * (bar_length - filled_length)
                
                # Format size info
                size_mb = size / 1024 / 1024 if size > 0 else 0
                remaining_mb = remaining / 1024 / 1024 if remaining > 0 else 0
                
                print(f"\r🔄 [{bar}] {int(progress):3d}% | {size_mb:.1f}MB | {remaining_mb:.1f}MB left | {speed_text} | ETA: {eta_text}", 
                    end='', flush=True)
                
                last_progress = progress
                last_size = current_uploaded
                last_time = current_time
            
            if ota_status == 'idle' and progress >= 100:
                # Calculate average speed for the entire upload
                if upload_start_time and size > 0:
                    total_time = time.time() - upload_start_time
                    avg_speed_bps = size / total_time
                    if avg_speed_bps >= 1024 * 1024:
                        avg_speed_text = f"{avg_speed_bps/(1024*1024):.1f}MB/s"
                    else:
                        avg_speed_text = f"{avg_speed_bps/1024:.1f}KB/s"
                    
                    bar = '█' * bar_length
                    print(f"\r✅ [{bar}] 100% | Upload completed! Avg: {avg_speed_text}                    ")
                else:
                    bar = '█' * bar_length
                    print(f"\r✅ [{bar}] 100% | Upload completed!                    ")
                break
            elif ota_status == 'idle' and last_progress > 0:
                # Upload finished
                bar = '█' * bar_length
                print(f"\r✅ [{bar}] 100% | Upload completed!                    ")
                break
                
            time.sleep(poll_interval)
        
        # Timeout check
        if time.time() - start_time >= max_wait:
            print(f"\r⚠️  Upload monitoring timeout after {max_wait}s")
            return False
            
        return True
    
    def wait_for_device_reboot(self, max_wait=60, check_interval=2):
        """Wait for device to reboot and come back online"""
        print("\n🔄 Device is rebooting", end='', flush=True)
        
        # Wait for device to go offline (it should be quick)
        start_time = time.time()
        while time.time() - start_time < 10:  # Wait max 10 seconds for offline
            try:
                response = self.session.get(f"{self.base_url}/api/v1/ota/status", timeout=2)
                if response.status_code != 200:
                    break
            except:
                break  # Device is offline
            print(".", end='', flush=True)
            time.sleep(0.5)
        
        # Now wait for device to come back online
        start_time = time.time()
        while time.time() - start_time < max_wait:
            try:
                response = self.session.get(f"{self.base_url}/api/v1/ota/status", timeout=3)
                if response.status_code == 200:
                    print(" 🟢 Device is back online!")
                    return True
            except:
                pass  # Still offline, expected during reboot
            
            print(".", end='', flush=True)
            time.sleep(check_interval)
        
        print(" 🔴 Device did not come back online within timeout")
        return False
    
    def create_release_directory(self, version, timestamp):
        """Create release directory structure"""
        release_dir = Path(f"releases/{timestamp}")
        release_dir.mkdir(parents=True, exist_ok=True)
        return release_dir
    
    def save_release_files(self, bin_path, elf_path, version, md5_hash, sha256_hash, release_dir):
        """Save .bin and .elf files with version and hash info"""
        try:
            # Copy .bin file with version and MD5
            bin_filename = f"energyme_home_{version.replace('.', '_')}_{md5_hash[:8]}.bin"
            bin_dest = release_dir / bin_filename
            
            with open(bin_path, 'rb') as src, open(bin_dest, 'wb') as dst:
                dst.write(src.read())
            
            print(f"💾 Saved: {bin_dest}")
            
            # Copy .elf file with version and SHA256
            elf_filename = None
            if elf_path and os.path.exists(elf_path):
                elf_filename = f"energyme_home_{version.replace('.', '_')}_{sha256_hash[:8]}.elf"
                elf_dest = release_dir / elf_filename
                
                with open(elf_path, 'rb') as src, open(elf_dest, 'wb') as dst:
                    dst.write(src.read())
                
                print(f"💾 Saved: {elf_dest}")
            else:
                print(f"⚠️ Warning: ELF file not found at {elf_path}")
                
            # Create metadata file
            metadata = {
                "timestamp": datetime.now().isoformat(),
                "version": version,
                "files": {
                    "firmware": {
                        "filename": bin_filename,
                        "md5": md5_hash,
                        "size": os.path.getsize(bin_path)
                    }
                }
            }
            
            if elf_filename is not None:
                metadata["files"]["debug"] = {
                    "filename": elf_filename,
                    "sha256": sha256_hash,
                    "size": os.path.getsize(elf_path)
                }
            
            metadata_file = release_dir / "metadata.json"
            with open(metadata_file, 'w') as f:
                json.dump(metadata, f, indent=2)
            
            print(f"📄 Saved: {metadata_file}")
            
        except Exception as e:
            print(f"❌ Error saving release files: {e}")
    
    def upload_firmware(self, bin_path, expected_md5, monitor_progress=True):
        """Upload firmware with progress monitoring"""
        if not os.path.exists(bin_path):
            print(f"❌ Error: Firmware file not found: {bin_path}")
            return False
            
        print(f"📤 Uploading firmware: {bin_path}")
        print(f"🔑 MD5: {expected_md5}")
        
        try:
            # Prepare upload
            with open(bin_path, 'rb') as f:
                files = {'firmware': ('firmware.bin', f, 'application/octet-stream')}
                headers = {'X-MD5': expected_md5}
                
                print("🚀 Starting firmware upload...")
                
                # Monitor progress during upload if requested
                if monitor_progress:
                    import threading
                    
                    # Start progress monitoring in background
                    monitor_thread = threading.Thread(target=self.monitor_ota_progress)
                    monitor_thread.daemon = True
                    monitor_thread.start()
                    
                    # Small delay to let monitoring start
                    time.sleep(1)
                
                # Start upload
                response = self.session.post(
                    f"{self.base_url}/api/v1/ota/upload",
                    files=files,
                    headers=headers,
                    timeout=300  # 5 minute timeout
                )
                
                response.raise_for_status()
                result = response.json()
                
                if result.get('success'):
                    print(f"\n✅ Upload successful: {result.get('message', 'No message')}")
                    return True
                else:
                    print(f"\n❌ Upload failed: {result.get('message', 'Unknown error')}")
                    return False
                    
        except requests.exceptions.RequestException as e:
            print(f"\n❌ Upload failed: {e}")
            return False
    
    def full_update(self, bin_path, elf_path, save_release=True):
        """Complete OTA update process with release management"""
        
        print("🚀 Starting EnergyMe-Home OTA Update")
        print("=" * 50)
        
        # Get firmware version
        version = self.get_firmware_version()
        if not version:
            print("❌ Could not determine firmware version")
            return False
            
        print(f"Firmware Version: {version}")
        
        # Calculate hashes
        print("🔍 Calculating file hashes...")
        md5_hash = self.calculate_md5(bin_path)
        sha256_hash = self.calculate_sha256(elf_path) if elf_path else None
        
        if not md5_hash:
            return False
            
        print(f"📋 Binary MD5: {md5_hash}")
        if sha256_hash:
            print(f"📋 ELF SHA256: {sha256_hash}")
        
        # Create release directory if requested
        release_dir = None
        if save_release:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            release_dir = self.create_release_directory(version, timestamp)
            print(f"📁 Release directory: {release_dir}")
        
        # Get current status
        print(f"\n📊 Current OTA Status:")
        current_status = self.get_ota_status()
        if current_status:
            print(f"  📌 Current Version: {current_status.get('currentVersion', 'Unknown')}")
            print(f"  📊 Status: {current_status.get('status', 'Unknown')}")
            print(f"  🔑 Current MD5: {current_status.get('currentMD5', 'Unknown')}")
        
        # Upload firmware
        print(f"\n🌐 Uploading firmware to {self.host}...")
        success = self.upload_firmware(bin_path, md5_hash, monitor_progress=True)
        
        if success:
            # Wait for device to reboot
            print("\n⏳ Waiting for device to reboot...")
            reboot_success = self.wait_for_device_reboot()
            
            if reboot_success:
                # Verify the update
                print(f"\n🔍 Verifying firmware update...")
                final_status = self.get_ota_status()
                if final_status:
                    new_version = final_status.get('currentVersion', '')
                    new_md5 = final_status.get('currentMD5', '')
                    
                    print(f"  📌 New Version: {new_version}")
                    print(f"  📊 Status: {final_status.get('status', 'Unknown')}")
                    print(f"  🔑 New MD5: {new_md5}")
                    
                    # Verify version and MD5
                    version_match = new_version == version
                    md5_match = new_md5.lower() == md5_hash.lower()
                    
                    if version_match and md5_match:
                        print(f"  ✅ Version verification: PASSED ({version})")
                        print(f"  ✅ MD5 verification: PASSED")
                        verification_success = True
                    else:
                        if not version_match:
                            print(f"  ❌ Version verification: FAILED (expected {version}, got {new_version})")
                        if not md5_match:
                            print(f"  ❌ MD5 verification: FAILED (expected {md5_hash}, got {new_md5})")
                        verification_success = False
                        success = False
                else:
                    print("  ⚠️ Could not get final status for verification")
                    verification_success = False
        
        if success and save_release and release_dir:
            print(f"\n💾 Saving release files...")
            self.save_release_files(bin_path, elf_path, version, md5_hash, sha256_hash, release_dir)
        
        print("=" * 50)
        if success:
            print("🎉 OTA Update completed successfully!")
        else:
            print("💥 OTA Update failed!")
            
        return success


def main():
    parser = argparse.ArgumentParser(description='EnergyMe-Home OTA Updater')
    parser.add_argument('-H', '--host', default='energyme.local', help='Device IP address (e.g., 192.168.1.245). If not passed, it will use the default hostname energyme.local (WARNING: it will be slower and may cause problems)')
    parser.add_argument('-u', '--username', default='admin', help='Username (default: admin)')
    parser.add_argument('-p', '--password', default='energyme', help='Password (default: energyme)')
    parser.add_argument('-env', '--environment', default='dev', 
                       choices=['dev', 'dev-nosecrets', 'prod', 'prod-nosecrets'],
                       help='Build environment (default: dev)')
    parser.add_argument('-b', '--bin', 
                       help='Path to firmware .bin file (overrides --environment)')
    parser.add_argument('-e', '--elf', 
                       help='Path to firmware .elf file (overrides --environment)')
    parser.add_argument('--no-save', action='store_true', 
                       help='Skip saving release files')
    parser.add_argument('--status-only', action='store_true', 
                       help='Only check OTA status')
    
    args = parser.parse_args()
    
    # Determine bin and elf paths based on environment if not explicitly provided
    if not args.bin or not args.elf:
        env_map = {
            'dev': 'esp32s3-dev',
            'dev-nosecrets': 'esp32s3-dev-nosecrets',
            'prod': 'esp32s3-prod',
            'prod-nosecrets': 'esp32s3-prod-nosecrets'
        }
        env_name = env_map[args.environment]
        
        if not args.bin:
            args.bin = f'.pio/build/{env_name}/firmware.bin'
        if not args.elf:
            args.elf = f'.pio/build/{env_name}/firmware.elf'
    
    print(f"🔧 Environment: {args.environment}")
    print(f"📦 Binary: {args.bin}")
    print(f"🐛 Debug: {args.elf}")
    print()
    
    updater = OTAUpdater(args.host, args.username, args.password)
    
    if args.status_only:
        print("📊 Getting OTA Status...")
        status = updater.get_ota_status()
        if status:
            print(json.dumps(status, indent=2))
        else:
            print("❌ Failed to get status")
        return
    
    # Perform full update
    success = updater.full_update(
        bin_path=args.bin,
        elf_path=args.elf,
        save_release=not args.no_save
    )
    
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
