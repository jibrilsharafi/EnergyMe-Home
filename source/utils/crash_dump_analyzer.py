#!/usr/bin/env python3
"""
EnergyMe-Home Crash Dump Analyzer

This script fetches crash information and core dump data from the EnergyMe-Home device,
then decodes and analyzes the crash dump for debugging purposes.

Usage:
    python crash_dump_analyzer.py <device_ip> [username] [password] [chunk_size]

Example:
    python crash_dump_analyzer.py 192.168.1.100
    python crash_dump_analyzer.py 192.168.1.100 admin secret123
    python crash_dump_analyzer.py 192.168.1.100 admin secret123 4096
"""

import sys
import json
import base64
import requests
from requests.auth import HTTPDigestAuth
from datetime import datetime
from typing import Optional, Dict, Any, List


class CrashDumpAnalyzer:
    def __init__(self, device_ip: str, username: Optional[str] = None, password: Optional[str] = None, chunk_size: int = 2048):
        self.device_ip = device_ip
        self.base_url = f"http://{device_ip}"
        self.chunk_size = chunk_size
        self.session = requests.Session()
        self.session.timeout = 30
        
        # Set up authentication if provided
        if username and password:
            self.session.auth = HTTPDigestAuth(username, password)
            print(f"🔐 Using digest authentication for user: {username}")

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

    def print_crash_info(self, crash_info: Dict[str, Any]) -> None:
        """Print crash information in a readable format."""
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
        
        print("="*80)

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

    def save_core_dump(self, data: bytes, filename: Optional[str] = None) -> str:
        """Save core dump data to a file."""
        if filename is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"coredump_{self.device_ip}_{timestamp}.bin"
        
        try:
            with open(filename, 'wb') as f:
                f.write(data)
            print(f"💾 Core dump saved to: {filename}")
            return filename
        except Exception as e:
            print(f"❌ Error saving core dump: {e}")
            return ""

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

    def analyze(self, save_dump: bool = True, clear_after: bool = False) -> None:
        """Main analysis function."""
        print(f"🚀 Starting crash dump analysis for {self.device_ip}")
        
        # Step 1: Get crash information
        crash_info = self.get_crash_info()
        if not crash_info:
            return
        
        self.print_crash_info(crash_info)
        
        # Step 2: Check if core dump is available
        if not crash_info.get('hasCoreDump'):
            print("\nℹ️  No core dump available on device")
            return
        
        # Step 3: Download core dump
        core_dump_data = self.get_core_dump_chunks()
        if not core_dump_data:
            return
        
        # Step 4: Analyze core dump
        self.analyze_core_dump_header(core_dump_data)
        
        # Step 5: Save core dump if requested
        if save_dump:
            filename = self.save_core_dump(core_dump_data)
            if filename:
                print(f"\n📝 COMPLETE CORE DUMP ANALYSIS:")
                print(f"   Option 1 (Recommended): Use esp-coredump directly with your firmware.elf:")
                print(f"   esp-coredump.exe --gdb-timeout-sec 10 info_corefile -c {filename} -t raw .pio/build/esp32dev/firmware.elf")
                print(f"")
                print(f"   Option 2: Use idf.py commands (may have issues with PlatformIO projects):")
                print(f"   idf.py coredump-info -c {filename}")
                print(f"   idf.py coredump-debug -c {filename}")
                print(f"")
                print(f"   💡 Note: If you get 'Core dump version not supported' or SHA256 mismatch errors,")
                print(f"        make sure you're using the exact firmware.elf file that was running when the crash occurred.")
                print(f"        The --gdb-timeout-sec parameter is important for large firmware files.")
        
        # Step 6: Clear core dump if requested
        if clear_after:
            self.clear_core_dump()


def main():
    if len(sys.argv) < 2:
        print("Usage: python crash_dump_analyzer.py <device_ip> [username] [password] [chunk_size]")
        print("Example: python crash_dump_analyzer.py 192.168.1.100")
        print("Example: python crash_dump_analyzer.py 192.168.1.100 admin secret123")
        print("Example: python crash_dump_analyzer.py 192.168.1.100 admin secret123 4096")
        sys.exit(1)
    
    device_ip = sys.argv[1]
    username = sys.argv[2] if len(sys.argv) > 2 else None
    password = sys.argv[3] if len(sys.argv) > 3 else None
    
    # Determine chunk_size based on number of arguments
    if len(sys.argv) > 4:
        chunk_size = int(sys.argv[4])
    elif len(sys.argv) == 3:  # Only device_ip and one other arg (assume it's chunk_size)
        try:
            chunk_size = int(sys.argv[2])
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
        # Ask user preferences
        save_dump = input("\n💾 Save core dump to file? (y/N): ").lower().startswith('y')
        clear_after = input("🗑️  Clear core dump from device after download? (y/N): ").lower().startswith('y')
        
        analyzer.analyze(save_dump=save_dump, clear_after=clear_after)
        
    except KeyboardInterrupt:
        print("\n\n⚠️  Analysis interrupted by user")
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")


if __name__ == "__main__":
    main()
