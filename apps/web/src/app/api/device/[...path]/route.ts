import { NextRequest, NextResponse } from "next/server";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

const allowed = new Set([
  "status","queue","bluetooth/status","bluetooth/reconnect",
  "keyboard/type","keyboard/press","keyboard/hotkey","keyboard/sleep","keyboard/sequence","keyboard/config",
  "mouse/move","mouse/click","mouse/wheel",
  "queue/pause","queue/resume","queue/stop",
  "mode","logs","logs/config",
  "recording/start","recording/stop","recording/status",
  "macros","macros/play","macros/delete",
  "wifi/config","telemetry/config"
]);

type Context = { params: Promise<{ path: string[] }> };

async function proxy(request: NextRequest, context: Context) {
  const params = await context.params;
  const route = params.path.join("/");
  if (!allowed.has(route)) return NextResponse.json({ error: "route_not_allowed" }, { status: 404 });
  const base = process.env.ESP32_API_URL;
  if (!base) return NextResponse.json({ error: "ESP32_API_URL_not_configured" }, { status: 503 });
  const auth = request.headers.get("authorization");
  const publicRead = route === "status" || route === "bluetooth/status";
  if (!publicRead && !auth?.startsWith("Bearer ")) {
    return NextResponse.json({ error: "device_token_required" }, { status: 401 });
  }

  try {
    const response = await fetch(base.replace(/\/$/, "") + "/api/" + route, {
      method: request.method,
      headers: {
        ...(auth ? { Authorization: auth } : {}),
        ...(request.method === "GET" ? {} : { "Content-Type": "application/json" })
      },
      body: request.method === "GET" ? undefined : await request.text(),
      cache: "no-store",
      signal: AbortSignal.timeout(Number(process.env.ESP32_API_TIMEOUT_MS ?? 8000))
    });
    return new NextResponse(await response.text(), {
      status: response.status,
      headers: { "content-type": response.headers.get("content-type") ?? "application/json" }
    });
  } catch {
    return NextResponse.json({ error: "device_unreachable" }, { status: 502 });
  }
}

export const GET = proxy;
export const POST = proxy;
