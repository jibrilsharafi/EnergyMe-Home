# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

import time
from typing import Any, Dict, List
from pymodbus.client import ModbusTcpClient
import sys
import statistics

# Modbus server details
SERVER_PORT = 502

# Register definitions based on modbustcp.cpp
def get_register_definitions() -> Dict[str, Dict[str, Any]]:
    """Get all register definitions."""
    registers = {}

    # General system registers
    registers["System/Timestamp (high)"] = {"address": 0, "size": 1, "type": "uint16", "unit": ""}
    registers["System/Timestamp (mid-high)"] = {"address": 1, "size": 1, "type": "uint16", "unit": ""}
    registers["System/Timestamp (mid-low)"] = {"address": 2, "size": 1, "type": "uint16", "unit": ""}
    registers["System/Timestamp (low)"] = {"address": 3, "size": 1, "type": "uint16", "unit": ""}
    
    registers["System/Uptime (high)"] = {"address": 4, "size": 1, "type": "uint16", "unit": "ms"}
    registers["System/Uptime (mid-high)"] = {"address": 5, "size": 1, "type": "uint16", "unit": "ms"}
    registers["System/Uptime (mid-low)"] = {"address": 6, "size": 1, "type": "uint16", "unit": "ms"}
    registers["System/Uptime (low)"] = {"address": 7, "size": 1, "type": "uint16", "unit": "ms"}

    # Meter values
    registers["Meter/Voltage"] = {"address": 100, "size": 2, "type": "float32", "unit": "V"}
    registers["Meter/Grid Frequency"] = {"address": 102, "size": 2, "type": "float32", "unit": "Hz"}

    # Role-based aggregations
    roles = {
        "Grid": 200,
        "Load": 300,
        "PV": 400,
        "Battery": 500,
        "Inverter": 600
    }

    metrics = [
        ("Active Power", 0),
        ("Reactive Power", 2),
        ("Apparent Power", 4),
        ("Power Factor", 6),
        ("Active Energy Imported", 8),
        ("Active Energy Exported", 10),
        ("Reactive Energy Imported", 12),
        ("Reactive Energy Exported", 14),
        ("Apparent Energy", 16),
    ]

    for role_name, base_addr in roles.items():
        for metric_name, offset in metrics:
            addr = base_addr + offset
            registers[f"{role_name}/{metric_name}"] = {
                "address": addr,
                "size": 2,
                "type": "float32",
                "unit": "W" if "Power" in metric_name else "Wh" if "Energy" in metric_name else ""
            }

    return registers


def get_datatype_mapping(client: ModbusTcpClient) -> Dict[str, Any]:
    """Get mapping from string types to client's DATATYPE enum."""
    return {
        "uint16": client.DATATYPE.UINT16,
        "float32": client.DATATYPE.FLOAT32,
        "float64": client.DATATYPE.FLOAT64,
        "int16": client.DATATYPE.INT16,
        "int32": client.DATATYPE.INT32,
        "int64": client.DATATYPE.INT64,
        "uint32": client.DATATYPE.UINT32,
        "uint64": client.DATATYPE.UINT64,
    }


def calculate_percentiles(values: List[float]) -> Dict[str, float]:
    """Calculate percentiles from a list of values."""
    if not values:
        return {}
    
    sorted_values = sorted(values)
    
    def percentile(p):
        index = int(len(sorted_values) * (p / 100.0))
        if index >= len(sorted_values):
            index = len(sorted_values) - 1
        return sorted_values[index]
    
    return {
        "min": min(sorted_values),
        "p50": percentile(50),
        "p75": percentile(75),
        "p90": percentile(90),
        "p95": percentile(95),
        "p99": percentile(99),
        "max": max(sorted_values),
    }


