# EnergyMe-Home MCP Server

An MCP (Model Context Protocol) server that provides access to EnergyMe-Home project information and API endpoints.

## Features

- **Static Project Information**: Read project structure, documentation, and metadata
- **API Information**: Parse and serve Swagger/OpenAPI specifications
- **Dynamic Information**: Query device status, health, logs, and system info

## Installation

```bash
cd mcp-server
pip install -e .
```

## Usage

### Running the server

```bash
python server.py
```

### Configuration

Set the device base URL (default: http://192.168.2.75):

```bash
export ENERGYME_BASE_URL=http://your-device-ip
```

## Available Tools

- `get_project_info` - Get basic project information
- `read_documentation` - Read documentation files
- `list_files` - List project files
- `get_swagger_spec` - Get the full OpenAPI specification
- `list_api_endpoints` - List all available API endpoints
- `get_endpoint_info` - Get detailed info about a specific endpoint
- `check_health` - Check device health status
- `get_system_info` - Get device system information
- `get_logs` - Retrieve device logs
