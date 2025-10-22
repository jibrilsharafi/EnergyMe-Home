# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

import time
import json
import os
from typing import Any, Dict
from pymodbus.client import ModbusTcpClient
import sys

# Modbus server details
SERVER_PORT = 502  # Replace with your server's port if different

# Mapping from old string types to ModbusTcpClient.DATATYPE enum values
def get_datatype_mapping(client: ModbusTcpClient) -> Dict[str, Any]:
    """Get mapping from string types to client's DATATYPE enum."""
    return {
        "uint32": client.DATATYPE.UINT32,
        "float": client.DATATYPE.FLOAT32,
        "float32": client.DATATYPE.FLOAT32,
        "float64": client.DATATYPE.FLOAT64,
        "int16": client.DATATYPE.INT16,
        "uint16": client.DATATYPE.UINT16,
        "int32": client.DATATYPE.INT32,
        "int64": client.DATATYPE.INT64,
        "uint64": client.DATATYPE.UINT64,
        "string": client.DATATYPE.STRING,
        "bits": client.DATATYPE.BITS
    }

def load_registers_from_json(json_file_path: str) -> Dict[str, Dict[str, Any]]:
    """Load register definitions from JSON file and convert to our format."""
    if not os.path.exists(json_file_path):
        print(f"Warning: JSON file {json_file_path} not found")
        return {}
    
    # try:
    with open(json_file_path, 'r') as f:
        # Read the file content and remove comments (basic approach)
        content = f.read()
        lines = content.split('\n')
        cleaned_lines = []
        for line in lines:
            # Remove lines that start with // (basic comment removal)
            if not line.strip().startswith('//'):
                cleaned_lines.append(line)
        cleaned_content = '\n'.join(cleaned_lines)
        
        json_registers : Dict[str, Dict[str, Any]] = {}
        json_registers = json.loads(cleaned_content)
    
    converted_registers = {}
    for reg_addr, reg_info in json_registers.items():
        # Convert Modbus address (e.g., "40001") to zero-based address
        # Modbus 4xxxx registers start at address 0 in the holding register space
        modbus_addr = int(reg_addr) - 40001
        
        # Calculate size based on data type
        if reg_info["data_type"] in ["uint32", "float32", "float"]:
            size = 2
        elif reg_info["data_type"] in ["uint64", "float64", "int64"]:
            size = 4
        else:
            size = 1
        
        # Create a readable name with unit
        display_name = reg_info["name"]
        if reg_info.get("unit"):
            display_name += f" ({reg_info['unit']})"
        
        converted_registers[display_name] = {
            "address": modbus_addr,
            "size": size,
            "type": reg_info["data_type"],
            "description": reg_info.get("description", ""),
            "unit": reg_info.get("unit", "")
        }
    
    print(f"Loaded {len(converted_registers)} registers from {json_file_path}")
    return converted_registers
    
    # except Exception as e:
    #     print(f"Error loading JSON file {json_file_path}: {e}")
    #     return {}


def read_register(client: ModbusTcpClient, address: int, size: int, reg_type: str) -> Any:
    """Read a register and decode its value based on the type."""
    result = client.read_holding_registers(address=address, count=size, device_id=1)
    if not result.isError():
        # Map string types to client's DATATYPE enum
        type_mapping = get_datatype_mapping(client)
        datatype = type_mapping.get(reg_type)
        if datatype is None:
            raise ValueError(f"Unknown register type: {reg_type}")
            
        return client.convert_from_registers(
            result.registers, data_type=datatype, word_order="big"
        )
    else:
        print(f"Error reading register at address {address}")

    return None


def test_registers(client: ModbusTcpClient, registers: Dict[str, Dict[str, Any]]):
    """Test all defined registers."""
    print("\n" + "="*80)
    print("📊 COMPREHENSIVE REGISTER TESTING")
    print("="*80)
    
    start_time = time.time()
    counter = 0
    successful_reads = 0
    failed_reads = 0
    response_times = []

    while True and counter < 100:
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
                print(f"✗ {name:45}: {'ERROR':>12} ({response_time:.1f}ms)")

    total_time = time.time() - start_time
    avg_response_time = sum(response_times) / len(response_times) if response_times else 0
    
    print("\n" + "─"*80)
    print(f"📈 SUMMARY: {successful_reads}/{counter} successful reads ({(successful_reads/counter)*100:.1f}%)")
    print(f"⏱️  Total time: {total_time:.2f}s | Avg response: {avg_response_time:.1f}ms")
    print("─"*80)
    
