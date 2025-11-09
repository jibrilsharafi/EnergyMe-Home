import requests
from requests.auth import HTTPDigestAuth
import numpy as np
import matplotlib.pyplot as plt
from dataclasses import dataclass
from typing import List, Dict, Optional
import time
import argparse


@dataclass
class WaveformData:
    """Container for waveform capture data"""
    channel_index: int
    sample_count: int
    capture_start_unix_millis: int
    voltage: List[float]
    current: List[float]
    micros_delta: List[int]
    
    @property
    def time_seconds(self) -> np.ndarray:
        """Convert microseconds delta to seconds array"""
        return np.array(self.micros_delta) / 1_000_000


@dataclass
class ComputedProperties:
    """Electrical properties computed from waveform"""
    voltage_rms: float
    current_rms: float
    active_power: float
    reactive_power: float
    apparent_power: float
    power_factor: float
    frequency: float
    voltage_thd: float
    current_thd: float
    voltage_shifted: np.ndarray  # Phase-shifted voltage for plotting
    phase_shift_degrees: float  # Applied phase shift


class EnergyMeterAPI:
    """Client for interacting with the energy meter API"""
    
    def __init__(self, base_url: str, username: str = "admin", password: str = "energyme00"):
        self.base_url = base_url.rstrip('/')
        self.auth = HTTPDigestAuth(username, password)
        self.session = requests.Session()
        
    def arm_waveform_capture(self, channel_index: int) -> Dict:
        """Arm waveform capture for a specific channel"""
        url = f"{self.base_url}/api/v1/ade7953/waveform/arm"
        payload = {"channelIndex": channel_index}
        response = self.session.post(url, json=payload, auth=self.auth)
        response.raise_for_status()
        return response.json()
    
    def get_capture_status(self) -> Dict:
        """Get current waveform capture status"""
        url = f"{self.base_url}/api/v1/ade7953/waveform/status"
        response = self.session.get(url, auth=self.auth)
        response.raise_for_status()
        return response.json()
    
    def get_waveform_data(self) -> WaveformData:
        """Retrieve captured waveform data"""
        url = f"{self.base_url}/api/v1/ade7953/waveform/data"
        response = self.session.get(url, auth=self.auth)
        response.raise_for_status()
        data = response.json()
        
        return WaveformData(
            channel_index=data['channelIndex'],
            sample_count=data['sampleCount'],
            capture_start_unix_millis=data['captureStartUnixMillis'],
            voltage=data['voltage'],
            current=data['current'],
            micros_delta=data['microsDelta']
        )
    
    def get_meter_values(self, channel_index: Optional[int] = None) -> Dict:
        """Get real-time meter values"""
        url = f"{self.base_url}/api/v1/ade7953/meter-values"
        params = {"index": channel_index} if channel_index is not None else {}
        response = self.session.get(url, params=params, auth=self.auth)
        response.raise_for_status()
        return response.json()
    
    def get_channel_config(self, channel_index: Optional[int] = None) -> Dict:
        """Get channel configuration"""
        url = f"{self.base_url}/api/v1/ade7953/channel"
        params = {"index": channel_index} if channel_index is not None else {}
        response = self.session.get(url, params=params, auth=self.auth)
        response.raise_for_status()
        return response.json()
    
    def capture_waveform_blocking(self, channel_index: int, timeout: int = 10) -> WaveformData:
        """Arm capture and wait for completion"""
        print(f"Arming waveform capture for channel {channel_index}...")
        self.arm_waveform_capture(channel_index)
        
        start_time = time.time()
        while time.time() - start_time < timeout:
            status = self.get_capture_status()
            state = status.get('state', 'unknown')
            print(f"Status: {state}")
            
            if state == 'complete':
                print("Capture complete! Retrieving data...")
                return self.get_waveform_data()
            elif state == 'error':
                raise Exception("Capture failed with error state")
            
            time.sleep(0.2)
        
        raise TimeoutError(f"Capture did not complete within {timeout} seconds")

