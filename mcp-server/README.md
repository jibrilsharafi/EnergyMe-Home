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

### Environment Variables

- `ENERGYME_BASE_URL` - Device IP address (default: `http://192.168.2.75`)
- `ENERGYME_USERNAME` - API username (optional, for authenticated endpoints)
- `ENERGYME_PASSWORD` - API password (optional, for authenticated endpoints)

### VS Code / Claude Desktop Configuration

Add this to your MCP settings file:

**Windows:** `%APPDATA%\Code\User\globalStorage\saoudrizwan.claude-dev\settings\cline_mcp_settings.json`

**Mac/Linux:** `~/.config/Code/User/globalStorage/saoudrizwan.claude-dev/settings/cline_mcp_settings.json`

```json
{
  "mcpServers": {
    "energyme-home": {
      "command": "uv",
      "args": [
        "--directory",
        "C:\\Users\\jibri\\Documents\\GitHub\\EnergyMe-Home\\mcp-server",
        "run",
        "server.py"
      ],
      "env": {
        "ENERGYME_BASE_URL": "http://192.168.2.75",
        "ENERGYME_USERNAME": "your-username",
        "ENERGYME_PASSWORD": "your-password"
      }
    }
  }
}
```

**Note:** Adjust the path in `--directory` to match your installation location.

### Testing the Server

You can test individual functions using the MCP inspector:

```bash
npx @modelcontextprotocol/inspector uv --directory . run server.py
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
