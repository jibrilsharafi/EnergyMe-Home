#!/usr/bin/env python3
"""
ESP32 Serial Monitor with colored log formatting
Monitors EnergyMe-Home ESP32 logs and formats them nicely
"""

import serial
import serial.tools.list_ports
import re
import sys
import time
from datetime import datetime
from typing import Optional

# ANSI Color codes
class Colors:
    RESET = '\033[0m'
    BOLD = '\033[1m'
    DIM = '\033[2m'
    
    # Text colors
    BLACK = '\033[30m'
    RED = '\033[31m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    BLUE = '\033[34m'
    MAGENTA = '\033[35m'
    CYAN = '\033[36m'
    WHITE = '\033[37m'
    
    # Bright colors
    BRIGHT_RED = '\033[91m'
    BRIGHT_GREEN = '\033[92m'
    BRIGHT_YELLOW = '\033[93m'
    BRIGHT_BLUE = '\033[94m'
    BRIGHT_MAGENTA = '\033[95m'
    BRIGHT_CYAN = '\033[96m'
    BRIGHT_WHITE = '\033[97m'
    
    # Background colors
    BG_RED = '\033[41m'
    BG_GREEN = '\033[42m'
    BG_YELLOW = '\033[43m'
    BG_BLUE = '\033[44m'
    BG_MAGENTA = '\033[45m'
    BG_CYAN = '\033[46m'

class LogFormatter:
    def __init__(self):
        # Regex pattern for EnergyMe logs
        self.log_pattern = re.compile(
            r'\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\] '  # timestamp
            r'\[(\d+\s+\d+\s+ms)\] '                        # millis
            r'\[([A-Z]+)\s*\] '                            # log level
            r'\[Core\s+(\d+)\] '                           # core
            r'\[([^\]]+)\] '                               # component
            r'(.+)'                                        # message
        )
        
        # Log level colors
        self.level_colors = {
            'VERBOSE': Colors.DIM + Colors.WHITE,
            'DEBUG': Colors.CYAN,
            'INFO': Colors.GREEN,
            'WARNING': Colors.YELLOW,
            'ERROR': Colors.RED,
            'FATAL': Colors.BG_RED + Colors.BRIGHT_WHITE
        }
        
        # Component colors (cycling through colors)
        self.component_colors = [
            Colors.BLUE, Colors.MAGENTA, Colors.CYAN, 
            Colors.BRIGHT_BLUE, Colors.BRIGHT_MAGENTA, Colors.BRIGHT_CYAN
        ]
        self.component_color_map = {}
        self.color_index = 0
    
    def get_component_color(self, component: str) -> str:
        """Get consistent color for component"""
        if component not in self.component_color_map:
            self.component_color_map[component] = self.component_colors[self.color_index]
            self.color_index = (self.color_index + 1) % len(self.component_colors)
        return self.component_color_map[component]
    
    def format_log(self, line: str) -> str:
        """Format a single log line with colors"""
        match = self.log_pattern.match(line.strip())
        if not match:
            # Not a structured log, return as-is with dim color
            return f"{Colors.DIM}{line}{Colors.RESET}"
        
        timestamp, millis, level, core, component, message = match.groups()
        
        # Get colors
        level_color = self.level_colors.get(level, Colors.WHITE)
        component_color = self.get_component_color(component)
        
        # Format components
        formatted_timestamp = f"{Colors.DIM}[{timestamp}]{Colors.RESET}"
        formatted_millis = f"{Colors.DIM}[{millis}]{Colors.RESET}"
        formatted_level = f"{level_color}[{level:8}]{Colors.RESET}"
        formatted_core = f"{Colors.DIM}[Core {core}]{Colors.RESET}"
        formatted_component = f"{component_color}[{component}]{Colors.RESET}"
        
        # Special formatting for certain message types
        formatted_message = self.format_message(message, component, level)
        
        return (f"{formatted_timestamp} {formatted_millis} {formatted_level} "
                f"{formatted_core} {formatted_component} {formatted_message}")
    
    def format_message(self, message: str, component: str, level: str) -> str:
        """Apply special formatting to message content"""
        # Highlight numbers and units for meter values
        if "V |" in message and "A ||" in message:  # Energy meter reading
            # Highlight voltage, current, power values
            message = re.sub(r'(\d+\.\d+)\s*V', f'{Colors.BRIGHT_YELLOW}\\1 V{Colors.RESET}', message)
            message = re.sub(r'(\d+\.\d+)\s*A', f'{Colors.BRIGHT_CYAN}\\1 A{Colors.RESET}', message)
            message = re.sub(r'(\d+\.\d+)\s*W', f'{Colors.BRIGHT_GREEN}\\1 W{Colors.RESET}', message)
            message = re.sub(r'(\d+\.\d+)\s*PF', f'{Colors.BRIGHT_MAGENTA}\\1 PF{Colors.RESET}', message)
        
        # Highlight JSON content
        elif message.startswith('JSON'):
            if 'successfully' in message.lower():
                message = f"{Colors.GREEN}{message}{Colors.RESET}"
            elif 'failed' in message.lower() or 'error' in message.lower():
                message = f"{Colors.RED}{message}{Colors.RESET}"
        
        # Highlight heap/memory info
        elif 'Heap:' in message:
            message = re.sub(r'(\d+\.\d+%)', f'{Colors.BRIGHT_YELLOW}\\1{Colors.RESET}', message)
            message = re.sub(r'(\d+\s+free)', f'{Colors.GREEN}\\1{Colors.RESET}', message)
            message = re.sub(r'(\d+\s+bytes)', f'{Colors.CYAN}\\1{Colors.RESET}', message)
        
        # Highlight IP addresses
        elif 'IP:' in message:
            message = re.sub(r'(\d+\.\d+\.\d+\.\d+)', f'{Colors.BRIGHT_BLUE}\\1{Colors.RESET}', message)
        
        # Highlight URLs
        elif 'URL:' in message:
            message = re.sub(r'(URL: [^\s]+)', f'{Colors.BRIGHT_CYAN}\\1{Colors.RESET}', message)
        
        # Default message color based on level
        elif level == 'ERROR' or level == 'FATAL':
            message = f"{Colors.BRIGHT_RED}{message}{Colors.RESET}"
        elif level == 'WARNING':
            message = f"{Colors.BRIGHT_YELLOW}{message}{Colors.RESET}"
        elif level == 'INFO':
            message = f"{Colors.BRIGHT_WHITE}{message}{Colors.RESET}"
        
        return message

def scan_serial_ports():
    """Scan and return available serial ports"""
    ports = serial.tools.list_ports.comports()
    available_ports = []
    
    print(f"{Colors.CYAN}Scanning for serial ports...{Colors.RESET}")
    
    for port in ports:
        # Look for ESP32 or USB-to-Serial devices
        if any(keyword in port.description.lower() for keyword in 
               ['esp32', 'silicon labs', 'ch340', 'cp210', 'ftdi', 'usb serial']):
            available_ports.append(port)
            print(f"{Colors.GREEN}Found: {port.device} - {port.description}{Colors.RESET}")
        else:
            print(f"{Colors.DIM}Skipped: {port.device} - {port.description}{Colors.RESET}")
    
    return available_ports

def select_port(available_ports) -> Optional[str]:
    """Let user select a serial port"""
    if not available_ports:
        print(f"{Colors.RED}No suitable serial ports found!{Colors.RESET}")
        return None
    
    if len(available_ports) == 1:
        port = available_ports[0].device
        print(f"{Colors.GREEN}Auto-selected: {port}{Colors.RESET}")
        return port
    
    print(f"\n{Colors.YELLOW}Multiple ports found. Please select:{Colors.RESET}")
    for i, port in enumerate(available_ports):
        print(f"{Colors.CYAN}{i+1}. {port.device} - {port.description}{Colors.RESET}")
    
    while True:
        try:
            choice = input(f"\n{Colors.WHITE}Enter port number (1-{len(available_ports)}): {Colors.RESET}")
            index = int(choice) - 1
            if 0 <= index < len(available_ports):
                return available_ports[index].device
            else:
                print(f"{Colors.RED}Invalid choice. Please try again.{Colors.RESET}")
        except ValueError:
            print(f"{Colors.RED}Please enter a number.{Colors.RESET}")
        except KeyboardInterrupt:
            print(f"\n{Colors.RED}Cancelled by user.{Colors.RESET}")
            return None

def monitor_serial(port: str, baudrate: int = 115200):
    """Monitor serial port and format logs"""
    formatter = LogFormatter()
    
    try:
        print(f"{Colors.GREEN}Connecting to {port} at {baudrate} baud...{Colors.RESET}")
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"{Colors.GREEN}Connected! Monitoring logs...{Colors.RESET}")
        print(f"{Colors.DIM}{'='*80}{Colors.RESET}")
        
        while True:
            try:
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='replace').strip()
                    if line:
                        formatted_line = formatter.format_log(line)
                        print(formatted_line)
                else:
                    time.sleep(0.01)  # Small delay to prevent busy waiting
                    
            except UnicodeDecodeError:
                # Skip lines that can't be decoded
                continue
                
    except serial.SerialException as e:
        print(f"{Colors.RED}Serial error: {e}{Colors.RESET}")
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}Monitoring stopped by user.{Colors.RESET}")
    finally:
        if 'ser' in locals():
            ser.close()
            print(f"{Colors.GREEN}Serial port closed.{Colors.RESET}")

def main():
    print(f"{Colors.BOLD}{Colors.BRIGHT_CYAN}EnergyMe-Home Serial Monitor{Colors.RESET}")
    print(f"{Colors.DIM}Press Ctrl+C to stop monitoring{Colors.RESET}\n")
    
    # Scan for ports
    available_ports = scan_serial_ports()
    
    # Select port
    selected_port = select_port(available_ports)
    if not selected_port:
        sys.exit(1)
    
    # Monitor the port
    monitor_serial(selected_port)

if __name__ == "__main__":
    main()