def draw_histogram(values: List[float], width: int = 60, bins: int = 10):
    """Draw a simple ASCII histogram of response times."""
    if not values:
        return
    
    min_val = min(values)
    max_val = max(values)
    range_val = max_val - min_val if max_val > min_val else 1
    
    # Create bins
    bin_counts = [0] * bins
    for val in values:
        if max_val > min_val:
            bin_index = int((val - min_val) / range_val * (bins - 1))
        else:
            bin_index = 0
        bin_counts[bin_index] += 1
    
    max_count = max(bin_counts)
    
    print("\n📊 Response Time Distribution (ASCII Histogram):")
    print("─" * (width + 30))
    
    for i, count in enumerate(bin_counts):
        bin_start = min_val + (i * range_val / bins)
        bin_end = bin_start + (range_val / bins)
        bar_width = int((count / max_count) * width) if max_count > 0 else 0
        bar = "█" * bar_width
        pct = (count / len(values)) * 100
        print(f"{bin_start:6.1f}-{bin_end:6.1f}ms │ {bar:<{width}} │ {count:4d} ({pct:5.1f}%)")
    
    print("─" * (width + 30))


def print_percentile_summary(percentiles: Dict[str, float]):
    """Print a formatted percentile summary."""
    print("\n📈 Response Time Percentiles:")
    print("─" * 50)
    
    print(f"   Min:  {percentiles['min']:7.2f}ms")
    print(f"   p50:  {percentiles['p50']:7.2f}ms (median)")
    print(f"   p75:  {percentiles['p75']:7.2f}ms")
    print(f"   p90:  {percentiles['p90']:7.2f}ms")
    print(f"   p95:  {percentiles['p95']:7.2f}ms")
    print(f"   p99:  {percentiles['p99']:7.2f}ms")
    print(f"   Max:  {percentiles['max']:7.2f}ms")
    
    # Jitter indicator
    jitter = percentiles['p99'] - percentiles['p50']
    if jitter < 5:
        jitter_status = "✓ Excellent (stable)"
    elif jitter < 20:
        jitter_status = "✓ Good"
    elif jitter < 50:
        jitter_status = "⚠️ Fair (moderate jitter)"
    else:
        jitter_status = "❌ Poor (high jitter)"
    
    print(f"   Jitter (p99-p50): {jitter:6.2f}ms - {jitter_status}")
    print("─" * 50)



def read_register(client: ModbusTcpClient, address: int, size: int, reg_type: str) -> Any:
    """Read a register and decode its value based on the type."""
    result = client.read_holding_registers(address=address, count=size, device_id=1)
    if not result.isError():
        type_mapping = get_datatype_mapping(client)
        datatype = type_mapping.get(reg_type)
        if datatype is None:
            raise ValueError(f"Unknown register type: {reg_type}")
            
        return client.convert_from_registers(
            result.registers, data_type=datatype, word_order="big"
        )
    return None


def test_all_registers(client: ModbusTcpClient, registers: Dict[str, Dict[str, Any]]):
    """Test all defined registers."""
    print("\n" + "="*90)
    print("📊 COMPREHENSIVE REGISTER TESTING - ALL REGISTERS")
    print("="*90)
    
    start_time = time.time()
    counter = 0
    successful_reads = 0
    failed_reads = 0
    response_times = []

    for name, reg_info in registers.items():
        request_time = time.time()
        counter += 1
        value = read_register(
            client, reg_info["address"], reg_info["size"], reg_info["type"]
        )
        response_time = (time.time() - request_time) * 1000
        
        if value is not None:
            successful_reads += 1
            response_times.append(response_time)
            unit_str = f" {reg_info.get('unit', '')}" if reg_info.get('unit') else ""
            print(f"✓ {name:45}: {value:12.3f}{unit_str:8} ({response_time:.1f}ms)")
        else:
            failed_reads += 1
            print(f"✗ {name:45}: {'ERROR':>12}")

    total_time = time.time() - start_time
    avg_response_time = sum(response_times) / len(response_times) if response_times else 0
    median_response_time = statistics.median(response_times) if response_times else 0
    stdev_response_time = statistics.stdev(response_times) if len(response_times) > 1 else 0
    
    percentiles = calculate_percentiles(response_times)
    
    print("\n" + "─"*90)
    print(f"📈 SUMMARY: {successful_reads}/{counter} successful reads ({(successful_reads/counter)*100:.1f}%)")
    print(f"⏱️  Total time: {total_time:.2f}s | Avg response: {avg_response_time:.2f}ms")
    print(f"📊 Median: {median_response_time:.2f}ms | Std Dev: {stdev_response_time:.2f}ms")
    print("─"*90)
    
    print_percentile_summary(percentiles)
    draw_histogram(response_times, width=50, bins=12)

