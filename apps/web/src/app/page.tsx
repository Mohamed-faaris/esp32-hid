"use client";

import { useCallback, useEffect, useState } from "react";
import {
  Activity, ArrowDown, ArrowLeft, ArrowRight, ArrowUp, Bluetooth,
  CircleStop, Code2, Keyboard, MousePointer2, Pause, Play,
  RefreshCw, Save, ShieldCheck, Wifi
} from "lucide-react";

type Status = {
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
};

export default function Home() {
  const [token, setToken] = useState("");
  const [status, setStatus] = useState<Status | null>(null);
  const [text, setText] = useState("");
  const [macroName, setMacroName] = useState("workflow");
  const [macros, setMacros] = useState<string[]>([]);
  const [message, setMessage] = useState("Connect to the device to enable controls.");
  const [busy, setBusy] = useState(false);
  const [tab, setTab] = useState<"keyboard" | "mouse" | "recording" | "activity" | "settings">("keyboard");
  const [logs, setLogs] = useState<string[]>([]);
  const [logLevel, setLogLevel] = useState("INFO");
  const [wifiSsid, setWifiSsid] = useState("");
  const [wifiPassword, setWifiPassword] = useState("");
  const [telemetryEndpoint, setTelemetryEndpoint] = useState("");
  const [telemetryKey, setTelemetryKey] = useState("");
  const [telemetryEnabled, setTelemetryEnabled] = useState(false);
  const [wsConnected, setWsConnected] = useState(false);

  const api = useCallback(async (path: string, body?: unknown, method = "POST") => {
    const response = await fetch("/api/device/" + path, {
      method,
      headers: {
        ...(token ? { Authorization: "Bearer " + token } : {}),
        ...(body === undefined ? {} : { "Content-Type": "application/json" })
      },
      body: body === undefined ? undefined : JSON.stringify(body),
      cache: "no-store"
    });
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(payload.error ?? ("HTTP " + response.status));
    return payload;
  }, [token]);

  const refresh = useCallback(async () => {
    try {
      const next = await api("status", undefined, "GET") as Status;
      setStatus(next);
      if (token) {
        const saved = await api("macros", undefined, "GET") as { macros: string[] };
        setMacros(saved.macros ?? []);
        const result = await api("logs", undefined, "GET") as { logs: string[] };
        setLogs(result.logs ?? []);
      }
    } catch (error) {
      setMessage(error instanceof Error ? error.message : "Device unavailable.");
    }
  }, [api, token]);

  useEffect(() => {
    const saved = window.sessionStorage.getItem("esp32-hid-token");
    if (saved) setToken(saved);
  }, []);

  useEffect(() => {
    void refresh();
    const timer = window.setInterval(() => { void refresh(); }, 5000);
    return () => window.clearInterval(timer);
  }, [refresh]);

  useEffect(() => {
    if (!token || !status?.ap_ip) {
      setWsConnected(false);
      return;
    }
    const wsHost = status.sta_connected && status.sta_ip ? status.sta_ip : status.ap_ip;
    const socket = new WebSocket("ws://" + wsHost + ":81/");
    socket.onopen = () => socket.send(token);
    socket.onmessage = event => {
      if (event.data === "AUTH_OK") setWsConnected(true);
      else if (event.data === "AUTH_FAILED") setWsConnected(false);
      else if (typeof event.data === "string") setLogs(current => [...current.slice(-99), event.data]);
    };
    socket.onclose = () => setWsConnected(false);
    socket.onerror = () => setWsConnected(false);
    return () => socket.close();
  }, [token, status?.ap_ip, status?.sta_connected, status?.sta_ip]);

  async function run(action: () => Promise<unknown>, success: string) {
    setBusy(true);
    try {
      await action();
      setMessage(success);
      await refresh();
    } catch (error) {
      setMessage(error instanceof Error ? error.message : "Operation failed.");
    } finally {
      setBusy(false);
    }
  }

  function saveToken(value: string) {
    setToken(value);
    if (value) window.sessionStorage.setItem("esp32-hid-token", value);
    else window.sessionStorage.removeItem("esp32-hid-token");
  }

  const buttonDisabled = !token || busy;

  return (
    <main className="min-h-screen">
      <div className="mx-auto flex min-h-screen max-w-[1440px]">
        <aside className="hidden w-64 shrink-0 border-r border-zinc-800 px-5 py-6 md:flex md:flex-col">
          <div className="mb-10 flex items-center gap-3">
            <div className="flex h-9 w-9 items-center justify-center rounded-lg bg-zinc-100 text-zinc-950"><Keyboard size={18}/></div>
            <div><div className="text-sm font-semibold">ESP32 HID</div><div className="text-xs text-zinc-500">Control center</div></div>
          </div>
          <div className="eyebrow mb-3">Device</div>
          <div className="mb-8 rounded-lg border border-zinc-800 bg-zinc-900/60 p-3">
            <div className="flex items-center gap-2 text-xs"><span className={"h-2 w-2 rounded-full " + (status?.ble_keyboard_ready ? "bg-emerald-400" : "bg-zinc-600")}/>{status?.ble_keyboard_ready ? "BLE HID ready" : "Waiting for host"}</div>
            <div className="mt-2 font-mono text-xs text-zinc-500">{status?.ap_ip ?? "192.168.4.1"}</div>
          </div>
          <div className="eyebrow mb-3">Control</div>
          {([
            ["keyboard","Keyboard",Keyboard],
            ["mouse","Mouse",MousePointer2],
            ["recording","Recordings",Save],
            ["activity","Activity",Activity],
            ["settings","Settings",ShieldCheck]
          ] as const).map(([id, label, Icon]) => (
            <button key={id} onClick={() => setTab(id)} className={"mb-1 flex items-center gap-3 rounded-lg px-3 py-2.5 text-left text-sm " + (tab === id ? "bg-zinc-800 text-white" : "text-zinc-400 hover:bg-zinc-900 hover:text-white")}>
              <Icon size={16}/>{label}
            </button>
          ))}
          <div className="mt-auto border-t border-zinc-800 pt-4 text-xs text-zinc-600">Local-first · Composite BLE HID</div>
        </aside>

        <section className="min-w-0 flex-1 px-5 py-6 md:px-9 md:py-8">
          <header className="mb-8 flex flex-wrap items-start justify-between gap-4">
            <div>
              <div className="eyebrow mb-2">ESP32-WROOM-32 / composite BLE HID</div>
              <h1 className="text-2xl font-semibold tracking-tight">Device control</h1>
              <p className="mt-1 text-sm text-zinc-500">Keyboard, mouse, macros and device diagnostics.</p>
            </div>
            <button className="btn" onClick={() => void refresh()}><RefreshCw size={14}/>Refresh</button>
          </header>

          <div className="mb-6 grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
            <StatusCard icon={<Bluetooth size={16}/>} label="BLE keyboard" value={status?.ble_keyboard_ready ? "Ready" : "Waiting"} />
            <StatusCard icon={<MousePointer2 size={16}/>} label="BLE mouse" value={status?.ble_mouse_ready ? "Ready" : "Waiting"} />
            <StatusCard icon={<Wifi size={16}/>} label="Network" value={status?.sta_connected ? "STA connected" : "AP active"} detail={status?.ap_ip}/>
            <StatusCard icon={<Activity size={16}/>} label="Queue" value={String(status?.queue_depth ?? "—")} detail={status?.queue_paused ? "Paused" : "Running"}/>
          </div>

          <div className="mb-6 panel p-4">
            <div className="mb-2 flex items-center gap-2 text-sm font-medium"><ShieldCheck size={15}/>Device access</div>
            <div className="flex flex-col gap-2 sm:flex-row">
              <input className="field font-mono text-xs" type="password" placeholder="ESP32 API secret" value={token} onChange={event => saveToken(event.target.value)}/>
              <button className="btn" onClick={() => void refresh()}>Connect</button>
              <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("bluetooth/reconnect", {}), "BLE advertising restarted. Pair/reconnect from the host OS.")}>Re-advertise</button>
            </div>
            <p className="mt-2 text-xs text-zinc-500">Stored only in this tab's session storage. HTTP calls use the local web proxy; the live log socket authenticates directly with the ESP32.</p>
          </div>

          <div className="mb-6 flex flex-wrap gap-2 md:hidden">
            {(["keyboard","mouse","recording","activity","settings"] as const).map(item => <button key={item} className={"btn " + (tab === item ? "btn-primary" : "")} onClick={() => setTab(item)}>{item}</button>)}
          </div>

          {tab === "keyboard" && <div className="grid gap-4 xl:grid-cols-[1.2fr_.8fr]">
            <div className="panel p-5">
              <div className="mb-4 flex items-center justify-between"><div><h2 className="font-semibold">Keyboard input</h2><p className="mt-1 text-xs text-zinc-500">Send explicit text, keys and shortcuts.</p></div><Keyboard size={18} className="text-zinc-500"/></div>
              <textarea className="field min-h-32 resize-y" value={text} onChange={event => setText(event.target.value)} placeholder="Text to type on the paired computer…"/>
              <div className="mt-3 flex flex-wrap gap-2">
                <button className="btn btn-primary" disabled={buttonDisabled || !text.trim()} onClick={() => void run(() => api("keyboard/type", { type: "type", value: text }), "Text queued.")}><Keyboard size={14}/>Type text</button>
                <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("keyboard/hotkey", { type: "hotkey", value: "CTRL+SHIFT+ESC" }), "Hotkey queued.")}>Ctrl + Shift + Esc</button>
                <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("keyboard/press", { type: "press", value: "ENTER" }), "Enter queued.")}>Enter</button>
              </div>
            </div>
            <div className="panel p-5">
              <h2 className="font-semibold">Queue controls</h2>
              <p className="mb-4 mt-1 text-xs text-zinc-500">Pause preserves pending work. Stop clears the queue and releases all HID state.</p>
              <div className="grid grid-cols-3 gap-2">
                <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("queue/pause"), "Queue paused.")}><Pause size={14}/>Pause</button>
                <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("queue/resume"), "Queue resumed.")}><Play size={14}/>Resume</button>
                <button className="btn btn-danger" disabled={buttonDisabled} onClick={() => void run(() => api("queue/stop"), "Queue stopped and cleared.")}><CircleStop size={14}/>Stop</button>
              </div>
              <div className="mt-5 border-t border-zinc-800 pt-4">
                <div className="eyebrow mb-2">Operating mode</div>
                <select className="field" disabled={!token} value={status?.mode ?? "NORMAL"} onChange={event => void run(() => api("mode", { mode: event.target.value }), "Mode updated.")}>
                  {["NORMAL","TYPING","INSTANT","SEQUENCE","RANDOMIZED","QUIET","DIAGNOSTIC"].map(item => <option key={item}>{item}</option>)}
                </select>
              </div>
            </div>
          </div>}

          {tab === "mouse" && <div className="grid gap-4 lg:grid-cols-[1fr_.8fr]">
            <div className="panel p-5">
              <div className="mb-5 flex items-center justify-between"><div><h2 className="font-semibold">Mouse pad</h2><p className="mt-1 text-xs text-zinc-500">Relative pointer movement and clicks.</p></div><MousePointer2 size={18} className="text-zinc-500"/></div>
              <div className="mx-auto grid max-w-[270px] grid-cols-3 gap-2">
                <span/><button className="btn h-14" disabled={buttonDisabled} onClick={() => void run(() => api("mouse/move", { dx: 0, dy: -30 }), "Mouse moved up.")}><ArrowUp/></button><span/>
                <button className="btn h-14" disabled={buttonDisabled} onClick={() => void run(() => api("mouse/move", { dx: -30, dy: 0 }), "Mouse moved left.")}><ArrowLeft/></button>
                <button className="btn h-14" disabled={buttonDisabled} onClick={() => void run(() => api("mouse/move", { dx: 0, dy: 30 }), "Mouse moved down.")}><ArrowDown/></button>
                <button className="btn h-14" disabled={buttonDisabled} onClick={() => void run(() => api("mouse/move", { dx: 30, dy: 0 }), "Mouse moved right.")}><ArrowRight/></button>
              </div>
              <div className="mt-6 grid grid-cols-3 gap-2">
                <button className="btn btn-primary" disabled={buttonDisabled} onClick={() => void run(() => api("mouse/click", { button: "left" }), "Left click queued.")}>Left click</button>
                <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("mouse/click", { button: "middle" }), "Middle click queued.")}>Middle</button>
                <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("mouse/click", { button: "right" }), "Right click queued.")}>Right click</button>
              </div>
            </div>
            <div className="panel p-5">
              <h2 className="font-semibold">Scroll</h2>
              <p className="mt-1 text-xs text-zinc-500">Send bounded wheel deltas.</p>
              <div className="mt-5 grid grid-cols-2 gap-2">
                <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("mouse/wheel", { delta: 5 }), "Scroll up queued.")}>Scroll up</button>
                <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("mouse/wheel", { delta: -5 }), "Scroll down queued.")}>Scroll down</button>
              </div>
              <p className="mt-5 rounded-lg border border-zinc-800 bg-zinc-950 p-3 text-xs text-zinc-500">Relative motion only. Each request is clamped by the firmware.</p>
            </div>
          </div>}

          {tab === "recording" && <div className="grid gap-4 lg:grid-cols-[1fr_.8fr]">
            <div className="panel p-5">
              <div className="mb-4 flex items-center gap-2"><Activity size={17}/><h2 className="font-semibold">Macro recording</h2></div>
              <p className="mb-4 text-xs text-zinc-500">Records actions submitted through this ESP32 controller. It does not capture physical keyboard input on the paired computer.</p>
              <label className="mb-2 block text-xs text-zinc-400">Macro name</label>
              <input className="field mb-3" value={macroName} onChange={event => setMacroName(event.target.value)} maxLength={14}/>
              <div className="flex flex-wrap gap-2">
                <button className="btn btn-primary" disabled={buttonDisabled || !/^[A-Za-z0-9_-]{1,14}$/.test(macroName)} onClick={() => void run(() => api("recording/start", { name: macroName }), "Recording started.")}><Activity size={14}/>Start recording</button>
                <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("recording/stop"), "Recording stopped.")}><Save size={14}/>Stop & save</button>
              </div>
              <div className="mt-4 rounded-lg border border-zinc-800 bg-zinc-950 p-3 text-xs text-zinc-400">State: {status?.recording ? "Recording" : "Idle"}</div>
            </div>
            <div className="panel p-5">
              <div className="mb-4 flex items-center justify-between"><h2 className="font-semibold">Saved macros</h2><button className="btn" disabled={!token} onClick={() => void refresh()}><RefreshCw size={13}/>Refresh</button></div>
              <div className="space-y-2">
                {macros.length === 0 && <p className="text-sm text-zinc-500">No macros saved.</p>}
                {macros.map(name => <div key={name} className="flex items-center justify-between gap-2 rounded-lg border border-zinc-800 p-3">
                  <span className="min-w-0 truncate font-mono text-sm">{name}</span>
                  <div className="flex shrink-0 gap-2">
                    <button className="btn" disabled={buttonDisabled} onClick={() => void run(() => api("macros/play", { name }), "Macro queued.")}><Play size={13}/>Play</button>
                    <button className="btn btn-danger" disabled={buttonDisabled} onClick={() => {
                      if (window.confirm("Delete macro '" + name + "'?")) {
                        void run(() => api("macros/delete", { name }), "Macro deleted.");
                      }
                    }}>Delete</button>
                  </div>
                </div>)}
              </div>
            </div>
          </div>}

          {tab === "activity" && <div className="panel p-5">
            <div className="mb-4 flex items-center justify-between"><div><h2 className="font-semibold">Device activity</h2><p className="mt-1 text-xs text-zinc-500">ESP32 command and diagnostic events, not host keyboard capture.</p></div><span className="text-xs text-zinc-500">{wsConnected ? "WebSocket live" : "Polling"}</span></div>
            <div className="max-h-[480px] space-y-1 overflow-auto rounded-lg border border-zinc-800 bg-zinc-950 p-3 font-mono text-xs text-zinc-300">
              {logs.length ? logs.slice(-100).reverse().map((line, index) => <div key={index} className="border-b border-zinc-900 py-1">{line}</div>) : <div className="text-zinc-600">No events available.</div>}
            </div>
          </div>}


          {tab === "settings" && <div className="grid gap-4 xl:grid-cols-2">
            <div className="panel p-5">
              <h2 className="font-semibold">Logging</h2>
              <p className="mb-4 mt-1 text-xs text-zinc-500">Set the device log verbosity and WebSocket event feed.</p>
              <label className="mb-2 block text-xs text-zinc-400">Log level</label>
              <select className="field mb-3" value={logLevel} onChange={event => setLogLevel(event.target.value)}>
                {["NONE","ERROR","WARN","INFO","DEBUG","TRACE"].map(value => <option key={value}>{value}</option>)}
              </select>
              <button className="btn btn-primary" disabled={buttonDisabled} onClick={() => void run(() => api("logs/config", { level: logLevel }), "Log level updated.")}>Save log level</button>
            </div>
            <div className="panel p-5">
              <h2 className="font-semibold">Wi-Fi station</h2>
              <p className="mb-4 mt-1 text-xs text-zinc-500">Connect the ESP32 to an existing network. Its setup hotspot remains available.</p>
              <input className="field mb-2" placeholder="Wi-Fi SSID" value={wifiSsid} onChange={event => setWifiSsid(event.target.value)}/>
              <input className="field mb-3" type="password" placeholder="Wi-Fi password" value={wifiPassword} onChange={event => setWifiPassword(event.target.value)}/>
              <button className="btn btn-primary" disabled={buttonDisabled || !wifiSsid || wifiPassword.length < 8} onClick={() => void run(() => api("wifi/config", { ssid: wifiSsid, password: wifiPassword }), "Wi-Fi configuration saved.")}>Save Wi-Fi</button>
            </div>
            <div className="panel p-5 xl:col-span-2">
              <h2 className="font-semibold">External telemetry</h2>
              <p className="mb-4 mt-1 text-xs text-zinc-500">Forward ESP32 diagnostic event metadata to an HTTPS endpoint. Typed text and credentials are excluded. Firmware must contain the endpoint's trusted CA certificate.</p>
              <div className="grid gap-2 md:grid-cols-2">
                <input className="field" placeholder="https://api.example.com/events" value={telemetryEndpoint} onChange={event => setTelemetryEndpoint(event.target.value)}/>
                <input className="field" type="password" placeholder="Telemetry API key" value={telemetryKey} onChange={event => setTelemetryKey(event.target.value)}/>
              </div>
              <label className="my-3 flex items-center gap-2 text-xs text-zinc-400">
                <input type="checkbox" checked={telemetryEnabled} onChange={event => setTelemetryEnabled(event.target.checked)}/>
                Enable external telemetry
              </label>
              <button className="btn btn-primary" disabled={buttonDisabled || (telemetryEnabled && (!telemetryEndpoint.startsWith("https://") || !telemetryKey))} onClick={() => void run(() => api("telemetry/config", { enabled: telemetryEnabled, endpoint: telemetryEndpoint, api_key: telemetryKey }), "Telemetry configuration submitted.")}>Save telemetry</button>
            </div>
          </div>}
          <div className="mt-6 flex items-center gap-2 rounded-lg border border-zinc-800 bg-zinc-950 px-4 py-3 text-xs text-zinc-400"><Activity size={14}/><span>{message}</span><span className="ml-auto hidden font-mono text-zinc-600 sm:inline">HTTP · WebSocket</span></div>
          <footer className="mt-8 flex flex-wrap items-center justify-between gap-3 text-xs text-zinc-600">
            <span>ESP32 HID · Local-first controller</span>
            <span className="flex items-center gap-2"><Code2 size={13}/>Web · CLI · MCP share one device API</span>
          </footer>
        </section>
      </div>
    </main>
  );
}

function StatusCard({ icon, label, value, detail }: { icon: React.ReactNode; label: string; value: string; detail?: string }) {
  return <div className="panel p-4">
    <div className="mb-3 flex items-center justify-between text-zinc-500"><span className="eyebrow">{label}</span>{icon}</div>
    <div className="text-lg font-semibold">{value}</div>
    {detail && <div className="mt-1 truncate font-mono text-xs text-zinc-500">{detail}</div>}
  </div>;
}
