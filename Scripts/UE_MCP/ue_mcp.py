"""Minimal stdio/CLI client for the Unreal Engine 5.8 built-in MCP server.

The editor exposes an MCP (Model Context Protocol) HTTP endpoint when the
``ModelContextProtocol`` plugin is enabled:

    Config/DefaultEditorPerProjectUserSettings.ini
    [/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]
    bAutoStartServer = True
    ServerPortNumber = 8000
    ServerUrlPath = /mcp

or, at runtime, via the console command ``ModelContextProtocol.StartServer [port]``
or the command line ``-ModelContextProtocolStartServer -ModelContextProtocolPort=8000``.

No third-party dependency: this file only needs a Python 3.9+ interpreter.

Usage
-----
    python Scripts/UE_MCP/ue_mcp.py ping
    python Scripts/UE_MCP/ue_mcp.py toolsets
    python Scripts/UE_MCP/ue_mcp.py describe editor_toolset.toolsets.data_asset.DataAssetTools
    python Scripts/UE_MCP/ue_mcp.py call get_asset --toolset editor_toolset.toolsets.data_asset.DataAssetTools \
        --json '{"asset_path": "/Game/Extraction/Weapons/SCARH/DA_SCARH_Presentation"}'
    python Scripts/UE_MCP/ue_mcp.py call list_toolsets
    python Scripts/UE_MCP/ue_mcp.py raw tools/list

Exit code is 0 on success, 1 when the server is unreachable or the tool reports
an error, so callers can chain it with ``&&``.
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import urllib.error
import urllib.request

DEFAULT_URL = os.environ.get("UE_MCP_URL", "http://127.0.0.1:8000/mcp")
PROTOCOL_VERSION = "2025-06-18"
CLIENT_INFO = {"name": "workbuddy-ue-mcp", "version": "1.0"}


class McpError(RuntimeError):
    pass


class McpClient:
    """JSON-RPC 2.0 over the MCP streamable-HTTP transport used by Unreal."""

    def __init__(self, url: str = DEFAULT_URL, timeout: float = 120.0):
        self.url = url
        self.timeout = timeout
        self.session_id: str | None = None
        self._next_id = 0

    # -- transport ---------------------------------------------------------
    def _post(self, payload: dict) -> dict | None:
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        request = urllib.request.Request(
            self.url, data=json.dumps(payload).encode("utf-8"), headers=headers, method="POST"
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                session = response.headers.get("Mcp-Session-Id")
                if session:
                    self.session_id = session
                body = response.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as error:
            detail = error.read().decode("utf-8", "replace")
            raise McpError(f"HTTP {error.code} from {self.url}: {detail[:2000]}") from None
        except urllib.error.URLError as error:
            raise McpError(
                f"Cannot reach the Unreal MCP server at {self.url} ({error.reason}). "
                "Start the editor with -ModelContextProtocolStartServer, or run the console "
                "command ModelContextProtocol.StartServer."
            ) from None
        if not body.strip():
            return None
        return json.loads(_unwrap_sse(body))

    # -- protocol ----------------------------------------------------------
    def request(self, method: str, params: dict | None = None) -> dict:
        self._next_id += 1
        payload = {"jsonrpc": "2.0", "id": self._next_id, "method": method}
        if params is not None:
            payload["params"] = params
        response = self._post(payload)
        if response is None:
            raise McpError(f"Empty response for {method}")
        if "error" in response:
            error = response["error"]
            raise McpError(f"{method} failed: {error.get('code')} {error.get('message')}")
        return response["result"]

    def notify(self, method: str, params: dict | None = None) -> None:
        payload = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            payload["params"] = params
        self._post(payload)

    def handshake(self) -> dict:
        result = self.request(
            "initialize",
            {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {}},
                "clientInfo": CLIENT_INFO,
            },
        )
        self.notify("notifications/initialized")
        return result

    # -- convenience -------------------------------------------------------
    def call(self, tool: str, arguments: dict | None = None, toolset: str | None = None) -> tuple[str, bool]:
        """Invoke a tool, transparently routing through call_tool when needed."""
        arguments = arguments or {}
        if toolset:
            result = self.request(
                "tools/call",
                {
                    "name": "call_tool",
                    "arguments": {"toolset_name": toolset, "tool_name": tool, "arguments": arguments},
                },
            )
        else:
            result = self.request(
                "tools/call", {"name": tool, "arguments": arguments}
            )
        return _flatten(result)


def _unwrap_sse(body: str) -> str:
    """Return the JSON payload of a server-sent-events response."""
    if not body.lstrip().startswith(("event:", "data:", "id:", ":")):
        return body
    chunks = [line[5:].strip() for line in body.splitlines() if line.startswith("data:")]
    return chunks[-1] if chunks else body


def _flatten(result: dict) -> tuple[str, bool]:
    is_error = bool(result.get("isError"))
    parts: list[str] = []
    for item in result.get("content") or []:
        if item.get("type") == "text":
            parts.append(item.get("text", ""))
        else:
            parts.append(json.dumps(item, ensure_ascii=False))
    if not parts and "structuredContent" in result:
        parts.append(json.dumps(result["structuredContent"], ensure_ascii=False))
    return "\n".join(parts).strip(), is_error


def _load_arguments(raw: str | None, path: str | None) -> dict:
    if path:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    if raw:
        return json.loads(raw)
    return {}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Unreal Engine MCP client")
    parser.add_argument("--url", default=DEFAULT_URL, help=f"MCP endpoint (default {DEFAULT_URL})")
    parser.add_argument("--timeout", type=float, default=120.0)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("ping", help="check connectivity and print server info")
    sub.add_parser("toolsets", help="list registered toolsets")
    describe = sub.add_parser("describe", help="describe one toolset and its input schemas")
    describe.add_argument("toolset")

    call = sub.add_parser("call", help="invoke a tool")
    call.add_argument("tool")
    call.add_argument("--toolset", help="toolset name; omit for top-level MCP tools")
    call.add_argument("--json", dest="payload", help="inline JSON arguments")
    call.add_argument("--file", help="path to a JSON file with arguments")

    raw = sub.add_parser("raw", help="send a raw JSON-RPC method")
    raw.add_argument("method")
    raw.add_argument("params", nargs="?")

    args = parser.parse_args(argv)
    client = McpClient(args.url, args.timeout)
    try:
        info = client.handshake()
        if args.command == "ping":
            tools = client.request("tools/list").get("tools", [])
            print(json.dumps({
                "endpoint": args.url,
                "session": client.session_id,
                "protocol": info.get("protocolVersion"),
                "capabilities": info.get("capabilities"),
                "tool_count": len(tools),
                "tools": [tool["name"] for tool in tools],
            }, ensure_ascii=False, indent=2))
            return 0
        if args.command == "toolsets":
            text, failed = client.call("list_toolsets")
            print(text)
            return 1 if failed else 0
        if args.command == "describe":
            text, failed = client.call("describe_toolset", {"toolset_name": args.toolset})
            print(text)
            return 1 if failed else 0
        if args.command == "call":
            text, failed = client.call(args.tool, _load_arguments(args.payload, args.file), args.toolset)
            print(text)
            return 1 if failed else 0
        if args.command == "raw":
            params = json.loads(args.params) if args.params else None
            print(json.dumps(client.request(args.method, params), ensure_ascii=False, indent=2))
            return 0
    except McpError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except socket.timeout:
        print("error: timed out talking to the editor", file=sys.stderr)
        return 1
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
