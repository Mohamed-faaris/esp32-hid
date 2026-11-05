#!/usr/bin/env node
import dotenv from "dotenv";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";

dotenv.config({ path: resolve(fileURLToPath(new URL("..", import.meta.url)), ".env") });

const baseUrl = (process.env.ESP32_API_URL || "http://192.168.4.1").replace(/\/$/, "");
const token = process.env.ESP32_API_SECRET || "";
const mode = process.argv[2] || "record";
const macroName = process.argv[3] || "hw-smoke";

if (!token) {
  console.error("Missing ESP32_API_SECRET in root .env.");
  process.exit(2);
}
if (!["record", "verify"].includes(mode)) {
  console.error("Usage: pnpm hardware:smoke [record|verify] [macro-name]");
  process.exit(2);
}

async function request(path, method = "GET", body) {
  const response = await fetch(baseUrl + "/api/" + path, {
    method,
    headers: {
      Authorization: "Bearer " + token,
      ...(body === undefined ? {} : { "Content-Type": "application/json" })
    },
    body: body === undefined ? undefined : JSON.stringify(body),
    signal: AbortSignal.timeout(8000),
    cache: "no-store"
  });
  const raw = await response.text();
  let value = {};
  try { value = raw ? JSON.parse(raw) : {}; }
  catch { throw new Error(path + " returned non-JSON HTTP " + response.status); }
  if (!response.ok) throw new Error(path + " returned HTTP " + response.status + ": " + JSON.stringify(value));
  return value;
}

async function waitForQueueToDrain(timeoutMs = 30000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const queue = await request("queue");
    if (!queue.paused && !queue.active && queue.depth === 0) return;
    await new Promise(resolve => setTimeout(resolve, 250));
  }
  throw new Error("Command queue did not drain before timeout.");
}

async function assertDeviceReady() {
  const status = await request("status");
  if (!status.ble_keyboard_ready || !status.ble_mouse_ready) {
    throw new Error("Both BLE interfaces must be paired and ready. " +
      JSON.stringify({ keyboard: status.ble_keyboard_ready, mouse: status.ble_mouse_ready }));
  }
  console.log("PASS: BLE keyboard and mouse are connected to a host.");
  return status;
}

async function queueTests() {
  await request("queue/pause", "POST");
  let queue = await request("queue");
  if (!queue.paused) throw new Error("Pause did not persist.");
  await request("keyboard/type", "POST", { type: "type", value: "QUEUE-PAUSE-RESUME", instant: true });
  queue = await request("queue");
  if (!queue.paused || queue.depth < 1) throw new Error("Paused queue did not preserve new commands.");
  await request("queue/resume", "POST");
  await waitForQueueToDrain();
  console.log("PASS: pause preserves queued commands; resume drains them.");

  await request("queue/pause", "POST");
  await request("keyboard/sleep", "POST", { type: "sleep", ms: 60000 });
  await request("queue/stop", "POST");
  await new Promise(resolve => setTimeout(resolve, 300));
  queue = await request("queue");
  if (queue.depth !== 0 || queue.active) throw new Error("Stop did not clear the queue.");
  await request("queue/resume", "POST");
  console.log("PASS: stop clears queued work; queue can resume.");
}

async function recordNvsMacro() {
  await request("recording/start", "POST", { name: macroName });
  await request("keyboard/type", "POST", { type: "type", value: "ESP32-HID-" + macroName, instant: true });
  await request("mouse/move", "POST", { dx: 18, dy: 12, wheel: 0 });
  const saved = await request("recording/stop", "POST");
  if (!saved.saved || saved.commands < 2) throw new Error("Macro did not save: " + JSON.stringify(saved));
  const macros = await request("macros");
  if (!macros.macros.includes(macroName)) throw new Error("Saved macro missing from macro index.");
  await waitForQueueToDrain();
  console.log("PASS: API macro saved into device NVS.");
  console.log("NEXT: Power-cycle the ESP32, reconnect BLE, then run: pnpm hardware:smoke -- verify " + macroName);
}

async function verifyNvsMacro() {
  const macros = await request("macros");
  if (!macros.macros.includes(macroName)) {
    throw new Error("Macro was not present after reboot. NVS persistence check failed.");
  }
  const played = await request("macros/play", "POST", { name: macroName });
  if (!played.queued) throw new Error("Saved macro did not replay: " + JSON.stringify(played));
  await waitForQueueToDrain();
  console.log("PASS: macro survived a power cycle and replayed.");
}

try {
  await assertDeviceReady();
  if (mode === "record") {
    await queueTests();
    await recordNvsMacro();
  } else {
    await verifyNvsMacro();
  }
  console.log("Hardware API checks completed. Confirm the visible keyboard text and mouse movement on the paired host.");
} catch (error) {
  console.error("FAIL:", error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}
