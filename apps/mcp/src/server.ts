import dotenv from "dotenv";
import { fileURLToPath } from "node:url";
import { createServer } from "node:http";
import { McpServer, createMcpHandler } from "@modelcontextprotocol/server";
import { serveStdio } from "@modelcontextprotocol/server/stdio";
import { toNodeHandler, localhostHostValidation } from "@modelcontextprotocol/node";
import { originValidationResponse, localhostAllowedOrigins } from "@modelcontextprotocol/server";
import * as z from "zod/v4";
import { DeviceClient, type MouseButton, type QueueAction } from "@esp32-hid/device-client";

dotenv.config({ path: fileURLToPath(new URL("../../../.env", import.meta.url)) });

function device() {
  return new DeviceClient({
    baseUrl: process.env.ESP32_API_URL ?? "http://192.168.4.1",
    token: process.env.ESP32_API_SECRET ?? "",
    timeoutMs: Number(process.env.ESP32_API_TIMEOUT_MS ?? 8000)
  });
}

function createMcp() {
  const server = new McpServer({ name: "esp32-hid", version: "0.1.0" });

  server.registerTool("device_status", {
    description: "Read ESP32 BLE keyboard, mouse, Wi-Fi and queue state.",
    inputSchema: z.object({})
  }, async () => {
    const result = await device().status();
    return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
  });

  server.registerTool("keyboard_type", {
    description: "Type explicitly supplied text through the ESP32 BLE keyboard.",
    inputSchema: z.object({
      text: z.string().min(1).max(96),
      delay_ms: z.number().int().min(0).max(1000).optional(),
      instant: z.boolean().optional()
    })
  }, async ({ text, delay_ms, instant }) => {
    const result = await device().typeText(text, { delay_ms, instant });
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("keyboard_press", {
    description: "Tap a supported HID key such as ENTER, ESC or F5.",
    inputSchema: z.object({ key: z.string().min(1).max(16) })
  }, async ({ key }) => {
    const result = await device().pressKey(key);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("keyboard_hotkey", {
    description: "Send a keyboard chord such as CTRL+SHIFT+ESC.",
    inputSchema: z.object({ combination: z.string().min(1).max(48) })
  }, async ({ combination }) => {
    const result = await device().hotkey(combination);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("mouse_move", {
    description: "Move the pointer by bounded relative deltas.",
    inputSchema: z.object({
      dx: z.number().int().min(-100).max(100),
      dy: z.number().int().min(-100).max(100),
      wheel: z.number().int().min(-100).max(100).optional()
    })
  }, async ({ dx, dy, wheel }) => {
    const result = await device().mouseMove(dx, dy, wheel ?? 0);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("mouse_click", {
    description: "Click a mouse button on the paired host.",
    inputSchema: z.object({ button: z.enum(["left","right","middle","back","forward"]).default("left") })
  }, async ({ button }) => {
    const result = await device().mouseClick(button as MouseButton);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("mouse_wheel", {
    description: "Scroll the mouse wheel with a bounded delta.",
    inputSchema: z.object({ delta: z.number().int().min(-100).max(100) })
  }, async ({ delta }) => {
    const result = await device().mouseWheel(delta);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("queue_control", {
    description: "Pause, resume or stop the HID command queue. Stop clears and releases all controls.",
    inputSchema: z.object({ action: z.enum(["pause","resume","stop"]) })
  }, async ({ action }) => {
    const result = await device().queue(action as QueueAction);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("device_mode", {
    description: "Set the firmware operating mode.",
    inputSchema: z.object({ mode: z.enum(["NORMAL","TYPING","INSTANT","SEQUENCE","RANDOMIZED","QUIET","DIAGNOSTIC"]) })
  }, async ({ mode }) => {
    const result = await device().setMode(mode);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("bluetooth_advertise", {
    description: "Restart ESP32 BLE HID advertising. Pairing and reconnect approval remain host-OS controlled.",
    inputSchema: z.object({})
  }, async () => {
    const result = await device().restartAdvertising();
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("set_log_level", {
    description: "Set the ESP32 diagnostic log verbosity.",
    inputSchema: z.object({ level: z.enum(["NONE","ERROR","WARN","INFO","DEBUG","TRACE"]) })
  }, async ({ level }) => {
    const result = await device().setLogLevel(level);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("wifi_config", {
    description: "Configure the ESP32 station Wi-Fi credentials. Existing AP remains active.",
    inputSchema: z.object({ ssid: z.string().max(32), password: z.string().min(8).max(63) })
  }, async ({ ssid, password }) => {
    const result = await device().setWifi(ssid, password);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("telemetry_config", {
    description: "Configure an HTTPS telemetry endpoint and API key. TLS CA must be installed in firmware.",
    inputSchema: z.object({ endpoint: z.string().url().refine(value => value.startsWith("https://")), api_key: z.string().min(1), enabled: z.boolean().default(true) })
  }, async ({ endpoint, api_key, enabled }) => {
    const result = await device().setTelemetry(endpoint, api_key, enabled);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("device_logs", {
    description: "Read bounded ESP32 diagnostics; log messages omit typed text.",
    inputSchema: z.object({})
  }, async () => {
    const result = await device().logs();
    return { content: [{ type: "text", text: JSON.stringify(result, null, 2) }] };
  });

  server.registerTool("recording_start", {
    description: "Record actions explicitly sent through this ESP32 controller API or CLI.",
    inputSchema: z.object({ name: z.string().regex(/^[A-Za-z0-9_-]{1,14}$/) })
  }, async ({ name }) => {
    const result = await device().startRecording(name);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("recording_stop", {
    description: "Stop and save the current controller command macro in ESP32 NVS.",
    inputSchema: z.object({})
  }, async () => {
    const result = await device().stopRecording();
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("macro_list", {
    description: "List explicitly recorded controller macros.",
    inputSchema: z.object({})
  }, async () => {
    const result = await device().listMacros();
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("recording_status", {
    description: "Read the explicit controller macro recording state.",
    inputSchema: z.object({})
  }, async () => {
    const result = await device().recordingStatus();
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("macro_delete", {
    description: "Delete a saved controller macro.",
    inputSchema: z.object({ name: z.string().regex(/^[A-Za-z0-9_-]{1,14}$/) })
  }, async ({ name }) => {
    const result = await device().request("macros/delete", { method: "POST", body: { name } });
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  server.registerTool("macro_play", {
    description: "Queue a saved controller command macro for execution.",
    inputSchema: z.object({ name: z.string().regex(/^[A-Za-z0-9_-]{1,14}$/) })
  }, async ({ name }) => {
    const result = await device().playMacro(name);
    return { content: [{ type: "text", text: JSON.stringify(result) }] };
  });

  return server;
}

const transport = process.argv.find(value => value.startsWith("--transport="))?.split("=")[1]
  ?? process.env.MCP_TRANSPORT ?? "http";

if (transport === "stdio") {
  serveStdio(() => createMcp());
} else {
  const host = process.env.MCP_HOST ?? "127.0.0.1";
  const port = Number(process.env.MCP_PORT ?? 3333);
  const token = process.env.MCP_AUTH_TOKEN ?? "";
  if (!token || token.startsWith("replace-with-")) {
    throw new Error("Set MCP_AUTH_TOKEN in the root .env before starting MCP HTTP transport.");
  }
  const mcpHandler = createMcpHandler(() => createMcp());
  const nodeHandler = toNodeHandler(mcpHandler);
  const validateHost = localhostHostValidation();
  const server = createServer((request, response) => {
    if (request.url?.split("?")[0] !== "/mcp") {
      response.writeHead(404, { "content-type": "application/json" });
      response.end(JSON.stringify({ error: "not_found" }));
      return;
    }
    if (!validateHost(request, response)) return;
    const hostHeader = request.headers.host ?? host;
    const originHeaders = request.headers.origin
      ? { Origin: request.headers.origin }
      : {};
    const originRequest = new Request("http://" + hostHeader + "/", { headers: originHeaders });
    const rejectedOrigin = originValidationResponse(originRequest, localhostAllowedOrigins());
    if (rejectedOrigin) {
      response.writeHead(rejectedOrigin.status, Object.fromEntries(rejectedOrigin.headers.entries()));
      void rejectedOrigin.text().then(body => response.end(body));
      return;
    }
    if (request.headers.authorization !== "Bearer " + token) {
      response.writeHead(401, { "content-type": "application/json" });
      response.end(JSON.stringify({ error: "unauthorized" }));
      return;
    }
    void nodeHandler(request, response);
  });
  server.listen(port, host, () => {
    console.error("ESP32 HID MCP HTTP listening on http://" + host + ":" + port + "/mcp");
  });
}