def test_energy_balance(client: ModbusTcpClient, registers: Dict[str, Dict[str, Any]]):
    """Test energy balance: Grid + PV + Battery should equal Load (approximately)."""
    print("\n" + "="*90)
    print("⚡ ENERGY BALANCE TEST: Grid + PV + Battery vs Load")
    print("="*90)
    
    # Helper to extract register value
    def get_register_value(name: str) -> float:
        if name not in registers:
            raise ValueError(f"Register '{name}' not defined")
        reg = registers[name]
        return read_register(client, reg["address"], reg["size"], reg["type"])
    
    start_time = time.time()
    counter = 0
    successful_comparisons = 0
    power_differences = []
    energy_differences = []
    
    print("📋 Reading power and energy values for balance check...")
    print("─"*90)
    
    while counter < 50:
        counter += 1
        
        # Read active power values
        grid_power = get_register_value("Grid/Active Power")
        load_power = get_register_value("Load/Active Power")
        pv_power = get_register_value("PV/Active Power")
        battery_power = get_register_value("Battery/Active Power")
        
        # Read active energy imported values
        grid_energy = get_register_value("Grid/Active Energy Imported")
        load_energy = get_register_value("Load/Active Energy Imported")
        pv_energy = get_register_value("PV/Active Energy Imported")
        battery_energy = get_register_value("Battery/Active Energy Imported")
        
        if all(v is not None for v in [grid_power, load_power, pv_power, battery_power,
                                        grid_energy, load_energy, pv_energy, battery_energy]):
            successful_comparisons += 1
            
            # Or: Grid + PV + Battery = Load (for consumption)
            source_power = pv_power + battery_power + grid_power  # PV + Battery providing + Grid taking
            load_diff = abs(load_power - source_power)
            load_diff_percent = (load_diff / load_power * 100) if load_power != 0 else 0
            power_differences.append(load_diff)
            
            # Energy balance
            source_energy = pv_energy + battery_energy + grid_energy
            load_diff_energy = abs(load_energy - source_energy)
            energy_differences.append(load_diff_energy)
            
            status = "✓" if load_diff < 100 else "⚠️" if load_diff < 500 else "❌"
            
            print(f"{status} [{counter:2d}] Power: Grid={grid_power:8.1f}W | "
                  f"PV={pv_power:8.1f}W | Battery={battery_power:8.1f}W | "
                  f"Load={load_power:8.1f}W | Δ={load_diff:8.1f}W ({load_diff_percent:5.1f}%)")
        else:
            print(f"❌ Error reading values (attempt {counter})")
        
        time.sleep(0.1)

    if successful_comparisons > 0:
        avg_power_diff = sum(power_differences) / len(power_differences)
        avg_energy_diff = sum(energy_differences) / len(energy_differences)
        min_power_diff = min(power_differences)
        max_power_diff = max(power_differences)
        
        print("\n" + "─"*90)
        print(f"⚡ ENERGY BALANCE RESULTS ({successful_comparisons}/{counter} successful):")
        print(f"   Power Balance:")
        print(f"      Average difference: {avg_power_diff:.1f}W")
        print(f"      Min/Max difference: {min_power_diff:.1f}W / {max_power_diff:.1f}W")
        print(f"   Energy Balance:")
        print(f"      Average difference: {avg_energy_diff:.1f}Wh")
        print(f"   Test duration: {time.time() - start_time:.2f}s")
        print("─"*90)
    else:
        print(f"❌ No successful comparisons out of {counter} attempts")


