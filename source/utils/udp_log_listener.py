#!/usr/bin/env python3
"""
UDP Log Listener for EnergyMe-Home
==================================

This script listens for UDP multicast/unicast messages from EnergyMe-Home devices
and displays them in a formatted way. It's designed to work with the syslog
format used by the ESP32 firmware.

Features:
- Automatic device-specific log files (creates separate .txt files per device)
- Reboot detection (creates new log file when device restarts)
- Real-time log filtering and colorized display
- Multiple output formats (structured, raw, JSON)
- Multicast and unicast support

Usage:
    python udp_log_listener.py [--port 514] [--multicast 239.255.255.250] [--filter LEVEL]

Examples:
    python udp_log_listener.py                          # Listen on multicast (default)
    python udp_log_listener.py --unicast                # Use unicast mode
    python udp_log_listener.py --port 1514              # Listen on custom port
    python udp_log_listener.py --filter info            # Only show info and above
    python udp_log_listener.py --multicast 239.1.1.1    # Custom multicast group
    python udp_log_listener.py --no-auto-device-logs    # Disable auto device logging
    python udp_log_listener.py --exclude-files src/ade7953.cpp          # Filter out all logs from ade7953.cpp
    python udp_log_listener.py --exclude-functions _printMeterValues    # Filter out _printMeterValues function logs
    python udp_log_listener.py --exclude-files src/ade7953.cpp src/utils.cpp --exclude-functions _printMeterValues printStatus  # Multiple filters

Device-specific logging:
    The listener automatically creates separate log files for each device in the logs/
    directory with the format: energyme_{device_id}_{timestamp}.txt
    
    New log files are created when:
    - Device reboots are detected (millis reset or "Guess who's back" message)
    - First message from a new device is received
    
    Log format
    2025-08-01 22:09:10 588c81c47ad8 [110246ms] INFO    [Core0] utils: Restarting system
"""

import socket
import struct
import argparse
import sys
import time
import re
import os
import signal
from datetime import datetime
from typing import Optional, Dict, Any, TextIO

class Colors:
    """ANSI color codes for terminal output"""
    RESET = '\033[0m'
    BOLD = '\033[1m'
    DIM = '\033[2m'
    
    # Log level colors
    VERBOSE = '\033[90m'  # Dark gray
    DEBUG = '\033[36m'    # Cyan
    INFO = '\033[32m'     # Green
    WARNING = '\033[33m'  # Yellow
    ERROR = '\033[31m'    # Red
    FATAL = '\033[35m'    # Magenta
    
    # Component colors
    TIMESTAMP = '\033[94m'  # Blue
    DEVICE = '\033[96m'     # Light cyan
    FUNCTION = '\033[93m'   # Light yellow

class LogFilter:
    """Filter logs by level, file/module, and function"""
    LEVELS = {
        'verbose': 0,
        'debug': 1,
        'info': 2,
        'warning': 3,
        'error': 4,
        'fatal': 5
    }
    
    def __init__(self, min_level: str = 'verbose', exclude_files: list = None, exclude_functions: list = None):
        self.min_level_value = self.LEVELS.get(min_level.lower(), 0)
        self.exclude_files = [f.lower() for f in (exclude_files or [])]
        self.exclude_functions = [f.lower() for f in (exclude_functions or [])]
    
    def should_show(self, level: str, function: str = None) -> bool:
        # Check log level first
        level_value = self.LEVELS.get(level.lower(), 0)
        if level_value < self.min_level_value:
            return False
        
        # Check function filtering if function is provided
        if function and self.exclude_functions:
            function_lower = function.lower()
            for exclude_func in self.exclude_functions:
                if exclude_func in function_lower:
                    return False
        
        # Check file filtering if function contains file info
        if function and self.exclude_files:
            function_lower = function.lower()
            for exclude_file in self.exclude_files:
                if exclude_file in function_lower:
                    return False
        
        return True

