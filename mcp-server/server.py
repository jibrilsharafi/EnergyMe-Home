#!/usr/bin/env python3
"""
EnergyMe-Home MCP Server

Provides access to project information, documentation, and device API.
"""

import os
import json
from pathlib import Path
from typing import Any, Optional
import yaml
import requests
from requests.auth import HTTPDigestAuth
from mcp.server import Server
from mcp.types import Tool, TextContent
import mcp.server.stdio

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent.resolve()
SWAGGER_PATH = PROJECT_ROOT / "source" / "resources" / "swagger.yaml"
API_USERNAME = os.getenv("ENERGYME_USERNAME", "")
API_PASSWORD = os.getenv("ENERGYME_PASSWORD", "")


def get_base_url() -> str:
    """Read and normalize ENERGYME_BASE_URL from environment.

    Ensures a URL scheme (http://) is present so callers don't need to.
    """
    raw = os.getenv("ENERGYME_BASE_URL", "http://192.168.2.75")
    raw = raw.strip()
    if not raw:
        return "http://192.168.2.75"
    if raw.startswith("http://") or raw.startswith("https://"):
        return raw
    # If user provided an IP or host without scheme, default to http
    return "http://" + raw

# Initialize MCP server
app = Server("energyme-home-mcp")


# ============================================================================
# PRIORITY 1: Static Project Information
# ============================================================================

@app.list_tools()
async def list_tools() -> list[Tool]:
    """List all available tools."""
    return [
        Tool(
            name="get_project_info",
            description="Get basic information about the EnergyMe-Home project including name, description, and version",
            inputSchema={
                "type": "object",
                "properties": {},
            },
        ),
        Tool(
            name="read_documentation",
            description="Read documentation files from the project. Provide a relative path from the documentation folder (e.g., 'README.md', 'Components/Energy IC/README.md')",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {
                        "type": "string",
                        "description": "Relative path to the documentation file",
                    }
                },
                "required": ["path"],
            },
        ),
        Tool(
            name="list_files",
            description="List files in a specific directory of the project. Provide a relative path from project root (e.g., 'documentation', 'source/include')",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {
                        "type": "string",
                        "description": "Relative path to the directory (empty string for root)",
                        "default": "",
                    },
                    "recursive": {
                        "type": "boolean",
                        "description": "Whether to list files recursively",
                        "default": False,
                    },
                },
            },
        ),
        # Priority 2: API Information
        Tool(
            name="get_swagger_spec",
            description="Get the full OpenAPI/Swagger specification for the EnergyMe-Home API",
            inputSchema={
                "type": "object",
                "properties": {},
            },
        ),
        Tool(
            name="list_api_endpoints",
            description="List all available API endpoints with their methods and descriptions",
            inputSchema={
                "type": "object",
                "properties": {
                    "tag": {
                        "type": "string",
                        "description": "Optional: filter by tag (e.g., 'System', 'ADE7953', 'MQTT')",
                    }
                },
            },
        ),
        Tool(
            name="get_endpoint_info",
            description="Get detailed information about a specific API endpoint including parameters, request body, and responses",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {
                        "type": "string",
                        "description": "API endpoint path (e.g., '/api/v1/system/info')",
                    },
                    "method": {
                        "type": "string",
                        "description": "HTTP method (e.g., 'get', 'post', 'put')",
                        "enum": ["get", "post", "put", "patch", "delete"],
                    },
                },
                "required": ["path", "method"],
            },
        ),
        # Priority 3: Dynamic Information
        Tool(
            name="check_health",
            description="Check if the EnergyMe-Home device is online and responsive",
            inputSchema={
                "type": "object",
                "properties": {},
            },
        ),
        Tool(
            name="get_system_info",
            description="Get detailed system information from the device including firmware version, hardware details, and uptime",
            inputSchema={
                "type": "object",
                "properties": {},
            },
        ),
        Tool(
            name="get_logs",
            description="Retrieve logs from the device",
            inputSchema={
                "type": "object",
                "properties": {},
            },
        ),
        # Diagnostics
        Tool(
            name="get_server_config",
            description="Get basic MCP server configuration (base URL and whether credentials are set). Passwords are never returned.",
            inputSchema={
                "type": "object",
                "properties": {},
            },
        ),
        Tool(
            name="auth_status",
            description="Check authentication status on the device (calls /api/v1/auth/status)",
            inputSchema={
                "type": "object",
                "properties": {},
            },
        ),
    ]