class WaveformAnalyzer:
    """Analyze waveform data and compute electrical properties"""
    
    @staticmethod
    def compute_rms(values: np.ndarray) -> float:
        """Compute RMS value"""
        return np.sqrt(np.mean(np.square(values)))
    
    @staticmethod
    def estimate_frequency(time: np.ndarray, signal: np.ndarray) -> float:
        """Estimate frequency using zero-crossing method"""
        # Find zero crossings
        zero_crossings = np.where(np.diff(np.sign(signal)))[0]
        
        if len(zero_crossings) < 2:
            return 0.0
        
        # Calculate average period from zero crossings (half cycles)
        periods = np.diff(time[zero_crossings])
        avg_half_period = np.median(periods)
        
        # Full period is twice the half period
        frequency = 1.0 / (2 * avg_half_period) if avg_half_period > 0 else 0.0
        return float(frequency)
    
    @staticmethod
    def compute_thd(signal: np.ndarray, fundamental_freq: float, sample_rate: float) -> float:
        """Compute Total Harmonic Distortion using FFT"""
        # Perform FFT
        fft_values = np.fft.rfft(signal)
        fft_freq = np.fft.rfftfreq(len(signal), 1/sample_rate)
        
        # Find fundamental component
        fund_idx = np.argmin(np.abs(fft_freq - fundamental_freq))
        fundamental_magnitude = np.abs(fft_values[fund_idx])
        
        if fundamental_magnitude == 0:
            return 0.0
        
        # Sum harmonics (2nd through 10th)
        harmonic_sum = 0.0
        for n in range(2, 11):
            harmonic_freq = n * fundamental_freq
            harm_idx = np.argmin(np.abs(fft_freq - harmonic_freq))
            if harm_idx < len(fft_values):
                harmonic_sum += np.abs(fft_values[harm_idx]) ** 2
        
        thd = np.sqrt(harmonic_sum) / fundamental_magnitude * 100
        return thd
    
    @classmethod
    def analyze(cls, waveform: WaveformData, voltage_phase: str = "A", 
                current_phase: str = "A") -> ComputedProperties:
        """Compute all electrical properties from waveform"""
        time = waveform.time_seconds
        voltage = np.array(waveform.voltage)
        current = np.array(waveform.current)
        
        # Frequency estimation (do this first, before any phase shifts)
        frequency = cls.estimate_frequency(time, voltage)
        
        # Calculate phase shift needed
        phase_shift = cls.calculate_phase_shift(voltage_phase, current_phase)
        
        # Apply phase shift if needed
        if voltage_phase != current_phase:
            print(f"Applying phase shift: {phase_shift:.1f} degrees")
            # Shift voltage to align with current phase
            voltage_shifted = cls.shift_signal_by_phase(voltage, time, frequency, phase_shift)
        else:
            voltage_shifted = voltage.copy()
        
        # RMS values (use shifted voltage for accurate power calculations)
        v_rms = cls.compute_rms(voltage_shifted)
        i_rms = cls.compute_rms(current)
        
        # Sample rate for THD calculation
        if len(time) > 1:
            sample_rate = 1.0 / np.mean(np.diff(time))
        else:
            sample_rate = 1000.0
        
        # THD (use shifted voltage)
        v_thd = cls.compute_thd(voltage_shifted, frequency, float(sample_rate))
        i_thd = cls.compute_thd(current, frequency, float(sample_rate))
        
        # Power calculations (use shifted voltage)
        instant_power = voltage_shifted * current
        active_power = np.mean(instant_power)
        
        # Apparent power
        apparent_power = v_rms * i_rms
        
        # Reactive power
        reactive_power = np.sqrt(max(0, apparent_power**2 - active_power**2))
        if active_power < 0:
            reactive_power = -reactive_power
        
        # Power factor
        power_factor = active_power / apparent_power if apparent_power > 0 else 0.0
        
        return ComputedProperties(
            voltage_rms=float(v_rms),
            current_rms=float(i_rms),
            active_power=float(active_power),
            reactive_power=float(reactive_power),
            apparent_power=float(apparent_power),
            power_factor=float(power_factor),
            frequency=float(frequency),
            voltage_thd=float(v_thd),
            current_thd=float(i_thd),
            voltage_shifted=voltage_shifted,
            phase_shift_degrees=float(phase_shift)
        )
        
    @staticmethod
    def shift_signal_by_phase(signal: np.ndarray, time: np.ndarray, frequency: float, phase_degrees: float) -> np.ndarray:
        """
        Shift a signal by a given phase angle using FFT
        
        Args:
            signal: Input signal array
            time: Time array in seconds
            frequency: Fundamental frequency in Hz
            phase_degrees: Phase shift in degrees (positive = leading, negative = lagging)
        
        Returns:
            Phase-shifted signal
        """
        if frequency == 0:
            return signal
        
        # Convert phase to radians
        phase_rad = np.deg2rad(phase_degrees)
        
        # Perform FFT
        fft_signal = np.fft.rfft(signal)
        fft_freq = np.fft.rfftfreq(len(signal), np.mean(np.diff(time)))
        
        # Apply phase shift to all frequency components
        # Phase shift in frequency domain is: exp(-j * 2π * f * Δt)
        # For a phase shift: Δt = phase / (2π * f)
        for i, freq in enumerate(fft_freq):
            if freq > 0:  # Don't shift DC component
                time_shift = phase_rad / (2 * np.pi * freq)
                fft_signal[i] *= np.exp(-1j * 2 * np.pi * freq * time_shift)
        
        # Convert back to time domain
        shifted_signal = np.fft.irfft(fft_signal, len(signal))
        
        return shifted_signal


    @staticmethod
    def calculate_phase_shift(voltage_phase: str, current_phase: str) -> float:
        """
        Calculate the phase shift needed to align voltage with current phase
        
        In a 3-phase system:
        - Phase A is reference (0°)
        - Phase B lags by 120° (-120°)
        - Phase C leads by 120° (+120°)
        
        Returns phase shift in degrees to apply to voltage
        """
        phase_angles = {
            "A": 0.0,
            "B": -120.0,
            "C": 120.0
        }
        
        if voltage_phase not in phase_angles or current_phase not in phase_angles:
            return 0.0
        
        # Calculate shift needed: current_phase - voltage_phase
        return phase_angles[current_phase] - phase_angles[voltage_phase]