class SyslogParser:
    """Parse syslog-formatted messages from EnergyMe-Home"""
    
    # Regex pattern to match the new syslog format from ESP32
    # Format: <16>2025-08-08T18:36:33.275Z 588c81c47a98[9313286]: [DEBUG][Core1] src/utils.cpp[printDeviceStatusDynamic]: Message
    # Also supports: <16>2025-08-29T09:28:46.204Z [112882]: [DEBUG][Core1] src/ade7953.cpp[_printMeterValues]: Message
    SYSLOG_PATTERN = re.compile(
        r'<(\d+)>'                                  # Priority
        r'(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+Z)\s+'  # Timestamp (ISO 8601)
        r'(?:([a-fA-F0-9]+))?\[(\d+)\]:\s+'         # Optional Device[millis]: or just [millis]:
        r'\[([^\]]+)\]'                             # [level]
        r'\[Core(\d+)\]\s+'                         # [CoreX]
        r'([^:]+):\s+'                              # function: (e.g., src/utils.cpp[printDeviceStatusDynamic])
        r'(.+)'                                     # message
    )
    
    @classmethod
    def parse(cls, message: str) -> Optional[Dict[str, Any]]:
        """Parse a syslog message and extract components"""
        match = cls.SYSLOG_PATTERN.match(message.strip())
        if not match:
            return None
        
        priority, timestamp, device, millis, level, core, function, msg = match.groups()
        
        # Handle optional device ID - use 'unknown' if not present
        device_id = device.strip() if device else 'unknown'
        
        return {
            'priority': int(priority),
            'timestamp': timestamp,
            'device': device_id,
            'millis': int(millis),
            'level': level.lower(),
            'core': int(core),
            'function': function.strip(),
            'message': msg.strip(),
            'raw': message
        }

