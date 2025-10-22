# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

#!/usr/bin/env python3
"""
EnergyMe-Home Crash Dump Analyzer

This script fetches crash information and core dump data from the EnergyMe-Home device,
then decodes and analyzes the crash dump for debugging purposes. It automatically
searches for the correct ELF file in the releases/ folder based on SHA256 matching.

Usage:
    python crash_dump_analyzer.py <device_ip> [username] [password]

Example:
    python crash_dump_analyzer.py 192.168.1.100 admin secret123
"""

import sys
import json
import base64
import requests
from requests.auth import HTTPDigestAuth
from datetime import datetime
from typing import Optional, Dict, Any
import os
import hashlib


class CrashDumpAnalyzer:
    def __init__(self, device_ip: str, username: Optional[str] = None, password: Optional[str] = None, chunk_size: int = 2048):
        self.device_ip = device_ip
        self.base_url = f"http://{device_ip}"
        self.chunk_size = chunk_size
        self.session = requests.Session()
        
        # Set up authentication if provided
        if username and password:
            self.session.auth = HTTPDigestAuth(username, password)
            print(f"🔐 Using digest authentication for user: {username}")

    def _find_toolchain_addr2line(self) -> Optional[str]:
        """Find the correct xtensa-esp32-elf-addr2line executable."""
        import platform
        import glob
        
        # Common PlatformIO toolchain locations (prioritize the one that worked)
        platformio_paths = [
            os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s2-elf-addr2line*"),
            os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-addr2line*"),
            os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-addr2line*"),
        ]
        
        # Try to find the tool
        for pattern in platformio_paths:
            matches = glob.glob(pattern)
            if matches:
                tool_path = matches[0]  # Take the first match
                print(f"✅ Found toolchain: {tool_path}")
                return tool_path
        
        # Fallback: try common system paths
        if platform.system() == "Windows":
            # Try Windows-specific paths
            win_paths = [
                "C:/Espressif/tools/xtensa-esp32-elf/*/xtensa-esp32-elf/bin/xtensa-esp32-elf-addr2line.exe",
                "C:/Users/*/AppData/Local/Arduino15/packages/esp32/tools/xtensa-esp32-elf-gcc/*/bin/xtensa-esp32-elf-addr2line.exe"
            ]
            for pattern in win_paths:
                matches = glob.glob(pattern)
                if matches:
                    return matches[0]
        
        print("⚠️  Could not find xtensa-esp32-elf-addr2line tool")
        return None

    def _run_debug_command(self, debug_cmd: str) -> Optional[str]:
        """Run the debug command and return the output."""
        try:
            import subprocess
            import platform
            
            print(f"🔍 Running debug command...")
            
            # Handle different operating systems
            if platform.system() == "Windows":
                # On Windows, try to find and use the correct toolchain
                if "xtensa-esp32-elf-addr2line" in debug_cmd:
                    # Extract the command parts
                    parts = debug_cmd.split()
                    if len(parts) > 1:
                        # Find the correct tool
                        tool_path = self._find_toolchain_addr2line()
                        if tool_path:
                            # Replace the tool name with the full path
                            parts[0] = f'"{tool_path}"'  # Quote the path in case of spaces
                            debug_cmd = " ".join(parts)
                        else:
                            print("❌ Could not find addr2line tool")
                            return None
                
                # Run the command directly on Windows
                result = subprocess.run(
                    debug_cmd,
                    shell=True,
                    capture_output=True,
                    text=True
                )
            else:
                # On Unix/Mac, use ESP-IDF environment
                esp_idf_cmd = f". $HOME/esp/esp-idf/export.sh && {debug_cmd}"
                result = subprocess.run(
                    esp_idf_cmd,
                    shell=True,
                    capture_output=True,
                    text=True,
                    executable="/bin/zsh"  # Use zsh for macOS
                )
            
            if result.returncode == 0:
                print("✅ Debug command completed successfully!")
                print(result.stdout)
                return result.stdout
            else:
                print(f"⚠️  Debug command failed with code {result.returncode}")
                if result.stderr:
                    print(f"Error: {result.stderr}")
                return None
                
        except Exception as e:
            print(f"❌ Error running debug command: {e}")
            return None

    def get_crash_info(self) -> Optional[Dict[str, Any]]:
        """Fetch crash information from the device."""
        try:
            print(f"🔍 Fetching crash information from {self.device_ip}...")
            response = self.session.get(f"{self.base_url}/api/v1/crash/info")
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"❌ Error fetching crash info: {e}")
            return None

    def print_crash_info(self, crash_info: Dict[str, Any]) -> Optional[str]:
        """Print crash information in a readable format and return debug output."""
        debug_output = None
        
        print("\n" + "="*80)
        print("🚨 CRASH ANALYSIS REPORT")
        print("="*80)
        
        # Basic crash information
        print(f"Reset Reason: {crash_info.get('resetReason', 'Unknown')}")
        print(f"Reset Code: {crash_info.get('resetReasonCode', 'Unknown')}")
        print(f"Crash Count: {crash_info.get('crashCount', 0)} (consecutive: {crash_info.get('consecutiveCrashCount', 0)})")
        print(f"Reset Count: {crash_info.get('resetCount', 0)} (consecutive: {crash_info.get('consecutiveResetCount', 0)})")
        print(f"Has Core Dump: {'Yes' if crash_info.get('hasCoreDump') else 'No'}")
        
        if crash_info.get('hasCoreDump'):
            print(f"Core Dump Size: {crash_info.get('coreDumpSize', 0):,} bytes")
            print(f"Core Dump Address: 0x{crash_info.get('coreDumpAddress', 0):08x}")
            
            # Task information
            if 'taskName' in crash_info:
                print(f"Crashed Task: {crash_info['taskName']}")
                print(f"Program Counter: 0x{crash_info.get('programCounter', 0):08x}")
                print(f"Task Control Block: 0x{crash_info.get('taskControlBlock', 0):08x}")
            
            # Backtrace information
            backtrace = crash_info.get('backtrace', {})
            if backtrace:
                print(f"\n📍 BACKTRACE INFO:")
                print(f"Depth: {backtrace.get('depth', 0)}")
                print(f"Corrupted: {'Yes' if backtrace.get('corrupted') else 'No'}")
                
                addresses = backtrace.get('addresses', [])
                if addresses:
                    print(f"Addresses: {' '.join([f'0x{addr:08x}' for addr in addresses])}")
                
                debug_cmd = backtrace.get('debugCommand')
                if debug_cmd:
                    print(f"\n🔧 DEBUG COMMAND:")
                    print(f"{debug_cmd}")
                    
                    # Run the debug command
                    debug_output = self._run_debug_command(debug_cmd)
        
        print("="*80)
        return debug_output

    def get_core_dump_chunks(self) -> Optional[bytes]:
        """Fetch all core dump data in chunks and return as bytes."""
        try:
            print(f"\n📥 Fetching core dump data (chunk size: {self.chunk_size} bytes)...")
            
            all_data = bytearray()
            offset = 0
            chunk_count = 0
            
            while True:
                print(f"  📦 Fetching chunk {chunk_count + 1} (offset: {offset:,})...", end="")
                
                response = self.session.get(
                    f"{self.base_url}/api/v1/crash/dump",
                    params={'offset': offset, 'size': self.chunk_size}
                )
                response.raise_for_status()
                
                chunk_data = response.json()
                
                if 'error' in chunk_data:
                    print(f" ❌ Error: {chunk_data['error']}")
                    return None
                
                # Decode base64 data
                encoded_data = chunk_data.get('data', '')
                if not encoded_data:
                    print(" ❌ No data in chunk")
                    break
                
                try:
                    decoded_chunk = base64.b64decode(encoded_data)
                    all_data.extend(decoded_chunk)
                    
                    actual_size = chunk_data.get('actualChunkSize', 0)
                    total_size = chunk_data.get('totalSize', 0)
                    has_more = chunk_data.get('hasMore', False)
                    
                    print(f" ✅ {actual_size} bytes (total: {len(all_data):,}/{total_size:,})")
                    
                    if not has_more:
                        break
                    
                    offset += actual_size
                    chunk_count += 1
                    
                except Exception as e:
                    print(f" ❌ Failed to decode chunk: {e}")
                    return None
            
            print(f"✅ Core dump download complete: {len(all_data):,} bytes")
            return bytes(all_data)
            
        except requests.exceptions.RequestException as e:
            print(f"❌ Error fetching core dump: {e}")
            return None

    def save_crash_dump_text(self, crash_info: Dict[str, Any], debug_output: Optional[str] = None, core_dump_data: Optional[bytes] = None) -> str:
        """Save comprehensive crash dump information to a text file."""
        try:
            # Create coredump directory if it doesn't exist
            dump_dir = "coredump"
            os.makedirs(dump_dir, exist_ok=True)
            
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = os.path.join(dump_dir, f"crash_dump_{self.device_ip}_{timestamp}.txt")
            
            with open(filename, 'w', encoding='utf-8') as f:
                # Write header
                f.write("="*80 + "\n")
                f.write("ENERGYME-HOME CRASH DUMP ANALYSIS REPORT\n")
                f.write("="*80 + "\n")
                f.write(f"Device IP: {self.device_ip}\n")
                f.write(f"Analysis Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
                f.write("="*80 + "\n\n")
                
                # Basic crash information
                f.write("CRASH INFORMATION:\n")
                f.write("-"*40 + "\n")
                f.write(f"Reset Reason: {crash_info.get('resetReason', 'Unknown')}\n")
                f.write(f"Reset Code: {crash_info.get('resetReasonCode', 'Unknown')}\n")
                f.write(f"Crash Count: {crash_info.get('crashCount', 0)} (consecutive: {crash_info.get('consecutiveCrashCount', 0)})\n")
                f.write(f"Reset Count: {crash_info.get('resetCount', 0)} (consecutive: {crash_info.get('consecutiveResetCount', 0)})\n")
                f.write(f"Has Core Dump: {'Yes' if crash_info.get('hasCoreDump') else 'No'}\n")
                
                if crash_info.get('hasCoreDump'):
                    f.write(f"Core Dump Size: {crash_info.get('coreDumpSize', 0):,} bytes\n")
                    f.write(f"Core Dump Address: 0x{crash_info.get('coreDumpAddress', 0):08x}\n")
                    
                    # Task information
                    if 'taskName' in crash_info:
                        f.write(f"Crashed Task: {crash_info['taskName']}\n")
                        f.write(f"Program Counter: 0x{crash_info.get('programCounter', 0):08x}\n")
                        f.write(f"Task Control Block: 0x{crash_info.get('taskControlBlock', 0):08x}\n")
                
                f.write("\n")
                
                # Backtrace information
                backtrace = crash_info.get('backtrace', {})
                if backtrace:
                    f.write("BACKTRACE INFORMATION:\n")
                    f.write("-"*40 + "\n")
                    f.write(f"Depth: {backtrace.get('depth', 0)}\n")
                    f.write(f"Corrupted: {'Yes' if backtrace.get('corrupted') else 'No'}\n")
                    
                    addresses = backtrace.get('addresses', [])
                    if addresses:
                        f.write(f"Addresses: {' '.join([f'0x{addr:08x}' for addr in addresses])}\n")
                    
                    debug_cmd = backtrace.get('debugCommand')
                    if debug_cmd:
                        f.write(f"\nDEBUG COMMAND:\n")
                        f.write(f"{debug_cmd}\n")
                        
                        if debug_output:
                            f.write(f"\nDEBUG OUTPUT:\n")
                            f.write("-"*40 + "\n")
                            f.write(debug_output)
                            f.write("\n")
                
                f.write("\n" + "="*80 + "\n")
                
                # Core dump data (hex dump)
                if core_dump_data:
                    f.write("CORE DUMP DATA (HEX):\n")
                    f.write("="*80 + "\n")
                    
                    # Write hex dump in 16-byte lines
                    for i in range(0, len(core_dump_data), 16):
                        chunk = core_dump_data[i:i+16]
                        hex_bytes = ' '.join(f'{b:02x}' for b in chunk)
                        ascii_chars = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in chunk)
                        f.write(f"{i:08x}: {hex_bytes:<48} |{ascii_chars}|\n")
                    
                    f.write("\n" + "="*80 + "\n")
            
            print(f"💾 Comprehensive crash dump saved to: {filename}")
            return filename
            
        except Exception as e:
            print(f"❌ Error saving crash dump text file: {e}")
            return ""

    def save_core_dump_temp(self, data: bytes) -> str:
        """Save core dump data to a temporary file."""
        try:
            # Create temp directory if it doesn't exist
            temp_dir = "temp"
            os.makedirs(temp_dir, exist_ok=True)
            
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = os.path.join(temp_dir, f"coredump_{self.device_ip}_{timestamp}.bin")
            
            with open(filename, 'wb') as f:
                f.write(data)
            print(f"💾 Core dump saved to temporary file: {filename}")
            return filename
        except Exception as e:
            print(f"❌ Error saving core dump: {e}")
            return ""

    def get_firmware_sha256(self, firmware_path: str) -> Optional[str]:
        """Get SHA256 hash of the firmware file."""
        try:
            with open(firmware_path, 'rb') as f:
                sha256_hash = hashlib.sha256()
                for chunk in iter(lambda: f.read(4096), b""):
                    sha256_hash.update(chunk)
                return sha256_hash.hexdigest()
        except Exception as e:
            print(f"❌ Error calculating firmware SHA256: {e}")
            return None

    def find_matching_elf_in_releases(self, device_sha256: str) -> Optional[str]:
        """Find the matching ELF file in releases folder based on SHA256."""
        releases_dir = "releases"
        
        if not os.path.exists(releases_dir):
            print(f"📁 Releases directory not found: {releases_dir}")
            return None
        
        print(f"🔍 Searching for ELF file matching SHA256: {device_sha256}...")
        
        # Get all release folders
        try:
            release_folders = [f for f in os.listdir(releases_dir) 
                             if os.path.isdir(os.path.join(releases_dir, f))]
            release_folders.sort(reverse=True)  # Most recent first
            
            print(f"📁 Found {len(release_folders)} release folders")
            
            for folder in release_folders:
                folder_path = os.path.join(releases_dir, folder)
                metadata_path = os.path.join(folder_path, "metadata.json")
                
                if not os.path.exists(metadata_path):
                    continue
                
                try:
                    with open(metadata_path, 'r') as f:
                        metadata = json.load(f)
                    
                    # Get SHA256 from metadata
                    debug_info = metadata.get('files', {}).get('debug', {})
                    metadata_sha256 = debug_info.get('sha256', '')
                    elf_filename = debug_info.get('filename', '')
                    
                    if not metadata_sha256 or not elf_filename:
                        continue
                    
                    # Check if device SHA256 matches (partial match)
                    if metadata_sha256.lower().startswith(device_sha256.lower()):
                        elf_path = os.path.join(folder_path, elf_filename)
                        
                        if os.path.exists(elf_path):
                            print(f"✅ Found matching ELF file!")
                            print(f"   Release: {folder}")
                            print(f"   Version: {metadata.get('version', 'unknown')}")
                            print(f"   ELF file: {elf_filename}")
                            print(f"   SHA256: {metadata_sha256}")
                            print(f"   Path: {elf_path}")
                            return elf_path
                        else:
                            print(f"⚠️  Metadata found but ELF file missing: {elf_path}")
                    
                except json.JSONDecodeError as e:
                    print(f"⚠️  Invalid JSON in {metadata_path}: {e}")
                except Exception as e:
                    print(f"⚠️  Error reading {metadata_path}: {e}")
            
            print(f"❌ No matching ELF file found for SHA256: {device_sha256}")
            return None
            
        except Exception as e:
            print(f"❌ Error scanning releases directory: {e}")
            return None

    def verify_firmware_sha256(self, crash_info: Dict[str, Any], firmware_path: str) -> bool:
        """Verify firmware SHA256 against device using crash info."""
        device_sha256 = crash_info.get('appElfSha256', '')
        if not device_sha256:
            print(f"⚠️  No firmware SHA256 available in crash info")
            return False
        
        local_sha256 = self.get_firmware_sha256(firmware_path)
        if not local_sha256:
            return False
        
        # Compare partial SHA256 (device provides partial hash)
        if local_sha256.lower().startswith(device_sha256.lower()):
            print(f"✅ Firmware SHA256 matches: {device_sha256}...")
            return True
        else:
            print(f"⚠️  WARNING: Firmware SHA256 mismatch!")
            print(f"   Device (partial): {device_sha256}")
            print(f"   Local (full):     {local_sha256}")
            print(f"   Analysis may be inaccurate - rebuild and flash firmware")
            return False

    def analyze_core_dump_header(self, data: bytes) -> None:
        """Analyze the core dump header to show basic information."""
        print(f"\n🔬 CORE DUMP ANALYSIS:")
        print(f"File size: {len(data):,} bytes")
        
        if len(data) < 16:
            print("❌ Core dump too small to analyze")
            return
        
        # Show first 32 bytes as hex
        print(f"Header (first 32 bytes): {data[:32].hex()}")
        
        # Try to detect ELF format
        if data[:4] == b'\x7fELF':
            print("✅ Detected ELF format core dump")
            self._analyze_elf_header(data)
        else:
            print("ℹ️  Binary format core dump (or unknown format)")
            self._analyze_binary_header(data)

    def _analyze_elf_header(self, data: bytes) -> None:
        """Analyze ELF header information."""
        if len(data) < 52:  # Minimum ELF header size
            return
        
        # ELF identification
        ei_class = data[4]
        ei_data = data[5]
        ei_version = data[6]
        
        print(f"  ELF Class: {'32-bit' if ei_class == 1 else '64-bit' if ei_class == 2 else 'Unknown'}")
        print(f"  Endianness: {'Little' if ei_data == 1 else 'Big' if ei_data == 2 else 'Unknown'}")
        print(f"  ELF Version: {ei_version}")

    def _analyze_binary_header(self, data: bytes) -> None:
        """Analyze binary format header."""
        # This would need ESP-IDF specific knowledge
        print("  Binary format analysis not implemented")

    def _find_esp_idf_installation(self) -> Optional[str]:
        """Find ESP-IDF installation directory on Windows."""
        import platform
        import glob
        
        if platform.system() != "Windows":
            return None
        
        # Common ESP-IDF installation paths
        esp_idf_paths = [
            "C:/Espressif/frameworks/esp-idf-*",
            os.path.expanduser("~/esp/esp-idf"),
            "C:/esp-idf",
        ]
        
        for pattern in esp_idf_paths:
            matches = glob.glob(pattern)
            if matches:
                # Take the most recent version if multiple matches
                matches.sort(reverse=True)
                esp_idf_dir = matches[0]
                export_script = os.path.join(esp_idf_dir, "export.ps1")
                if os.path.exists(export_script):
                    return esp_idf_dir
        
        return None

    def _generate_analysis_script(self, filename: str, firmware_path: str, esp_idf_dir: Optional[str]) -> str:
        """Generate a helper PowerShell script to run the analysis."""
        import platform
        
        script_path = os.path.join(os.path.dirname(filename), "run_coredump_analysis.ps1")
        
        try:
            with open(script_path, 'w', encoding='utf-8') as f:
                f.write("# ESP-IDF Core Dump Analysis Helper Script\n")
                f.write("# Auto-generated by crash_dump_analyzer.py\n\n")
                
                if platform.system() == "Windows" and esp_idf_dir:
                    f.write("# Source ESP-IDF environment\n")
                    f.write(f'. "{os.path.join(esp_idf_dir, "export.ps1")}"\n\n')
                
                f.write("# Run core dump analysis\n")
                f.write(f'python -m esp_coredump info_corefile -c "{filename}" -t elf "{firmware_path}"\n')
            
            return script_path
        except Exception as e:
            print(f"⚠️  Could not generate helper script: {e}")
            return ""

    def analyze_with_esp_idf(self, filename: str, crash_info: Dict[str, Any]) -> bool:
        """Automatically run ESP-IDF core dump analysis with firmware verification."""
        try:
            import subprocess
            import platform
            
            print(f"\n🔧 Running ESP-IDF core dump analysis...")
            
            # First, try to find matching ELF in releases folder
            device_sha256 = crash_info.get('appElfSha256', '')
            firmware_path = None
            
            if device_sha256:
                firmware_path = self.find_matching_elf_in_releases(device_sha256)
            
            # Fallback to current build if no match found in releases
            if not firmware_path:
                firmware_path = ".pio/build/esp32s3-dev/firmware.elf"
                if not os.path.exists(firmware_path):
                    print(f"❌ Firmware file not found: {firmware_path}")
                    print(f"   Make sure you've built the project first with: pio run")
                    print(f"   Or ensure releases folder contains the correct ELF file")
                    return False
                
                print(f"⚠️  Using current build ELF (no match found in releases): {firmware_path}")
            
            # Verify firmware SHA256 using crash info
            print("🔍 Verifying firmware SHA256 against device...")
            self.verify_firmware_sha256(crash_info, firmware_path)
            
            # Check if esp_coredump is available in current environment
            esp_coredump_cmd = f'python -m esp_coredump info_corefile -c "{filename}" -t elf "{firmware_path}"'
            
            # Try to run directly first (will work if ESP-IDF env is already sourced)
            try:
                print(f"🔧 Attempting direct execution...")
                result = subprocess.run(
                    esp_coredump_cmd,
                    shell=True,
                    capture_output=True,
                    text=True,
                    timeout=60
                )
                
                if result.returncode == 0:
                    print("✅ ESP-IDF analysis completed successfully!")
                    print("\n" + "="*80)
                    print("📊 ESP-IDF CORE DUMP ANALYSIS OUTPUT:")
                    print("="*80)
                    print(result.stdout)
                    return True
                else:
                    # Command failed - likely because ESP-IDF environment not sourced
                    error_msg = result.stderr.lower() if result.stderr else ""
                    if "no module named esp_coredump" in error_msg or "no module" in error_msg:
                        print(f"⚠️  ESP-IDF environment not available in current shell")
                    else:
                        print(f"⚠️  Command failed with code {result.returncode}")
                        if result.stderr:
                            print(f"   Error: {result.stderr}")
            except subprocess.TimeoutExpired:
                print(f"⚠️  Command timed out")
            except Exception as e:
                print(f"⚠️  Command failed: {e}")
            
            # If direct execution failed, provide helpful instructions
            print(f"\n" + "="*80)
            print("💡 MANUAL ANALYSIS REQUIRED")
            print("="*80)
            
            if platform.system() == "Windows":
                # Try to find ESP-IDF installation
                esp_idf_dir = self._find_esp_idf_installation()
                
                if esp_idf_dir:
                    print(f"✅ Found ESP-IDF installation: {esp_idf_dir}")
                    
                    # Generate helper script
                    script_path = self._generate_analysis_script(filename, firmware_path, esp_idf_dir)
                    if script_path:
                        print(f"\n📝 Generated helper script: {script_path}")
                        print(f"\n🚀 Run this command in a NEW PowerShell terminal:")
                        print(f"   {script_path}")
                    else:
                        print(f"\n🚀 Run these commands in a NEW PowerShell terminal:")
                        print(f"   . {os.path.join(esp_idf_dir, 'export.ps1')}")
                        print(f"   {esp_coredump_cmd}")
                else:
                    print(f"⚠️  Could not locate ESP-IDF installation")
                    print(f"\n🚀 Run these commands in a NEW PowerShell terminal:")
                    print(f"   . C:/Espressif/frameworks/esp-idf-v5.5.1/export.ps1")
                    print(f"   {esp_coredump_cmd}")
            else:
                # Unix/Mac instructions
                print(f"\n� Run these commands in your terminal:")
                print(f"   . $HOME/esp/esp-idf/export.sh")
                print(f"   {esp_coredump_cmd}")
            
            print("="*80)
            return False
                
        except Exception as e:
            print(f"❌ Error running ESP-IDF analysis: {e}")
            return False

    def clear_core_dump(self) -> bool:
        """Clear the core dump from device flash."""
        try:
            print(f"\n🗑️  Clearing core dump from device...")
            response = self.session.post(f"{self.base_url}/api/v1/crash/clear")
            response.raise_for_status()
            
            result = response.json()
            if result.get('success'):
                print(f"✅ {result.get('message', 'Core dump cleared')}")
                return True
            else:
                print(f"❌ Failed to clear core dump")
                return False
                
        except requests.exceptions.RequestException as e:
            print(f"❌ Error clearing core dump: {e}")
            return False

    def analyze(self) -> Optional[str]:
        """Main analysis function. Always saves to temp and runs analysis automatically."""
        print(f"🚀 Starting crash dump analysis for {self.device_ip}")
        
        # Step 1: Get crash information
        crash_info = self.get_crash_info()
        if not crash_info:
            return None
        
        debug_output = self.print_crash_info(crash_info)
        
        # Step 2: Check if core dump is available
        if not crash_info.get('hasCoreDump'):
            print("\nℹ️  No core dump available on device")
            # Still save crash info to text file even without core dump
            self.save_crash_dump_text(crash_info, debug_output, None)
            return None
        
        # Step 3: Download core dump
        core_dump_data = self.get_core_dump_chunks()
        if not core_dump_data:
            return None
        
        # Step 4: Analyze core dump
        self.analyze_core_dump_header(core_dump_data)
        
        # Step 5: Save core dump to temp file (binary)
        filename = self.save_core_dump_temp(core_dump_data)
        if not filename:
            return None
        
        # Step 6: Save comprehensive crash dump to text file
        self.save_crash_dump_text(crash_info, debug_output, core_dump_data)
        
        # Step 7: Automatically run ESP-IDF analysis with smart ELF detection
        self.analyze_with_esp_idf(filename, crash_info)
        
        return filename


