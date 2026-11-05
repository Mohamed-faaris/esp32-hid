export type QueueAction = "pause" | "resume" | "stop";
export type MouseButton = "left" | "right" | "middle" | "back" | "forward";

export interface DeviceStatus {
  device: string;
  ble_keyboard_ready: boolean;
  ble_mouse_ready: boolean;
  wifi_mode: string;
  ap_ip: string;
  sta_connected: boolean;
  sta_ip: string;
  queue_depth: number;
  queue_paused: boolean;
  log_level: string;
  mode: string;
  recording: boolean;
}

export interface DeviceClientOptions {
  baseUrl: string;
  token: string;
  timeoutMs?: number;
}

export class DeviceApiError extends Error {
  constructor(message: string, public readonly status: number, public readonly payload?: unknown) {
    super(message);
    this.name = "DeviceApiError";
  }
}

export class DeviceClient {
  private readonly baseUrl: string;
  private readonly token: string;
  private readonly timeoutMs: number;

  constructor(options: DeviceClientOptions) {
    if (!options.baseUrl) throw new Error("ESP32_API_URL is required");
    if (!options.token) throw new Error("ESP32_API_SECRET is required");
    this.baseUrl = options.baseUrl.replace(/\/$/, "");
    this.token = options.token;
    this.timeoutMs = options.timeoutMs ?? 8000;
  }

  async request<T = unknown>(
    path: string,
    options: { method?: "GET" | "POST"; body?: unknown; auth?: boolean } = {}
  ): Promise<T> {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      const response = await fetch(this.baseUrl + "/api/" + path.replace(/^\//, ""), {
        method: options.method ?? "GET",
        headers: {
          ...(options.auth === false ? {} : { Authorization: "Bearer " + this.token }),
          ...(options.body === undefined ? {} : { "Content-Type": "application/json" })
        },
        body: options.body === undefined ? undefined : JSON.stringify(options.body),
        signal: controller.signal,
        cache: "no-store"
      });
      const raw = await response.text();
      let payload: unknown = raw;
      try { payload = raw ? JSON.parse(raw) : {}; } catch { /* keep response text */ }
      if (!response.ok) {
        const errorMessage = typeof payload === "object" && payload !== null && "error" in payload
          ? String((payload as { error: unknown }).error)
          : "Device API returned HTTP " + response.status;
        throw new DeviceApiError(errorMessage, response.status, payload);
      }
      return payload as T;
    } finally {
      clearTimeout(timer);
    }
  }

  status() {
    return this.request<DeviceStatus>("status", { auth: false });
  }

  typeText(text: string, options: { delay_ms?: number; jitter_ms?: number; instant?: boolean } = {}) {
    return this.request("keyboard/type", { method: "POST", body: { type: "type", value: text, ...options } });
  }

  pressKey(key: string) {
    return this.request("keyboard/press", { method: "POST", body: { type: "press", value: key } });
  }

  hotkey(value: string) {
    return this.request("keyboard/hotkey", { method: "POST", body: { type: "hotkey", value } });
  }

  sequence(commands: Array<Record<string, unknown>>) {
    return this.request("keyboard/sequence", { method: "POST", body: { commands } });
  }

  mouseMove(dx: number, dy: number, wheel = 0) {
    return this.request("mouse/move", { method: "POST", body: { dx, dy, wheel } });
  }

  mouseClick(button: MouseButton = "left") {
    return this.request("mouse/click", { method: "POST", body: { button } });
  }

  mouseWheel(delta: number) {
    return this.request("mouse/wheel", { method: "POST", body: { delta } });
  }

  queue(action: QueueAction) {
    return this.request("queue/" + action, { method: "POST" });
  }

  startRecording(name: string) {
    return this.request("recording/start", { method: "POST", body: { name } });
  }

  stopRecording() {
    return this.request("recording/stop", { method: "POST" });
  }

  recordingStatus() {
    return this.request("recording/status");
  }

  setMode(mode: string) {
    return this.request("mode", { method: "POST", body: { mode } });
  }

  logs() {
    return this.request<{ logs: string[] }>("logs");
  }

  setLogLevel(level: string) {
    return this.request("logs/config", { method: "POST", body: { level } });
  }

  restartAdvertising() {
    return this.request("bluetooth/reconnect", { method: "POST" });
  }

  setWifi(ssid: string, password: string) {
    return this.request("wifi/config", { method: "POST", body: { ssid, password } });
  }

  setTelemetry(endpoint: string, api_key: string, enabled = true) {
    return this.request("telemetry/config", { method: "POST", body: { endpoint, api_key, enabled } });
  }

  listMacros() {
    return this.request<{ macros: string[] }>("macros");
  }

  playMacro(name: string) {
    return this.request("macros/play", { method: "POST", body: { name } });
  }

  deleteMacro(name: string) {
    return this.request("macros/delete", { method: "POST", body: { name } });
  }
}