def plot_waveforms(waveform: WaveformData, computed: ComputedProperties, 
                   meter_values: Dict, channel_config: Dict):
    """Create comprehensive visualization of waveform data"""
    time = waveform.time_seconds
    voltage_original = np.array(waveform.voltage)
    voltage = computed.voltage_shifted  # Use phase-shifted voltage
    current = np.array(waveform.current)
    
    fig = plt.figure(figsize=(16, 10))
    
    # Channel info
    channel_label = channel_config.get('label', f"Channel {waveform.channel_index}")
    phase_info = f" (Phase shift: {computed.phase_shift_degrees:.1f}°)" if computed.phase_shift_degrees != 0 else ""
    fig.suptitle(f"{channel_label} - Waveform Analysis{phase_info}", fontsize=16, fontweight='bold')
    
    # 1. Voltage and Current waveforms
    ax1 = plt.subplot(3, 2, 1)
    if computed.phase_shift_degrees != 0:
        ax1.plot(time * 1000, voltage_original, 'b:', linewidth=1, alpha=0.5, label='Voltage (original)')
        ax1.plot(time * 1000, voltage, 'b-', linewidth=1, label=f'Voltage (shifted {computed.phase_shift_degrees:.0f}°)')
    else:
        ax1.plot(time * 1000, voltage, 'b-', linewidth=1, label='Voltage')
    ax1.set_xlabel('Time (ms)')
    ax1.set_ylabel('Voltage (V)', color='b')
    ax1.tick_params(axis='y', labelcolor='b')
    ax1.grid(True, alpha=0.3)
    ax1.legend(loc='upper left')
    
    ax1_twin = ax1.twinx()
    ax1_twin.plot(time * 1000, current, 'r-', linewidth=1, label='Current')
    ax1_twin.set_ylabel('Current (A)', color='r')
    ax1_twin.tick_params(axis='y', labelcolor='r')
    ax1_twin.legend(loc='upper right')
    
    # 2. Instantaneous Power
    ax2 = plt.subplot(3, 2, 2)
    instant_power = voltage * current
    ax2.plot(time * 1000, instant_power, 'g-', linewidth=1)
    ax2.axhline(y=computed.active_power, color='r', linestyle='--', 
                label=f'Average: {computed.active_power:.1f} W')
    ax2.set_xlabel('Time (ms)')
    ax2.set_ylabel('Instantaneous Power (W)')
    ax2.set_title('Instantaneous Power')
    ax2.grid(True, alpha=0.3)
    ax2.legend()
    
    # 3. Voltage FFT
    ax3 = plt.subplot(3, 2, 3)
    if len(time) > 1:
        sample_rate = 1.0 / np.mean(np.diff(time))
        fft_v = np.fft.rfft(voltage)
        fft_freq = np.fft.rfftfreq(len(voltage), 1/sample_rate)
        
        # Plot only up to 1000 Hz
        mask = fft_freq <= 1000
        ax3.semilogy(fft_freq[mask], np.abs(fft_v[mask]), 'b-', linewidth=1)
        ax3.set_xlabel('Frequency (Hz)')
        ax3.set_ylabel('Magnitude')
        ax3.set_title(f'Voltage Spectrum (THD: {computed.voltage_thd:.2f}%)')
        ax3.grid(True, alpha=0.3)
    
    # 4. Current FFT
    ax4 = plt.subplot(3, 2, 4)
    if len(time) > 1:
        sample_rate = 1.0 / np.mean(np.diff(time))
        fft_i = np.fft.rfft(current)
        fft_freq = np.fft.rfftfreq(len(current), 1/sample_rate)
        mask = fft_freq <= 1000
        ax4.semilogy(fft_freq[mask], np.abs(fft_i[mask]), 'r-', linewidth=1)
        ax4.set_xlabel('Frequency (Hz)')
        ax4.set_ylabel('Magnitude')
        ax4.set_title(f'Current Spectrum (THD: {computed.current_thd:.2f}%)')
        ax4.grid(True, alpha=0.3)
    
    # 5. Computed vs Meter Values Comparison
    ax5 = plt.subplot(3, 2, 5)
    ax5.axis('off')
    
    comparison_text = f"""
COMPUTED FROM WAVEFORM:
  Voltage RMS:      {computed.voltage_rms:.2f} V
  Current RMS:      {computed.current_rms:.3f} A
  Active Power:     {computed.active_power:.2f} W
  Reactive Power:   {computed.reactive_power:.2f} VAR
  Apparent Power:   {computed.apparent_power:.2f} VA
  Power Factor:     {computed.power_factor:.3f}
  Frequency:        {computed.frequency:.2f} Hz

METER VALUES:
  Voltage:          {meter_values.get('voltage', 'N/A')} V
  Current:          {meter_values.get('current', 'N/A')} A
  Active Power:     {meter_values.get('activePower', 'N/A')} W
  Reactive Power:   {meter_values.get('reactivePower', 'N/A')} VAR
  Apparent Power:   {meter_values.get('apparentPower', 'N/A')} VA
  Power Factor:     {meter_values.get('powerFactor', 'N/A')}
"""
    
    ax5.text(0.1, 0.9, comparison_text, transform=ax5.transAxes,
             fontfamily='monospace', fontsize=9, verticalalignment='top')
    
    # 6. Difference Analysis
    ax6 = plt.subplot(3, 2, 6)
    ax6.axis('off')
    
    # Calculate differences
    differences = []
    if 'voltage' in meter_values:
        v_diff = abs(computed.voltage_rms - meter_values['voltage'])
        v_diff_pct = (v_diff / meter_values['voltage'] * 100) if meter_values['voltage'] > 0 else 0
        differences.append(f"Voltage:        {v_diff:6.2f} V  ({v_diff_pct:5.2f}%)")
    
    if 'current' in meter_values:
        i_diff = abs(computed.current_rms - meter_values['current'])
        i_diff_pct = (i_diff / meter_values['current'] * 100) if meter_values['current'] > 0 else 0
        differences.append(f"Current:        {i_diff:6.3f} A  ({i_diff_pct:5.2f}%)")
    
    if 'activePower' in meter_values:
        p_diff = abs(computed.active_power - meter_values['activePower'])
        p_diff_pct = (p_diff / abs(meter_values['activePower']) * 100) if meter_values['activePower'] != 0 else 0
        differences.append(f"Active Power:   {p_diff:6.2f} W  ({p_diff_pct:5.2f}%)")
    
    if 'powerFactor' in meter_values:
        pf_diff = abs(computed.power_factor - meter_values['powerFactor'])
        differences.append(f"Power Factor:   {pf_diff:6.3f}")
    
    diff_text = "DIFFERENCES (Computed - Meter):\n\n" + "\n".join(differences)
    
    ax6.text(0.1, 0.9, diff_text, transform=ax6.transAxes,
             fontfamily='monospace', fontsize=10, verticalalignment='top')
    
    plt.tight_layout()
    return fig


