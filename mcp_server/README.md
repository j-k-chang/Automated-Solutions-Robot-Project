# Model Context Protocol (MCP) Server Component Overview

This document details the files and functionalities implemented for the Mission Control Program (MCP) Server running on the Raspberry Pi. To fulfill the goals of native integration with the **Claude CLI**, this system has been upgraded to act as an official Anthropic **Model Context Protocol (MCP)** server, abstracting the complexity of multi-pump control and exposing tools directly to the AI agent over stdio.

## 📁 Directory Structure
*   `mcp_server/`: Contains all Python source code and configuration for the server application.
    *   `main_api.py`
    *   `models.py`
    *   `recipes.json`
    *   `requirements.txt`

## 📄 File Functionalities Detail

### `mcp_server/requirements.txt`
**Functionality:** Lists all required Python packages for the server to run.
**Dependencies:** `mcp` (the official Anthropic Python SDK), `pydantic` (>=2.0 for robust data validation and Tool schema generation).

### `mcp_server/models.py`
**Functionality:** Defines the strict, typed data contract (`pydantic.BaseModel`) used throughout the system. The MCP SDK uses these models to enforce schema compliance for tool calls, generating perfect descriptions for Claude to use.

### `mcp_server/recipes.json`
**Functionality:** Persistent storage for known chemical recipes. This JSON file dictates which sequence of steps (defined by pump IDs and chemicals) must be executed when a user requests a named recipe. Note: This file must be strict standard JSON.

### `mcp_server/main_api.py`
**Functionality:** The primary entry point for the MCP Server, written using Anthropic's `FastMCP`.
*   **Initialization:** Loads initial state and recipes upon startup.
*   **`get_system_status` Tool:** Exposes the aggregated state of all connected pumps.
*   **`dispense_direct` Tool:** Executes a direct dispense command. It operates **asynchronously** and simulates the physical pumping process over time. Crucially, it uses `ctx.info()` to emit intermediate progress logs. Claude CLI surfaces these logs as a **Live Feed** to the user while the hardware is running.
*   **`dispense_recipe` Tool:** Executes a sequence of physical dispenses based on a known recipe name from `recipes.json`, calling `dispense_direct` sequentially.
*   **Completion Signal:** Because the tools block the agent's turn until physical operations finish, the final return string acts as the completion signal.

## 🚀 Claude CLI Integration & Execution Flow
Because this is now a native MCP Server, you no longer need an intermediary script to translate HTTP POSTs to Claude. 

To use this with Claude CLI or Claude Desktop, add it to your configuration (usually `claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "LiquidDispenser": {
      "command": "python",
      "args": ["-m", "mcp_server.main_api"],
      "cwd": "/path/to/your/project/root"
    }
  }
}
```

1.  **Prompt:** The user tells Claude: "Dispense the Neutralization Test Mix."
2.  **Tool Discovery:** Claude already knows the `dispense_recipe` tool exists and the required parameters because of the FastMCP integration.
3.  **Execution & Live Feed:** Claude calls the tool. The tool runs its physical simulation loop and sends `ctx.info()` messages over the stdio transport. Claude CLI surfaces this real-time feed.
4.  **Completion:** The tool finishes the loop, returning "Success" to Claude. Claude then summarizes the final state back to the user.