def test_polling_performance(client: ModbusTcpClient, registers: Dict[str, Dict[str, Any]]):
    """Test polling performance with all registers."""
    print("\n" + "="*90)
    print("🚀 POLLING PERFORMANCE TEST - ALL REGISTERS")
    print("="*90)
    
    start_time = time.time()
    counter = 0
    successful_reads = 0
    failed_reads = 0
    response_times = []
    
    print(f"📊 Total registers to poll: {len(registers)}")
    print(f"📊 Target: 10 full polling cycles")
    print("─"*90)

    while counter < 10:
        counter += 1
        cycle_start = time.time()
        cycle_success = 0
        
        for name, reg_info in registers.items():
            request_time = time.time()
            value = read_register(
                client, 
                reg_info["address"],
                reg_info["size"],
                reg_info["type"]
            )
            response_time = (time.time() - request_time) * 1000
            response_times.append(response_time)
            
            if value is not None:
                successful_reads += 1
                cycle_success += 1
            else:
                failed_reads += 1
        
        cycle_time = time.time() - cycle_start
        print(f"📈 Cycle {counter:2d}: {cycle_success:2d}/{len(registers)} registers read in {cycle_time:.2f}s")

    total_time = time.time() - start_time
    avg_response_time = sum(response_times) / len(response_times) if response_times else 0
    median_response_time = statistics.median(response_times) if response_times else 0
    stdev_response_time = statistics.stdev(response_times) if len(response_times) > 1 else 0
    
    # Calculate percentiles
    percentiles = calculate_percentiles(response_times)
    
    print("\n" + "─"*90)
    print(f"🎯 POLLING PERFORMANCE RESULTS:")
    print(f"   ✓ Successful reads: {successful_reads}/{successful_reads + failed_reads}")
    print(f"   ❌ Failed reads: {failed_reads}")
    print(f"   ⏱️  Total time: {total_time:.2f}s")
    print(f"   📊 Polls per second: {(successful_reads + failed_reads)/total_time:.1f}")
    print(f"   📊 Cycles per second: {10/total_time:.2f}")
    print(f"   ⚡ Avg response time: {avg_response_time:.2f}ms")
    print(f"   📊 Median response time: {median_response_time:.2f}ms")
    print(f"   📈 Std Dev: {stdev_response_time:.2f}ms")
    print("─"*90)
    
    # Print percentile summary
    print_percentile_summary(percentiles)
    
    # Draw histogram
    draw_histogram(response_times, width=50, bins=12)

def main():
    if len(sys.argv) < 2:
        print("Usage: python modbus_tester.py <SERVER_IP>")
        print("Example: python modbus_tester.py 192.168.1.60")
        return

    server_ip = sys.argv[1]
    client = ModbusTcpClient(server_ip, port=SERVER_PORT)

    if not client.connect():
        print("❌ Failed to connect to the Modbus server")
        return

    print("=" * 90)
    print("🔌 MODBUS REGISTER TESTING SUITE - ENERGYME-HOME")
    print("=" * 90)
    print(f"🌐 Server: {server_ip}:{SERVER_PORT}")
    
    # Load register definitions
    registers = get_register_definitions()
    
    if not registers:
        print("❌ No registers defined. Exiting.")
        return
    
    print(f"📊 Loaded {len(registers)} registers")
    
    overall_start_time = time.time()
    
    print(f"\n🧪 TEST 1: All Registers Validation")
    test_all_registers(client, registers)
    
    print(f"\n🧪 TEST 2: Energy Balance Test (Grid + PV + Battery vs Load)")
    test_energy_balance(client, registers)
    
    print(f"\n🧪 TEST 3: Polling Performance")
    test_polling_performance(client, registers)
    
    total_test_time = time.time() - overall_start_time
    
    print("\n" + "="*90)
    print("📈 TEST SUITE COMPLETED")
    print("="*90)
    print(f"🏁 Total Test Duration: {total_test_time:.2f}s")
    print(f"📊 Total Registers Tested: {len(registers)}")
    print(f"🌐 Modbus Server: {server_ip}:{SERVER_PORT}")
    
    # Analyze registers by category
    categories = {}
    for name in registers.keys():
        category = name.split("/")[0]
        categories[category] = categories.get(category, 0) + 1
    
    print(f"\n📋 REGISTER CATEGORIES:")
    for category, count in sorted(categories.items()):
        print(f"   • {category:20}: {count:3} registers")
    
    print(f"\n✅ Test suite completed successfully!")
    print("="*90)

if __name__ == "__main__":
    main()
