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
    throw new Error("Set a real value for " + key + " in the root .env.");
  }
}
if (values.ESP32_API_SECRET.length < 32) {
  throw new Error("ESP32_API_SECRET must contain at least 32 characters. Generate one with: openssl rand -hex 32");
}
if (values.ESP32_AP_PASSWORD.length < 8) {
  throw new Error("ESP32_AP_PASSWORD must contain at least 8 characters for WPA2 soft-AP.");
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
  "#define HID_OTA_ROOT_CA " + cppString(values.ESP32_OTA_ROOT_CA || ""),
  "#define HID_OTA_PUBLIC_KEY " + cppString(values.ESP32_OTA_PUBLIC_KEY || ""),
  ""
];
const output = resolve(root, "firmware/esp32/include/secrets.h");
await writeFile(output, lines.join("\n"), { mode: 0o600 });
console.log("Generated ignored firmware secrets header from root .env.");
