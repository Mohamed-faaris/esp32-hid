import { readFile, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import dotenv from "dotenv";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const values = dotenv.parse(await readFile(resolve(root, ".env")).catch(() => {
  throw new Error("Missing root .env. Copy .env.example to .env first.");
}));
for (const key of ["ESP32_API_SECRET", "ESP32_AP_PASSWORD"]) {
  if (!values[key] || values[key].startsWith("replace-with-")) {
    throw new Error("Set a strong value for " + key + " in the root .env.");
  }
}
function cppString(value = "") {
  return '"' + String(value).replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\r/g, "").replace(/\n/g, "\\n") + '"';
}
const lines = [
  "#pragma once",
  "// Generated from root .env. This file is gitignored.",
  "#define HID_API_SECRET " + cppString(values.ESP32_API_SECRET),
  "#define HID_AP_SSID " + cppString(values.ESP32_AP_SSID || "ESP32-HID"),
  "#define HID_AP_PASSWORD " + cppString(values.ESP32_AP_PASSWORD),
  "#define HID_STA_SSID " + cppString(values.ESP32_STA_SSID || ""),
  "#define HID_STA_PASSWORD " + cppString(values.ESP32_STA_PASSWORD || ""),
  "#define HID_TELEMETRY_ROOT_CA " + cppString(values.ESP32_TELEMETRY_ROOT_CA || ""),
  ""
];
const output = resolve(root, "firmware/esp32/include/secrets.h");
await writeFile(output, lines.join("\n"), { mode: 0o600 });
console.log("Generated ignored firmware secrets header from root .env.");