# Now test channel 0 active power vs aggregated active power without channel 0
def test_channel0_active_power(client: ModbusTcpClient, registers: Dict[str, Dict[str, Any]]):
    """Test channel 0 active power vs aggregated active power without channel 0."""
    print("\n" + "="*80)
    print("⚡ CHANNEL 0 VS AGGREGATED POWER COMPARISON")
    print("="*80)
    
    start_time = time.time()
    counter = 0
    abs_difference_tot = 0.0
    rel_difference_tot = 0.0
    successful_comparisons = 0
    
    # Find the registers by their display names (with units)
    ch0_active_power_key = None
    agg_no_ch0_key = None
    
    for name, reg_info in registers.items():
        if "Ch0-Active-Power" in name or "Channel 0 Active Power" in name:
            ch0_active_power_key = name
        elif "Aggregated-Active-Power-No-Ch0" in name or "Aggregated Active Power Without Ch0" in name:
            agg_no_ch0_key = name
    
    if not ch0_active_power_key or not agg_no_ch0_key:
        print("❌ Could not find required registers for channel 0 active power comparison")
        print(f"   Looking for: Ch0-Active-Power and Aggregated-Active-Power-No-Ch0")
        return
    
    print(f"📋 Comparing: {ch0_active_power_key}")
    print(f"📋 Against:   {agg_no_ch0_key}")
    print("─"*80)
    
    while counter < 100:
        counter += 1
        channel0_active_power = read_register(
            client, registers[ch0_active_power_key]["address"], registers[ch0_active_power_key]["size"], registers[ch0_active_power_key]["type"]
        )
        aggregated_active_power_without_ch0 = read_register(
            client, registers[agg_no_ch0_key]["address"], registers[agg_no_ch0_key]["size"], registers[agg_no_ch0_key]["type"]
        )
        if channel0_active_power is not None and aggregated_active_power_without_ch0 is not None:
            successful_comparisons += 1
            absolute_difference = abs(channel0_active_power - aggregated_active_power_without_ch0)
            relative_difference = (absolute_difference / channel0_active_power) * 100 if channel0_active_power != 0 else float('inf')
            
            abs_difference_tot += absolute_difference
            rel_difference_tot += relative_difference
            
            status = "✓" if relative_difference < 5.0 else "⚠️" if relative_difference < 10.0 else "❌"
            
            print(
                f"{status} Ch0: {channel0_active_power:10.3f}W | Agg: {aggregated_active_power_without_ch0:10.3f}W | "
                f"Δ: {absolute_difference:8.3f}W ({relative_difference:6.2f}%)"
            )
        else:
            print(f"❌ Error reading power values (attempt {counter})")
            
        time.sleep(0.1)  # Small delay between reads to not overwhelm the server

    if successful_comparisons > 0:
        avg_abs_diff = abs_difference_tot / successful_comparisons
        avg_rel_diff = rel_difference_tot / successful_comparisons
        print("\n" + "─"*80)
        print(f"📊 COMPARISON RESULTS ({successful_comparisons}/{counter} successful):")
        print(f"   Average absolute difference: {avg_abs_diff:.3f}W")
        print(f"   Average relative difference: {avg_rel_diff:.3f}%")
        print(f"   Test duration: {time.time() - start_time:.2f}s")
        print("─"*80)
    else:
        print(f"❌ No successful comparisons out of {counter} attempts")

def test_channel0_polling_speed(client: ModbusTcpClient, registers: Dict[str, Dict[str, Any]]):
    """Test the polling speed of channel 0 active power."""
    print("\n" + "="*80)
    print("🚀 CHANNEL 0 ACTIVE POWER POLLING SPEED TEST")
    print("="*80)
    
    start_time = time.time()
    counter = 0
    total_power = 0
    successful_reads = 0
    failed_reads = 0
    response_times = []
    
    # Find the channel 0 active power register
    ch0_active_power_key = None
    for name, reg_info in registers.items():
        if "Ch0-Active-Power" in name or "Channel 0 Active Power" in name:
            ch0_active_power_key = name
            break
    
    if not ch0_active_power_key:
        print("❌ Could not find Channel 0 Active Power register")
        return
    
    print(f"📋 Testing register: {ch0_active_power_key}")
    print(f"📊 Target: 1000 polls")
    print("─"*80)

    while counter < 1000:
        counter += 1
        request_time = time.time()
        value = read_register(
            client, 
            registers[ch0_active_power_key]["address"],
            registers[ch0_active_power_key]["size"],
            registers[ch0_active_power_key]["type"]
        )
        response_time = (time.time() - request_time) * 1000
        response_times.append(response_time)
        
        if value is not None:
            successful_reads += 1
            total_power += value
            if counter % 100 == 0:  # Show progress every 100 polls
                print(f"📈 Progress: {counter}/1000 polls | Current: {value:.2f}W | Avg response: {response_time:.1f}ms")
        else:
            failed_reads += 1
            print(f"❌ Error reading Channel 0 Active Power (poll {counter})")

    total_time = time.time() - start_time
    avg_response_time = sum(response_times) / len(response_times) if response_times else 0
    min_response_time = min(response_times) if response_times else 0
    max_response_time = max(response_times) if response_times else 0
    avg_power = (total_power / successful_reads) if successful_reads > 0 else 0
    
    print("\n" + "─"*80)
    print(f"🎯 POLLING SPEED RESULTS:")
    print(f"   ✓ Successful reads: {successful_reads}/{counter} ({(successful_reads/counter)*100:.1f}%)")
    print(f"   ❌ Failed reads: {failed_reads}")
    print(f"   ⏱️  Total time: {total_time:.2f}s")
    print(f"   📊 Polls per second: {counter/total_time:.1f}")
    print(f"   ⚡ Avg response time: {avg_response_time:.1f}ms")
    print(f"   📈 Min/Max response: {min_response_time:.1f}ms / {max_response_time:.1f}ms")
    print(f"   🔋 Avg power reading: {avg_power:.2f}W")
    print("─"*80)

