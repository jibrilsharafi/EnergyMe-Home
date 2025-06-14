#!/usr/bin/env python3
"""
UDP Log Listener for EnergyMe-Home
==================================

This script listens for UDP broadcast messages from EnergyMe-Home devices
and displays them in a formatted way. It's designed to work with the syslog
format used by the ESP32 firmware.

Usage:
    python udp_log_listener.py [--port 514] [--host 0.0.0.0] [--filter LEVEL]

Examples:
    python udp_log_listener.py                    # Listen on default port 514
    python udp_log_listener.py --port 1514        # Listen on custom port
    python udp_log_listener.py --filter info      # Only show info and above
    python udp_log_listener.py --filter debug     # Show debug and above
"""

import socket
import argparse
import sys
import time
import re
import os
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
    """Filter logs by level"""
    LEVELS = {
        'verbose': 0,
        'debug': 1,
        'info': 2,
        'warning': 3,
        'error': 4,
        'fatal': 5
    }
    
    def __init__(self, min_level: str = 'verbose'):
        self.min_level_value = self.LEVELS.get(min_level.lower(), 0)
    
    def should_show(self, level: str) -> bool:
        level_value = self.LEVELS.get(level.lower(), 0)
        return level_value >= self.min_level_value

class SyslogParser:
    """Parse syslog-formatted messages from EnergyMe-Home"""
    
    # Regex pattern to match the syslog format from ESP32
    # Format: <16>2024-12-13 10:30:45 energyme-abc123[12345]: [info][Core0] main: Setup done!
    SYSLOG_PATTERN = re.compile(
        r'<(\d+)>'                    # Priority
        r'([^>]+?)\s+'                # Timestamp
        r'([^\[]+)\[(\d+)\]:\s+'      # Device[millis]:
        r'\[([^\]]+)\]'               # [level]
        r'\[Core(\d+)\]\s+'           # [CoreX]
        r'([^:]+):\s+'                # function:
        r'(.+)'                       # message
    )
    
    @classmethod
    def parse(cls, message: str) -> Optional[Dict[str, Any]]:
        """Parse a syslog message and extract components"""
        match = cls.SYSLOG_PATTERN.match(message.strip())
        if not match:
            return None
        
        priority, timestamp, device, millis, level, core, function, msg = match.groups()
        
        return {
            'priority': int(priority),
            'timestamp': timestamp,
            'device': device.strip(),
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
                 log_file: Optional[str] = None, log_format: str = 'structured'):
        self.host = host
        self.port = port
        self.filter = log_filter or LogFilter()
        self.log_file = log_file
        self.log_format = log_format  # 'structured', 'raw', or 'json'
        self.file_handle: Optional[TextIO] = None
        self.socket = None
        self.running = False
        self.stats = {
            'total_messages': 0,
            'parsed_messages': 0,
            'filtered_messages': 0,
            'logged_messages': 0,
            'start_time': None
        }
        
    def start(self):
        """Start the UDP listener"""
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
            self.socket.bind((self.host, self.port))
            self.running = True
            self.stats['start_time'] = time.time()
            
            print(f"{Colors.BOLD}EnergyMe-Home UDP Log Listener{Colors.RESET}")
            print(f"Listening on {self.host}:{self.port}")
            print(f"Filter: {self.filter.min_level_value} and above")
            print(f"Press Ctrl+C to stop\n")
            
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
    
    def _listen_loop(self):
        """Main listening loop"""
        while self.running:
            try:
                data, addr = self.socket.recvfrom(1024)
                message = data.decode('utf-8', errors='ignore')
                self._handle_message(message, addr)
                
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"{Colors.ERROR}Error receiving data: {e}{Colors.RESET}")
    def _handle_message(self, message: str, addr: tuple):
        """Handle received message"""
        self.stats['total_messages'] += 1
        
        # Try to parse as syslog format
        parsed = SyslogParser.parse(message)
        
        if parsed:
            self.stats['parsed_messages'] += 1
            
            # Apply filter
            if not self.filter.should_show(parsed['level']):
                self.stats['filtered_messages'] += 1
                # Still log filtered messages to file if enabled
                if self.file_handle:
                    self._log_to_file(parsed, addr, message)
                return
            
            # Log to file before displaying
            if self.file_handle:
                self._log_to_file(parsed, addr, message)
            
            self._display_parsed_message(parsed, addr)
        else:
            # Log raw message to file
            if self.file_handle:
                self._log_to_file(None, addr, message)
            
            # Display raw message if parsing fails
            self._display_raw_message(message, addr)
    
    def _display_parsed_message(self, parsed: Dict[str, Any], addr: tuple):
        """Display a parsed log message with formatting"""
        level_color = getattr(Colors, parsed['level'].upper(), Colors.RESET)
        
        # Format timestamp
        timestamp = f"{Colors.TIMESTAMP}{parsed['timestamp']}{Colors.RESET}"
        
        # Format device info
        device_info = f"{Colors.DEVICE}{parsed['device']}{Colors.RESET} [{parsed['millis']}ms]"
        
        # Format level with color
        level_str = f"{level_color}{parsed['level'].upper():<7}{Colors.RESET}"
        
        # Format core info
        core_info = f"Core{parsed['core']}"
        
        # Format function
        function = f"{Colors.FUNCTION}{parsed['function']}{Colors.RESET}"
        
        # Print formatted message
        print(f"{timestamp} {device_info} {level_str} [{core_info}] {function}: {parsed['message']}")
    
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
        if self.socket:
            self.socket.close()
        
        # Write session end to log file
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

def main():
    parser = argparse.ArgumentParser(
        description='UDP Log Listener for EnergyMe-Home',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument(
        '--host', 
        default='0.0.0.0',
        help='Host to bind to (default: 0.0.0.0 for all interfaces)'
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
    log_filter = LogFilter(args.filter)
    
    # Start listener
    listener = UDPLogListener(args.host, args.port, log_filter, args.log_file, args.log_format)
    listener.start()

if __name__ == '__main__':
    main()
