# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

#!/usr/bin/env python3
"""
Simple mock server for EnergyMe HTML development
Serves static files and provides basic mock API responses
"""

import http.server
import socketserver
from http import HTTPStatus
import functools
import itertools
import json
import os
import random
from datetime import datetime, timedelta
from urllib.parse import urlparse, parse_qs
import threading
import time
import math

PORT = 8081

class SimpleHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory='.', **kwargs)
    
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        super().end_headers()
    
    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()
    
    def do_POST(self):
        # Handle POST requests with generic success responses
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        success_endpoints = [
            '/api/v1/system/restart',
            '/api/v1/system/factory-reset',
            '/api/v1/network/wifi/reset',
            '/api/v1/crash/clear',
            '/api/v1/logs/clear',
            '/api/v1/custom-mqtt/config/reset',
            '/api/v1/influxdb/config/reset',
            '/api/v1/ade7953/config/reset',
            '/api/v1/ade7953/channel/reset',
            '/api/v1/ade7953/energy/reset',
            '/api/v1/auth/change-password',
            '/api/v1/auth/reset-password',
            '/api/v1/ota/rollback'
        ]
        
        if path in success_endpoints:
            self.send_json({"success": True, "message": "Operation completed"})
        else:
            # Special-case waveform arm endpoint
            if path == '/api/v1/ade7953/waveform/arm':
                # Read body if present
                try:
                    length = int(self.headers.get('Content-Length', 0))
                    body = self.rfile.read(length) if length > 0 else b''
                    payload = json.loads(body) if body else {}
                except Exception:
                    payload = {}

                channelIndex = payload.get('channelIndex', 0)
                sample_rate_hz = int(payload.get('sampleRateHz', 25000))
                capture_duration_ms = int(payload.get('durationMs', 40))

                # If already capturing, return 409
                current_state = getattr(self.server, 'waveform_state', 'idle')
                if current_state in ('armed', 'capturing'):
                    # send JSON with 409 status (send_json forces 200 so write manually)
                    body = json.dumps({"success": False, "message": "Capture already in progress"}).encode()
                    self.send_response(409)
                    self.send_header('Content-Type', 'application/json')
                    self.send_header('Content-Length', str(len(body)))
                    self.end_headers()
                    try:
                        self.wfile.write(body)
                    except BrokenPipeError:
                        pass
                    return

                # Arm the capture and spawn simulator thread
                self.server.waveform_state = 'armed'
                self.server.waveform_channel = channelIndex

                def simulate_capture(server, channel, sample_rate_hz_inner, capture_duration_ms_inner):
                    # Wait a short random time to simulate waiting for line cycle
                    server.waveform_state = 'capturing'
                    # use provided parameters
                    sample_interval_us = int(1_000_000 / sample_rate_hz_inner)
                    sample_count = max(16, int((capture_duration_ms_inner * 1000) / sample_interval_us))

                    # Build arrays: microsDelta cumulative, voltage (V), current (A)
                    micros = []
                    voltage = []
                    current = []
                    start_us = int(time.time() * 1_000_000)
                    for n in range(sample_count):
                        micros.append((n * sample_interval_us))
                        # simple sine-like mock for voltage around 230V peak-to-peak small noise
                        t = (n / sample_count) * 2.0 * 3.14159
                        v = 230 + 5.0 * math.sin(t * 10) + random.uniform(-0.5, 0.5)
                        i = 0.5 * math.sin(t * 10) + random.uniform(-0.02, 0.02)
                        voltage.append(round(v, 3))
                        current.append(round(i, 4))

                    # Simulate a small processing delay
                    time.sleep(capture_duration_ms_inner / 1000.0)

                    server.waveform_data = {
                        "channelIndex": channel,
                        "state": "complete",
                        "captureStartUnixMillis": int(time.time() * 1000) - capture_duration_ms_inner,
                        "sampleCount": sample_count,
                        "voltage": voltage,
                        "current": current,
                        "microsDelta": micros
                    }
                    server.waveform_state = 'complete'

                thread = threading.Thread(
                    target=simulate_capture,
                    args=(self.server, channelIndex, sample_rate_hz, capture_duration_ms),
                    daemon=True
                )
                thread.start()

                self.send_json({"success": True, "message": "Capture armed"})
                return

            self.send_response(404)
            self.end_headers()
    
    def do_PUT(self):
        # Handle PUT requests with generic success responses
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        success_endpoints = [
            '/api/v1/logs/level',
            '/api/v1/custom-mqtt/config',
            '/api/v1/influxdb/config',
            '/api/v1/led/brightness',
            '/api/v1/ade7953/config',
            '/api/v1/ade7953/sample-time',
            '/api/v1/ade7953/channel',
            '/api/v1/ade7953/register',
            '/api/v1/ade7953/energy',
            '/api/v1/mqtt/cloud-services'
        ]
        
        if path in success_endpoints or path.startswith('/api/v1/ade7953/channel'):
            self.send_json({"success": True, "message": "Configuration updated"})
        else:
            self.send_response(404)
            self.end_headers()
    
    def do_PATCH(self):
        # Handle PATCH requests with generic success responses
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        success_endpoints = [
            '/api/v1/logs/level',
            '/api/v1/custom-mqtt/config',
            '/api/v1/influxdb/config',
            '/api/v1/ade7953/config',
            '/api/v1/ade7953/channel'
        ]
        
        if path in success_endpoints or path.startswith('/api/v1/ade7953/channel'):
            self.send_json({"success": True, "message": "Configuration updated"})
        else:
            self.send_response(404)
            self.end_headers()
    
    def do_GET(self):
        parsed_path = urlparse(self.path)
        path = parsed_path.path

        # Live-reload JS client
        if path == '/livereload.js':
            self.send_response(200)
            self.send_header('Content-Type', 'application/javascript')
            self.end_headers()
            js = (
                "(function(){\n"
                "  try{\n"
                "    var es=new EventSource('/livereload');\n"
                "    es.onmessage=function(e){ if(e.data==='reload') location.reload(); };\n"
                "  }catch(e){console.error('LiveReload failed',e);}\n"
                "})();\n"
            )
            self.wfile.write(js.encode('utf-8'))
            return

        # Server-Sent Events endpoint for live-reload
        if path == '/livereload':
            # Send SSE headers and keep connection open
            self.send_response(HTTPStatus.OK)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Cache-Control', 'no-cache')
            self.send_header('Connection', 'keep-alive')
            self.end_headers()

            # Register client writer
            clients = getattr(self.server, 'sse_clients', None)
            if clients is None:
                clients = []
                setattr(self.server, 'sse_clients', clients)

            clients.append(self.wfile)
            try:
                # Keep the handler thread alive while client listens
                while True:
                    time.sleep(1)
                    # If server requests shutdown, break
                    if getattr(self.server, 'shutdown_requested', False):
                        break
            except Exception:
                pass
            finally:
                try:
                    clients.remove(self.wfile)
                except Exception:
                    pass
            return
        
        # API endpoints
        if path == '/api/v1/ade7953/channel':
            self.send_json([
                {"index": 0, "active": True, "label": "Total House", "multiplier": 1},
                {"index": 1, "active": True, "label": "Kitchen", "multiplier": 1},
                {"index": 2, "active": True, "label": "Living Room", "multiplier": 1},
                {"index": 3, "active": True, "label": "Bedroom", "multiplier": 1},
                {"index": 4, "active": True, "label": "Office", "multiplier": 1},
                {"index": 5, "active": True, "label": "Bathroom", "multiplier": 1}
            ])
        
        elif path == '/api/v1/ade7953/meter-values':
            self.send_json([
                {
                    "index": 0,
                    "label": "Total House",
                    "data": {
                        "voltage": 235.2 + random.uniform(-2, 2),
                        "activePower": random.uniform(800, 1500),
                        "activeEnergyImported": random.uniform(50000, 100000)
                    }
                },
                {
                    "index": 1,
                    "label": "Kitchen",
                    "data": {
                        "voltage": 235.2,
                        "activePower": random.uniform(100, 400),
                        "activeEnergyImported": random.uniform(8000, 15000)
                    }
                },
                {
                    "index": 2,
                    "label": "Living Room", 
                    "data": {
                        "voltage": 235.2,
                        "activePower": random.uniform(50, 200),
                        "activeEnergyImported": random.uniform(6000, 12000)
                    }
                },
                {
                    "index": 3,
                    "label": "Bedroom",
                    "data": {
                        "voltage": 235.2,
                        "activePower": random.uniform(20, 100),
                        "activeEnergyImported": random.uniform(3000, 8000)
                    }
                },
                {
                    "index": 4,
                    "label": "Office",
                    "data": {
                        "voltage": 235.2,
                        "activePower": random.uniform(80, 250),
                        "activeEnergyImported": random.uniform(5000, 10000)
                    }
                },
                {
                    "index": 5,
                    "label": "Bathroom",
                    "data": {
                        "voltage": 235.2,
                        "activePower": random.uniform(10, 80),
                        "activeEnergyImported": random.uniform(2000, 5000)
                    }
                }
            ])
        
        elif path == '/api/v1/list-files':
            # Generate some mock energy files
            files = {}
            for i in range(30):  # Last 30 days
                date = (datetime.now() - timedelta(days=i)).strftime('%Y-%m-%d')
                files[f'energy/{date}.csv'] = f'energy_{date}.csv'
            self.send_json(files)
        
        elif path.startswith('/api/v1/files/'):
            # Handle file requests with URL decoding
            import urllib.parse
            filepath = urllib.parse.unquote(path.replace('/api/v1/files/', ''))
            
            if filepath.startswith('energy/') and filepath.endswith('.csv'):
                # Generate mock CSV data
                date_str = filepath.split('/')[-1].replace('.csv', '')
                csv_data = self.generate_mock_csv(date_str)
                self.send_response(200)
                self.send_header('Content-Type', 'text/csv')
                self.end_headers()
                self.wfile.write(csv_data.encode())
            else:
                self.send_response(404)
                self.end_headers()
        
        elif path == '/api/v1/firmware/update-info':
            self.send_json({"latest": True, "version": "1.0.0"})
        
        elif path == '/api/v1/health':
            self.send_json({"status": "healthy", "timestamp": datetime.now().isoformat()})
        
        elif path == '/api/v1/auth/status':
            self.send_json({"authenticated": True, "user": "admin"})
        
        elif path == '/api/v1/system/info':
            self.send_json({
                "firmware": {"version": "1.0.0", "buildDate": "2025-08-16"},
                "hardware": {"model": "ESP32-S3", "mac": "AA:BB:CC:DD:EE:FF"},
                "uptime": 123456
            })
        
        elif path == '/api/v1/system/statistics':
            self.send_json({
                "freeHeap": 200000,
                "usedHeap": 100000,
                "uptime": 123456,
                "wifiRssi": -45
            })
        
        elif path == '/api/v1/ota/status':
            self.send_json({"currentVersion": "1.0.0", "status": "idle"})
        
        elif path == '/api/v1/crash/info':
            self.send_json({"hasCoreDump": False, "lastResetReason": "Power on"})
        
        elif path == '/api/v1/logs/level':
            self.send_json({"printLevel": "INFO", "saveLevel": "WARNING"})
        
        elif path == '/api/v1/logs':
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            logs = "2025-08-16 10:00:00 [INFO] System started\n2025-08-16 10:00:01 [INFO] WiFi connected\n"
            self.wfile.write(logs.encode())
        
        elif path == '/api/v1/custom-mqtt/config':
            self.send_json({
                "enabled": False,
                "server": "localhost",
                "port": 1883,
                "username": "",
                "password": "",
                "useCredentials": False
            })
        
        elif path == '/api/v1/custom-mqtt/status':
            self.send_json({"status": "disconnected", "lastConnected": None})
        
        elif path == '/api/v1/influxdb/config':
            self.send_json({
                "enabled": False,
                "server": "localhost",
                "port": 8086,
                "database": "energyme",
                "username": "",
                "password": "",
                "useCredentials": False,
                "useSsl": False
            })
        
        elif path == '/api/v1/influxdb/status':
            self.send_json({"status": "disconnected", "lastWrite": None})
        
        elif path == '/api/v1/led/brightness':
            self.send_json({"brightness": 128})
        
        elif path == '/api/v1/ade7953/config':
            self.send_json({
                "aVGain": 1024,
                "bVGain": 1024,
                "aIGain": 1024,
                "bIGain": 1024,
                "phCalA": 0,
                "phCalB": 0
            })
        
        elif path == '/api/v1/ade7953/sample-time':
            self.send_json({"sampleTime": 1000})
        
        elif path == '/api/v1/ade7953/grid-frequency':
            self.send_json({"frequency": 50.0 + random.uniform(-0.1, 0.1)})

        # Waveform capture simulation endpoints
        elif path == '/api/v1/ade7953/waveform/status':
            # Return simulated status stored on the server object
            state = getattr(self.server, 'waveform_state', 'idle')
            channel = getattr(self.server, 'waveform_channel', None)
            self.send_json({
                "state": state,
                "channelIndex": channel
            })

        elif path == '/api/v1/ade7953/waveform/data':
            # Return simulated waveform data if available
            data = getattr(self.server, 'waveform_data', None)
            if data is None:
                self.send_response(404)
                self.end_headers()
            else:
                self.send_json(data)

            # Clear the stored data after serving
            setattr(self.server, 'waveform_state', 'idle')
            setattr(self.server, 'waveform_data', None)
        
        elif path == '/api/v1/mqtt/cloud-services':
            self.send_json({"enabled": False})
        
        elif path == '/api/v1/system/secrets':
            self.send_json({"hasSecrets": True})
        
        # HTML page routing
        elif path == '/' or path == '/index':
            self.serve_html_file('html/index.html')
        elif path == '/info':
            self.serve_html_file('html/info.html')
        elif path == '/configuration':
            self.serve_html_file('html/configuration.html')
        elif path == '/update':
            self.serve_html_file('html/update.html')
        elif path == '/calibration':
            self.serve_html_file('html/calibration.html')
        elif path == '/channel':
            self.serve_html_file('html/channel.html')
        elif path == '/log':
            self.serve_html_file('html/log.html')
        elif path == '/swagger-ui':
            self.serve_html_file('html/swagger.html')
        elif path == '/ade7953-tester':
            self.serve_html_file('html/ade7953-tester.html')
        
        else:
            # Serve static files
            super().do_GET()
    
    def send_json(self, data):
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())
    
    def serve_html_file(self, filename):
        """Serve an HTML file from the current directory"""
        try:
            filepath = filename  # Remove the '../' prefix since we're in the correct directory
            with open(filepath, 'r', encoding='utf-8') as f:
                content = f.read()
            
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(content.encode('utf-8'))
        except FileNotFoundError:
            self.send_response(404)
            self.send_header('Content-Type', 'text/html')
            self.end_headers()
            self.wfile.write(b'<h1>404 - Page Not Found</h1>')
        except Exception as e:
            print(f"Error serving {filename}: {e}")
            self.send_response(500)
            self.end_headers()
    
    def generate_mock_csv(self, date_str):
        """Generate realistic mock CSV energy data for a day"""
        csv_lines = ["timestamp,channel,activeImported,activeExported"]
        
        # Generate hourly data for each channel
        base_date = datetime.strptime(date_str, '%Y-%m-%d')
        
        for hour in range(24):
            timestamp = (base_date + timedelta(hours=hour)).isoformat()
            
            # Channel 0 (total) - cumulative energy
            total_energy = hour * random.uniform(0.5, 2.0) * 1000  # Wh
            csv_lines.append(f"{timestamp},0,{total_energy:.1f},0")
            
            # Other channels - portions of total
            for channel in range(1, 6):
                channel_energy = hour * random.uniform(0.1, 0.5) * 1000  # Wh
                csv_lines.append(f"{timestamp},{channel},{channel_energy:.1f},0")
        
        return '\n'.join(csv_lines)

