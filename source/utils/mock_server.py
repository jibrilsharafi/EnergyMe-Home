# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Jibril Sharafi

#!/usr/bin/env python3
"""
Development server for EnergyMe HTML development.
Serves static files and API responses from JSON mock files.

Usage:
    python mock_server.py                              # Mock mode (from mocks/ folder)
    python mock_server.py --proxy 192.168.2.75 pass   # Proxy mode (real device)
    python mock_server.py --fetch 192.168.2.75 pass   # Fetch real data to mocks/
"""

import argparse
import http.server
import socketserver
from http import HTTPStatus
import json
import os
from datetime import datetime, timedelta
from urllib.parse import urlparse, unquote
import threading
import time
import urllib.request
import urllib.error

PORT = 8081
PROXY_TARGET = None
PROXY_PASSWORD = None
MOCKS_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'mocks')
LITTLE_FS_DIR = os.path.join(MOCKS_DIR, 'little-fs')

# All GET endpoints from swagger.yaml
GET_ENDPOINTS = [
    '/api/v1/health',
    '/api/v1/auth/status',
    '/api/v1/ota/status',
    '/api/v1/system/info',
    '/api/v1/system/statistics',
    '/api/v1/system/safe-mode',
    '/api/v1/system/secrets',
    '/api/v1/system/time',
    '/api/v1/firmware/update-info',
    '/api/v1/list-files',
    '/api/v1/crash/info',
    '/api/v1/logs/level',
    '/api/v1/logs',
    '/api/v1/logs-udp-destination',
    '/api/v1/custom-mqtt/config',
    '/api/v1/custom-mqtt/status',
    '/api/v1/mqtt/cloud-services',
    '/api/v1/influxdb/config',
    '/api/v1/influxdb/status',
    '/api/v1/led/brightness',
    '/api/v1/ade7953/config',
    '/api/v1/ade7953/sample-time',
    '/api/v1/ade7953/channel',
    '/api/v1/ade7953/meter-values',
    '/api/v1/ade7953/grid-frequency',
    '/api/v1/ade7953/waveform/status',
    '/api/v1/ade7953/waveform/data',
]


def get_mock_file_path(api_path: str) -> str:
    """Convert API path to mock file path."""
    # Remove leading slash and add .json extension
    # /api/v1/system/info -> mocks/api/v1/system/info.json
    relative_path = api_path.lstrip('/') + '.json'
    return os.path.join(MOCKS_DIR, relative_path)


def load_mock_response(api_path: str):
    """Load mock response from JSON file."""
    file_path = get_mock_file_path(api_path)
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            return json.load(f)
    except FileNotFoundError:
        return None
    except json.JSONDecodeError as e:
        print(f"Error parsing {file_path}: {e}")
        return None


def save_mock_response(api_path: str, data):
    """Save response data to mock JSON file."""
    file_path = get_mock_file_path(api_path)
    os.makedirs(os.path.dirname(file_path), exist_ok=True)
    with open(file_path, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2)
    print(f"  Saved: {file_path}")


def list_little_fs_files(folder = None):
    """List files in mocks/little-fs directory, mimicking device's LittleFS.
    
    - Returns dict of {filepath: size}
    - If folder specified, returns only filenames (not full paths) within that folder
    - Paths are relative to little-fs root (no leading slash)
    """
    result = {}
    
    if not os.path.isdir(LITTLE_FS_DIR):
        return result
    
    for root, dirs, files in os.walk(LITTLE_FS_DIR):
        for filename in files:
            full_path = os.path.join(root, filename)
            rel_path = os.path.relpath(full_path, LITTLE_FS_DIR).replace('\\', '/')
            
            # Apply folder filter if specified
            if folder:
                if not rel_path.startswith(folder + '/'):
                    continue
                # Return just the filename when filtering by folder
                rel_path = os.path.basename(rel_path)
            
            result[rel_path] = os.path.getsize(full_path)
    
    return result