def main():
    """Main execution function"""
    # Parse command-line arguments
    parser = argparse.ArgumentParser(
        description='Capture and analyze waveform data from EnergyMe-Home energy meter',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument('--host', type=str, required=True,
                        help='Energy meter IP address or hostname')
    parser.add_argument('--channel', type=int, required=True,
                        help='Channel index to capture (0-16)')
    parser.add_argument('--username', type=str, default='admin',
                        help='Authentication username')
    parser.add_argument('--password', type=str, default='energyme00',
                        help='Authentication password')
    parser.add_argument('--timeout', type=int, default=15,
                        help='Capture timeout in seconds')
    
    args = parser.parse_args()
    
    # Build base URL
    base_url = args.host if args.host.startswith('http') else f"http://{args.host}"
    
    # Initialize API client
    api = EnergyMeterAPI(base_url, args.username, args.password)
    
    try:
        # Get channel configuration
        print(f"\nFetching channel {args.channel} configuration...")
        channel_config = api.get_channel_config(args.channel)
        print(f"Channel: {channel_config.get('label', 'Unknown')}")
        print(f"Active: {channel_config.get('active', False)}")

        # Get phase information
        voltage_phase = api.get_channel_config(0).get('phase', 'A')  # Voltage reference from channel 0
        current_phase = channel_config.get('phase', 'A')

        print(f"Voltage Phase: {voltage_phase}, Current Phase: {current_phase}")
        
        # Capture waveform
        waveform = api.capture_waveform_blocking(args.channel, timeout=args.timeout)
        print(f"\nCaptured {waveform.sample_count} samples")
        
        # Get meter values for comparison
        print("\nFetching meter values...")
        meter_values = api.get_meter_values(args.channel)
        
        # Analyze waveform
        print("\nAnalyzing waveform...")
        computed = WaveformAnalyzer.analyze(waveform, voltage_phase, current_phase)

        # Print results
        print("\n" + "="*60)
        print("ANALYSIS RESULTS")
        print("="*60)
        print(f"\nComputed Properties:")
        print(f"  Voltage RMS:      {computed.voltage_rms:.2f} V")
        print(f"  Current RMS:      {computed.current_rms:.3f} A")
        print(f"  Active Power:     {computed.active_power:.2f} W")
        print(f"  Reactive Power:   {computed.reactive_power:.2f} VAR")
        print(f"  Apparent Power:   {computed.apparent_power:.2f} VA")
        print(f"  Power Factor:     {computed.power_factor:.3f}")
        print(f"  Frequency:        {computed.frequency:.2f} Hz")
        print(f"  Voltage THD:      {computed.voltage_thd:.2f}%")
        print(f"  Current THD:      {computed.current_thd:.2f}%")
        if computed.phase_shift_degrees != 0:
            print(f"  Phase Shift:      {computed.phase_shift_degrees:.1f}°")
        
        # Plot results
        print("\nGenerating plots...")
        fig = plot_waveforms(waveform, computed, meter_values, channel_config)
        plt.savefig(f'temp/waveform_analysis_ch{args.channel}.png', dpi=150, bbox_inches='tight')
        print(f"Plot saved as 'temp/waveform_analysis_ch{args.channel}.png'")
        plt.show()
        
    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()