def test_register_source_comparison(client: ModbusTcpClient, register_dict: Dict[str, Dict[str, Any]]):
    """Test a specific set of registers and show their source."""
    print("\n" + "="*80)
    print(f"📋 TESTING {len(register_dict)} REGISTERS FROM JSON FILE")
    print("="*80)
    
    start_time = time.time()
    successful_reads = 0
    failed_reads = 0
    response_times = []
    
    for name, reg_info in register_dict.items():
        request_time = time.time()
        value = read_register(
            client, reg_info["address"], reg_info["size"], reg_info["type"]
        )
        response_time = (time.time() - request_time) * 1000
        response_times.append(response_time)
        
        if value is not None:
            successful_reads += 1
            unit_str = f" {reg_info.get('unit', '')}" if reg_info.get('unit') else ""
            print(f"✓ {name:45}: {value:12.3f}{unit_str:8} ({response_time:.1f}ms)")
        else:
            failed_reads += 1
            print(f"✗ {name:45}: {'ERROR':>12} ({response_time:.1f}ms)")
    
    total_time = time.time() - start_time
    avg_response_time = sum(response_times) / len(response_times) if response_times else 0
    
    print("\n" + "─"*80)
    print(f"📊 REGISTER TEST SUMMARY:")
    print(f"   ✓ Successful reads: {successful_reads}/{len(register_dict)} ({(successful_reads/len(register_dict))*100:.1f}%)")
    print(f"   ❌ Failed reads: {failed_reads}")
    print(f"   ⏱️  Total time: {total_time:.2f}s | Avg response: {avg_response_time:.1f}ms per register")
    print("─"*80)

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
    print("🔌 MODBUS REGISTER TESTING SUITE - JSON CONFIGURATION")
    print("=" * 90)
    print(f"🌐 Server: {server_ip}:{SERVER_PORT}")
    
    # Load JSON registers
    script_dir = os.path.dirname(os.path.abspath(__file__))
    json_file_path = os.path.join(script_dir, "modbus_registers.json")
    registers = load_registers_from_json(json_file_path)
    
    if not registers:
        print("❌ No registers loaded from JSON file. Exiting.")
        return
    
    print(f"📊 Loaded {len(registers)} registers from configuration")
    
    overall_start_time = time.time()
    test_results = {}
    
    print(f"\n🧪 TEST 1: Individual Register Validation")
    test_register_source_comparison(client, registers)
    
    print(f"\n🧪 TEST 2: High-Speed Polling Performance")
    test_channel0_polling_speed(client, registers)
    
    print(f"\n🧪 TEST 3: Power Measurement Accuracy")
    test_channel0_active_power(client, registers)
    
    total_test_time = time.time() - overall_start_time
    
    print("\n" + "="*90)
    print("📈 COMPREHENSIVE TEST SUITE RESULTS")
    print("="*90)
    print(f"🏁 Test Suite Duration: {total_test_time:.2f} seconds")
    print(f"📊 Total Registers Tested: {len(registers)}")
    print(f"🌐 Modbus Server: {server_ip}:{SERVER_PORT}")
    print(f"📋 Configuration Source: {json_file_path}")
    
    type_counts = {}
    address_ranges = {"min": float('inf'), "max": 0}
    
    for name, reg_info in registers.items():
        reg_type = reg_info["type"]
        type_counts[reg_type] = type_counts.get(reg_type, 0) + 1
        
        addr = reg_info["address"]
        if addr < address_ranges["min"]:
            address_ranges["min"] = addr
        if addr > address_ranges["max"]:
            address_ranges["max"] = addr
    
    print(f"\n📋 REGISTER ANALYSIS:")
    print(f"   📍 Address Range: {address_ranges['min']} - {address_ranges['max']}")
    print(f"   🏷️  Data Types:")
    for dtype, count in sorted(type_counts.items()):
        percentage = (count / len(registers)) * 100
        print(f"      • {dtype:12}: {count:3} registers ({percentage:5.1f}%)")
    
    print(f"\n✅ Test suite completed successfully!")
    print("="*90)

if __name__ == "__main__":
    main()