class UDPLogListener:
    """UDP log listener for EnergyMe-Home devices"""
    
    def __init__(self, host: str = '0.0.0.0', port: int = 514, log_filter: Optional[LogFilter] = None, 
                 log_file: Optional[str] = None, log_format: str = 'structured', multicast_group: Optional[str] = None,
                 auto_device_logs: bool = True, debug: bool = False):
        self.host = host
        self.port = port
        self.filter = log_filter or LogFilter()
        self.log_file = log_file
        self.log_format = log_format  # 'structured', 'raw', or 'json'
        self.multicast_group = multicast_group  # Multicast group IP
        self.auto_device_logs = auto_device_logs  # Enable automatic device-specific logging
        self.debug = debug  # Enable debug output
        self.file_handle: Optional[TextIO] = None
        self.socket = None
        self.running = False
        self.device_files: Dict[str, TextIO] = {}  # Track separate files per device (using session keys)
        self.device_last_millis: Dict[str, int] = {}  # Track last seen millis per device (using actual device IDs)
        self.device_session_ids: Dict[str, str] = {}  # Track current session ID per device
        self.device_last_display_millis: Dict[str, int] = {}  # Track last displayed millis per device for delta calculation
        self.stats = {
            'total_messages': 0,
            'parsed_messages': 0,
            'filtered_messages': 0,
            'logged_messages': 0,
            'start_time': None
        }
        
    def start(self):
        """Start the UDP listener"""
        # Set up signal handler for graceful shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        
        try:
            # Open log file if specified
            if self.log_file:
                try:
                    # Create directory if it doesn't exist
                    log_dir = os.path.dirname(self.log_file)
                    if log_dir and not os.path.exists(log_dir):
                        os.makedirs(log_dir)
                    
                    # Open file in append mode with UTF-8 encoding
                    self.file_handle = open(self.log_file, 'a', encoding='utf-8', buffering=1)  # Line buffering
                    print(f"📝 Logging to file: {self.log_file} (format: {self.log_format})")
                except Exception as e:
                    print(f"{Colors.ERROR}Error opening log file: {e}{Colors.RESET}")
                    self.log_file = None
            
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            
            # Set socket timeout to allow periodic checking for shutdown
            self.socket.settimeout(1.0)
            
            # Handle multicast setup
            if self.multicast_group:
                # On Windows, bind to INADDR_ANY and the specific port
                self.socket.bind(('', self.port))
                
                # Join the multicast group
                mreq = struct.pack("4sl", socket.inet_aton(self.multicast_group), socket.INADDR_ANY)
                self.socket.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
                
                # On Windows, also set multicast loopback and TTL
                self.socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
                self.socket.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 32)
                
                print(f"{Colors.BOLD}EnergyMe-Home UDP Log Listener (Multicast){Colors.RESET}")
                print(f"Listening on multicast group {self.multicast_group}:{self.port}")
            else:
                # Regular unicast/broadcast
                self.socket.bind((self.host, self.port))
                print(f"{Colors.BOLD}EnergyMe-Home UDP Log Listener{Colors.RESET}")
                print(f"Listening on {self.host}:{self.port}")
            
            self.running = True
            self.stats['start_time'] = time.time()
            
            print(f"Filter: {self.filter.min_level_value} and above")
            if self.filter.exclude_files:
                print(f"Excluding files: {', '.join(self.filter.exclude_files)}")
            if self.filter.exclude_functions:
                print(f"Excluding functions: {', '.join(self.filter.exclude_functions)}")
            if self.auto_device_logs:
                print("Device-specific logging: enabled (auto-creates separate txt files per device)")
            else:
                print("Device-specific logging: disabled")
            if self.debug:
                print("Debug mode: enabled")
            print(f"Press Ctrl+C to stop\n")
            
            if self.debug:
                print(f"{Colors.DEBUG}Debug: Socket created and bound successfully{Colors.RESET}")
                print(f"{Colors.DEBUG}Debug: Starting to listen for messages...{Colors.RESET}")
            
            # Write session header to log file
            if self.file_handle:
                session_start = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                self.file_handle.write(f"\n=== UDP Log Session Started: {session_start} ===\n")
                self.file_handle.flush()
            
            self._listen_loop()
            
        except PermissionError:
            print(f"{Colors.ERROR}Error: Permission denied. Try running as administrator or use a port > 1024{Colors.RESET}")
            sys.exit(1)
        except OSError as e:
            print(f"{Colors.ERROR}Error: {e}{Colors.RESET}")
            sys.exit(1)
        except KeyboardInterrupt:
            self._stop()
        finally:
            if self.file_handle:
                self.file_handle.close()
    
    def _signal_handler(self, signum, frame):
        """Handle Ctrl+C signal gracefully"""
        print(f"\n{Colors.WARNING}Received interrupt signal, stopping...{Colors.RESET}")
        self.running = False
    
    def _extract_device_id(self, device_string: str) -> str:
        """Extract the actual device ID (MAC address) from the device string"""
        # The device string might be like "22:20:55 588c81c47ad8" or just "588c81c47ad8"
        # We want to extract the MAC-like part (12 hex characters)
        import re
        match = re.search(r'([a-f0-9]{12})$', device_string.strip())
        if match:
            return match.group(1)
        # Fallback: clean the entire string
        return re.sub(r'[^\w\-_]', '_', device_string.strip())

    def _get_device_log_file(self, parsed: Dict[str, Any]) -> Optional[TextIO]:
        """Get or create a device-specific log file, creating new file on reboot detection"""
        device_raw = parsed['device']
        current_millis = parsed['millis']
        
        # Extract actual device ID (MAC address)
        actual_device_id = self._extract_device_id(device_raw)
        
        # Check for reboot indicators
        is_reboot = False
        reboot_reason = ""
        
        # Check for "Guess who's back" message indicating restart
        if "Guess who's back" in parsed['message']:
            is_reboot = True
            reboot_reason = "'Guess who's back' message"
        
        # Check for millis reset (current millis significantly lower than last seen)
        # Only check if we have previous millis and current is much smaller (indicating restart)
        elif actual_device_id in self.device_last_millis:
            last_millis = self.device_last_millis[actual_device_id]
            # More conservative: only if current millis is less than 10 minutes AND 
            # last millis was more than 30 minutes (to avoid false positives from short uptimes)
            if (current_millis < 600000 and  # Current millis < 10 minutes
                last_millis > 1800000 and   # Last millis was > 30 minutes  
                current_millis < last_millis):  # And it's actually lower
                is_reboot = True
                reboot_reason = f"millis reset from {last_millis}ms to {current_millis}ms"
        
        # Update last seen millis
        self.device_last_millis[actual_device_id] = current_millis
        
        # Reset delta tracking on reboot to avoid large negative deltas
        if is_reboot:
            self.device_last_display_millis.pop(actual_device_id, None)
        
        # Generate session key for file tracking
        if is_reboot or actual_device_id not in self.device_session_ids:
            # Create new session ID
            session_timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            self.device_session_ids[actual_device_id] = session_timestamp
            
            # Close existing file if it exists
            old_session_key = None
            for session_key in list(self.device_files.keys()):
                if session_key.startswith(f"{actual_device_id}_"):
                    old_session_key = session_key
                    break
            
            if old_session_key and old_session_key in self.device_files:
                self.device_files[old_session_key].close()
                del self.device_files[old_session_key]
                if is_reboot:
                    print(f"📱 Device {actual_device_id} reboot detected: {reboot_reason}")
                else:
                    print(f"📝 Starting log file for new device: {actual_device_id}")
        
        # Current session key
        session_key = f"{actual_device_id}_{self.device_session_ids[actual_device_id]}"
        
        # Create file if it doesn't exist
        if session_key not in self.device_files:
            # Create filename 
            filename = f"energyme_{session_key}.txt"
            
            # Create logs directory if it doesn't exist
            log_dir = "logs"
            if not os.path.exists(log_dir):
                os.makedirs(log_dir)
            
            filepath = os.path.join(log_dir, filename)
            
            try:
                # Open new file
                file_handle = open(filepath, 'w', encoding='utf-8', buffering=1)
                self.device_files[session_key] = file_handle
                
                # Write header
                session_start = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                file_handle.write(f"=== EnergyMe-Home Device Log: {actual_device_id} ===\n")
                file_handle.write(f"=== Session Started: {session_start} ===\n")
                if is_reboot:
                    file_handle.write(f"=== Device Reboot Detected: {reboot_reason} ===\n")
                file_handle.write("\n")
                file_handle.flush()
                
                print(f"📝 Created device log: {filepath}")
                
            except Exception as e:
                print(f"{Colors.ERROR}Error creating device log file: {e}{Colors.RESET}")
                return None
        
        return self.device_files.get(session_key)

    def _listen_loop(self):
        """Main listening loop"""
        message_count = 0
        while self.running:
            try:
                data, addr = self.socket.recvfrom(1024)
                message = data.decode('utf-8', errors='ignore')
                message_count += 1
                
                if self.debug and message_count <= 5:
                    print(f"{Colors.DEBUG}Debug: Received message #{message_count} from {addr}: {message[:100]}...{Colors.RESET}")
                
                self._handle_message(message, addr)
                
            except socket.timeout:
                # Timeout is expected, just continue to check if we should stop
                if self.debug and message_count == 0:
                    print(f"{Colors.DEBUG}Debug: Socket timeout (no messages received yet)...{Colors.RESET}")
                continue
            except KeyboardInterrupt:
                print(f"\n{Colors.WARNING}KeyboardInterrupt caught in listen loop{Colors.RESET}")
                break
            except Exception as e:
                if self.running:  # Only show error if we're still supposed to be running
                    print(f"{Colors.ERROR}Error receiving data: {e}{Colors.RESET}")
        
        print(f"{Colors.INFO}Listen loop ended (received {message_count} messages total){Colors.RESET}")
    def _handle_message(self, message: str, addr: tuple):
        """Handle received message"""
        self.stats['total_messages'] += 1
        
        # Try to parse as syslog format
        parsed = SyslogParser.parse(message)
        
        if parsed:
            self.stats['parsed_messages'] += 1
            
            # Log to device-specific file if enabled
            if self.auto_device_logs:
                device_file = self._get_device_log_file(parsed)
                if device_file:
                    self._log_to_device_file(device_file, parsed, addr)
            
            # Apply filter for display
            if not self.filter.should_show(parsed['level'], parsed['function']):
                self.stats['filtered_messages'] += 1
                # Still log filtered messages to main file if enabled
                if self.file_handle:
                    self._log_to_file(parsed, addr, message)
                return
            
            # Log to main file if enabled
            if self.file_handle:
                self._log_to_file(parsed, addr, message)
            
            self._display_parsed_message(parsed, addr)
        else:
            # Log raw message to main file if enabled
            if self.file_handle:
                self._log_to_file(None, addr, message)
            
            # Display raw message if parsing fails
            self._display_raw_message(message, addr)
    
    def _log_to_device_file(self, file_handle: TextIO, parsed: Dict[str, Any], addr: tuple):
        """Log parsed message to device-specific file in simple text format"""
        try:
            # Extract actual device ID for cleaner log format
            actual_device_id = self._extract_device_id(parsed['device'])
            # Use current time to ensure we have full timestamp with time
            current_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            # Format: YYYY-MM-DD HH:MM:SS DEVICE [MILLISms] LEVEL [CoreX] FUNCTION: MESSAGE
            line = f"{current_time} {actual_device_id} [{parsed['millis']}ms] {parsed['level'].upper():<7} [Core{parsed['core']}] {parsed['function']}: {parsed['message']}\n"
            file_handle.write(line)
            file_handle.flush()
            
        except Exception as e:
            print(f"{Colors.ERROR}Error writing to device log file: {e}{Colors.RESET}")

    def _display_parsed_message(self, parsed: Dict[str, Any], addr: tuple):
        """Display a parsed log message with formatting"""
        level_color = getattr(Colors, parsed['level'].upper(), Colors.RESET)
        
        # Extract actual device ID for delta tracking
        actual_device_id = self._extract_device_id(parsed['device'])
        current_millis = parsed['millis']
        
        # Check for reboot indicators
        is_reboot = False
        if "Guess who's back" in parsed['message']:
            is_reboot = True
        elif actual_device_id in self.device_last_display_millis:
            last_millis = self.device_last_display_millis[actual_device_id]
            # More conservative: only if current millis is less than 10 minutes AND 
            # last millis was more than 30 minutes (to avoid false positives from short uptimes)
            if (current_millis < 600000 and  # Current millis < 10 minutes
                last_millis > 1800000 and   # Last millis was > 30 minutes  
                current_millis < last_millis):  # And it's actually lower
                is_reboot = True
        
        # Calculate delta from last message
        delta_str = ""
        if actual_device_id in self.device_last_display_millis and not is_reboot:
            last_millis = self.device_last_display_millis[actual_device_id]
            delta = current_millis - last_millis
            if delta >= 0:  # Only show positive deltas (avoid negative on reboot)
                delta_str = f" (+{delta:03d}ms)"
        
        # Update last display millis (reset on reboot)
        self.device_last_display_millis[actual_device_id] = current_millis
        
        # Format timestamp
        timestamp = f"{Colors.TIMESTAMP}{parsed['timestamp']}{Colors.RESET}"
        
        # Format millis with delta (no device ID)
        millis_info = f"[{parsed['millis']}ms{delta_str}]"
        
        # Format level with color
        level_str = f"{level_color}{parsed['level'].upper():<7}{Colors.RESET}"
        
        # Format core info
        core_info = f"Core{parsed['core']}"
        
        # Format function
        function = f"{Colors.FUNCTION}{parsed['function']}{Colors.RESET}"
        
        # Print formatted message
        print(f"{timestamp} {millis_info} {level_str} [{core_info}] {function}: {parsed['message']}")
    
    def _display_raw_message(self, message: str, addr: tuple):
        """Display raw message when parsing fails"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"{Colors.DIM}{timestamp} [RAW from {addr[0]}]: {message.strip()}{Colors.RESET}")
    
    def _log_to_file(self, parsed: Optional[Dict[str, Any]], addr: tuple, raw_message: str):
        """Log message to file in specified format"""
        if not self.file_handle:
            return
        
        try:
            self.stats['logged_messages'] += 1
            current_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]  # Include milliseconds
            
            if self.log_format == 'json' and parsed:
                import json
                log_entry = {
                    'received_at': current_time,
                    'source_ip': addr[0],
                    'source_port': addr[1],
                    'timestamp': parsed['timestamp'],
                    'device': parsed['device'],
                    'millis': parsed['millis'],
                    'level': parsed['level'],
                    'core': parsed['core'],
                    'function': parsed['function'],
                    'message': parsed['message'],
                    'priority': parsed['priority']
                }
                self.file_handle.write(json.dumps(log_entry) + '\n')
                
            elif self.log_format == 'structured':
                if parsed:
                    # Structured format: RECEIVED_TIME [SOURCE_IP] ESP_TIME DEVICE[MILLIS] LEVEL [CORE] FUNCTION: MESSAGE
                    line = f"{current_time} [{addr[0]}] {parsed['timestamp']} {parsed['device']}[{parsed['millis']}ms] {parsed['level'].upper():<7} [Core{parsed['core']}] {parsed['function']}: {parsed['message']}\n"
                else:
                    # Raw message with metadata
                    line = f"{current_time} [{addr[0]}] [RAW] {raw_message.strip()}\n"
                self.file_handle.write(line)
                
            else:  # raw format
                # Just the original message with timestamp and source
                line = f"{current_time} [{addr[0]}] {raw_message.strip()}\n"
                self.file_handle.write(line)
            
            self.file_handle.flush()  # Ensure immediate write
            
        except Exception as e:
            print(f"{Colors.ERROR}Error writing to log file: {e}{Colors.RESET}")
    
    def _stop(self):
        """Stop the listener and show statistics"""
        self.running = False
        
        # Leave multicast group if we joined one
        if self.socket and self.multicast_group:
            try:
                mreq = struct.pack("4sl", socket.inet_aton(self.multicast_group), socket.INADDR_ANY)
                self.socket.setsockopt(socket.IPPROTO_IP, socket.IP_DROP_MEMBERSHIP, mreq)
            except Exception as e:
                print(f"{Colors.ERROR}Error leaving multicast group: {e}{Colors.RESET}")
        
        if self.socket:
            self.socket.close()
        
        # Close all device-specific files
        for session_key, file_handle in self.device_files.items():
            try:
                session_end = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                file_handle.write(f"\n=== Session Ended: {session_end} ===\n")
                file_handle.close()
                device_id = session_key.split('_')[0]  # Extract device ID from session key
                print(f"📝 Closed device log for: {device_id}")
            except Exception as e:
                print(f"{Colors.ERROR}Error closing device log for {session_key}: {e}{Colors.RESET}")
        
        # Write session end to main log file
        if self.file_handle:
            session_end = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            self.file_handle.write(f"=== UDP Log Session Ended: {session_end} ===\n\n")
            self.file_handle.close()
        
        print(f"\n{Colors.BOLD}Statistics:{Colors.RESET}")
        runtime = time.time() - self.stats['start_time'] if self.stats['start_time'] else 0
        print(f"Runtime: {runtime:.1f} seconds")
        print(f"Total messages: {self.stats['total_messages']}")
        print(f"Parsed messages: {self.stats['parsed_messages']}")
        print(f"Filtered messages: {self.stats['filtered_messages']}")
        print(f"Displayed messages: {self.stats['parsed_messages'] - self.stats['filtered_messages']}")
        if self.log_file:
            print(f"Logged messages: {self.stats['logged_messages']}")
            print(f"Log file: {self.log_file}")
        if self.device_files:
            print(f"Device-specific log files created: {len(self.device_files)}")

def main():
    parser = argparse.ArgumentParser(
        description='UDP Log Listener for EnergyMe-Home',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument(
        '--multicast',
        default='239.255.255.250',
        help='Multicast group IP to listen on (default: 239.255.255.250 for EnergyMe-Home)'
    )
    
    parser.add_argument(
        '--unicast',
        action='store_true',
        help='Use unicast instead of multicast (listen on --host IP)'
    )
    
    parser.add_argument(
        '--host', 
        default='0.0.0.0',
        help='Host to bind to for unicast mode (default: 0.0.0.0 for all interfaces)'
    )
    
    parser.add_argument(
        '--port', 
        type=int, 
        default=514,
        help='Port to listen on (default: 514)'
    )
    
    parser.add_argument(
        '--filter',
        choices=['verbose', 'debug', 'info', 'warning', 'error', 'fatal'],
        default='verbose',
        help='Minimum log level to display (default: verbose)'
    )
    
    parser.add_argument(
        '--exclude-files',
        nargs='*',
        help='Exclude log messages from specific files/modules (e.g., --exclude-files src/ade7953.cpp src/utils.cpp)'
    )
    
    parser.add_argument(
        '--exclude-functions',
        nargs='*', 
        help='Exclude log messages from specific functions (e.g., --exclude-functions _printMeterValues printStatus)'
    )
    
    parser.add_argument(
        '--no-color',
        action='store_true',
        help='Disable colored output'
    )
    
    parser.add_argument(
        '--log-file',
        help='Save logs to file (e.g., logs/energyme.log)'
    )
    
    parser.add_argument(
        '--log-format',
        choices=['structured', 'raw', 'json'],
        default='structured',
        help='Log file format (default: structured)'
    )
    
    parser.add_argument(
        '--auto-device-logs',
        action='store_true',
        default=True,
        help='Automatically create device-specific log files (default: enabled)'
    )
    
    parser.add_argument(
        '--no-auto-device-logs',
        action='store_true',
        help='Disable automatic device-specific log files'
    )
    
    parser.add_argument(
        '--debug',
        action='store_true',
        help='Enable debug output for troubleshooting'
    )
    
    args = parser.parse_args()
    
    # Auto-generate log filename if not specified but logging requested
    if args.log_file == '':
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.log_file = f"energyme_udp_{timestamp}.log"
    
    # Disable colors if requested
    if args.no_color:
        for attr in dir(Colors):
            if not attr.startswith('_'):
                setattr(Colors, attr, '')
    
    # Create filter
    log_filter = LogFilter(args.filter, args.exclude_files, args.exclude_functions)
    
    # Determine multicast group
    multicast_group = None if args.unicast else args.multicast
    
    # Determine auto device logs setting
    auto_device_logs = args.auto_device_logs and not args.no_auto_device_logs
    
    # Start listener
    listener = UDPLogListener(args.host, args.port, log_filter, args.log_file, args.log_format, multicast_group, auto_device_logs, args.debug)
    listener.start()

if __name__ == '__main__':
    main()
