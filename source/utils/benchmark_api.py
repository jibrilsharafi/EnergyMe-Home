# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

#!/usr/bin/env python3
"""
EnergyMe-Home API Benchmarking Tool

This script polls all GET endpoints from the EnergyMe-Home API and benchmarks their performance.
It measures response times, success rates, and provides detailed statistics.

Usage:
    python3 benchmark_api.py -H <host> [options]

Example:
    python3 benchmark_api.py -H 192.168.1.200
    python3 benchmark_api.py -H energyme.local --port 80 -r 10 -c 5
"""

import argparse
import json
import sys
import threading
import time
import statistics
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple
import requests
from requests.adapters import HTTPAdapter
from requests.auth import HTTPDigestAuth
from urllib3.util.retry import Retry

from _device_auth import add_device_args, resolve_credentials


@dataclass
class EndpointResult:
    """Results for a single endpoint benchmark"""
    endpoint: str
    success_count: int = 0
    error_count: int = 0
    response_times: List[float] = field(default_factory=list)
    status_codes: Dict[int, int] = field(default_factory=dict)
    error_messages: List[str] = field(default_factory=list)
    
    @property
    def total_requests(self) -> int:
        return self.success_count + self.error_count
    
    @property
    def success_rate(self) -> float:
        if self.total_requests == 0:
            return 0.0
        return (self.success_count / self.total_requests) * 100
    
    @property
    def avg_response_time(self) -> float:
        return statistics.mean(self.response_times) if self.response_times else 0.0
    
    @property
    def min_response_time(self) -> float:
        return min(self.response_times) if self.response_times else 0.0
    
    @property
    def max_response_time(self) -> float:
        return max(self.response_times) if self.response_times else 0.0
    
    @property
    def median_response_time(self) -> float:
        return statistics.median(self.response_times) if self.response_times else 0.0