class FileWatcher(threading.Thread):
    def __init__(self, server, watch_paths, interval=0.5):
        super().__init__(daemon=True)
        self.server = server
        self.watch_paths = watch_paths
        self.interval = interval
        self._snap = {}

    def snapshot(self):
        snap = {}
        for root in self.watch_paths:
            for dirpath, dirnames, filenames in os.walk(root):
                for f in filenames:
                    if f.endswith(('.html', '.js', '.css', '.py')):
                        p = os.path.join(dirpath, f)
                        try:
                            snap[p] = os.path.getmtime(p)
                        except Exception:
                            pass
        return snap

    def run(self):
        self._snap = self.snapshot()
        while True:
            time.sleep(self.interval)
            new = self.snapshot()
            if new != self._snap:
                self._snap = new
                # notify SSE clients
                clients = getattr(self.server, 'sse_clients', [])
                for w in list(clients):
                    try:
                        w.write(b'data: reload\n\n')
                        w.flush()
                    except Exception:
                        try:
                            clients.remove(w)
                        except Exception:
                            pass


if __name__ == '__main__':
    print(f"Starting simple mock server on http://localhost:{PORT}")
    print("Serving HTML files and mock APIs...")
    print("Press Ctrl+C to stop")
    
    # Use a threading server so SSE clients don't block other handlers
    with socketserver.ThreadingTCPServer(("", PORT), SimpleHandler) as httpd:
        # Start file watcher watching the repo root
        watcher = FileWatcher(httpd, ['.'], interval=0.5)
        watcher.start()
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped")