@app.call_tool()
async def call_tool(name: str, arguments: Any) -> list[TextContent]:
    """Handle tool calls."""
    
    # Priority 1: Static Project Information
    if name == "get_project_info":
        return [TextContent(type="text", text=get_project_info())]
    
    elif name == "read_documentation":
        path = arguments.get("path", "")
        return [TextContent(type="text", text=read_documentation(path))]
    
    elif name == "list_files":
        path = arguments.get("path", "")
        recursive = arguments.get("recursive", False)
        return [TextContent(type="text", text=list_files(path, recursive))]
    
    # Priority 2: API Information
    elif name == "get_swagger_spec":
        return [TextContent(type="text", text=get_swagger_spec())]
    
    elif name == "list_api_endpoints":
        tag = arguments.get("tag")
        return [TextContent(type="text", text=list_api_endpoints(tag))]
    
    elif name == "get_endpoint_info":
        path = arguments["path"]
        method = arguments["method"]
        return [TextContent(type="text", text=get_endpoint_info(path, method))]
    
    # Priority 3: Dynamic Information
    elif name == "check_health":
        return [TextContent(type="text", text=check_health())]
    
    elif name == "get_system_info":
        return [TextContent(type="text", text=get_system_info())]
    
    elif name == "get_logs":
        return [TextContent(type="text", text=get_logs())]
    
    # Diagnostics
    elif name == "get_server_config":
        return [TextContent(type="text", text=get_server_config())]
    
    elif name == "auth_status":
        return [TextContent(type="text", text=auth_status())]
    
    else:
        raise ValueError(f"Unknown tool: {name}")


# ============================================================================
# Implementation Functions - Priority 1: Static Project Information
# ============================================================================

def get_project_info() -> str:
    """Get basic project information."""
    try:
        # Read main README
        readme_path = PROJECT_ROOT / "README.md"
        readme_content = ""
        if readme_path.exists():
            with open(readme_path, 'r', encoding='utf-8') as f:
                lines = f.readlines()
                # Get first 10 lines for description
                readme_content = ''.join(lines[:10])
        
        # Read platformio.ini for version info
        platformio_path = PROJECT_ROOT / "source" / "platformio.ini"
        version = "Unknown"
        if platformio_path.exists():
            with open(platformio_path, 'r', encoding='utf-8') as f:
                content = f.read()
                # Try to extract version
                for line in content.split('\n'):
                    if 'VERSION' in line or 'version' in line:
                        version = line.strip()
                        break
        
        info = {
            "name": "EnergyMe-Home",
            "description": "ESP32-based energy monitoring system with ADE7953 energy meter",
            "readme_excerpt": readme_content,
            "version_info": version,
            "project_root": str(PROJECT_ROOT),
        }
        
        return json.dumps(info, indent=2)
    
    except Exception as e:
        return f"Error getting project info: {str(e)}"


def read_documentation(path: str) -> str:
    """Read a documentation file."""
    try:
        # Handle both absolute paths from doc root and paths starting with "documentation/"
        if path.startswith("documentation/"):
            file_path = PROJECT_ROOT / path
        else:
            # Try documentation folder first
            file_path = PROJECT_ROOT / "documentation" / path
            if not file_path.exists():
                # Try project root
                file_path = PROJECT_ROOT / path
        
        if not file_path.exists():
            return f"Error: File not found: {path}"
        
        if not file_path.is_file():
            return f"Error: Path is not a file: {path}"
        
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        return content
    
    except Exception as e:
        return f"Error reading documentation: {str(e)}"


def list_files(path: str, recursive: bool = False) -> str:
    """List files in a directory."""
    try:
        dir_path = PROJECT_ROOT / path if path else PROJECT_ROOT
        
        if not dir_path.exists():
            return f"Error: Directory not found: {path}"
        
        if not dir_path.is_dir():
            return f"Error: Path is not a directory: {path}"
        
        files = []
        
        if recursive:
            for item in dir_path.rglob('*'):
                if item.is_file():
                    rel_path = item.relative_to(PROJECT_ROOT)
                    files.append(str(rel_path))
        else:
            for item in dir_path.iterdir():
                rel_path = item.relative_to(PROJECT_ROOT)
                marker = "/" if item.is_dir() else ""
                files.append(f"{rel_path}{marker}")
        
        result = {
            "path": path if path else ".",
            "count": len(files),
            "files": sorted(files),
        }
        
        return json.dumps(result, indent=2)
    
    except Exception as e:
        return f"Error listing files: {str(e)}"


# ============================================================================
# Implementation Functions - Priority 2: API Information
# ============================================================================

def load_swagger() -> Optional[dict]:
    """Load and parse the swagger.yaml file."""
    try:
        with open(SWAGGER_PATH, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)
    except Exception as e:
        return None


def get_swagger_spec() -> str:
    """Get the full Swagger specification."""
    try:
        swagger = load_swagger()
        if swagger is None:
            return "Error: Could not load swagger.yaml"
        
        return json.dumps(swagger, indent=2)
    
    except Exception as e:
        return f"Error getting swagger spec: {str(e)}"