def main():
    if len(sys.argv) < 2:
        print("Usage: python crash_dump_analyzer.py <device_ip> [username] [password] [chunk_size] [--clear]")
        print("Example: python crash_dump_analyzer.py 192.168.1.100")
        print("Example: python crash_dump_analyzer.py 192.168.1.100 admin secret123")
        print("Example: python crash_dump_analyzer.py 192.168.1.100 admin secret123 4096")
        print("Example: python crash_dump_analyzer.py 192.168.1.100 --clear  # Clear dump after analysis")
        sys.exit(1)
    
    # Check for --clear flag
    clear_after = '--clear' in sys.argv
    args = [arg for arg in sys.argv[1:] if arg != '--clear']
    
    device_ip = args[0]
    username = args[1] if len(args) > 1 else None
    password = args[2] if len(args) > 2 else None
    
    # Determine chunk_size based on number of arguments
    if len(args) > 3:
        chunk_size = int(args[3])
    elif len(args) == 2:  # Only device_ip and one other arg (assume it's chunk_size)
        try:
            chunk_size = int(args[1])
            username = None
            password = None
        except ValueError:
            # If it's not a number, treat it as username and use default chunk_size
            chunk_size = 2048
    else:
        chunk_size = 2048
    
    # Validate chunk size
    if chunk_size < 512 or chunk_size > 8192:
        print("❌ Chunk size must be between 512 and 8192 bytes")
        sys.exit(1)
    
    analyzer = CrashDumpAnalyzer(device_ip, username, password, chunk_size)
    
    try:
        # Run analysis automatically (no prompts)
        filename = analyzer.analyze()
        
        # Clear core dump if --clear flag was provided
        if clear_after and filename:
            print(f"\n🗑️  Clearing core dump from device (--clear flag provided)...")
            analyzer.clear_core_dump()
        
    except KeyboardInterrupt:
        print("\n\n⚠️  Analysis interrupted by user")
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")


if __name__ == "__main__":
    main()
