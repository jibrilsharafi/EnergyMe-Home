# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

#!/usr/bin/env python3
"""
EnergyMe-Home Crash Dump Analyzer

This script fetches an archived crash record and its core dump from an EnergyMe-Home
device, then decodes and analyzes the dump for debugging purposes. It locates the
matching ELF in the releases/ folder by firmware version, falling back to SHA256.

Devices archive each crash to flash on the boot it is detected, so several records may
be waiting. With exactly one, it's analyzed automatically; with several and neither
--crash-id nor --all, an interactive prompt lists them (with a human-readable
timestamp) and lets you analyze one, analyze all, or delete one in place.

Usage:
    python crash_dump_analyzer.py -H <device_ip> [options]

Example:
    python crash_dump_analyzer.py -H 192.168.1.100
    python crash_dump_analyzer.py -H 192.168.1.100 --crash-id 3f8a1c02d94b7e15
    python crash_dump_analyzer.py -H 192.168.1.100 --all
    python crash_dump_analyzer.py -H 192.168.1.100 --delete 3f8a1c02d94b7e15
    python crash_dump_analyzer.py -H 192.168.1.100 -u admin -p secret123 --clear
"""

import argparse
import gzip
import json
import requests
from requests.auth import HTTPDigestAuth
from datetime import datetime
from typing import Optional, Dict, Any
import os
import re
import sys
import hashlib

from _device_auth import add_device_args, resolve_credentials

# Windows consoles default to the cp1252 codepage, which cannot encode the emoji
# this script prints throughout - reconfigure to UTF-8 so a plain `uv run` from
# PowerShell doesn't crash on the first print. Both streams support reconfigure()
# on the 3.13+ this project requires.
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# Unix ms for 2001-09-09. Records archived before NTP carry a few thousand ms
# instead, which is not a date worth formatting.
UNSET_CLOCK_THRESHOLD_MS = 1_000_000_000_000