def list_api_endpoints(tag: Optional[str] = None) -> str:
    """List all API endpoints."""
    try:
        swagger = load_swagger()
        if swagger is None:
            return "Error: Could not load swagger.yaml"
        
        paths = swagger.get('paths', {})
        endpoints = []
        
        for path, methods in paths.items():
            for method, details in methods.items():
                if method in ['get', 'post', 'put', 'patch', 'delete']:
                    endpoint_tags = details.get('tags', [])
                    
                    # Filter by tag if provided
                    if tag and tag not in endpoint_tags:
                        continue
                    
                    endpoints.append({
                        "path": path,
                        "method": method.upper(),
                        "summary": details.get('summary', ''),
                        "description": details.get('description', ''),
                        "tags": endpoint_tags,
                    })
        
        result = {
            "count": len(endpoints),
            "filter": f"tag={tag}" if tag else "none",
            "endpoints": endpoints,
        }
        
        return json.dumps(result, indent=2)
    
    except Exception as e:
        return f"Error listing endpoints: {str(e)}"


def get_endpoint_info(path: str, method: str) -> str:
    """Get detailed information about a specific endpoint."""
    try:
        swagger = load_swagger()
        if swagger is None:
            return "Error: Could not load swagger.yaml"
        
        paths = swagger.get('paths', {})
        
        if path not in paths:
            return f"Error: Endpoint not found: {path}"
        
        method_lower = method.lower()
        if method_lower not in paths[path]:
            return f"Error: Method {method.upper()} not found for endpoint: {path}"
        
        endpoint = paths[path][method_lower]
        
        info = {
            "path": path,
            "method": method.upper(),
            "summary": endpoint.get('summary', ''),
            "description": endpoint.get('description', ''),
            "tags": endpoint.get('tags', []),
            "parameters": endpoint.get('parameters', []),
            "requestBody": endpoint.get('requestBody', {}),
            "responses": endpoint.get('responses', {}),
        }
        
        return json.dumps(info, indent=2)
    
    except Exception as e:
        return f"Error getting endpoint info: {str(e)}"


# ============================================================================
# Implementation Functions - Priority 3: Dynamic Information
# ============================================================================

def get_auth() -> Optional[HTTPDigestAuth]:
    """Get authentication credentials if configured."""
    if API_USERNAME and API_PASSWORD:
        return HTTPDigestAuth(API_USERNAME, API_PASSWORD)
    return None


def check_health() -> str:
    """Check device health."""
    try:
        base = get_base_url()
        response = requests.get(f"{base}/api/v1/health", timeout=5, auth=get_auth())
        
        result = {
            "online": response.status_code == 200,
            "status_code": response.status_code,
            "base_url": base,
        }
        
        return json.dumps(result, indent=2)
    
    except requests.exceptions.RequestException as e:
        result = {
            "online": False,
            "error": str(e),
            "base_url": get_base_url(),
        }
        return json.dumps(result, indent=2)


def get_system_info() -> str:
    """Get system information from device."""
    try:
        response = requests.get(f"{get_base_url()}/api/v1/system/info", timeout=5, auth=get_auth())

        if response.status_code == 200:
            return json.dumps(response.json(), indent=2)
        else:
            return f"Error: HTTP {response.status_code}"

    except requests.exceptions.RequestException as e:
        return f"Error: {str(e)}"


def get_logs() -> str:
    """Get logs from device."""
    try:
        response = requests.get(f"{get_base_url()}/api/v1/logs", timeout=10, auth=get_auth())

        if response.status_code != 200:
            return f"Error: HTTP {response.status_code}"

        content_type = response.headers.get('content-type', '')
        # If the server returns JSON, pretty-print it. Otherwise return raw text.
        if 'application/json' in content_type.lower():
            try:
                return json.dumps(response.json(), indent=2)
            except ValueError:
                # Malformed JSON — fall back to raw text
                return response.text
        else:
            # Logs are typically plain text — return as-is
            return response.text

    except requests.exceptions.RequestException as e:
        return f"Error: {str(e)}"


# ============================================================================
# Diagnostics helpers
# ============================================================================

def get_server_config() -> str:
    """Return basic server configuration state for troubleshooting."""
    try:
        info = {
            "base_url": get_base_url(),
            "has_username": bool(API_USERNAME),
            "has_password": bool(API_PASSWORD),
            "project_root": str(PROJECT_ROOT),
            "swagger_path": str(SWAGGER_PATH),
        }
        return json.dumps(info, indent=2)
    except Exception as e:
        return f"Error: {str(e)}"


def auth_status() -> str:
    """Check authentication status endpoint on device."""
    try:
        response = requests.get(f"{get_base_url()}/api/v1/auth/status", timeout=5, auth=get_auth())

        if response.headers.get('content-type', '').startswith('application/json'):
            body = response.json()
        else:
            body = response.text
        result = {
            "status_code": response.status_code,
            "body": body,
        }
        return json.dumps(result, indent=2)

    except requests.exceptions.RequestException as e:
        return f"Error: {str(e)}"


# ============================================================================
# Main Entry Point
# ============================================================================

async def main():
    """Run the MCP server."""
    async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
        await app.run(
            read_stream,
            write_stream,
            app.create_initialization_options()
        )


if __name__ == "__main__":
    import asyncio
    asyncio.run(main())
