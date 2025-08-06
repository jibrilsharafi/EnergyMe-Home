"""
PlatformIO Monitor Filter for EnergyMe-Home Project

This filter adds ANSI color coding to serial monitor output based on log level and function names.
It buffers incoming data to handle chunked serial communication and only outputs complete lines.

Log format expected: [timestamp] [uptime] [LEVEL] [Core] [function] message

Colors:
- DEBUG: Cyan
- INFO: Green  
- WARN: Yellow
- ERROR: Red
- FATAL: Magenta
- Functions: Rotating palette of 6 colors for visual distinction
- Timestamps/Core: Dimmed gray to reduce visual noise
"""

from platformio.device.monitor.filters.base import DeviceMonitorFilterBase


class Colors(DeviceMonitorFilterBase):
    NAME = "colors"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.buffer = ""  # Accumulate incomplete lines from chunked serial data
        print("--- Colors filter loaded - buffering lines for coloring")

    def rx(self, text):
        """
        Process received data from device.
        
        Handles chunked serial data by buffering until complete lines are available,
        then applies color coding based on log level and function name.
        
        Args:
            text: Raw text received from serial port (may be partial)
            
        Returns:
            Colored complete lines with ANSI escape codes, or empty string if no complete lines
        """
        import re
        
        # ANSI color codes for log levels
        RESET = '\033[0m'
        DIM = '\033[2m'           # For timestamps and core info
        DEBUG_COLOR = '\033[36m'  # Cyan
        INFO_COLOR = '\033[32m'   # Green
        WARN_COLOR = '\033[33m'   # Yellow
        ERROR_COLOR = '\033[31m'  # Red
        FATAL_COLOR = '\033[35m'  # Magenta
        
        # Function name colors - rotating palette for visual distinction
        FUNC_COLORS = [
            '\033[94m',  # Light Blue
            '\033[96m',  # Light Cyan  
            '\033[92m',  # Light Green
            '\033[93m',  # Light Yellow
            '\033[95m',  # Light Magenta
            '\033[97m',  # White
        ]
        
        # Buffer incoming data until we have complete lines
        self.buffer += text
        lines = self.buffer.split('\n')
        
        # Keep incomplete line in buffer, process complete ones
        self.buffer = lines[-1]
        complete_lines = lines[:-1]
        
        if not complete_lines:
            return ""  # Don't output partial data - wait for complete lines
            
        colored_lines = []
        
        for line in complete_lines:
            line = line.rstrip('\r')  # Handle Windows line endings
            
            if not line.strip():
                colored_lines.append(line)
                continue
            
            # Parse EnergyMe log format: [timestamp] [uptime] [LEVEL] [Core] [function] message
            log_pattern = r'(\[.*?\])\s*(\[.*?\])\s*\[(\w+)\s*\]\s*(\[.*?\])\s*\[(\w+)\]\s*(.*)'
            match = re.match(log_pattern, line.strip())
            
            if not match:
                # Non-matching lines pass through unchanged
                colored_lines.append(line)
                continue
                
            timestamp, uptime, level, core, function, message = match.groups()
            
            # Map log levels to colors
            level_colors = {
                'DEBUG': DEBUG_COLOR,
                'INFO': INFO_COLOR,
                'WARN': WARN_COLOR,
                'ERROR': ERROR_COLOR,
                'FATAL': FATAL_COLOR
            }
            level_color = level_colors.get(level, RESET)
            
            # Assign consistent color to each function using hash
            func_color = FUNC_COLORS[hash(function) % len(FUNC_COLORS)]
            
            # Build colorized line with proper spacing
            colored_line = (
                f"{DIM}{timestamp} {uptime}{RESET} "       # Dimmed timestamp/uptime
                f"{level_color}[{level.ljust(7)}]{RESET} " # Level in its color
                f"{DIM}{core}{RESET} "                     # Dimmed core info
                f"{func_color}[{function}]{RESET} "        # Function in unique color
                f"{level_color}{message}{RESET}"           # Message in level color
            )
            
            colored_lines.append(colored_line)
        
        # Return complete colored lines with newlines
        result = '\n'.join(colored_lines)
        if colored_lines:
            result += '\n'
            
        return result

    def tx(self, text):
        """Process transmitted data to device - no coloring needed for outgoing data"""
        return text