class CrashDumpAnalyzer:
    def __init__(self, device_ip: str, username: Optional[str] = None, password: Optional[str] = None,
                 elf_path: Optional[str] = None):
        self.device_ip = device_ip
        self.base_url = f"http://{device_ip}"
        self.session = requests.Session()
        # Explicit ELF wins over both the releases lookup and the default build
        # path, which is what makes non-default environments usable here
        self.elf_path = elf_path
        # Resolved on first use: the releases scan opens and parses every
        # metadata.json, and both the backtrace decode and the esp-idf analysis
        # ask for the same ELF
        self._resolved_elf: Optional[str] = None
        self._resolved_elf_cached = False

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

    @staticmethod
    def format_timestamp(timestamp: Optional[int]) -> str:
        """Human-readable local time for a device-supplied epoch-ms timestamp.

        Written before NTP on a cold boot, so a near-epoch value is expected
        rather than wrong - show it raw in that case. Tested on the raw value
        because fromtimestamp() itself raises OSError on a near-epoch input in
        any timezone west of UTC, which would abort the run before the dump is
        even downloaded.
        """
        if not timestamp:
            return "unknown"
        if timestamp <= UNSET_CLOCK_THRESHOLD_MS:
            return f"{timestamp} ms - clock was not set when this was archived"
        return datetime.fromtimestamp(timestamp / 1000).strftime('%Y-%m-%d %H:%M:%S')

    @staticmethod
    def _flatten_crash_record(record: Dict[str, Any]) -> Dict[str, Any]:
        """Merge the archive-level fields (crashId, timestamp, sizes) into the
        nested crashInfo the device reports, so callers see one flat dict."""
        flattened = dict(record.get('crashInfo', {}))
        for key in ('crashId', 'timestamp', 'firmwareVersion',
                    'coreDumpRawSize', 'coreDumpCompressedSize'):
            if key in record:
                flattened[key] = record[key]
        return flattened

    def list_crashes(self) -> list:
        """Fetch every archived crash record, oldest first, flattened like get_crash_info()."""
        try:
            print(f"🔍 Fetching archived crashes from {self.device_ip}...")
            response = self.session.get(f"{self.base_url}/api/v1/crash/info")
            response.raise_for_status()
            payload = response.json()
        except requests.exceptions.RequestException as e:
            print(f"❌ Error fetching crash info: {e}")
            return []

        return [self._flatten_crash_record(r) for r in payload.get('crashes', [])]

    def get_crash_info(self, crash_id: Optional[str] = None) -> Optional[Dict[str, Any]]:
        """Fetch one archived crash record from the device.

        The device stores each crash as frozen metadata plus a gzipped dump, so
        /api/v1/crash/info returns a list rather than a single live reading.
        """
        crashes = self.list_crashes()
        if not crashes:
            print("ℹ️  No archived crashes on device")
            return None

        if crash_id is not None:
            record = next((c for c in crashes if str(c.get('crashId')) == str(crash_id)), None)
            if record is None:
                available = ', '.join(str(c.get('crashId')) for c in crashes)
                print(f"❌ No archived crash with id {crash_id}. Available: {available}")
                return None
            return record

        record = crashes[0]  # Oldest, which is also the next to be published
        if len(crashes) > 1:
            print(f"ℹ️  {len(crashes)} archived crashes, analyzing the oldest "
                  f"(crashId {record.get('crashId')}). Use --crash-id, --all, or run "
                  f"without either to pick interactively.")
        return record

    def delete_crash(self, crash_id: str) -> bool:
        """Remove one archived crash via DELETE /api/v1/crash/entry, keeping the rest."""
        try:
            response = self.session.delete(
                f"{self.base_url}/api/v1/crash/entry", params={"id": crash_id})
            if response.status_code == 200:
                print(f"🗑️  Deleted crash {crash_id}")
                return True
            print(f"❌ Failed to delete crash {crash_id}: HTTP {response.status_code}")
            return False
        except requests.exceptions.RequestException as e:
            print(f"❌ Error deleting crash {crash_id}: {e}")
            return False

    def choose_crashes_interactively(self, crashes: list) -> Optional[list]:
        """Prompt the user to pick which archived crash(es) to analyze, or delete
        crashes in place. Returns the list of crashIds to analyze, or None to quit.
        Re-fetches after every delete so the menu never goes stale."""
        while True:
            print(f"\n{len(crashes)} archived crashes:")
            for i, c in enumerate(crashes, start=1):
                when = self.format_timestamp(c.get('timestamp'))
                task = c.get('taskName', 'unknown task')
                reason = c.get('resetReason', 'unknown reason')
                print(f"  [{i}] {c.get('crashId')}  {when}  {reason}  {task}")

            choice = input(
                "\nAnalyze which? (number, 'a' for all, 'd<N>' to delete one, 'q' to quit): "
            ).strip().lower()

            if choice in ('q', ''):
                return None
            if choice == 'a':
                return [c.get('crashId') for c in crashes]
            if choice.startswith('d') and choice[1:].isdigit():
                idx = int(choice[1:]) - 1
                if 0 <= idx < len(crashes):
                    self.delete_crash(crashes[idx].get('crashId'))
                    crashes = self.list_crashes()
                    if not crashes:
                        print("ℹ️  No archived crashes left")
                        return None
                else:
                    print(f"❌ No crash numbered {idx + 1}")
                continue
            if choice.isdigit():
                idx = int(choice) - 1
                if 0 <= idx < len(crashes):
                    return [crashes[idx].get('crashId')]
                print(f"❌ No crash numbered {choice}")
                continue

            print("❌ Not understood - enter a number, 'a', 'd<N>', or 'q'")

    def print_crash_info(self, crash_info: Dict[str, Any]) -> Optional[str]:
        """Print crash information in a readable format and return debug output."""
        debug_output = None
        
        print("\n" + "="*80)
        print("🚨 CRASH ANALYSIS REPORT")
        print("="*80)
        
        # Basic crash information
        if crash_info.get('crashId'):
            print(f"Crash ID: {crash_info['crashId']}")
        if crash_info.get('timestamp'):
            print(f"Archived at: {self.format_timestamp(crash_info['timestamp'])}")
        print(f"Firmware Version: {crash_info.get('firmwareVersion', 'Unknown')}")
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
                
                debug_cmd = self._build_addr2line_command(addresses, crash_info)
                if debug_cmd:
                    print(f"\n🔧 DEBUG COMMAND:")
                    print(f"{debug_cmd}")

                    debug_output = self._run_debug_command(debug_cmd)

        print("="*80)
        return debug_output

    @staticmethod
    def _parse_addr2line_output(output: Optional[str], addresses: list) -> list:
        """Turn addr2line -pfC's text into structured frames for the JSON report.

        One line per frame: "func at file:line", optionally followed by one or
        more " (inlined by) func at file:line" continuation lines for the same
        address. rpartition(':') (not split) because a Windows path's drive
        letter ("C:\\...") also contains a colon - only the last one, right
        before the line number, is the real separator.
        """
        if not output:
            return []

        line_re = re.compile(r'^\s*(?:\(inlined by\)\s+)?(?P<func>.*) at (?P<loc>.+)$')
        frames: list = []
        for raw_line in output.splitlines():
            if not raw_line.strip():
                continue
            m = line_re.match(raw_line)
            if not m:
                continue

            file, _, lineno = m.group('loc').strip().rpartition(':')
            entry = {
                'function': m.group('func').strip(),
                'file': file or m.group('loc').strip(),
                'line': int(lineno) if lineno.isdigit() else None,
            }

            if raw_line.lstrip().startswith('(inlined by)') and frames:
                frames[-1].setdefault('inlined_by', []).append(entry)
            else:
                if len(frames) < len(addresses):
                    entry['address'] = f'0x{addresses[len(frames)]:08x}'
                frames.append(entry)
        return frames

    def _build_addr2line_command(self, addresses, crash_info: Dict[str, Any]) -> Optional[str]:
        """Compose an addr2line invocation for `addresses` against the local ELF."""
        if not addresses:
            return None

        elf = self.elf_path or self.find_matching_elf_in_releases(
            crash_info.get('appElfSha256', ''), crash_info.get('firmwareVersion')
        )
        if not elf:
            print("ℹ️  No local ELF resolved yet, skipping backtrace decode "
                  "(pass --elf to decode against a specific build)")
            return None

        joined = ' '.join(f'0x{addr:08x}' for addr in addresses)
        return f'xtensa-esp32-elf-addr2line -pfC -e "{elf}" {joined}'

    def download_core_dump(self, crash_id: Optional[str] = None) -> Optional[bytes]:
        """Download one archived core dump and return the raw ELF bytes.

        The endpoint serves the stored gzip file as application/gzip with no
        Content-Encoding, so the bytes arrive compressed and are inflated here.
        The magic-number check keeps older firmware, which did set
        Content-Encoding and so had requests inflate in transit, working too.
        """
        try:
            print(f"\n📥 Downloading core dump...")

            params = {'id': crash_id} if crash_id is not None else {}
            response = self.session.get(f"{self.base_url}/api/v1/crash/dump", params=params)
            response.raise_for_status()

            data = response.content
            if not data:
                print("❌ Empty core dump response")
                return None

            if data[:2] == b'\x1f\x8b':
                compressed_size = len(data)
                data = gzip.decompress(data)
                print(f"✅ Core dump downloaded: {compressed_size:,} bytes gzipped, "
                      f"{len(data):,} bytes inflated")
            else:
                print(f"✅ Core dump downloaded: {len(data):,} bytes (inflated in transit)")

            return data

        except requests.exceptions.RequestException as e:
            print(f"❌ Error fetching core dump: {e}")
            return None
        except (OSError, EOFError) as e:
            print(f"❌ Failed to decompress core dump: {e}")
            return None

    def save_crash_dump_json(self, crash_info: Dict[str, Any], run_dir: str, debug_output: Optional[str] = None,
                              firmware_path: Optional[str] = None) -> str:
        """Save the crash's structured, machine-parseable data: the flattened
        crash_info the device reported, plus the backtrace decoded into frames
        (function/file/line, with inlined_by nesting) instead of a raw text blob."""
        try:
            filename = os.path.join(run_dir, "report.json")

            backtrace = dict(crash_info.get('backtrace') or {})
            addresses = backtrace.get('addresses', [])
            backtrace['frames'] = self._parse_addr2line_output(debug_output, addresses)
            backtrace['addresses'] = [f'0x{a:08x}' for a in addresses]

            report = dict(crash_info)
            report['backtrace'] = backtrace
            report['archivedAt'] = self.format_timestamp(crash_info.get('timestamp'))
            report['deviceIp'] = self.device_ip
            report['analyzedAt'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            if firmware_path:
                report['elfPath'] = firmware_path

            with open(filename, 'w', encoding='utf-8') as f:
                json.dump(report, f, indent=2)

            print(f"💾 Crash info saved to: {filename}")
            return filename

        except Exception as e:
            print(f"❌ Error saving crash info JSON: {e}")
            return ""

    def save_full_core_dump_text(self, full_dump_output: str, run_dir: str) -> str:
        """Save the full core dump analysis (registers, per-thread backtraces with
        locals - the output of analyze_locally()) as-is. Nothing else goes in this
        file: basic crash info lives in the JSON, and the binary is already on disk
        via save_core_dump_bin(), so there is no reason to also hex-dump it here."""
        try:
            filename = os.path.join(run_dir, "full_dump.txt")

            with open(filename, 'w', encoding='utf-8') as f:
                f.write(full_dump_output)

            print(f"💾 Full core dump analysis saved to: {filename}")
            return filename

        except Exception as e:
            print(f"❌ Error saving full core dump text file: {e}")
            return ""

    def make_run_dir(self) -> str:
        """One folder per analysis run, named by timestamp, holding the binary
        dump, the structured JSON report, and the full core dump text together -
        rather than scattered across temp/ and coredump/ under three different
        naming schemes."""
        run_dir = os.path.join("coredump", datetime.now().strftime("%Y%m%d_%H%M%S"))
        os.makedirs(run_dir, exist_ok=True)
        return run_dir

    def save_core_dump_bin(self, data: bytes, run_dir: str) -> str:
        """Save the raw core dump bytes into this run's folder."""
        try:
            filename = os.path.join(run_dir, "core.bin")
            with open(filename, 'wb') as f:
                f.write(data)
            print(f"💾 Core dump saved to: {filename}")
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

    def find_matching_elf_in_releases(self, device_sha256: str,
                                      firmware_version: Optional[str] = None) -> Optional[str]:
        """Find the matching ELF file in the releases folder, caching the result.

        Crash payloads carry the firmware version explicitly, so a direct version
        match is tried first. The SHA256 prefix scan remains the fallback, both
        for records from firmware predating that field and as a cross-check.
        """
        if not self._resolved_elf_cached:
            self._resolved_elf = self._scan_releases_for_elf(device_sha256, firmware_version)
            self._resolved_elf_cached = True
        return self._resolved_elf

    def _scan_releases_for_elf(self, device_sha256: str,
                               firmware_version: Optional[str] = None) -> Optional[str]:
        releases_dir = "releases"

        if not os.path.exists(releases_dir):
            print(f"📁 Releases directory not found: {releases_dir}")
            return None

        try:
            release_folders = [f for f in os.listdir(releases_dir)
                             if os.path.isdir(os.path.join(releases_dir, f))]
            release_folders.sort(reverse=True)  # Most recent first

            print(f"📁 Found {len(release_folders)} release folders")

            # Read every release's metadata once, then match against it
            candidates = []
            for folder in release_folders:
                metadata_path = os.path.join(releases_dir, folder, "metadata.json")
                if not os.path.exists(metadata_path):
                    continue

                try:
                    with open(metadata_path, 'r') as f:
                        metadata = json.load(f)
                except json.JSONDecodeError as e:
                    print(f"⚠️  Invalid JSON in {metadata_path}: {e}")
                    continue
                except OSError as e:
                    print(f"⚠️  Error reading {metadata_path}: {e}")
                    continue

                debug_info = metadata.get('files', {}).get('debug', {})
                elf_filename = debug_info.get('filename', '')
                if not elf_filename:
                    continue

                candidates.append({
                    'folder': folder,
                    'version': metadata.get('version', ''),
                    'sha256': debug_info.get('sha256', ''),
                    'elf_path': os.path.join(releases_dir, folder, elf_filename),
                    'elf_filename': elf_filename,
                })

            def _accept(match, how):
                if not os.path.exists(match['elf_path']):
                    print(f"⚠️  Metadata found but ELF file missing: {match['elf_path']}")
                    return None
                print(f"✅ Found matching ELF file ({how})!")
                print(f"   Release: {match['folder']}")
                print(f"   Version: {match['version'] or 'unknown'}")
                print(f"   ELF file: {match['elf_filename']}")
                print(f"   SHA256: {match['sha256'] or 'unknown'}")
                print(f"   Path: {match['elf_path']}")
                return match['elf_path']

            if firmware_version:
                print(f"🔍 Looking for the ELF of firmware version {firmware_version}...")
                for match in candidates:
                    if match['version'] == firmware_version:
                        if device_sha256 and match['sha256'] and \
                                not match['sha256'].lower().startswith(device_sha256.lower()):
                            print(f"⚠️  Release {match['folder']} matches version {firmware_version} "
                                  f"but its SHA256 ({match['sha256']}) does not start with the "
                                  f"device's {device_sha256}; falling back to a SHA256 search")
                            break
                        found = _accept(match, f"version {firmware_version}")
                        if found:
                            return found

            if device_sha256:
                print(f"🔍 Searching for ELF file matching SHA256: {device_sha256}...")
                for match in candidates:
                    if match['sha256'] and match['sha256'].lower().startswith(device_sha256.lower()):
                        found = _accept(match, f"SHA256 {device_sha256}")
                        if found:
                            return found

            print(f"❌ No matching ELF file found (version: {firmware_version or 'unknown'}, "
                  f"SHA256: {device_sha256 or 'unknown'})")
            return None

        except OSError as e:
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

    def _find_gdb(self) -> Optional[str]:
        """Find a xtensa GDB binary in the local PlatformIO toolchain, if any is
        installed. esp_coredump.CoreDump needs one for info_corefile() - it has no
        gdb-less mode. PlatformIO's default xtensa-esp toolchain does not bundle
        gdb (that's a separate package), so this commonly returns None; the caller
        degrades gracefully rather than demanding a manual install.

        Requires gdb >= 14: espressif/tool-xtensa-esp-elf-gdb@12.1.0 (the version
        `pio pkg install -g -t tool-xtensa-esp-elf-gdb` picks without a pinned
        owner/version) raises a KeyError on 'current-thread-id' decoding a synthetic
        ELF core file - its -thread-info MI response omits that field for a
        --core=-loaded target. platformio/tool-xtensa-esp-elf-gdb@14.2.0+20240403
        does not have this bug; that is what this glob picks up as the unversioned
        default install directory."""
        import glob

        candidates = [
            "~/.platformio/packages/tool-xtensa-esp-elf-gdb/bin/xtensa-esp32-elf-gdb*",
            "~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-gdb*",
        ]
        for pattern in candidates:
            matches = sorted(glob.glob(os.path.expanduser(pattern)))
            if matches:
                return matches[0]
        return None

    def _resolve_firmware_elf(self, crash_info: Dict[str, Any]) -> Optional[str]:
        """Find the ELF matching this crash: an explicit --elf override first,
        then the releases/ lookup by SHA256/version, then the current build as a
        last resort. Shared by analyze_locally() and the JSON report, so both
        agree on which ELF was actually used."""
        device_sha256 = crash_info.get('appElfSha256', '')
        firmware_version = crash_info.get('firmwareVersion')

        if self.elf_path:
            if not os.path.exists(self.elf_path):
                print(f"❌ ELF file not found: {self.elf_path}")
                return None
            print(f"📎 Using the ELF given on the command line: {self.elf_path}")
            return self.elf_path

        firmware_path = None
        if device_sha256 or firmware_version:
            firmware_path = self.find_matching_elf_in_releases(device_sha256, firmware_version)
        if firmware_path:
            return firmware_path

        # Fallback to current build if no match found in releases
        firmware_path = ".pio/build/esp32s3-dev/firmware.elf"
        if not os.path.exists(firmware_path):
            print(f"❌ Firmware file not found: {firmware_path}")
            print(f"   Make sure you've built the project first with: pio run")
            print(f"   Or ensure releases folder contains the correct ELF file")
            return None

        print(f"⚠️  Using current build ELF (no match found in releases): {firmware_path}")
        return firmware_path

    def analyze_locally(self, filename: str, crash_info: Dict[str, Any], firmware_path: str) -> Optional[str]:
        """Decode the core dump entirely in-process: no subprocess shell-out to
        `python -m esp_coredump`, no ESP-IDF export script, no separate terminal.
        esp_coredump is a plain project dependency (uv add esp-coredump), so its
        CoreDump class is just called directly like any other library.

        `firmware_path` is resolved once by the caller (via _resolve_firmware_elf())
        and shared with the JSON report, rather than re-resolved here - that would
        re-scan every release folder's metadata.json a second time per run.

        Returns the full captured output (also printed live) so the caller can
        save it, or None if the dump could not be analyzed.
        """
        from esp_coredump import CoreDump

        gdb_path = self._find_gdb()
        if not gdb_path:
            print("\nℹ️  Skipping the register/thread-level dump: no xtensa gdb found locally.")
            print("   The addr2line backtrace above already has task/file/line for each frame,")
            print("   which covers most debugging needs. For registers and locals too, install")
            print("   `pio pkg install -g -t \"platformio/tool-xtensa-esp-elf-gdb@14.2.0+20240403\"` "
                  "and re-run this script.")
            return None

        print(f"\n🔧 Running core dump analysis (gdb: {gdb_path})...")
        print("=" * 80)
        import contextlib
        import io

        class _Tee:
            """Mirrors writes to both the real console and a buffer, so the full
            dump still streams live while we also capture it for save_crash_dump_text()."""
            def __init__(self, *streams):
                self._streams = streams

            def write(self, data):
                for s in self._streams:
                    s.write(data)

            def flush(self):
                for s in self._streams:
                    s.flush()

        buffer = io.StringIO()
        try:
            with contextlib.redirect_stdout(_Tee(sys.stdout, buffer)):
                CoreDump(chip='esp32s3', core=filename, core_format='elf', prog=firmware_path, gdb=gdb_path).info_corefile()
            print("=" * 80)
            return buffer.getvalue()
        except Exception as e:
            print("=" * 80)
            print(f"❌ Core dump analysis failed: {e}")
            return buffer.getvalue() or None

    def clear_core_dump(self) -> bool:
        """Delete every archived crash record from device flash."""
        try:
            print(f"\n🗑️  Clearing archived crashes from device...")
            response = self.session.post(f"{self.base_url}/api/v1/crash/clear")
            response.raise_for_status()

            result = response.json()
            if result.get('success'):
                print(f"✅ {result.get('message', 'Archived crashes cleared')}")
                return True
            else:
                print(f"❌ Failed to clear archived crashes")
                return False

        except requests.exceptions.RequestException as e:
            print(f"❌ Error clearing archived crashes: {e}")
            return False

    def analyze(self, crash_id: Optional[str] = None) -> Optional[str]:
        """Main analysis function. Always saves to temp and runs analysis automatically."""
        print(f"🚀 Starting crash dump analysis for {self.device_ip}")

        # Step 1: Get crash information
        crash_info = self.get_crash_info(crash_id)
        if not crash_info:
            return None

        debug_output = self.print_crash_info(crash_info)
        firmware_path = self._resolve_firmware_elf(crash_info)
        run_dir = self.make_run_dir()

        # Step 2: Check if core dump is available
        if not crash_info.get('hasCoreDump'):
            print("\nℹ️  No core dump available on device")
            # Still save the structured crash info even without a core dump
            self.save_crash_dump_json(crash_info, run_dir, debug_output, firmware_path)
            return None

        # Step 3: Download core dump
        core_dump_data = self.download_core_dump(crash_info.get('crashId'))
        if not core_dump_data:
            return None

        # Step 4: Analyze core dump (a live sanity check only - not saved anywhere;
        # the binary itself is already on disk via Step 5)
        self.analyze_core_dump_header(core_dump_data)

        # Step 5: Save core dump binary into this run's folder
        filename = self.save_core_dump_bin(core_dump_data, run_dir)
        if not filename:
            return None

        # Step 6: Save the structured crash info + parsed backtrace as JSON,
        # alongside the binary in the same run folder
        self.save_crash_dump_json(crash_info, run_dir, debug_output, firmware_path)

        # Step 7: Run the register/thread-level dump in-process, and save its
        # full text into the same run folder - nothing else goes in this file,
        # the binary and the basic info both already have their own file there
        if firmware_path:
            print("🔍 Verifying firmware SHA256 against device...")
            self.verify_firmware_sha256(crash_info, firmware_path)

            full_dump_output = self.analyze_locally(filename, crash_info, firmware_path)
            if full_dump_output:
                self.save_full_core_dump_text(full_dump_output, run_dir)

        return filename


def main():
    parser = argparse.ArgumentParser(
        description="Fetch and analyze a crash/core dump from an EnergyMe-Home device",
    )
    add_device_args(parser)
    parser.add_argument('--crash-id', default=None,
                       help='crashId of the archived record to analyze. With multiple '
                            'archived crashes and no --crash-id/--all, prompts interactively')
    parser.add_argument('--all', action='store_true',
                       help='Analyze every archived crash, oldest first, without prompting')
    parser.add_argument('--delete', metavar='CRASH_ID', default=None,
                       help='Delete one archived crash by id and exit, without analyzing anything')
    parser.add_argument('--clear', action='store_true',
                       help='Delete every archived crash from the device after analysis')
    parser.add_argument('--elf', default=None,
                       help='ELF to decode against, bypassing the releases lookup and the '
                            'default build path (needed for non-default environments, '
                            'e.g. .pio/build/esp32s3-dev-v5/firmware.elf)')
    args = parser.parse_args()

    username, password = resolve_credentials(args)
    analyzer = CrashDumpAnalyzer(args.host, username, password, elf_path=args.elf)

    try:
        if args.delete:
            analyzer.delete_crash(args.delete)
            return

        if args.crash_id:
            crash_ids = [args.crash_id]
        elif args.all:
            crash_ids = [c.get('crashId') for c in analyzer.list_crashes()]
            if not crash_ids:
                print("ℹ️  No archived crashes on device")
                return
        else:
            crashes = analyzer.list_crashes()
            if not crashes:
                print("ℹ️  No archived crashes on device")
                return
            if len(crashes) == 1:
                crash_ids = [crashes[0].get('crashId')]
            else:
                crash_ids = analyzer.choose_crashes_interactively(crashes)
                if not crash_ids:
                    return

        analyzed_any = False
        for crash_id in crash_ids:
            filename = analyzer.analyze(crash_id)
            analyzed_any = analyzed_any or bool(filename)

        # Clear archived crashes if --clear flag was provided
        if args.clear and analyzed_any:
            print(f"\n🗑️  Clearing archived crashes from device (--clear flag provided)...")
            analyzer.clear_core_dump()

    except KeyboardInterrupt:
        print("\n\n⚠️  Analysis interrupted by user")
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")


if __name__ == "__main__":
    main()
