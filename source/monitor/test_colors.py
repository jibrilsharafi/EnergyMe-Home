#!/usr/bin/env python3

"""
Test Script for EnergyMe-Home PlatformIO Monitor Color Filter

This script simulates the color filter behavior to test:
1. Buffering logic for chunked serial data
2. Log parsing and color application
3. Complete line processing

Run with: python3 test_colors.py

The script demonstrates how the filter handles real-world serial communication
where data arrives in small chunks rather than complete lines.
"""

import re

class TestColorsFilter:
    """
    Test implementation of the Colors filter.
    
    Simulates the actual PlatformIO filter behavior for testing and debugging.
    Uses the same buffering and coloring logic as the real filter.
    """
    
    def __init__(self):
        self.buffer = ""  # Simulates the filter's line buffer

    def rx(self, text):
        """
        Simulate received data processing.
        
        Args:
            text: Incoming text chunk (may be partial line)
            
        Returns:
            Colored complete lines or empty string for partial data
        """
        import re
        
        # ANSI color definitions - same as actual filter
        RESET = '\033[0m'
        DIM = '\033[2m'
        VERBOSE_COLOR = '\033[90m'  # Light Gray
        DEBUG_COLOR = '\033[36m'    # Cyan
        INFO_COLOR = '\033[32m'     # Green
        WARNING_COLOR = '\033[33m'  # Yellow
        ERROR_COLOR = '\033[31m'    # Red
        FATAL_COLOR = '\033[35m'    # Magenta
        
        # Function color palette - consistent assignment per function
        FUNC_COLORS = [
            '\033[94m',  # Light Blue
            '\033[96m',  # Light Cyan  
            '\033[92m',  # Light Green
            '\033[93m',  # Light Yellow
            '\033[95m',  # Light Magenta
            '\033[97m',  # White
        ]
        
        # Accumulate data in buffer until complete lines available
        self.buffer += text
        lines = self.buffer.split('\n')
        
        self.buffer = lines[-1]  # Keep incomplete line
        complete_lines = lines[:-1]  # Process complete lines
        
        if not complete_lines:
            return ""  # Wait for complete lines before outputting
            
        colored_lines = []
        
        for line in complete_lines:
            line = line.rstrip('\r')  # Handle different line endings
            
            if not line.strip():
                colored_lines.append(line)
                continue
            
            # Parse EnergyMe log format
            log_pattern = r'(\[.*?\])\s*(\[.*?\])\s*\[(\w+)\s*\]\s*(\[.*?\])\s*\[(\w+)\]\s*(.*)'
            match = re.match(log_pattern, line.strip())
            
            if not match:
                # Pass through non-log lines unchanged
                colored_lines.append(line)
                continue
                
            timestamp, uptime, level, core, function, message = match.groups()
            
            # Map log levels to colors
            level_colors = {
                'VERBOSE': VERBOSE_COLOR,
                'DEBUG': DEBUG_COLOR,
                'INFO': INFO_COLOR,
                'WARNING': WARNING_COLOR,
                'ERROR': ERROR_COLOR,
                'FATAL': FATAL_COLOR
            }
            level_color = level_colors.get(level, RESET)
            
            # Consistent function coloring using hash
            func_color = FUNC_COLORS[hash(function) % len(FUNC_COLORS)]
            
            # Construct colored line with proper formatting
            colored_line = (
                f"{DIM}{timestamp} {uptime}{RESET} "
                f"{level_color}[{level.ljust(7)}]{RESET} "
                f"{DIM}{core}{RESET} "
                f"{func_color}[{function}]{RESET} "
                f"{level_color}{message}{RESET}"
            )
            
            colored_lines.append(colored_line)
        
        result = '\n'.join(colored_lines)
        if colored_lines:
            result += '\n'
            
        return result

def test_buffering():
    """
    Test the filter's ability to handle chunked serial data.
    
    Simulates how serial communication delivers data in small chunks
    rather than complete lines, testing the buffering mechanism.
    """
    
    print("Testing buffering logic with chunked serial data...")
    print("="*60)
    
    # Real chunked data from EnergyMe serial output
    # Shows how a single log line arrives in multiple chunks
    chunks = [
        "2",
        "025-07-30 09:37:16] [255 958 ms",
        "]",
        " [DEBUG   ] [Core 1] [utils] Wi",
        "F",
        "i: IP 192.168.1.123 | Gateway 1",
        "9",
        "2.168.1.254 | DNS 192.168.1.254",
        " ",
        "| Subnet 255.255.255.0\r\n[2025-0",  # First complete line ends here
        "7",
        "-30 09:37:16] [255 970 ms] [DEB",
        "U",
        "G   ] [Core 1] [utils] --------",
        "-",
        "----------------\r\n[2025-07-30 0",  # Second complete line
        "9",
        ":37:16] [255 982 ms] [DEBUG   ]",
        " ",
        "[Core 1] [utils] Maintenance ch",
        "e",
        "cks completed\r\n"  # Third complete line
    ]
    
    filter = TestColorsFilter()
    
    print("Processing chunks:")
    for i, chunk in enumerate(chunks):
        print(f"Chunk {i+1}: {repr(chunk)}")
        result = filter.rx(chunk)
        if result.strip():  # Only show when complete lines are output
            print(f"Output: {repr(result)}")
            print(f"Colored output:")
            print(result, end='')
            print("-" * 40)
    
    print(f"\nFinal buffer: {repr(filter.buffer)}")

def test_complete_lines():
    """
    Test the filter with complete log lines.
    
    Verifies that the regex parsing and color application work correctly
    when given properly formatted complete log lines.
    """
    
    print("\n\nTesting with complete log lines...")
    print("="*60)
    
    # Complete EnergyMe log lines for testing
    complete_lines = [
        "[2025-07-30 09:37:16] [255 958 ms] [DEBUG   ] [Core 1] [utils] WiFi: IP 192.168.1.123 | Gateway 192.168.1.254 | DNS 192.168.1.254 | Subnet 255.255.255.0\n",
        "[2025-07-30 09:37:16] [255 970 ms] [DEBUG   ] [Core 1] [utils] ------------------------\n",
        "[2025-07-30 09:37:16] [255 982 ms] [DEBUG   ] [Core 1] [utils] Maintenance checks completed\n"
    ]
    
    filter = TestColorsFilter()
    
    for line in complete_lines:
        print(f"Input: {repr(line)}")
        result = filter.rx(line)
        print(f"Colored output:")
        print(result, end='')
        print("-" * 40)

if __name__ == "__main__":
    test_buffering()
    test_complete_lines()