def fetch_data_files(opener, host: str):
    """Fetch all data files (energy CSVs, logs) from device and save locally."""
    print("\nFetching data files...")
    print("-" * 40)
    
    # Get the file list first
    try:
        url = f"http://{host}/api/v1/list-files"
        req = urllib.request.Request(url)
        with opener.open(req, timeout=10) as response:
            file_list = json.loads(response.read().decode('utf-8'))
    except Exception as e:
        print(f"  Failed to get file list: {e}")
        return
    
    # Download each file
    success_count = 0
    fail_count = 0
    
    for file_path, file_size in file_list.items():
        # Skip non-data files
        if not (file_path.startswith('energy/') or file_path == 'log.txt'):
            continue
        
        url = f"http://{host}/api/v1/files/{file_path}"
        local_path = os.path.join(LITTLE_FS_DIR, file_path)
        
        try:
            req = urllib.request.Request(url)
            with opener.open(req, timeout=30) as response:
                content = response.read()
                
            # Create directory structure
            os.makedirs(os.path.dirname(local_path), exist_ok=True)
            
            # Write file (binary for .gz, text for others)
            mode = 'wb' if file_path.endswith('.gz') else 'w'
            with open(local_path, mode) as f:
                if mode == 'wb':
                    f.write(content)
                else:
                    f.write(content.decode('utf-8'))
            
            print(f"  Saved: {local_path} ({file_size} bytes)")
            success_count += 1
            
        except urllib.error.HTTPError as e:
            print(f"  Failed: {file_path} (HTTP {e.code})")
            fail_count += 1
        except Exception as e:
            print(f"  Failed: {file_path} ({e})")
            fail_count += 1
    
    print(f"Data files: Success: {success_count}, Failed: {fail_count}")


def fetch_from_device(host: str, password: str):
    """Fetch all GET endpoint responses from real device and save to mocks/."""
    print(f"\nFetching data from http://{host}...")
    print("=" * 50)
    
    # Set up digest auth
    password_mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    password_mgr.add_password(None, f"http://{host}/", "admin", password)
    auth_handler = urllib.request.HTTPDigestAuthHandler(password_mgr)
    opener = urllib.request.build_opener(auth_handler)
    
    success_count = 0
    fail_count = 0
    
    for endpoint in GET_ENDPOINTS:
        url = f"http://{host}{endpoint}"
        try:
            req = urllib.request.Request(url)
            with opener.open(req, timeout=10) as response:
                content_type = response.headers.get('Content-Type', '')
                
                if 'application/json' in content_type:
                    data = json.loads(response.read().decode('utf-8'))
                    save_mock_response(endpoint, data)
                    success_count += 1
                elif 'text/plain' in content_type:
                    # For text responses like /api/v1/logs, save as-is in a wrapper
                    text = response.read().decode('utf-8')
                    save_mock_response(endpoint, {"_text": text})
                    success_count += 1
                else:
                    print(f"  Skipped: {endpoint} (content-type: {content_type})")
                    
        except urllib.error.HTTPError as e:
            print(f"  Failed: {endpoint} (HTTP {e.code})")
            fail_count += 1
        except Exception as e:
            print(f"  Failed: {endpoint} ({e})")
            fail_count += 1
    
    print("=" * 50)
    print(f"Done! Success: {success_count}, Failed: {fail_count}")
    print(f"Mock files saved to: {MOCKS_DIR}")
    
    # Also fetch data files (energy CSVs, logs)
    fetch_data_files(opener, host)


class MockHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory='.', **kwargs)
    
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, PATCH, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        super().end_headers()
    
    def log_message(self, format, *args):
        # Suppress default logging, we'll do our own
        pass
    
    def proxy_request(self, method='GET', body=None):
        """Forward request to real device."""
        parsed_path = urlparse(self.path)
        target_url = f"http://{PROXY_TARGET}{parsed_path.path}"
        if parsed_path.query:
            target_url += f"?{parsed_path.query}"
        
        try:
            assert PROXY_PASSWORD, "Proxy password not set"
            password_mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
            password_mgr.add_password(None, f"http://{PROXY_TARGET}/", "admin", PROXY_PASSWORD)
            auth_handler = urllib.request.HTTPDigestAuthHandler(password_mgr)
            opener = urllib.request.build_opener(auth_handler)
            
            req = urllib.request.Request(target_url, data=body, method=method)
            req.add_header('Content-Type', self.headers.get('Content-Type', 'application/json'))
            
            with opener.open(req, timeout=10) as response:
                content = response.read()
                content_type = response.headers.get('Content-Type', 'application/octet-stream')
                self.send_response(response.status)
                self.send_header('Content-Type', content_type)
                self.end_headers()
                self.wfile.write(content)
        except urllib.error.HTTPError as e:
            self.send_response(e.code)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            try:
                self.wfile.write(e.read())
            except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
                pass  # Client disconnected
            except:
                try:
                    self.wfile.write(json.dumps({"error": str(e)}).encode())
                except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
                    pass  # Client disconnected
        except (urllib.error.URLError, TimeoutError, ConnectionError, OSError) as e:
            print(f"Proxy connection error: {e}")
            try:
                self.send_response(504)  # Gateway Timeout
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({"error": f"Device unreachable: {e}"}).encode())
            except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
                pass  # Client disconnected
        except Exception as e:
            print(f"Proxy error: {e}")
            try:
                self.send_response(502)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps({"error": f"Proxy failed: {e}"}).encode())
            except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
                pass  # Client disconnected
    
    def send_json(self, data, status=200):
        try:
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(data).encode())
        except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
            pass  # Client disconnected, ignore
    
    def send_text(self, text, status=200):
        try:
            self.send_response(status)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(text.encode())
        except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError):
            pass  # Client disconnected, ignore
    
    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()
    
    def do_POST(self):
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        if PROXY_TARGET and path.startswith('/api/'):
            length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(length) if length > 0 else None
            self.proxy_request('POST', body)
            return
        
        # Mock mode: return success for all POST endpoints
        if path.startswith('/api/'):
            self.send_json({"success": True, "message": "Operation completed"})
        else:
            self.send_response(404)
            self.end_headers()
    
    def do_PUT(self):
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        if PROXY_TARGET and path.startswith('/api/'):
            length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(length) if length > 0 else None
            self.proxy_request('PUT', body)
            return
        
        if path.startswith('/api/'):
            self.send_json({"success": True, "message": "Configuration updated"})
        else:
            self.send_response(404)
            self.end_headers()
    
    def do_PATCH(self):
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        if PROXY_TARGET and path.startswith('/api/'):
            length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(length) if length > 0 else None
            self.proxy_request('PATCH', body)
            return
        
        if path.startswith('/api/'):
            self.send_json({"success": True, "message": "Configuration updated"})
        else:
            self.send_response(404)
            self.end_headers()
    
    def do_DELETE(self):
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        if PROXY_TARGET and path.startswith('/api/'):
            self.proxy_request('DELETE')
            return
        
        if path.startswith('/api/'):
            self.send_json({"success": True, "message": "Deleted"})
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
            self.send_response(HTTPStatus.OK)
            self.send_header('Content-Type', 'text/event-stream')
            self.send_header('Cache-Control', 'no-cache')
            self.send_header('Connection', 'keep-alive')
            self.end_headers()
            clients = getattr(self.server, 'sse_clients', None)
            if clients is None:
                clients = []
                setattr(self.server, 'sse_clients', clients)
            clients.append(self.wfile)
            try:
                while True:
                    time.sleep(1)
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
        
        # Proxy mode: forward API requests to real device
        if PROXY_TARGET and path.startswith('/api/'):
            self.proxy_request('GET')
            return
        
        # Mock mode: load from JSON files
        if path.startswith('/api/'):
            # Handle file requests (/api/v1/files/...)
            if path.startswith('/api/v1/files/'):
                file_path = unquote(path.replace('/api/v1/files/', ''))
                local_path = os.path.join(LITTLE_FS_DIR, file_path)
                
                if os.path.isfile(local_path):
                    # Determine content type
                    if file_path.endswith('.gz'):
                        content_type = 'application/gzip'
                    elif file_path.endswith('.csv'):
                        content_type = 'text/csv'
                    elif file_path.endswith('.txt'):
                        content_type = 'text/plain'
                    else:
                        content_type = 'application/octet-stream'
                    
                    try:
                        with open(local_path, 'rb') as f:
                            content = f.read()
                        self.send_response(200)
                        self.send_header('Content-Type', content_type)
                        self.send_header('Content-Length', str(len(content)))
                        self.end_headers()
                        self.wfile.write(content)
                        return
                    except Exception as e:
                        print(f"Error serving file {local_path}: {e}")
                        self.send_json({"error": f"Failed to read file: {e}"}, 500)
                        return
                else:
                    self.send_json({
                        "error": "File not found",
                        "message": f"File not found: {file_path}",
                        "hint": "Run with --fetch to download data files"
                    }, 404)
                    return
            
            # Handle list-files with dynamic folder filtering
            if path == '/api/v1/list-files':
                folder = None
                if '?' in self.path:
                    query = self.path.split('?', 1)[1]
                    for param in query.split('&'):
                        if param.startswith('folder='):
                            folder = unquote(param.split('=', 1)[1])
                            break
                
                files = list_little_fs_files(folder)
                self.send_json(files)
                return
            
            # Try to load mock response
            mock_data = load_mock_response(path)
            
            if mock_data is not None:
                # Handle text responses (wrapped in {"_text": ...})
                if isinstance(mock_data, dict) and "_text" in mock_data:
                    self.send_text(mock_data["_text"])
                else:
                    self.send_json(mock_data)
                return
            
            # No mock file found - return 404 with helpful message
            self.send_json({
                "error": "No mock data",
                "message": f"Create mock file: mocks{path}.json",
                "hint": "Run with --fetch to capture real data"
            }, 404)
            return
        
        # HTML page routing
        if path == '/' or path == '/index':
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
        elif path == '/waveform':
            self.serve_html_file('html/waveform.html')
        elif path == '/swagger-ui':
            self.serve_html_file('html/swagger.html')
        elif path == '/ade7953-tester':
            self.serve_html_file('html/ade7953-tester.html')
        else:
            # Serve static files (CSS, JS, images)
            super().do_GET()
    
    def serve_html_file(self, filename):
        """Serve an HTML file from the current directory."""
        try:
            with open(filename, 'r', encoding='utf-8') as f:
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
            for dirpath, _, filenames in os.walk(root):
                for f in filenames:
                    if f.endswith(('.html', '.js', '.css', '.json')):
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