class ApiBenchmark:
    """Main benchmarking class for EnergyMe-Home API"""
    
    # All GET endpoints from the swagger.yaml.
    # Excludes: /api/v1/files/{filepath} and /api/v1/crash/entry (need a real,
    # dynamic id/path we can't know in advance), and /api/v1/backup/* (large
    # streamed dumps not representative of a normal request/response cycle).
    GET_ENDPOINTS = [
        "/api/v1/health",
        "/api/v1/auth/status",
        "/api/v1/ota/status",
        "/api/v1/system/info",
        "/api/v1/system/statistics",
        "/api/v1/system/safe-mode",
        "/api/v1/system/secrets",
        "/api/v1/system/time",
        "/api/v1/system/issues",
        "/api/v1/firmware/update-info",
        "/api/v1/list-files",
        "/api/v1/network/wifi/scan",
        "/api/v1/network/wifi/status",
        "/api/v1/network/wifi/diagnostics",
        "/api/v1/network/config",
        "/api/v1/crash/info",
        "/api/v1/crash/dump",
        "/api/v1/logs/level",
        "/api/v1/logs",
        "/api/v1/logs-udp-destination",
        "/api/v1/custom-mqtt/config",
        "/api/v1/custom-mqtt/status",
        "/api/v1/mqtt/cloud-services",
        "/api/v1/influxdb/config",
        "/api/v1/influxdb/status",
        "/api/v1/led",
        "/api/v1/led/brightness",
        "/api/v1/ade7953/config",
        "/api/v1/ade7953/sample-time",
        "/api/v1/ade7953/channel",
        "/api/v1/ade7953/register?address=0x031E&bits=32",
        "/api/v1/ade7953/meter-values",
        "/api/v1/ade7953/grid-frequency",
        "/api/v1/ade7953/waveform/status",
        "/api/v1/ade7953/waveform/data",
        # Parameterized endpoints with common parameter values
        "/api/v1/ade7953/channel?index=0",
        "/api/v1/ade7953/channel?index=1",
        "/api/v1/ade7953/meter-values?index=0",
    ]

    # Endpoints reachable with zero credentials. Note GET /api/v1/auth/status
    # is exempt from the default-password 403 lockdown per swagger, but that
    # exemption is app-level only - it still requires valid Digest auth (401
    # without it), confirmed against a real device. Only /api/v1/health skips
    # authentication entirely.
    UNAUTH_ENDPOINTS = [
        "/api/v1/health",
    ]

    def __init__(self, host: str, port: int = 80, protocol: str = "http", timeout: float = 10.0,
                 username: str = None, password: str = None):
        """
        Initialize the benchmark with connection parameters

        Args:
            host: Target host IP address or hostname
            port: Target port number
            protocol: Protocol to use (http or https)
            timeout: Request timeout in seconds
            username: Username for authentication
            password: Password for authentication
        """
        self.base_url = f"{protocol}://{host}:{port}"
        self.timeout = timeout
        self.username = username
        self.password = password
        self.session = self._create_session(authenticated=True)
        # Only meaningful as a distinct check when credentials were supplied;
        # otherwise self.session is already anonymous.
        self.session_unauth = self._create_session(authenticated=False) if username and password else None
        self.results: Dict[str, EndpointResult] = {}
        # Endpoints run concurrently on separate threads; without this, their
        # incremental "Testing X... ..." progress prints interleave into
        # garbled output. Each endpoint's line is built up in memory and
        # printed as one atomic write instead.
        self._print_lock = threading.Lock()

    def _create_session(self, authenticated: bool) -> requests.Session:
        """Create a configured requests session with retry strategy"""
        session = requests.Session()

        # Configure authentication if credentials provided
        if authenticated and self.username and self.password:
            session.auth = HTTPDigestAuth(self.username, self.password)

        # Configure retry strategy
        retry_strategy = Retry(
            total=3,
            backoff_factor=0.1,
            status_forcelist=[429, 500, 502, 503, 504],
        )

        adapter = HTTPAdapter(max_retries=retry_strategy)
        session.mount("http://", adapter)
        session.mount("https://", adapter)

        # Set default headers
        session.headers.update({
            'User-Agent': 'EnergyMe-Home-Benchmark/1.0',
            'Accept': 'application/json',
        })

        return session

    def test_endpoint(self, endpoint: str, session: Optional[requests.Session] = None) -> Tuple[bool, float, int, Optional[str]]:
        """
        Test a single endpoint and return results

        Returns:
            Tuple of (success, response_time, status_code, error_message)
        """
        url = self.base_url + endpoint
        start_time = time.time()

        try:
            response = (session or self.session).get(url, timeout=self.timeout)
            response_time = time.time() - start_time
            
            # Consider 2xx and 3xx as success, 4xx/5xx as application errors
            success = response.status_code < 400
            return success, response_time, response.status_code, None
            
        except requests.exceptions.Timeout:
            response_time = time.time() - start_time
            return False, response_time, 0, "Request timeout"
        except requests.exceptions.ConnectionError:
            response_time = time.time() - start_time
            return False, response_time, 0, "Connection error"
        except requests.exceptions.RequestException as e:
            response_time = time.time() - start_time
            return False, response_time, 0, str(e)
    
    def benchmark_endpoint(self, endpoint: str, num_requests: int,
                            session: Optional[requests.Session] = None, label: Optional[str] = None) -> EndpointResult:
        """
        Benchmark a single endpoint with multiple requests

        Args:
            endpoint: The endpoint path to test
            num_requests: Number of requests to make
            session: Session to use (defaults to the authenticated session)
            label: Display/result-key override (defaults to endpoint)

        Returns:
            EndpointResult with aggregated statistics
        """
        display_name = label or endpoint
        result = EndpointResult(endpoint=display_name)
        progress = []

        for i in range(num_requests):
            success, response_time, status_code, error_msg = self.test_endpoint(endpoint, session=session)

            if success:
                result.success_count += 1
                result.response_times.append(response_time)
            else:
                result.error_count += 1
                if error_msg:
                    result.error_messages.append(error_msg)

            # Track status codes
            result.status_codes[status_code] = result.status_codes.get(status_code, 0) + 1

            # Show progress
            if num_requests > 1 and (i + 1) % max(1, num_requests // 10) == 0:
                progress.append(".")

        with self._print_lock:
            print(f"Testing {display_name}... {''.join(progress)} Done ({result.success_count}/{num_requests} successful)")
        return result
    
    def benchmark_all_endpoints(self, num_requests: int = 10, max_workers: int = 5) -> Dict[str, EndpointResult]:
        """
        Benchmark all endpoints concurrently
        
        Args:
            num_requests: Number of requests per endpoint
            max_workers: Maximum number of concurrent workers
            
        Returns:
            Dictionary mapping endpoint paths to their results
        """
        # (endpoint, session, result_key) for every request to make
        jobs = [(endpoint, self.session, endpoint) for endpoint in self.GET_ENDPOINTS]
        if self.session_unauth:
            jobs += [
                (endpoint, self.session_unauth, f"{endpoint} [unauth]")
                for endpoint in self.UNAUTH_ENDPOINTS
            ]

        print(f"Starting benchmark of {len(jobs)} endpoints...")
        print(f"Requests per endpoint: {num_requests}")
        print(f"Target: {self.base_url}")
        print(f"Timeout: {self.timeout}s")
        print("-" * 60)

        start_time = time.time()

        if max_workers == 1:
            # Sequential execution
            for endpoint, session, result_key in jobs:
                self.results[result_key] = self.benchmark_endpoint(endpoint, num_requests, session=session, label=result_key)
        else:
            # Concurrent execution
            with ThreadPoolExecutor(max_workers=max_workers) as executor:
                future_to_key = {
                    executor.submit(self.benchmark_endpoint, endpoint, num_requests, session, result_key): result_key
                    for endpoint, session, result_key in jobs
                }

                for future in as_completed(future_to_key):
                    result_key = future_to_key[future]
                    try:
                        result = future.result()
                        self.results[result_key] = result
                    except Exception as e:
                        print(f"Error testing {result_key}: {e}")
                        # Create empty result for failed endpoint
                        self.results[result_key] = EndpointResult(endpoint=result_key)
        
        total_time = time.time() - start_time
        print("-" * 60)
        print(f"Benchmark completed in {total_time:.2f} seconds")
        
        return self.results
    
    def print_summary(self):
        """Print a summary of benchmark results"""
        if not self.results:
            print("No results to display")
            return
        
        print("\n" + "=" * 80)
        print("BENCHMARK SUMMARY")
        print("=" * 80)
        
        # Overall statistics
        total_requests = sum(r.total_requests for r in self.results.values())
        total_successful = sum(r.success_count for r in self.results.values())
        total_errors = sum(r.error_count for r in self.results.values())
        
        all_response_times = []
        for result in self.results.values():
            all_response_times.extend(result.response_times)
        
        print(f"Total endpoints tested: {len(self.results)}")
        print(f"Total requests made: {total_requests}")
        print(f"Total successful: {total_successful}")
        print(f"Total errors: {total_errors}")
        print(f"Overall success rate: {(total_successful/total_requests*100):.1f}%" if total_requests > 0 else "N/A")
        
        if all_response_times:
            print(f"Average response time: {statistics.mean(all_response_times):.3f}s")
            print(f"Median response time: {statistics.median(all_response_times):.3f}s")
            print(f"Min response time: {min(all_response_times):.3f}s")
            print(f"Max response time: {max(all_response_times):.3f}s")
        
        print("\n" + "-" * 80)
        print("ENDPOINT DETAILS")
        print("-" * 80)
        print(f"{'Endpoint':<35} {'Success':<8} {'Avg (ms)':<9} {'Min (ms)':<9} {'Max (ms)':<9} {'Status'}")
        print("-" * 80)
        
        # Sort by average response time (fastest first)
        sorted_results = sorted(
            self.results.items(),
            key=lambda x: x[1].avg_response_time if x[1].response_times else float('inf')
        )
        
        for endpoint, result in sorted_results:
            # Truncate long endpoint names
            display_endpoint = endpoint[:32] + "..." if len(endpoint) > 35 else endpoint
            
            success_rate = f"{result.success_rate:.1f}%"
            avg_ms = f"{result.avg_response_time*1000:.1f}" if result.response_times else "N/A"
            min_ms = f"{result.min_response_time*1000:.1f}" if result.response_times else "N/A"
            max_ms = f"{result.max_response_time*1000:.1f}" if result.response_times else "N/A"
            
            # Get most common status code
            if result.status_codes:
                common_status = max(result.status_codes.items(), key=lambda x: x[1])[0]
                status_display = str(common_status)
            else:
                status_display = "N/A"
            
            print(f"{display_endpoint:<35} {success_rate:<8} {avg_ms:<9} {min_ms:<9} {max_ms:<9} {status_display}")
    
    def print_errors(self):
        """Print detailed error information"""
        error_results = {k: v for k, v in self.results.items() if v.error_count > 0}
        
        if not error_results:
            print("\n✅ No errors encountered!")
            return
        
        print("\n" + "=" * 80)
        print("ERROR DETAILS")
        print("=" * 80)
        
        for endpoint, result in error_results.items():
            print(f"\n{endpoint}:")
            print(f"  Error count: {result.error_count}/{result.total_requests}")
            
            # Show status code distribution
            error_status_codes = {k: v for k, v in result.status_codes.items() if k >= 400 or k == 0}
            if error_status_codes:
                print("  Status codes:")
                for status_code, count in error_status_codes.items():
                    status_name = "Connection Error" if status_code == 0 else f"HTTP {status_code}"
                    print(f"    {status_name}: {count}")
            
            # Show unique error messages
            if result.error_messages:
                unique_errors = list(set(result.error_messages))
                print("  Error messages:")
                for error in unique_errors[:5]:  # Limit to first 5 unique errors
                    print(f"    - {error}")
                if len(unique_errors) > 5:
                    print(f"    ... and {len(unique_errors) - 5} more")
    
    def save_results_json(self, filename: str):
        """Save benchmark results to JSON file"""
        data = {
            "benchmark_info": {
                "target_url": self.base_url,
                "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
                "timeout": self.timeout,
                "total_endpoints": len(self.results)
            },
            "results": {}
        }
        
        for endpoint, result in self.results.items():
            data["results"][endpoint] = {
                "success_count": result.success_count,
                "error_count": result.error_count,
                "total_requests": result.total_requests,
                "success_rate": result.success_rate,
                "response_times": result.response_times,
                "avg_response_time": result.avg_response_time,
                "min_response_time": result.min_response_time,
                "max_response_time": result.max_response_time,
                "median_response_time": result.median_response_time,
                "status_codes": result.status_codes,
                "error_messages": result.error_messages
            }
        
        with open(filename, 'w') as f:
            json.dump(data, f, indent=2)
        
        print(f"\nResults saved to: {filename}")


def main():
    """Main function to run the benchmark"""
    parser = argparse.ArgumentParser(
        description="Benchmark EnergyMe-Home API endpoints",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s -H 192.168.1.200
  %(prog)s -H energyme.local --port 80 -r 10 -c 5
  %(prog)s -H 192.168.1.200 --https -r 20 --timeout 15
  %(prog)s -H 192.168.1.200 -u admin -p mypassword -r 5 --sequential --save results.json
        """
    )
    
    add_device_args(parser)
    parser.add_argument('--port', type=int, default=80,
                       help='Target port number (default: 80)')
    parser.add_argument('--https', action='store_true',
                       help='Use HTTPS instead of HTTP')
    parser.add_argument('-r', '--requests', type=int, default=10,
                       help='Number of requests per endpoint (default: 10)')
    parser.add_argument('-c', '--concurrent', type=int, default=5,
                       help='Number of concurrent workers (default: 5)')
    parser.add_argument('--sequential', action='store_true',
                       help='Run tests sequentially instead of concurrently')
    parser.add_argument('-t', '--timeout', type=float, default=10.0,
                       help='Request timeout in seconds (default: 10.0)')
    parser.add_argument('--save', metavar='FILE',
                       help='Save results to JSON file')
    parser.add_argument('--no-errors', action='store_true',
                       help='Skip printing detailed error information')
    parser.add_argument('-v', '--verbose', action='store_true',
                       help='Enable verbose output')
    
    args = parser.parse_args()
    
    # Determine protocol
    protocol = "https" if args.https else "http"
    if args.https and args.port == 80:
        args.port = 443  # Default HTTPS port
    
    # Create benchmark instance
    username, password = resolve_credentials(args)
    benchmark = ApiBenchmark(
        host=args.host,
        port=args.port,
        protocol=protocol,
        timeout=args.timeout,
        username=username,
        password=password
    )
    
    # Determine concurrency
    max_workers = 1 if args.sequential else args.concurrent
    
    try:
        # Run benchmark
        results = benchmark.benchmark_all_endpoints(
            num_requests=args.requests,
            max_workers=max_workers
        )
        
        # Print results
        benchmark.print_summary()
        
        if not args.no_errors:
            benchmark.print_errors()
        
        # Save to file if requested
        if args.save:
            benchmark.save_results_json(args.save)
            
    except KeyboardInterrupt:
        print("\n\nBenchmark interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\nError during benchmark: {e}")
        if args.verbose:
            import traceback
            traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