def main():
    global PROXY_TARGET, PROXY_PASSWORD, PORT
    
    parser = argparse.ArgumentParser(
        description='EnergyMe development server',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python mock_server.py                              # Mock mode (uses mocks/ folder)
  python mock_server.py --proxy 192.168.2.75 pass   # Proxy to real device
  python mock_server.py --fetch 192.168.2.75 pass   # Fetch data from device to mocks/
        """
    )
    parser.add_argument('--proxy', nargs=2, metavar=('HOST', 'PASSWORD'),
                        help='Proxy API requests to real device')
    parser.add_argument('--fetch', nargs=2, metavar=('HOST', 'PASSWORD'),
                        help='Fetch all GET endpoint data from device and save to mocks/')
    parser.add_argument('--port', type=int, default=PORT,
                        help=f'Server port (default: {PORT})')
    args = parser.parse_args()
    
    # Fetch mode: capture data and exit
    if args.fetch:
        fetch_from_device(args.fetch[0], args.fetch[1])
        return
    
    # Set proxy mode
    if args.proxy:
        PROXY_TARGET = args.proxy[0]
        PROXY_PASSWORD = args.proxy[1]
    
    PORT = args.port
    
    print(f"Starting server on http://localhost:{PORT}")
    if PROXY_TARGET:
        print(f"PROXY MODE: API requests → http://{PROXY_TARGET}")
    else:
        print(f"MOCK MODE: Loading responses from {MOCKS_DIR}")
    print("Live-reload enabled (watches .html, .js, .css, .json files)")
    print("Press Ctrl+C to stop\n")
    
    with socketserver.ThreadingTCPServer(("", PORT), MockHandler) as httpd:
        watcher = FileWatcher(httpd, ['.', MOCKS_DIR], interval=0.5)
        watcher.start()
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped")


if __name__ == '__main__':
    main()
